create table T_pkey1 (
    key1        int4,
    key2        varchar(20),
    txt         varchar(40)
);

create table T_pkey2 (
    key1        int4,
    key2        varchar(20),
    txt         varchar(40)
);

create table T_dta1 (
    tkey        varchar(20),
    ref1        int4,
    ref2        varchar(20)
);

create table T_dta2 (
    tkey        varchar(20),
    ref1        int4,
    ref2        varchar(20)
);

--
-- Function to check key existance in T_pkey1
--
create function check_pkey1_exists(int4, varchar) returns bool as '
    if ! $Plans.key?("plan")
        $Plans["plan"] = PL::Plan.new("select 1 from T_pkey1
                                       where key1 = $1 and key2 = $2",
                                       ["int4", "varchar"]).save
    end
    if $Plans["plan"].exec(args, 1)
       return true
    else
       return false
    end
' language 'plruby';


--
-- Trigger function on every change to T_pkey1
--
create function trig_pkey1_before() returns trigger as '
    if ! $Plans.key?("plan_pkey1")
        $Plans["plan_pkey1"] = PL::Plan.new("select check_pkey1_exists($1, $2) as ret",
                                            ["int4", "varchar"]).save
        $Plans["plan_dta1"] = PL::Plan.new("select 1 from T_dta1
                                            where ref1 = $1 and ref2 = $2",
                                            ["int4", "varchar"]).save
    end        
    check_old_ref = false
    check_new_dup = false

    case tg["op"]
    when PL::INSERT
        check_new_dup = true
    when PL::UPDATE
        check_old_ref = new["key1"] != old["key1"] || new["key2"] != old["key2"]
        check_new_dup = true
    when PL::DELETE
        check_old_ref = true
    end
    if check_new_dup
        n = $Plans["plan_pkey1"].exec([new["key1"], new["key2"]], 1)
        if n["ret"] == "t"
             raise "duplicate key ''#{new[''key1'']}'', ''#{new[''key2'']}'' for T_pkey1"
         end
    end

    if check_old_ref
        if $Plans["plan_dta1"].exec([old["key1"], old["key2"]], 1)
             raise "key ''#{old[''key1'']}'', ''#{old[''key2'']}'' referenced by T_dta1"
         end
    end
    PL::OK
' language 'plruby';

create trigger pkey1_before before insert or update or delete on T_pkey1
 for each row execute procedure
 trig_pkey1_before();


--
-- Trigger function to check for duplicate keys in T_pkey2
-- and to force key2 to be upper case only without leading whitespaces
--
create function trig_pkey2_before() returns trigger as '
    if ! $Plans.key?("plan_pkey2")
        $Plans["plan_pkey2"] = PL::Plan.new("select 1 from T_pkey2
                                             where key1 = $1 and key2 = $2",
                                             ["int4", "varchar"]).save
    end
    new["key2"] = new["key2"].sub(/^\\s*/, "").sub(/\\s*$/, "").upcase
    if $Plans["plan_pkey2"].exec([new["key1"], new["key2"]], 1)
        raise "duplicate key ''#{new[''key1'']}'', ''#{new[''key2'']}'' for T_pkey2"
    end
    new
' language 'plruby';

create trigger pkey2_before before insert or update on T_pkey2
 for each row execute procedure
 trig_pkey2_before();


--
-- Trigger function to force references from T_dta2 follow changes
-- in T_pkey2 or be deleted too. This must be done AFTER the changes
-- in T_pkey2 are done so the trigger for primkey check on T_dta2
-- fired on our updates will see the new key values in T_pkey2.
--
create function trig_pkey2_after() returns trigger as '
    if ! $Plans["plan_dta2_upd"]
        $Plans["plan_dta2_upd"] = 
             PL::Plan.new("update T_dta2 
                           set ref1 = $3, ref2 = $4
                           where ref1 = $1 and ref2 = $2",
                          ["int4", "varchar", "int4", "varchar" ]).save
        $Plans["plan_dta2_del"] = 
             PL::Plan.new("delete from T_dta2 
                           where ref1 = $1 and ref2 = $2", 
                          ["int4", "varchar"]).save
    end

    old_ref_follow = false
    old_ref_delete = false

    case tg["op"]
    when PL::UPDATE
        new["key2"] = new["key2"].upcase
        old_ref_follow = (new["key1"] != old["key1"]) || 
                         (new["key2"] != old["key2"])
    when PL::DELETE
        old_ref_delete = true
    end

    if old_ref_follow
        n = $Plans["plan_dta2_upd"].exec([old["key1"], old["key2"], new["key1"], new["key2"]])
        warn "updated #{n} entries in T_dta2 for new key in T_pkey2" if n != 0
    end

    if old_ref_delete
        n = $Plans["plan_dta2_del"].exec([old["key1"], old["key2"]])
        warn "deleted #{n} entries from T_dta2" if n != 0
    end

    PL::OK
' language 'plruby';


create trigger pkey2_after after update or delete on T_pkey2
 for each row execute procedure
 trig_pkey2_after();


--
-- Generic trigger function to check references in T_dta1 and T_dta2
--
create function check_primkey() returns trigger as '
    plankey = ["plan",  tg["name"], tg["relid"]]
    planrel = ["relname", tg["relid"]]
    keyidx = args.size / 2
    keyrel = args[keyidx].downcase
    if ! $Plans[plankey]
        keylist = args[keyidx + 1 .. -1]
        query = "select 1 from #{keyrel}"
        qual = " where"
        typlist = []
        idx = 1
        keylist.each do |key|
            key = key.downcase
            query << "#{qual} #{key} = $#{idx}"
            qual = " and"
            n = PL.exec("select T.typname as typname
         from pg_type T, pg_attribute A, pg_class C
         where C.relname  = ''#{PL.quote(keyrel)}''
         and C.oid      = A.attrelid 
         and A.attname  = ''#{PL.quote(key)}''
         and A.atttypid = T.oid", 1)
            if ! n
                raise "table #{keyrel} doesn''t have a field named #{key}"
            end
            typlist.push(n["typname"])
            idx += 1
        end
        $Plans[plankey] = PL::Plan.new(query, typlist).save
        $Plans[planrel] = PL.exec("select relname from pg_class
                                       where oid = ''#{tg[''relid'']}''::oid", 1)
    end
    values = []
    keyidx.times {|x| values.push(new[args[x]]) }
    n = $Plans[plankey].exec(values, 1)
    if ! n
        raise "key for #{$Plans[planrel][''relname'']} not in #{keyrel}"
    end
    PL::OK
' language 'plruby';


create trigger dta1_before before insert or update on T_dta1
 for each row execute procedure
 check_primkey('ref1', 'ref2', 'T_pkey1', 'key1', 'key2');


create trigger dta2_before before insert or update on T_dta2
 for each row execute procedure
 check_primkey('ref1', 'ref2', 'T_pkey2', 'key1', 'key2');


create function ruby_int4add(int4,int4) returns int4 as '
    args[0].to_i + args[1].to_i
' language 'plruby';

create function ruby_int4_accum(_int4, int4) returns _int4 as '
   a = args[0]
   [a[0].to_i + args[1].to_i, a[1].to_i + 1]
' language 'plruby';

create function ruby_int4_avg(_int4) returns int4 as '
   a = args[0]
   if a[1].to_i == 0
      nil
   else
      a[0].to_i / a[1].to_i
   end
' language 'plruby';

create aggregate ruby_avg (
  sfunc = ruby_int4_accum,
  basetype = int4,
  stype = _int4,
  finalfunc = ruby_int4_avg,
  initcond = '{0,0}'
 );

create aggregate ruby_sum (
  sfunc = ruby_int4add,
  basetype = int4,
  stype = int4,
  initcond = '0'
 );

create function ruby_int4lt(int4,int4) returns bool as '
    args[0].to_i < args[1].to_i
' language 'plruby';

create operator @< (
  leftarg = int4,
  rightarg = int4,
  procedure = ruby_int4lt
 );
