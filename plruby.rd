=begin
= PL/Ruby

* ((<Defining function in PL Ruby>))
* ((<Function returning SET (SFRM Materialize)>))
* ((<Function returning SET (ExprMultiResult)>))
* ((<Trigger procedures in PL Ruby>))
* ((<plruby_singleton_methods>))
* ((<Conversion>))
* ((<Class and modules>))
  * ((<module PL>)) : general module
  * ((<class PL::Plan>)) : class for prepared plans
  * ((<class PL::Cursor>)) : class for cursors
  * ((<class Tinterval>)) : only if compiled with --enable-conversion
  * ((<class NetAddr>)) : only if compiled with --enable-network
  * ((<class MacAddr>)) : only if compiled with --enable-network
  * ((<class Box>)) : only if compiled with --enable-geometry
  * ((<class Circle>)) : only if compiled with --enable-geometry
  * ((<class Path>)) : only if compiled with --enable-geometry
  * ((<class Point>)) : only if compiled with --enable-geometry
  * ((<class Polygon>)) : only if compiled with --enable-geometry
  * ((<class Segment>)) : only if compiled with --enable-geometry

PL/Ruby is a loadable procedural language for the Postgres database
system  that enable the Ruby language to create functions and trigger
procedures

Functions and triggers are singleton methods of the module PLtemp.

= WARNING
((*if PL/Ruby was *NOT* compiled with ((%--enable-conversion%)),
all arguments (to the function or the triggers) are passed as string 
values, except for NULL values represented by ((%Qnil%)).*))
((*In this case you must explicitely call a conversion function (like to_i)
if you want to use an argument as an integer*))

== Defining function in PL Ruby

To create a function in the PL/Ruby language use the syntax

   CREATE FUNCTION funcname(arguments_type) RETURNS type AS '

    # PL/Ruby function body

   ' LANGUAGE 'plruby';

when calling the function in a query, the arguments are given ((*as
string values*)) in the array ((%args%)). To create a little max
function returning the higher of two int4 values write :

   CREATE FUNCTION ruby_max(int4, int4) RETURNS int4 AS '
       if args[0].to_i > args[1].to_i
           return args[0]
       else
           return args[1]
       end
   ' LANGUAGE 'plruby';


Tuple arguments are given as hash. Here is an example that defines
the overpaid_2 function (as found in the older Postgres documentation)
in PL/Ruby.

   CREATE FUNCTION overpaid_2 (EMP) RETURNS bool AS '
       args[0]["salary"].to_f > 200000 || 
          (args[0]["salary"].to_f > 100000 && args[0]["age"].to_i < 30)
   ' LANGUAGE 'plruby';

=== Warning : with PostgreSQL >= 7.4 "array" are given as a ruby Array

For example to define a function (int4[], int4) and return int4[],
in version < 7.4 you write

   CREATE FUNCTION ruby_int4_accum(_int4, int4) RETURNS _int4 AS '
       if /\\{(\\d+),(\\d+)\\}/ =~ args[0]
           a, b = $1, $2
           newsum = a.to_i + args[1].to_i
           newcnt = b.to_i + 1
       else
           raise "unexpected value #{args[0]}"
       end
       "{#{newsum},#{newcnt}}"
   ' LANGUAGE 'plruby';

This must now (>= 7.4) be written

   CREATE FUNCTION ruby_int4_accum(_int4, int4) RETURNS _int4 AS '
      a = args[0]
      [a[0].to_i + args[1].to_i, a[1].to_i + 1]
   ' LANGUAGE 'plruby';


== Function returning SET (SFRM Materialize)

The return type must be declared as SETOF

The function must call ((%yield%)) to return rows or return a String which
must be a valid SELECT statement

For example to concatenate 2 rows create the function

   plruby_test=# CREATE FUNCTION tu(varchar) RETURNS setof record
   plruby_test-# AS '
   plruby_test'#    size = PL.column_name(args[0]).size
   plruby_test'#    res = nil
   plruby_test'#    PL::Plan.new("select * from #{args[0]}", 
   plruby_test'#                 "block" => 50).each do |row|
   plruby_test'#       if res.nil?
   plruby_test'#          res = row.values
   plruby_test'#       else
   plruby_test'#          res.concat row.values
   plruby_test'#          yield res
   plruby_test'#          res = nil
   plruby_test'#       end
   plruby_test'#    end
   plruby_test'#    if res
   plruby_test'#       res.concat Array.new(size)
   plruby_test'#       yield res
   plruby_test'#    end
   plruby_test'# ' language 'plruby';
   CREATE FUNCTION
   plruby_test=# 
   plruby_test=# select * from tt;
    a | b  
   ---+----
    1 |  2
    3 |  4
    5 |  6
    7 |  8
    9 | 10
   (5 rows)
   
   plruby_test=# select * from tu('tt') as tbl(a int, b int, c int, d int);
    a | b  | c | d 
   ---+----+---+---
    1 |  2 | 3 | 4
    5 |  6 | 7 | 8
    9 | 10 |   |  
   (3 rows)
   
   plruby_test=# 

== Function returning SET (ExprMultiResult)

The return type must be declared as SETOF

The function is called until it returns nil

The method PL#context and PL#context= give the possibility to store information
between the call

For example

   plruby_test=# create or replace function vv(int) returns setof int as '
   plruby_test'#    i = PL.context || 0
   plruby_test'#    if i >= args[0].to_i
   plruby_test'#       nil
   plruby_test'#    else
   plruby_test'#       PL.context = i + 1
   plruby_test'#    end
   plruby_test'# ' language plruby;
   CREATE FUNCTION
   plruby_test=# 
   plruby_test=# select * from uu;
    b 
   ---
    2
   (1 row)
   
   plruby_test=# 
   plruby_test=# select *,vv(3) from uu;
    b | vv 
   ---+----
    2 |  1
    2 |  2
    2 |  3
   (3 rows)
   
   plruby_test=# 

== Trigger procedures in PL Ruby

Trigger procedures are defined in Postgres as functions without
arguments and a return type of trigger. In PL/Ruby the procedure is
called with 4 arguments :

:new (hash, tainted)
 an hash containing the values of the new table row on INSERT/UPDATE
 actions, or empty on DELETE. 
:old (hash, tainted)
 an hash containing the values of the old table row on UPDATE/DELETE
 actions, or empty on INSERT 
:args (array, tainted, frozen)
 An array of the arguments to the procedure as given in the CREATE
 TRIGGER statement 
:tg (hash, tainted, frozen)
 The following keys are defined

  :name
    The name of the trigger from the CREATE TRIGGER statement.

  :relname
    The name of the relation who has fired the trigger

  :relid
    The object ID of the table that caused the trigger procedure to be invoked.

  :relatts
    An array containing the name of the tables field.

  :when
     The constant ((%PL::BEFORE%)), ((%PL::AFTER%)) or
     ((%PL::UNKNOWN%)) depending on the event of the trigger call.

  :level
    The constant ((%PL::ROW%)) or ((%PL::STATEMENT%))
    depending on the event of the trigger call.

  :op
    The constant ((%PL::INSERT%)), ((%PL::UPDATE%)) or 
    ((%PL::DELETE%)) depending on the event of the trigger call.


The return value from a trigger procedure is one of the constant
((%PL::OK%)) or ((%PL::SKIP%)), or an hash. If the
return value is ((%PL::OK%)), the normal operation
(INSERT/UPDATE/DELETE) that fired this trigger will take
place. Obviously, ((%PL::SKIP%)) tells the trigger manager to
silently suppress the operation. The hash tells
PL/Ruby to return a modified row to the trigger manager that will be
inserted instead of the one given in ((%new%)) (INSERT/UPDATE
only). Needless to say that all this is only meaningful when the
trigger is BEFORE and FOR EACH ROW.

Here's a little example trigger procedure that forces an integer
value in a table to keep track of the # of updates that are performed
on the row. For new row's inserted, the value is initialized to 0 and
then incremented on every update operation :

    CREATE FUNCTION trigfunc_modcount() RETURNS TRIGGER AS '
        case tg["op"]
        when PL::INSERT
            new[args[0]] = 0
          when PL::UPDATE
              new[args[0]] = old[args[0]].to_i + 1
          else
              return PL::OK
          end
          new
      ' LANGUAGE 'plruby';

      CREATE TABLE mytab (num int4, modcnt int4, descr text);

      CREATE TRIGGER trig_mytab_modcount BEFORE INSERT OR UPDATE ON mytab
          FOR EACH ROW EXECUTE PROCEDURE trigfunc_modcount('modcnt');



A more complex example (extract from test_setup.sql in the distribution)
which use the global variable ((%$Plans%)) to store a prepared
plan

   create function trig_pkey2_after() returns trigger as '
      if ! $Plans.key?("plan_dta2_upd")
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
          n = $Plans["plan_dta2_upd"].exec([old["key1"], old["key2"], new["key1"],
   new["key2"]])
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


== plruby_singleton_methods

Sometime it can be usefull to define methods (in pure Ruby) which can be
called from a PL/Ruby function or a PL/Ruby trigger.

In this case, you have 2 possibilities

* the "stupid" way (({:-) :-) :-)}))

just close the current definition of the function (or trigger) with a
(({end})) and define your singleton method without the final (({end}))

Here a small and useless example

          plruby_test=# CREATE FUNCTION tutu() RETURNS int4 AS '
          plruby_test'#     toto(1, 3) + toto(4, 4)
          plruby_test'# end
          plruby_test'# 
          plruby_test'# def PLtemp.toto(a, b)
          plruby_test'#     a + b
          plruby_test'# ' LANGUAGE 'plruby';
          CREATE
          plruby_test=# select tutu();
          tutu
          ----
            12
          (1 row)
          
          plruby_test=#


* create a table plruby_singleton_methods with the columns (name, args, body)

At load time, PL/Ruby look if it exist a table plruby_singleton_methods and if
found try, for each row, to define singleton methods with the template :


          def PLtemp.#{name} (#{args})
              #{body}
          end

The previous example can be written (you have a more complete example in
test/plp/test_setup.sql)

          
          plruby_test=# SELECT * FROM plruby_singleton_methods;
          name|args|body 
          ----+----+-----
          toto|a, b|a + b
          (1 row)
          
          plruby_test=# CREATE FUNCTION tutu() RETURNS int4 AS '
          plruby_test'#     toto(1, 3) + toto(4, 4)
          plruby_test'# ' LANGUAGE 'plruby';
          CREATE
          plruby_test=# select tutu();
          tutu
          ----
            12
          (1 row)
          
          plruby_test=#

* Another example, if PLRuby was compiled with --enable-conversion and it 
  exist a column with the name '***' then it can create a singleton method
  from a PLRuby function


          plruby_test=# select * from plruby_singleton_methods;
           name | args | body 
          ------+------+------
           ***  |      | 
          (1 row)
          
          plruby_test=# create function add_value(int, int) returns int as '
          plruby_test'# args[0] + args[1]
          plruby_test'# ' language 'plruby';
          CREATE FUNCTION
          plruby_test=# 
          plruby_test=# select add_value(10, 2);
           add_value 
          -----------
                  12
          (1 row)
          
          plruby_test=# 
          plruby_test=# create function add_one(int) returns int as '
          plruby_test'# add_value(args[0], 1)
          plruby_test'# ' language 'plruby';
          CREATE FUNCTION
          plruby_test=# 
          plruby_test=# select add_one(11);
           add_one 
          ---------
                12
          (1 row)
          
          plruby_test=# 

== Conversion

When compiled with --enable-conversion the following conversions are
made

                  PostgreSQL             Ruby
                  ----------             ----
                  OID                    Fixnum
                  INT2OID                Fixnum
                  INT4OID                Fixnum
                  INT8OID                Fixnum (or Bignum)
                  FLOAT4OID              Float
                  FLOAT8OID              Float
                  CASHOID                Float
                  NUMERICOID             Float
                  BOOLOID                true, false
                  ABSTIMEOID             Time
                  RELTIMEOID             Time
                  TIMEOID                Time
                  TIMETZOID              Time
                  TIMESTAMPOID           Time
                  TIMESTAMPTZOID         Time
                  DATEOID                Time
                  INTERVALOID            Time
                  TINTERVALOID           Tinterval (new Ruby class)

The following conversions are added when compiled with --enable-network

                  INETOID                NetAddr (new Ruby class)
                  CIDROID                NetAddr (new Ruby class)
                  MACADDROID             MacAddr (new Ruby class)

The following conversions are added when compiled with --enable-geometry

                  POINTOID               Point   (new Ruby class)
                  LSEGOID                Segment (new Ruby class)
                  LINEOID                Line    (new Ruby class)
                  BOXOID                 Box     (new Ruby class)
                  PATHOID                Path    (new Ruby class)
                  POLYGONOID             Polygon (new Ruby class)
                  CIRCLEOID              Circle  (new Ruby class)

all others OID are converted to a String object


== Class and modules

=== Global

--- warn [level], message
    Ruby interface to PostgreSQL elog()

    Possible value for ((%level%)) are ((%NOTICE%)), ((%DEBUG%)) and ((%NOIND%))

    Use ((%raise()%)) if you want to simulate ((%elog(ERROR, "...")%))

--- $Plans (hash, tainted)
    can be used to store prepared plans.
 
=== module PL

    general module

--- args_type
    Return the type of the arguments given to the function

--- column_name(table)
    Return the name of the columns for the table

--- column_type(table)
    return the type of the columns for the table

--- context
    Return the context (or nil) associated with a SETOF function 
    (ExprMultiResult)

--- context=
    Set the context for a SETOF function (ExprMultiResult)

--- quote(string)
 
    Duplicates all occurences of single quote and backslash
    characters. It should be used when variables are used in the query
    string given to spi_exec or spi_prepare (not for the value list on
    execp).

--- result_name
    Return the name of the columns for a function returning a SETOF

--- result_type
    Return the type of the columns for a function returning a SETOF
    or the type of the return value

--- result_size
    Return the number of columns  for a function returning a SETOF

--- result_description
    Return the table description given to a function returning a SETOF

--- exec(string [, count [, type]])
--- spi_exec(string [, count [, type]])

    Call parser/planner/optimizer/executor for query. The optional
    ((%count%)) value tells spi_exec the maximum number of rows to be
    processed by the query.
    
      :SELECT
        If the query is a SELECT statement, an array is return (if count is
        not specified or with a value > 1). Each element of this array is an
        hash where the key is the column name.
    
        If type is specified it can take the value

          * "array" return for each column an array with the element
            ["name", "value", "type", "len", "typeid"]
          * "hash" return for each column an hash with the keys 
            {"name", "value", "type", "len", "typeid"}
          * "value" return all values

        For example this procedure display all rows in the table pg_table.
    
          CREATE FUNCTION pg_table_dis() RETURNS int4 AS '
          res = PLruby.exec("select * from pg_class")
          res.each do |x|
              warn "======================"
              x.each do |y, z|
                  warn "name = #{y} -- value = #{z}"
              end
              warn "======================"
          end
          return res.size
          ' LANGUAGE 'plruby';
    
        A block can be specified, in this case a call to yield() will be
        made.
    
        If count is specified with the value 1, only the first row (or
        FALSE if it fail) is returned as a hash. Here a little example :
    
    
           CREATE FUNCTION pg_table_dis() RETURNS int4 AS '
              PL.exec("select * from pg_class", 1) { |y, z|
                 warn "name = #{y} -- value = #{z}"
             }
             return 1
           ' LANGUAGE 'plruby';
    
        Another example with count = 1
    
          create table T_pkey1 (
              skey1        int4,
              skey2        varchar(20),
              stxt         varchar(40)
          );
            
          create function toto() returns bool as '
             warn("=======")
             PL.exec("select * from T_pkey1", 1, "hash") do |a|
                warn(a.inspect)
             end
             warn("=======")
             PL.exec("select * from T_pkey1", 1, "array") do |a|
                warn(a.inspect)
             end
             warn("=======")
             PL.exec("select * from T_pkey1", 1) do |a|
                warn(a.inspect)
             end
             warn("=======")
             return true
          ' language 'plruby';
    
            
          plruby_test=# select toto();
          NOTICE:  =======
          NOTICE:  {"name"=>"skey1", "typeid"=>23, "type"=>"int4", "value"=>"12", "len"=>4}
          NOTICE:  {"name"=>"skey2", "typeid"=>1043, "type"=>"varchar", "value"=>"a", "len"=>20}
          NOTICE:  {"name"=>"stxt", "typeid"=>1043, "type"=>"varchar", "value"=>"b", "len"=>40}
          NOTICE:  =======
          NOTICE:  ["skey1", "12", "int4", 4, 23]
          NOTICE:  ["skey2", "a", "varchar", 20, 1043]
          NOTICE:  ["stxt", "b", "varchar", 40, 1043]
          NOTICE:  =======
          NOTICE:  ["skey1", "12"]
          NOTICE:  ["skey2", "a"]
          NOTICE:  ["stxt", "b"]
          NOTICE:  =======
           toto 
          ------
           t
          (1 row)
            
          plruby_test=# 
    
    
      :SELECT INTO, INSERT, UPDATE, DELETE
         return the number of rows insered, updated, deleted, ...
    
      :UTILITY
         return TRUE

--- prepare(string[, types])
--- spi_prepare(string[, types])
--- prepare(string, "types" => types, "count" => count, "output" => type, "tmp" => true)

    Deprecated : See ((%PL::Plan::new%)) and ((%PL::Plan#save%))

    Prepares AND SAVES a query plan for later execution. It is a bit
    different from the C level SPI_prepare in that the plan is
    automatically copied to the toplevel memory context.

    If the query references arguments, the type names must be given as a
    Ruby array of strings. The return value from prepare is a
    ((%PL::Plan%)) object to be used in subsequent calls to
    ((%PL::Plan#exec%)).

    If the hash given has the keys ((%count%)), ((%output%)) these values
    will be given to the subsequent calls to ((%each%))

=== class PL::Plan

    class for prepared plan

--- initialize(string, "types" => types, "count" => count, "output" => type, "save" => false)

    Prepares a query plan for later execution.

    If the query references arguments, the type names must be given as a
    Ruby array of strings.

    If the hash given has the keys ((%output%)), ((%count%)) these values
    will be given to the subsequent calls to ((%each%))

    If ((%"save"%)) as a true value, the plan will be saved 


--- exec(values, [count [, type]])
--- execp(values, [count [, type]])
--- exec("values" => values, "count" => count, "output" => type)
--- execp("values" => values, "count" => count, "output" => type)

    Execute a prepared plan from ((%PL::PLan::new%)) with variable
    substitution. The optional ((%count%)) value tells
    ((%PL::Plan#exec%)) the maximum number of rows to be processed by the
    query.

    If there was a typelist given to ((%PL::Plan::new%)), an array
    of ((%values%)) of exactly the same length must be given to
    ((%PL::Plan#exec%)) as first argument. If the type list on
    ((%PL::Plan::new%)) was empty, this argument must be omitted.

    If the query is a SELECT statement, the same as described for
    ((%PL#exec%)) happens for the loop-body and the variables for
    the fields selected.

    If type is specified it can take the values
       * "array" return an array with the element ["name", "value", "type", "len", "typeid"]
       * "hash" return an hash with the keys {"name", "value", "type", "len", "typeid"}
       * "value" return an array with all values

    Here's an example for a PL/Ruby function using a prepared plan : 

        CREATE FUNCTION t1_count(int4, int4) RETURNS int4 AS '
            if ! $Plans.key?("plan")
                # prepare the saved plan on the first call
                $Plans["plan"] = PL::Plan.new("SELECT count(*) AS cnt FROM t1 
                                               WHERE num >= $1 AND num <= $2",
                                              ["int4", "int4"]).save
            end
            n = $Plans["plan"].exec([args[0], args[1]], 1)
            n["cnt"]
        ' LANGUAGE 'plruby';

--- cursor(name = nil, "values" => values, "output" => type)
 
    Create a new object PL::Cursor

    If output is specified it can take the values
       * "array" return an array with the element ["name", "value", "type", "len", "typeid"]
       *  "hash" return an hash with the keys {"name", "value", "type", "len", "typeid"}
       * "value" return an array with all values

    If there was a typelist given to ((%PL::Plan::new%)), an array
    of ((%values%)) of exactly the same length must be given to
    ((%PL::Plan#cursor%))

--- each(values, [count [, type ]]) { ... }
--- fetch(values, [count [, type ]]) { ... }
--- each("values" => values, "count" => count, "output" => type) { ... }
--- fetch("values" => values, "count" => count, "output" => type) { ... }

    Same then #exec but a call to SPI_cursor_open(), SPI_cursor_fetch() is made.

    Can be used only with a block and a SELECT statement
    
        create function toto() returns bool as '
               plan = PL::Plan.new("select * from T_pkey1")
               warn "=====> ALL"
               plan.each do |x|
                  warn(x.inspect)
               end
               warn "=====> FIRST 2"
               plan.each("count" => 2) do |x|
                  warn(x.inspect)
               end
               return true
        ' language 'plruby';
        
        plruby_test=# select * from T_pkey1;
         skey1 | skey2 | stxt 
        -------+-------+------
            12 | a     | b
            24 | c     | d
            36 | e     | f
        (3 rows)
        
        plruby_test=# 
        plruby_test=# select toto();
        NOTICE:  =====> ALL
        NOTICE:  {"skey1"=>"12", "skey2"=>"a", "stxt"=>"b"}
        NOTICE:  {"skey1"=>"24", "skey2"=>"c", "stxt"=>"d"}
        NOTICE:  {"skey1"=>"36", "skey2"=>"e", "stxt"=>"f"}
        NOTICE:  =====> FIRST 2
        NOTICE:  {"skey1"=>"12", "skey2"=>"a", "stxt"=>"b"}
        NOTICE:  {"skey1"=>"24", "skey2"=>"c", "stxt"=>"d"}
         toto 
        ------
         t
        (1 row)
        
        plruby_test=# 

--- release

    Release a query plan

--- save
  
    Save a query plan for later execution. The plan is copied to the
    toplevel memory context.

=== class PL::Cursor

    A cursor is created with the method PL::Plan#cursor

--- close

    Closes a cursor

--- each {|row| ... }
 
    Iterate over all rows (forward)

--- fetch(count = 1)
--- row(count = 1)

    Fetches some rows from a cursor

    if count > 0 fetch forward else backward

--- move(count)

    Move a cursor : if count > 0 move forward else backward

--- reverse_each {|row| ... }

    Iterate over all rows (backward)

--- rewind

    Positions the cursor at the beginning of the table

=== class NetAddr

The class NetAddr implement the PostgreSQL type ((|inet|))
and ((|cidr|))

only available if PL/Ruby was compiled with ((|--enable-network|))

The module Comparable is included

---  from_string(string, cidr = false)
     Convert a ((|String|)) to a ((|NetAddr|))

--- <=>(other)
     comparison function for 2 ((|NetAddr|)) objects
    
     comparison is first on the common bits of the network part, then on
     the length of the network part, and then on the whole unmasked address.

--- abbrev
     return the abbreviated display format as a ((|String|)) object

--- broadcast
     return the broadcast address from the network

--- contain?(other)
     return true if ((|other|)) is included in ((|self|))

--- contain_or_equal?(other)
     return true if ((|other|)) is included in ((|self|)), or equal

--- contained?(other)
     return true if ((|self|)) is included in ((|other|))

--- contained_or_equal?(other)
     return true if ((|self|)) is included in ((|other|)), or equal

--- family
     return the String "AF_INET" or "AF_INET6"

--- first
     return the first address in the network

--- host
     extract the IP address and return it as a ((|String|)) 

--- hostmask
     return the host mask for network

--- initialize(string, cidr = false)
     create a ((|NetAddr|)) from a ((|String|))

--- last
     return the last address in the network

--- masklen
     return the length of the netmask

--- netmask
     return the netmask for the network

--- network
     return the network part of the address

--- set_masklen(len)
     return a new ((|NetAddr|)) with netmask length ((|len|))

--- to_s
     return the string representation of the address

=== class MacAddr

The MacAddr implement the PostgreSQL type ((|macaddr|))

only available if PL/Ruby was compiled with ((|--enable-network|))

The module Comparable is included

--- from_string(string, cidr = false)
     Convert a ((|String|)) to a ((|MacAddr|))

--- <=>(other)
     comparison function for 2 ((|MacAddr|)) objects

--- initialize(string)
     create a ((|MacAddr|)) from a ((|String|))

--- to_s
     return the string representation of the MAC address

--- truncate
     return a new object with the last 3 bytes set to zero

=== class Tinterval

The Tinterval implement the PostgreSQL type ((|tinterval|))

only available if PL/Ruby was compiled with ((|--enable-conversion|))


--- from_string(string)
      Convert a ((|String|)) (PostgreSQL representation)
      to a ((|Tinterval|))

--- high
     return a ((|Time|)) which is the high value of the interval

--- high=(time)
     set the high value for the interval

--- initialize(low, high)
     create a ((|Tinterval|)) with the 2 ((|Time|)) objects
     ((|low|)) and ((|high|))

--- low
     return a ((|Time|)) which is the low value of the interval

--- low=(time)
     set the low value for the interval
 
--- to_s
     return the string representation of the object

=== class Box

The Box implement the PostgreSQL type ((|box|))

only available if PL/Ruby was compiled with ((|--enable-geometry|))

The module Comparable is included

--- from_string(string)
     Convert a ((|String|)) (PostgreSQL representation)
     to a ((|Box|)) object

--- +(point)
     translate (right, up) ((|self|))

--- -(point)
     translate (left, down) ((|self|))

--- *(point)
     scale and rotate ((|self|))

--- /(point)
     scale and rotate ((|self|))

--- ===(other)
     return true if the 2 boxes ((|self|)) and ((|other|)) are identical

--- <=>(other)
     comparison operator for 2 Box based on the area of the 2 objects, i.e.
     self.area <=> box.area

--- above?(other)
     return true if  ((|self|)) is above ((|other|))

--- area
     return the area of the Box

--- below?(other)
     return true if  ((|self|)) is below ((|other|))

--- center
     return the center point of the Box

--- closest(other)
     closest point to ((|other|))
    
     ((|other|)) can be a Point, or Segment

--- contain?(other)
     return true if  ((|self|)) contain ((|other|))

--- contained?(other)
     return true if  ((|self|)) is contained by ((|other|))

--- diagonal
     return a line Segment which happens to be the
     positive-slope diagonal of Box

--- height
     return the height of the Box (vertical magnitude)

--- in?(other)
     return true if  ((|self|)) is contained by ((|other|))

--- initialize(*args)
     create a new Box object
    
     ((|args|)) can be 2 Point objects (low, high) or 4 Float objects
     (low.x, low.y, high.x, high.y)

--- intersection(other)
     returns the overlapping portion of two boxes,
     or ((|nil|)) if they do not intersect.

--- intersect?(segment)
     returns  true if the Segment ((|segment|))
     intersect with the Box
    
     Segment completely inside box counts as intersection.
     If you want only segments crossing box boundaries,
     try converting Box to Path first.
     

--- left?(other)
     return true if ((|self|)) is strictly left of ((|other|))

--- overlap?(other)
     return true if ((|self|)) overlap ((|other|))
 
--- overleft?(other)
     return true if the right edge of ((|self|)) is to the left of
     the right edge of ((|other|))

--- overright?(other)
     return true if the left edge of ((|self|)) is to the right of
     the left edge of ((|other|))

--- right?(other)
     return true if ((|self|)) is strictly right of ((|other|))

--- same?(other)
     return true if the 2 boxes ((|self|)) and ((|other|)) are identical

--- to_circle
     convert a Box to a Circle

--- to_point
     return the center Point of the Box

--- to_polygon
     convert a Box to a Polygon

--- to_segment
     return a line Segment which happens to be the
     positive-slope diagonal of Box

--- width
     return the width of the Box (horizontal magnitude)

=== class Path

The Path implement the PostgreSQL type ((|path|))

only available if PL/Ruby was compiled with ((|--enable-geometry|))

The module Comparable is included

--- from_string(string)
     Convert a ((|String|)) (PostgreSQL representation)
     to a ((|Path|))

--- <<(path)
     concatenate the two paths (only if they are both open)

--- +(point)
     translate (right, up) ((|self|))

--- -(point)
     translate (left, down) ((|self|))

--- *(point)
     scale and rotate ((|self|))

--- /(point)
     scale and rotate ((|self|))

--- <=>(other)
     comparison function based on the path cardinality, i.e.
     self.npoints <=> other.npoints

--- close
     make a closed path

--- closed?
     return true if ((|self|)) is a closed path

--- concat(path)
     concatenate the two paths (only if they are both open)

--- initialize(points, closed = false)
     create a new Path object from the Array of Point ((|points|))

--- length
     return the length of ((|self|))

--- npoints
     return the path cardinality

--- open
     make an open path

--- to_polygon
     convert ((|self|)) to a Polygon object

=== class Point

The Point implement the PostgreSQL type ((|point|))

only available if PL/Ruby was compiled with ((|--enable-geometry|))

The module Comparable is included

--- from_string(string)
     Convert a ((|String|)) (PostgreSQL representation)
     to a ((|Point|))

--- +(point)
     translate (right, up) ((|self|))

--- -(point)
     translate (left, down) ((|self|))

--- *(point)
     scale and rotate ((|self|))

--- /(point)
     scale and rotate ((|self|))

--- [](indice)
     return the coordinate
    
     ((|indice|)) can have the value 0 or 1

--- []=(indice, value)
     set the coordinate
    
     ((|indice|)) can have the value 0 or 1

--- ==(other)
     return true if ((|self|)) and ((|other|)) are the same,
     i.e. self.x == other.x && self.y == other.y

--- above?(other)
     return true if ((|self|)) is above ((|other|)),
     i.e. self.y > other.y

--- below?(other)
     return true if ((|self|)) is below ((|other|)),
     i.e. self.y < other.y

--- contained?(other)
     return true if ((|self|)) is contained in ((|other|))
     
     ((|other|)) can be Point, Polygon or a Circle object

--- horizontal?(other)
     return true if ((|self|)) and ((|other|)) are horizontal,
     i.e. self.y == other.y

--- in?(other)
     return true if ((|self|)) is contained in ((|other|))
     
     ((|other|)) can be Point, Polygon or a Circle object

--- initialize(x, y)
     create a Point with the 2 Float object (x, y)

--- left?(other)
     return true if ((|self|)) is at the left of ((|other|)),
     i.e. self.x < other.x

--- on?(other)
     return true if ((|self|)) is on ((|other|))
     
     ((|other|)) can be Point, Line, Segment, Box or Path object

--- right?(other)
     return true if ((|self|)) is at the right of ((|other|)),
     i.e. self.x > other.x

--- vertical?(other)
     return true if ((|self|)) and ((|other|)) are vertical,
     i.e. self.x == other.x

--- x
     return ((|x|)) for ((|self|))

--- x=(value)
     set the ((|x|)) value for ((|self|))

--- y
     return ((|y|)) for ((|self|))

--- y=(value)
     set the ((|y|)) value for ((|self|))

=== class Segment

The Segment implement the PostgreSQL type ((|lseg|))

only available if PL/Ruby was compiled with ((|--enable-geometry|))

The module Comparable is included

--- from_string(string)
     Convert a ((|String|)) (PostgreSQL representation)
     to a ((|Segment|))

--- <=>(other)
     comparison function for the 2 segments, returns
    
      0  if self[0] == other[0] && self[1] == other[1]
    
      1  if distance(self[0], self[1]) > distance(other[0], other[1]) 
    
      -1 if distance(self[0], self[1]) < distance(other[0], other[1]) 

--- center
     return the center of the segment

--- closest(other)
     closest point to other
    
     ((|other|)) can be a Point, Segment or Box
    
     With a point, take the closest endpoint 
     if the point is left, right, above, or below the segment, otherwise 
     find the intersection point of the segment and its perpendicular through
     the point.

--- horizontal?
     returns true if ((|self|)) is a horizontal Segment

--- initialize(point0, point1)
     create a Segment from the 2 Point p0, p1

--- intersect?(other)
     returns true if ((|self|)) and ((|other|)) intersect

--- intersection(other)
     returns the Point where the 2 Segment ((|self|)) and ((|other|))
     intersect or nil

--- length
     return the length of ((|self|)), i.e. the distnace between the 2 points

--- on?(other)
     return true if ((|self|)) is on ((|other|))
    
     ((|other|)) can be a Segment, or a Box object

--- parallel?(other)
     returns true if the 2 Segment ((|self|)) and ((|other|)) 
     are parallel

--- perpendicular?(other)
     returns true if ((|self|)) is perpendicular to ((|other|))

--- to_point
     conversion function to a Point, return the center of the segment

--- vertical?
     returns true if ((|self|)) is a vertical Segment
=== class Polygon

The Polygon implement the PostgreSQL type ((|polygon|))

only available if PL/Ruby was compiled with ((|--enable-geometry|))


--- from_string(string)
     Convert a ((|String|)) (PostgreSQL representation)
     to a ((|Polygon|))

--- ==(other)
     return true if ((|self|)) is the same as ((|other|)), i.e. all
     the points are the same

--- center
     return the center of ((|self|)), i.e. create a circle and return its 
     center

--- contain?(other)
     return true if ((|self|)) contains ((|other|))
    
     ((|other|)) can be a Point or a Polygon

--- contained?(other)
     return true if ((|self|)) is contained in ((|other|)) by determining
     if ((|self|)) bounding box is contained by ((|other|))'s bounding box.

--- in?(other)
     return true if ((|self|)) is contained in ((|other|)) by determining
     if ((|self|)) bounding box is contained by ((|other|))'s bounding box.

--- initialize(points, closed = false)
     create a new Polygon object from the Array of Point ((|points|))

--- left?(other)
     return true if ((|self|)) is strictly left of ((|other|)), i.e.
     the right most point of ((|self|)) is left of the left
     most point of ((|other|))

--- overleft?(other)
     return true if ((|self|)) is overlapping or left of ((|other|)),
     i.e. the left most point of ((|self|)) is left of the right
     most point of ((|other|))

--- overright?(other)
     return true if ((|self|)) is overlapping or right of ((|other|)),
     i.e. the right most point of ((|self|)) is right of the left
     most point of ((|other|))

--- overlap?(other)
     return true if ((|self|)) and ((|other|)) overlap by determining if
     their bounding boxes overlap.

--- npoints
     return the number of points in ((|self|))

--- right?(other)
     return true if ((|self|)) is strictly right of ((|other|)), i.e.
     the left most point of ((|self|)) is right of the left
     most point of ((|other|))

--- same?(other)
     return true if ((|self|)) is the same as ((|other|)), i.e. all 
     the points are the same

--- to_box
     convert ((|self|)) to a Box

--- to_circle
     convert ((|self|)) to a Circle

--- to_path
     convert ((|self|)) to a Path

--- to_point
     convert ((|self|)) to a Point by returning its center

=== class Circle

The Circle implement the PostgreSQL type ((|circle|))

only available if PL/Ruby was compiled with ((|--enable-geometry|))

The module Comparable is included

--- from_string(string)
     Convert a ((|String|)) (PostgreSQL representation)
     to a ((|Circle|))

--- +(point)
     translate (right, up) ((|self|))

--- -(point)
     translate (left, down) ((|self|))

--- *(point)
     scale and rotate ((|self|))

--- /(point)
     scale and rotate ((|self|))

--- <=>(other)
     comparison function based on area,
     i.e. self.area <=> other.area

--- area
     return the area

--- above?(other)
     return true if ((|self|)) is entirely above ((|other|))

--- below?(other)
     return true if ((|self|)) is entirely below ((|other|))

--- contain?(other)
     return true if ((|self|)) contain ((|other|))

--- contained?(other)
     return true if ((|self|)) is contained in ((|other|))

--- diameter
     return the diameter

--- initialize(center, radius)
     create a Circle object with ((|center|)) and ((|radius|))
    
     ((|center|)) can be a Point or an Array [x, y]

--- overlap?(other)
     return true if ((|self|)) overlap ((|other|))

--- overleft?(other)
     return true if the right edge of ((|self|)) is to the left of
     the right edge of ((|other|))

--- left?(other)
     return true if ((|self|)) is strictly left of ((|other|))

--- overright?(other)
     return true if the left edge of ((|self|)) is to the right of
     the left edge of ((|other|))

--- radius
     return the radius

--- right?(other)
     return true if ((|self|)) is strictly right of ((|other|))

--- same?(other)
     return true if ((|self|)) is the same than ((|other|)), i.e.
     self.center == other.center && self.radius == other.radius

--- to_box
     convert ((|self|)) to a Box

--- to_point
     convert ((|self|)) to a Point by returning its center

--- to_polygon(npts)
     convert ((|self|)) to a Polygon with ((|npts|)) Points

=end

