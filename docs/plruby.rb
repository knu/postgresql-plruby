# = Warning
#
# <em>For documentation purpose, the modules PLRuby, PLRuby::Description
# are defined but don't exist in reality</em>
# 
# = PLRuby
#
# PLRuby is a loadable procedural language for the Postgres database
# system  that enable the Ruby language to create functions and trigger
# procedures
# 
# Functions and triggers are singleton methods of the module PLtemp.
# 
# = WARNING
# 
# <b>if PLRuby was NOT compiled with <em>--enable-conversion</em>
# all arguments (to the function or the triggers) are passed as string 
# values, except for NULL values represented by <em>nil</em>.</b>
# 
# <b>In this case, you must explicitely call a conversion function (like to_i)
# if you want to use an argument as an integer</b>
# 
# = See
#
# * PLRuby::Description::Function
#
#   To create a function 
#
# * PLRuby::Description::Function::SFRM
#
#   To create a function returning SET (SFRM Materialize)
#
# * PLRuby::Description::Function::ExprMultiResult
#
#   To create a function returning SET (ExprMultiResult)
#
# * PLRuby::Description::Trigger
#
#   To define a trigger
#
# * PLRuby::Description::Singleton_method
#
#   To define singleton methods
#
# * PLRuby::Description::Conversion
#
#   What conversions are done when this option is not disabled
#   (<em>--disable-conversion</em>)
#
# = Class hierarchy
#
# * PLRuby::PL
#
# * PLRuby::PL::Plan
#
# * PLRuby::PL::Cursor
#
# * PLRuby::PL::Transaction
#
# * PLRuby::BitString
#
# * PLRuby::Tinterval
#
# * PLRuby::NetAddr
#
# * PLRuby::MacAddr
#
# * PLRuby::Box
#
# * PLRuby::Circle
#
# * PLRuby::Path
#
# * PLRuby::Point
#
# * PLRuby::Polygon
#
# * PLRuby::Segment
#
# Global variable
#
# $Plans:: can be used to store prepared plans. (hash, tainted)
#
# 
module PLRuby
   #
   # Create a new transaction and yield an object <em>PL::Transaction</em>
   #
   # Only available with PostgreSQL >= 8.0
   def transaction()
      yield txn
   end

    # Ruby interface to PostgreSQL elog()
    #
    # Possible value for <tt>level</tt> are <tt>NOTICE</tt>, 
    # <tt>DEBUG</tt> and <tt>NOIND</tt>
    #
    # Use <tt>raise()</tt> if you want to simulate <tt>elog(ERROR, "...")</tt>
    def warn(level = NOTICE, message)
    end
end
#
# Pseudo module to describe the syntax to define function and triggers
#
# There is documentation for
# * PLRuby::Description::Function
#
#   To create a function 
#
# * PLRuby::Description::Function::SFRM
#
#   To create a function returning SET (SFRM Materialize)
#
# * PLRuby::Description::Function::ExprMultiResult
#
#   To create a function returning SET (ExprMultiResult)
#
# * PLRuby::Description::Trigger
#
#   To define a trigger
#
# * PLRuby::Description::Singleton_method
#
#   To define singleton methods
#
# * PLRuby::Description::Conversion
#
#   What conversions are done when this option is not disabled
#   (<em>--disable-conversion</em>)
module PLRuby::Description
end
# 
# To create a function in the PLRuby language use the syntax
# 
#    CREATE FUNCTION funcname(arguments_type) RETURNS type AS '
# 
#     # PLRuby function body
# 
#    ' LANGUAGE 'plruby';
# 
# when calling the function in a query, the arguments are given
# in the array <em>args</em>. To create a little max
# function returning the higher of two int4 values write :
# 
#    CREATE FUNCTION ruby_max(int4, int4) RETURNS int4 AS '
#        if args[0] > args[1]
#            return args[0]
#        else
#            return args[1]
#        end
#    ' LANGUAGE 'plruby';
# 
# 
# Tuple arguments are given as hash. Here is an example that defines
# the overpaid_2 function (as found in the older Postgres documentation)
# in PLRuby.
# 
#    CREATE FUNCTION overpaid_2 (EMP) RETURNS bool AS '
#        args[0]["salary"] > 200000 || 
#           (args[0]["salary"] > 100000 && args[0]["age"] < 30)
#    ' LANGUAGE 'plruby';
# 
# 
# === Warning : with PostgreSQL >= 7.4 "array" are given as a ruby Array
# 
# For example to define a function (int4[], int4) and return int4[],
# in version < 7.4 you write
# 
#    CREATE FUNCTION ruby_int4_accum(_int4, int4) RETURNS _int4 AS '
#        if /\\{(\\d+),(\\d+)\\}/ =~ args[0]
#            a, b = $1, $2
#            newsum = a + args[1]
#            newcnt = b + 1
#        else
#            raise "unexpected value #{args[0]}"
#        end
#        "{#{newsum},#{newcnt}}"
#    ' LANGUAGE 'plruby';
# 
# This must now (>= 7.4) be written
# 
#    CREATE FUNCTION ruby_int4_accum(_int4, int4) RETURNS _int4 AS '
#       a = args[0]
#       [a[0] + args[1], a[1] + 1]
#    ' LANGUAGE 'plruby';
# 
# === Release PostgreSQL 8.0
# 
# With this version, plruby can have named arguments and the previous functions
# can be written
# 
#    CREATE FUNCTION ruby_max(a int4, b int4) RETURNS int4 AS '
#        if a > b
#            a
#        else
#            b
#        end
#    ' LANGUAGE 'plruby';
# 
# 
#    CREATE FUNCTION overpaid_2 (emp EMP) RETURNS bool AS '
#        emp["salary"] > 200000 || 
#           (emp["salary"] > 100000 && emp["age"] < 30)
#    ' LANGUAGE 'plruby';
# 
# With this version, you can also use transaction. For example
# 
#    plruby_test=# create table tu (a int, b int);
#    CREATE TABLE
#    plruby_test=# create or replace function tt(abort bool) returns bool as '
#    plruby_test'#    transaction do |txn|
#    plruby_test'#       PL.exec("insert into tu values (1, 2)")
#    plruby_test'#       transaction do |txn1|
#    plruby_test'#          PL.exec("insert into tu values (3, 4)")
#    plruby_test'#          txn1.abort
#    plruby_test'#       end
#    plruby_test'#       PL.exec("insert into tu values (5, 6)")
#    plruby_test'#       txn.abort if abort
#    plruby_test'#    end
#    plruby_test'#    abort
#    plruby_test'# ' language 'plruby';
#    CREATE FUNCTION
#    plruby_test=# 
#    plruby_test=# select tt(true);
#     tt 
#    ----
#     t
#    (1 row)
#    
#    plruby_test=# select * from tu;
#     a | b 
#    ---+---
#    (0 rows)
#    
#    plruby_test=# select tt(false);
#     tt 
#    ----
#     f
#    (1 row)
#    
#    plruby_test=# select * from tu;
#     a | b 
#    ---+---
#     1 | 2
#     5 | 6
#    (2 rows)
#    
#    plruby_test=# 
# 
# 
module PLRuby::Description::Function
end
# 
# == Function returning SET (SFRM Materialize)
# 
# The return type must be declared as SETOF
# 
# The function must call <em>yield</em> to return rows or return a String
# which must be a valid SELECT statement
# 
# For example to concatenate 2 rows create the function
# 
#    plruby_test=# CREATE FUNCTION tu(varchar) RETURNS setof record
#    plruby_test-# AS '
#    plruby_test'#    size = PL.column_name(args[0]).size
#    plruby_test'#    res = nil
#    plruby_test'#    PL::Plan.new("select * from #{args[0]}", 
#    plruby_test'#                 "block" => 50).each do |row|
#    plruby_test'#       if res.nil?
#    plruby_test'#          res = row.values
#    plruby_test'#       else
#    plruby_test'#          res.concat row.values
#    plruby_test'#          yield res
#    plruby_test'#          res = nil
#    plruby_test'#       end
#    plruby_test'#    end
#    plruby_test'#    if res
#    plruby_test'#       res.concat Array.new(size)
#    plruby_test'#       yield res
#    plruby_test'#    end
#    plruby_test'# ' language 'plruby';
#    CREATE FUNCTION
#    plruby_test=# 
#    plruby_test=# select * from tt;
#     a | b  
#    ---+----
#     1 |  2
#     3 |  4
#     5 |  6
#     7 |  8
#     9 | 10
#    (5 rows)
#    
#    plruby_test=# select * from tu('tt') as tbl(a int, b int, c int, d int);
#     a | b  | c | d 
#    ---+----+---+---
#     1 |  2 | 3 | 4
#     5 |  6 | 7 | 8
#     9 | 10 |   |  
#    (3 rows)
#    
#    plruby_test=# 
# 
class PLRuby::Description::Function::SFRM
end
# == Function returning SET (ExprMultiResult)
# 
# The return type must be declared as SETOF
# 
# The function is called until it returns nil
# 
# The method PL#context and PL#context= give the possibility to store
# information between the call
# 
# For example
# 
#    plruby_test=# create or replace function vv(int) returns setof int as '
#    plruby_test'#    i = PL.context || 0
#    plruby_test'#    if i >= args[0]
#    plruby_test'#       nil
#    plruby_test'#    else
#    plruby_test'#       PL.context = i + 1
#    plruby_test'#    end
#    plruby_test'# ' language plruby;
#    CREATE FUNCTION
#    plruby_test=# 
#    plruby_test=# select * from uu;
#     b 
#    ---
#     2
#    (1 row)
#    
#    plruby_test=# 
#    plruby_test=# select *,vv(3) from uu;
#     b | vv 
#    ---+----
#     2 |  1
#     2 |  2
#     2 |  3
#    (3 rows)
#    
#    plruby_test=# 
# 
class PLRuby::Description::Function::ExprMultiResult
end
# 
# Trigger procedures are defined in Postgres as functions without
# arguments and a return type of trigger. In PLRuby the procedure is
# called with 4 arguments :
# 
# * new (hash, tainted)
#
#   an hash containing the values of the new table row on INSERT/UPDATE
#   actions, or empty on DELETE. 
# * old (hash, tainted)
#
#   an hash containing the values of the old table row on UPDATE/DELETE
#   actions, or empty on INSERT 
# * args (array, tainted, frozen)
#
#   An array of the arguments to the procedure as given in the CREATE
#   TRIGGER statement 
# * tg (hash, tainted, frozen)
#
#   The following keys are defined
#
#   - name
#
#     The name of the trigger from the CREATE TRIGGER statement.
#
#   - relname
#
#     The name of the relation who has fired the trigger
#
#   - relid
#
#     The object ID of the table that caused the trigger procedure to be invoked.
# 
#   - relatts
#
#     An array containing the name of the tables field.
# 
#   - when
#
#     The constant <em>PL::BEFORE</em>, <em>PL::AFTER</em> or
#     <em>PL::UNKNOWN</em> depending on the event of the trigger call.
# 
#   - level
#
#     The constant <em>PL::ROW</em> or <em>PL::STATEMENT</em>
#     depending on the event of the trigger call.
# 
#   - op
#
#     The constant <em>PL::INSERT</em>, <em>PL::UPDATE</em> or 
#     <em>PL::DELETE</em> depending on the event of the trigger call.
# 
# 
# The return value from a trigger procedure is one of the constant
# <em>PL::OK</em> or <em>PL::SKIP</em>, or an hash. If the
# return value is <em>PL::OK</em>, the normal operation
# (INSERT/UPDATE/DELETE) that fired this trigger will take
# place. Obviously, <em>PL::SKIP</em> tells the trigger manager to
# silently suppress the operation. The hash tells
# PLRuby to return a modified row to the trigger manager that will be
# inserted instead of the one given in <em>new</em> (INSERT/UPDATE
# only). Needless to say that all this is only meaningful when the
# trigger is BEFORE and FOR EACH ROW.
# 
# Here's a little example trigger procedure that forces an integer
# value in a table to keep track of the # of updates that are performed
# on the row. For new row's inserted, the value is initialized to 0 and
# then incremented on every update operation :
# 
#     CREATE FUNCTION trigfunc_modcount() RETURNS TRIGGER AS '
#         case tg["op"]
#         when PL::INSERT
#             new[args[0]] = 0
#           when PL::UPDATE
#               new[args[0]] = old[args[0]] + 1
#           else
#               return PL::OK
#           end
#           new
#       ' LANGUAGE 'plruby';
# 
#       CREATE TABLE mytab (num int4, modcnt int4, descr text);
# 
#       CREATE TRIGGER trig_mytab_modcount BEFORE INSERT OR UPDATE ON mytab
#           FOR EACH ROW EXECUTE PROCEDURE trigfunc_modcount('modcnt');
# 
# 
# 
# A more complex example (extract from test_setup.sql in the distribution)
# which use the global variable <em>$Plans</em> to store a prepared
# plan
# 
#    create function trig_pkey2_after() returns trigger as '
#       if ! $Plans.key?("plan_dta2_upd")
#           $Plans["plan_dta2_upd"] = 
#                PL::Plan.new("update T_dta2 
#                              set ref1 = $3, ref2 = $4
#                              where ref1 = $1 and ref2 = $2",
#                             ["int4", "varchar", "int4", "varchar" ]).save
#           $Plans["plan_dta2_del"] = 
#                PL::Plan.new("delete from T_dta2 
#                              where ref1 = $1 and ref2 = $2", 
#                             ["int4", "varchar"]).save
#       end
# 
#       old_ref_follow = false
#       old_ref_delete = false
# 
#       case tg["op"]
#       when PL::UPDATE
#           new["key2"] = new["key2"].upcase
#           old_ref_follow = (new["key1"] != old["key1"]) || 
#                            (new["key2"] != old["key2"])
#       when PL::DELETE
#           old_ref_delete = true
#       end
# 
#       if old_ref_follow
#           n = $Plans["plan_dta2_upd"].exec([old["key1"], old["key2"], new["key1"],
#    new["key2"]])
#           warn "updated #{n} entries in T_dta2 for new key in T_pkey2" if n != 0
#       end
# 
#       if old_ref_delete
#           n = $Plans["plan_dta2_del"].exec([old["key1"], old["key2"]])
#           warn "deleted #{n} entries from T_dta2" if n != 0
#       end
# 
#       PL::OK
#    ' language 'plruby';
# 
#    create trigger pkey2_after after update or delete on T_pkey2
#     for each row execute procedure
#     trig_pkey2_after();
# 
#
class PLRuby::Description::Trigger
end 
# == plruby_singleton_methods
# 
# Sometime it can be usefull to define methods (in pure Ruby) which can be
# called from a PLRuby function or a PLRuby trigger.
# 
# In this case, you have 2 possibilities
# 
# * the "stupid" way :-) :-) :-)
# 
# just close the current definition of the function (or trigger) with a
# <em>end</em> and define your singleton method without the final <em>end</em>
# 
# Here a small and useless example
# 
#           plruby_test=# CREATE FUNCTION tutu() RETURNS int4 AS '
#           plruby_test'#     toto(1, 3) + toto(4, 4)
#           plruby_test'# end
#           plruby_test'# 
#           plruby_test'# def PLtemp.toto(a, b)
#           plruby_test'#     a + b
#           plruby_test'# ' LANGUAGE 'plruby';
#           CREATE
#           plruby_test=# select tutu();
#           tutu
#           ----
#             12
#           (1 row)
#           
#           plruby_test=#
# 
# 
# * create a table plruby_singleton_methods with the columns (name, args, body)
# 
# At load time, PLRuby look if it exist a table plruby_singleton_methods 
# and if found try, for each row, to define singleton methods with the 
# template :
# 
#           def PLtemp.#{name}(#{args})
#               #{body}
#           end
# 
# The previous example can be written (you have a more complete example in
# test/plp/test_setup.sql)
# 
#           
#           plruby_test=# SELECT * FROM plruby_singleton_methods;
#           name|args|body 
#           ----+----+-----
#           toto|a, b|a + b
#           (1 row)
#           
#           plruby_test=# CREATE FUNCTION tutu() RETURNS int4 AS '
#           plruby_test'#     toto(1, 3) + toto(4, 4)
#           plruby_test'# ' LANGUAGE 'plruby';
#           CREATE
#           plruby_test=# select tutu();
#           tutu
#           ----
#             12
#           (1 row)
#           
#           plruby_test=#
#
# * Another example, if PLRuby was compiled with --enable-conversion and it 
#   exist  a column with the name '***' then it can create a singleton method
#   from a PLRuby function
# 
# 
#           plruby_test=# select * from plruby_singleton_methods;
#            name | args | body 
#           ------+------+------
#            ***  |      | 
#           (1 row)
#           
#           plruby_test=# create function add_value(int, int) returns int as '
#           plruby_test'# args[0] + args[1]
#           plruby_test'# ' language 'plruby';
#           CREATE FUNCTION
#           plruby_test=# 
#           plruby_test=# select add_value(10, 2);
#            add_value 
#           -----------
#                   12
#           (1 row)
#           
#           plruby_test=# 
#           plruby_test=# create function add_one(int) returns int as '
#           plruby_test'# add_value(args[0], 1)
#           plruby_test'# ' language 'plruby';
#           CREATE FUNCTION
#           plruby_test=# 
#           plruby_test=# select add_one(11);
#            add_one 
#           ---------
#                 12
#           (1 row)
#           
#           plruby_test=# 
# 
# 
# 
class PLRuby::Description::Singleton_method
end
#If the conversions was not disabled (--disable-conversion),  the following
#conversions are made
#
#                  PostgreSQL             Ruby
#                  ----------             ----
#                  OID                    Fixnum
#                  INT2OID                Fixnum
#                  INT4OID                Fixnum
#                  INT8OID                Fixnum (or Bignum)
#                  FLOAT4OID              Float
#                  FLOAT8OID              Float
#                  CASHOID                Float
#                  NUMERICOID             Float
#                  BOOLOID                true, false
#                  ABSTIMEOID             Time
#                  RELTIMEOID             Time
#                  TIMEOID                Time
#                  TIMETZOID              Time
#                  TIMESTAMPOID           Time
#                  TIMESTAMPTZOID         Time
#                  DATEOID                Time
#                  INTERVALOID            Time
#                  TINTERVALOID           Tinterval (new Ruby class)
#                  BITOID                 BitString (new Ruby class)
#                  VARBITOID              BitString (new Ruby class)
#                  INETOID                NetAddr   (new Ruby class)
#                  CIDROID                NetAddr   (new Ruby class)
#                  MACADDROID             MacAddr   (new Ruby class)
#                  POINTOID               Point     (new Ruby class)
#                  LSEGOID                Segment   (new Ruby class)
#                  BOXOID                 Box       (new Ruby class)
#                  PATHOID                Path      (new Ruby class)
#                  POLYGONOID             Polygon   (new Ruby class)
#                  CIRCLEOID              Circle    (new Ruby class)
#
#all others OID are converted to a String object
#
class PLRuby::Description::Conversion
end
#
# general module
# 
module PLRuby::PL
   class << self
   # 
   #Return the type of the arguments given to the function
   #
   def  args_type
   end
   # 
   #Return the name of the columns for the table
   #
   def  column_name(table)
   end
   # 
   #return the type of the columns for the table
   #
   def  column_type(table)
   end
   # 
   #Return the context (or nil) associated with a SETOF function 
   #(ExprMultiResult)
   #
   def  context
   end
   # 
   #Set the context for a SETOF function (ExprMultiResult)
   #
   def  context=
   end
   # 
   # 
   #Duplicates all occurences of single quote and backslash
   #characters. It should be used when variables are used in the query
   #string given to spi_exec or spi_prepare (not for the value list on
   #execp).
   #
   def  quote(string)
   end
   # 
   #Return the name of the columns for a function returning a SETOF
   #
   def  result_name
   end
   # 
   #Return the type of the columns for a function returning a SETOF
   #or the type of the return value
   #
   def  result_type
   end
   # 
   #Return the number of columns  for a function returning a SETOF
   #
   def  result_size
   end
   # 
   #Return the table description given to a function returning a SETOF
   #
   def  result_description
   end
   # 
   #
   #Call parser/planner/optimizer/executor for query. The optional
   #<em>count</em> value tells spi_exec the maximum number of rows to be
   #processed by the query.
   #    
   #* SELECT
   #If the query is a SELECT statement, an array is return (if count is
   #not specified or with a value > 1). Each element of this array is an
   #hash where the key is the column name.
   #    
   #If type is specified it can take the value
   #
   #* "array" return for each column an array with the element
   #["name", "value", "type", "len", "typeid"]
   #* "hash" return for each column an hash with the keys 
   #{"name", "value", "type", "len", "typeid"}
   #* "value" return all values
   #
   #For example this procedure display all rows in the table pg_table.
   #    
   #    CREATE FUNCTION pg_table_dis() RETURNS int4 AS '
   #       res = PL.exec("select * from pg_class")
   #       res.each do |x|
   #          warn "======================"
   #          x.each do |y, z|
   #             warn "name = #{y} -- value = #{z}"
   #         end
   #         warn "======================"
   #       end
   #       return res.size
   #    ' LANGUAGE 'plruby';
   #    
   #A block can be specified, in this case a call to yield() will be
   #made.
   #    
   #If count is specified with the value 1, only the first row (or
   #FALSE if it fail) is returned as a hash. Here a little example :
   #    
   #    
   #    CREATE FUNCTION pg_table_dis() RETURNS int4 AS '
   #       PL.exec("select * from pg_class", 1) { |y, z|
   #          warn "name = #{y} -- value = #{z}"
   #       }
   #       return 1
   #    ' LANGUAGE 'plruby';
   #    
   #Another example with count = 1
   #    
   #    create table T_pkey1 (
   #        skey1        int4,
   #        skey2        varchar(20),
   #        stxt         varchar(40)
   #    );
   #            
   #    create function toto() returns bool as '
   #       warn("=======")
   #       PL.exec("select * from T_pkey1", 1, "hash") do |a|
   #          warn(a.inspect)
   #       end
   #       warn("=======")
   #       PL.exec("select * from T_pkey1", 1, "array") do |a|
   #          warn(a.inspect)
   #       end
   #       warn("=======")
   #       PL.exec("select * from T_pkey1", 1) do |a|
   #          warn(a.inspect)
   #       end
   #       warn("=======")
   #       return true
   #    ' language 'plruby';
   #           
   #    plruby_test=# select toto();
   #    NOTICE:  =======
   #    NOTICE:  {"name"=>"skey1", "typeid"=>23, "type"=>"int4", "value"=>"12", "len"=>4}
   #    NOTICE:  {"name"=>"skey2", "typeid"=>1043, "type"=>"varchar", "value"=>"a", "len"=>20}
   #    NOTICE:  {"name"=>"stxt", "typeid"=>1043, "type"=>"varchar", "value"=>"b", "len"=>40}
   #    NOTICE:  =======
   #    NOTICE:  ["skey1", "12", "int4", 4, 23]
   #    NOTICE:  ["skey2", "a", "varchar", 20, 1043]
   #    NOTICE:  ["stxt", "b", "varchar", 40, 1043]
   #    NOTICE:  =======
   #    NOTICE:  ["skey1", "12"]
   #    NOTICE:  ["skey2", "a"]
   #    NOTICE:  ["stxt", "b"]
   #    NOTICE:  =======
   #     toto 
   #    ------
   #     t
   #    (1 row)
   #      
   #    plruby_test=# 
   #    
   #    
   #* SELECT INTO, INSERT, UPDATE, DELETE
   #return the number of rows insered, updated, deleted, ...
   #    
   #    
   #* UTILITY
   #return TRUE
   #    
   def  exec(string [, count [, type]])
   end
   #same than <em> exec</em>
   def  spi_exec(string [, count [, type]])
   end

   # 
   #
   #Deprecated : See <em>PL::Plan::new</em> and <em>PL::Plan#save</em>
   #
   #Prepares AND SAVES a query plan for later execution. It is a bit
   #different from the C level SPI_prepare in that the plan is
   #automatically copied to the toplevel memory context.
   #
   #If the query references arguments, the type names must be given as a
   #Ruby array of strings. The return value from prepare is a
   #<em>PL::Plan</em> object to be used in subsequent calls to
   #<em>PL::Plan#exec</em>.
   #
   #If the hash given has the keys <em>count</em>, <em>output</em> these values
   #will be given to the subsequent calls to <em>each</em>
   def  prepare(string[, types])
   end
   #same than <em> prepare</em>
   def  spi_prepare(string[, types])
   end
end
#
# class for prepared plan
#
class PL::Plan
   # 
   #
   #Prepares a query plan for later execution.
   #
   #If the query references arguments, the type names must be given as a
   #Ruby array of strings.
   #
   #If the hash given has the keys <em>output</em>, <em>count</em> these values
   #will be given to the subsequent calls to <em>each</em>
   #
   #If <em>"save"</em> as a true value, the plan will be saved 
   #
   #
   def  initialize(string, "types" => types, "count" => count, "output" => type, "save" => false)
   end
   # 
   #
   #Execute a prepared plan from <em>PL::Plan::new</em> with variable
   #substitution. The optional <em>count</em> value tells
   #<em>PL::Plan#exec</em> the maximum number of rows to be processed by the
   #query.
   #
   #If there was a typelist given to <em>PL::Plan::new</em>, an array
   #of <em>values</em> of exactly the same length must be given to
   #<em>PL::Plan#exec</em> as first argument. If the type list on
   #<em>PL::Plan::new</em> was empty, this argument must be omitted.
   #
   #If the query is a SELECT statement, the same as described for
   #<em>PL#exec</em> happens for the loop-body and the variables for
   #the fields selected.
   #
   #If type is specified it can take the values
   #* "array" return an array with the element ["name", "value", "type", "len", "typeid"]
   #* "hash" return an hash with the keys {"name", "value", "type", "len", "typeid"}
   #* "value" return an array with all values
   #
   #Here's an example for a PLRuby function using a prepared plan : 
   #
   #   CREATE FUNCTION t1_count(int4, int4) RETURNS int4 AS '
   #       if ! $Plans.key?("plan")
   #           # prepare the saved plan on the first call
   #           $Plans["plan"] = PL::Plan.new("SELECT count(*) AS cnt FROM t1 
   #                                          WHERE num >= $1 AND num <= $2",
   #                                         ["int4", "int4"]).save
   #       end
   #       n = $Plans["plan"].exec([args[0], args[1]], 1)
   #       n["cnt"]
   #   ' LANGUAGE 'plruby';
   #
   def  exec(values, [count [, type]])
   end
   #same than <em> exec</em>
   def  execp(values, [count [, type]])
   end
   #same than <em> exec</em>
   def  execp("values" => values, "count" => count, "output" => type)
   end
   # 
   # 
   #Create a new object PL::Cursor
   #
   #If output is specified it can take the values
   #* "array" return an array with the element ["name", "value", "type", "len", "typeid"]
   #*  "hash" return an hash with the keys {"name", "value", "type", "len", "typeid"}
   #* "value" return an array with all values
   #
   #If there was a typelist given to <em>PL::Plan::new</em>, an array
   #of <em>values</em> of exactly the same length must be given to
   #<em>PL::Plan#cursor</em>
   #
   def  cursor(name = nil, "values" => values, "output" => type)
   end
   # 
   #
   #Same then #exec but a call to SPI_cursor_open(), SPI_cursor_fetch() is made.
   #
   #Can be used only with a block and a SELECT statement
   #    
   #   create function toto() returns bool as '
   #          plan = PL::Plan.new("select * from T_pkey1")
   #          warn "=====> ALL"
   #          plan.each do |x|
   #             warn(x.inspect)
   #          end
   #          warn "=====> FIRST 2"
   #          plan.each("count" => 2) do |x|
   #             warn(x.inspect)
   #          end
   #          return true
   #   ' language 'plruby';
   #   
   #   plruby_test=# select * from T_pkey1;
   #    skey1 | skey2 | stxt 
   #   -------+-------+------
   #       12 | a     | b
   #       24 | c     | d
   #       36 | e     | f
   #   (3 rows)
   #   
   #   plruby_test=# 
   #   plruby_test=# select toto();
   #   NOTICE:  =====> ALL
   #   NOTICE:  {"skey1"=>"12", "skey2"=>"a", "stxt"=>"b"}
   #   NOTICE:  {"skey1"=>"24", "skey2"=>"c", "stxt"=>"d"}
   #   NOTICE:  {"skey1"=>"36", "skey2"=>"e", "stxt"=>"f"}
   #   NOTICE:  =====> FIRST 2
   #   NOTICE:  {"skey1"=>"12", "skey2"=>"a", "stxt"=>"b"}
   #   NOTICE:  {"skey1"=>"24", "skey2"=>"c", "stxt"=>"d"}
   #    toto 
   #   ------
   #    t
   #   (1 row)
   #   
   #   plruby_test=# 
   #
   def  each(values, [count [, type ]]) { ... }
   end
   #same than <em> each</em>
   def  fetch(values, [count [, type ]]) { ... }
   end
   #same than <em> each</em>
   def  fetch("values" => values, "count" => count, "output" => type) { ... }
   end
   # 
   #
   #Release a query plan
   #
   def  release
   end
   # 
   #  
   #Save a query plan for later execution. The plan is copied to the
   #toplevel memory context.
   #
   def  save
   end
end
end

#
# A cursor is created with the method PL::Plan#cursor
#
class PLRuby::PL::Cursor
   #Closes a cursor
   #
   def  close
   end
   # 
   # 
   #Iterate over all rows (forward)
   #
   def  each 
      yield row
   end
   # 
   #
   #Fetches some rows from a cursor
   #
   #if count > 0 fetch forward else backward
   #
   def  fetch(count = 1)
   end
   #same than <em> fetch</em>
   def  row(count = 1)
   end
   # 
   #
   #Move a cursor : if count > 0 move forward else backward
   #
   def  move(count)
   end
   # 
   #
   #Iterate over all rows (backward)
   #
   def  reverse_each 
      yield row
   end
end

#
# A transaction is created with the global function #transaction
#
# Only available with PostgreSQL >= 8.0
#
class PLRuby::PL::Transaction

   # abort the transaction
   def abort
   end

   # commit the transaction
   def commit
   end
end
#
# The class PLRuby::BitString implement the PostgreSQL type <em>bit</em>
# and <em>bit varying</em>
#
class PLRuby::BitString
   include Comparable
   include Enumerable

   class << self

      # Convert a <em>String</em> to a <em>BitString</em>
      def from_string(string, length = strlen(string))
      end
   end

   # comparison function for 2 <em>BitString</em> objects
   #
   # All bits are considered and additional zero bits may make one string
   # smaller/larger than the other, even if their zero-padded values would
   # be the same.
   def <=>(other)
   end

   # Concatenate <em>self</em> and <em>other</em>
   def +(other)
   end

   # AND operator
   def &(other)
   end

   # OR operator
   def |(other)
   end

   # XOR operator
   def ^(other)
   end

   # NOT operator
   def ~()
   end

   # LEFT SHIFT operator
   def <<(lshft)
   end

   # RIGHT SHIFT operator
   def >>(rshft)
   end

   # Element reference with the same syntax that for a <em>String</em> object
   #
   # Return a <em>BitString</em> or a <em>Fixnum</em> 0, 1
   #
   #   bitstring[fixnum]
   #   bitstring[fixnum, fixnum]
   #   bitstring[range]
   #   bitstring[regexp]
   #   bitstring[regexp, fixnum]
   #   bitstring[string]
   #   bitstring[other_bitstring]
   def [](*args)
   end

   # Element assignment with the same syntax that for a <em>String</em> object
   #
   #   bitstring[fixnum] = fixnum
   #   bitstring[fixnum] = string_or_bitstring
   #   bitstring[fixnum, fixnum] = string_or_bitstring
   #   bitstring[range] = string_or_bitstring
   #   bitstring[regexp] = string_or_bitstring
   #   bitstring[regexp, fixnum] = string_or_bitstring
   #   bitstring[other_str] = string_or_bitstring
   def []=(*args)
   end

   # append <em>other</em> to <em>self</em>
   def concat(other)
   end

   # iterate other each bit
   def each
   end

   # return <em>true</em> if <em>other</em> is included in <em>self</em>
   def include?(other)
   end

   # return the position of <em>other</em> in <em>self</em>
   #
   # return <em>nil</em> if <em>other</em> is not included in <em>self</em>
   def index(other)
   end

   # create a new <em>BitString</em> object with <em>nbits</em> bits
   #
   # <em>init</em> can be a <em>Fixnum</em> or a <em>String</em>
   #
   # For a <em>String</em> the first character can be 'x', 'X' for and
   # hexadecimal representation, or 'b', 'B' for a binary representation. 
   # The default is a binary representation
   def initialize(init, nbits = -1)
   end

   # return the length of <em>self</em> in bits
   def length
   end

   # return the length of <em>self</em> in octets
   def octet_length
   end

   # append <em>other</em> to <em>self</em>
   def push(other)
   end

   # convert <em>self</em> to a <em>Fixnum</em>
   def to_i
   end

   # convert <em>self</em> to a <em>String</em>
   def to_s
   end

end

#
# The class PLRuby::NetAddr implement the PostgreSQL type <em>inet</em>
# and <em>cidr</em>
#
class PLRuby::NetAddr
   include Comparable

   class << self

      # Convert a <em>String</em> to a <em>NetAddr</em>
      def from_string(string, cidr = false)
      end
   end

   # comparison function for 2 <em>NetAddr</em> objects
   #
   # comparison is first on the common bits of the network part, then on
   # the length of the network part, and then on the whole unmasked address.
   def <=>(other)
   end

   # return the abbreviated display format as a <em>String</em> object
   def abbrev
   end

   # return the broadcast address from the network
   def broadcast
   end

   # return true if <em>other</em> is included in <em>self</em>
   def contain?(other)
   end

   # return true if <em>other</em> is included in <em>self</em>, or equal
   def contain_or_equal?(other)
   end

   # return true if <em>self</em> is included in <em>other</em>
   def contained?(other)
   end

   # return true if <em>self</em> is included in <em>other</em>, or equal
   def contained_or_equal?(other)
   end

   # return the String "AF_INET" or "AF_INET6"
   def family
   end

   # return the first address in the network
   def first
   end

   # extract the IP address and return it as a <em>String</em> 
   def host
   end

   # return the host mask for network
   def hostmask
   end

   # create a <em>NetAddr</em> from a <em>String</em>
   def initialize(string, cidr = false)
   end

   # return the last address in the network
   def last
   end

   # return the length of the netmask
   def masklen
   end

   # return the netmask for the network
   def netmask
   end

   # return the network part of the address
   def network
   end

   # return a new <em>NetAddr</em> with netmask length <em>len</em>
   def set_masklen(len)
   end 

   # return the string representation of the address
   def to_s
   end

end

#
# The class PLRuby::MacAddr implement the PostgreSQL type <em>macaddr</em>
#
class PLRuby::MacAddr
   include Comparable

   class << self

      # Convert a <em>String</em> to a <em>MacAddr</em>
      def from_string(string, cidr = false)
      end
   end

   # comparison function for 2 <em>MacAddr</em> objects
   def <=>(other)
   end

   # create a <em>MacAddr</em> from a <em>String</em>
   def initialize(string)
   end

   # return the string representation of the MAC address
   def to_s
   end

   # return a new object with the last 3 bytes set to zero
   def truncate
   end

end

#
# The class PLRuby::Tinterval implement the PostgreSQL type <em>tinterval</em>
#
class PLRuby::Tinterval
   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Tinterval</em>
      def from_string(string)
      end
   end

   # return a <em>Time</em> which is the high value of the interval
   def high
   end

   # set the high value for the interval
   def high=(time)
   end

   # create a <em>Tinterval</em> with the 2 <em>Time</em> objects
   # <em>low</em> and <em>high</em>
   def initialize(low, high)
   end

   # return a <em>Time</em> which is the low value of the interval
   def low
   end

   # set the low value for the interval
   def low=(time)
   end
 
   # return the string representation of the object
   def to_s
   end

end

#
# The class PLRuby::Box implement the PostgreSQL type <em>box</em>
#
class PLRuby::Box
   include Comparable

   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Box</em> object
      def from_string(string)
      end
   end

   # translate (right, up) <em>self</em>
   def +(point)
   end

   # translate (left, down) <em>self</em>
   def -(point)
   end

   # scale and rotate <em>self</em>
   def *(point)
   end

   # scale and rotate <em>self</em>
   def /(point)
   end

   # return true if the 2 boxes <em>self</em> and <em>other</em> are identical
   def ===(other)
   end

   # comparison operator for 2 Box based on the area of the 2 objects, i.e.
   # self.area <=> box.area
   def <=>(other)
   end

   # return true if  <em>self</em> is above <em>other</em>
   def above?(other)
   end

   # return the area of the Box
   def area
   end

   # return true if  <em>self</em> is below <em>other</em>
   def below?(other)
   end

   # return the center point of the Box
   def center
   end

   # closest point to <em>other</em>
   #
   # <em>other</em> can be a Point, or Segment
   def closest(other)
   end

   # return true if  <em>self</em> contain <em>other</em>
   def contain?(other)
   end

   # return true if  <em>self</em> is contained by <em>other</em>
   def contained?(other)
   end

   # return a line Segment which happens to be the
   # positive-slope diagonal of Box
   def diagonal
   end

   # return the height of the Box (vertical magnitude)
   def height
   end

   # return true if  <em>self</em> is contained by <em>other</em>
   def in?(other)
   end

   # create a new Box object
   #
   # <em>args</em> can be 2 Point objects (low, high) or 4 Float objects
   # (low.x, low.y, high.x, high.y)
   def initialize(*args)
   end

   # returns the overlapping portion of two boxes,
   # or <em>nil</em> if they do not intersect.
   def intersection(other)
   end

   # returns  true if the Segment <em>segment</em>
   # intersect with the Box
   #
   # Segment completely inside box counts as intersection.
   # If you want only segments crossing box boundaries,
   # try converting Box to Path first.
   # 
   def intersect?(segment)
   end

   # return true if <em>self</em> is strictly left of <em>other</em>
   def left?(other)
   end

   # return true if <em>self</em> overlap <em>other</em>
   def overlap?(other)
   end
 
   # return true if the right edge of <em>self</em> is to the left of
   # the right edge of <em>other</em>
   def overleft?(other)
   end

   # return true if the left edge of <em>self</em> is to the right of
   # the left edge of <em>other</em>
   def overright?(other)
   end

   # return true if <em>self</em> is strictly right of <em>other</em>
   def right?(other)
   end

   # return true if the 2 boxes <em>self</em> and <em>other</em> are identical
   def same?(other)
   end

   # convert a Box to a Circle
   def to_circle
   end

   # return the center Point of the Box
   def to_point
   end

   # convert a Box to a Polygon
   def to_polygon
   end

   # return a line Segment which happens to be the
   # positive-slope diagonal of Box
   def to_segment
   end

   # return the width of the Box (horizontal magnitude)
   def width
   end
end

#
# The class PLRuby::Path implement the PostgreSQL type <em>path</em>
#
class PLRuby::Path
   include Comparable

   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Path</em>
      def from_string(string)
      end
   end

   # concatenate the two paths (only if they are both open)
   def <<(path)
   end

   # translate (right, up) <em>self</em>
   def +(point)
   end

   # translate (left, down) <em>self</em>
   def -(point)
   end

   # scale and rotate <em>self</em>
   def *(point)
   end

   # scale and rotate <em>self</em>
   def /(point)
   end

   # comparison function based on the path cardinality, i.e.
   # self.npoints <=> other.npoints
   def <=>(other)
   end

   # make a closed path
   def close
   end

   # return true if <em>self</em> is a closed path
   def closed?
   end

   # concatenate the two paths (only if they are both open)
   def concat(path)
   end

   # create a new Path object from the Array of Point <em>points</em>
   def initialize(points, closed = false)
   end

   # return the length of <em>self</em>
   def length
   end

   # return the path cardinality
   def npoints
   end

   # make an open path
   def open
   end

   # convert <em>self</em> to a Polygon object
   def to_polygon
   end
end
#
# The class PLRuby::Point implement the PostgreSQL type <em>point</em>
#
class PLRuby::Point
   include Comparable

   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Point</em>
      def from_string(string)
      end
   end

   # translate (right, up) <em>self</em>
   def +(point)
   end

   # translate (left, down) <em>self</em>
   def -(point)
   end

   # scale and rotate <em>self</em>
   def *(point)
   end

   # scale and rotate <em>self</em>
   def /(point)
   end

   # return the coordinate
   #
   # <em>indice</em> can have the value 0 or 1
   def [](indice)
   end

   # set the coordinate
   #
   # <em>indice</em> can have the value 0 or 1
   def []=(indice, value)
   end

   # return true if <em>self</em> and <em>other</em> are the same,
   # i.e. self.x == other.x && self.y == other.y
   def ==(other)
   end

   # return true if <em>self</em> is above <em>other</em>,
   # i.e. self.y > other.y
   def above?(other)
   end

   # return true if <em>self</em> is below <em>other</em>,
   # i.e. self.y < other.y
   def below?(other)
   end

   # return true if <em>self</em> is contained in <em>other</em>
   # 
   # <em>other</em> can be Point, Polygon or a Circle object
   def contained?(other)
   end

   # return true if <em>self</em> and <em>other</em> are horizontal,
   # i.e. self.y == other.y
   def horizontal?(other)
   end

   # return true if <em>self</em> is contained in <em>other</em>
   # 
   # <em>other</em> can be Point, Polygon or a Circle object
   def in?(other)
   end

   # create a Point with the 2 Float object (x, y)
   def initialize(x, y)
   end

   # return true if <em>self</em> is at the left of <em>other</em>,
   # i.e. self.x < other.x
   def left?(other)
   end

   # return true if <em>self</em> is on <em>other</em>
   # 
   # <em>other</em> can be Point, Segment, Box or Path object
   def on?(other)
   end

   # return true if <em>self</em> is at the right of <em>other</em>,
   # i.e. self.x > other.x
   def right?(other)
   end

   # return true if <em>self</em> and <em>other</em> are vertical,
   # i.e. self.x == other.x
   def vertical?(other)
   end

   # return <em>x</em> for <em>self</em>
   def x
   end

   # set the <em>x</em> value for <em>self</em>
   def x=(value)
   end

   # return <em>y</em> for <em>self</em>
   def y
   end

   # set the <em>y</em> value for <em>self</em>
   def y=(value)
   end

end

#
# The class PLRuby::Segment implement the PostgreSQL type <em>lseg</em>
#
class PLRuby::Segment
   include Comparable

   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Segment</em>
      def from_string(string)
      end
   end

   # comparison function for the 2 segments, returns
   #
   #  0  if self[0] == other[0] && self[1] == other[1]
   #
   #  1  if distance(self[0], self[1]) > distance(other[0], other[1]) 
   #
   #  -1 if distance(self[0], self[1]) < distance(other[0], other[1]) 
   def <=>(other)
   end

   # return the center of the segment
   def center
   end

   # closest point to other
   #
   # <em>other</em> can be a Point, Segment or Box
   #
   # With a point, take the closest endpoint 
   # if the point is left, right, above, or below the segment, otherwise 
   # find the intersection point of the segment and its perpendicular through
   # the point.
   def closest(other)
   end

   # returns true if <em>self</em> is a horizontal Segment
   def horizontal?
   end

   # create a Segment from the 2 Point p0, p1
   def initialize(point0, point1)
   end

   # returns true if <em>self</em> and <em>other</em> intersect
   def intersect?(other)
   end

   # returns the Point where the 2 Segment <em>self</em> and <em>other</em>
   # intersect or nil
   def intersection(other)
   end

   # return the length of <em>self</em>, i.e. the distnace between the 2 points
   def length
   end

   # return true if <em>self</em> is on <em>other</em>
   #
   # <em>other</em> can be a Segment, or a Box object
   def on?(other)
   end

   # returns true if the 2 Segment <em>self</em> and <em>other</em> 
   # are parallel
   def parallel?(other)
   end

   # returns true if <em>self</em> is perpendicular to <em>other</em>
   def perpendicular?(other)
   end

   # conversion function to a Point, return the center of the segment
   def to_point
   end

   # returns true if <em>self</em> is a vertical Segment
   def vertical?
   end
end
#
# The class PLRuby::Polygon implement the PostgreSQL type <em>polygon</em>
#
class PLRuby::Polygon
   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Polygon</em>
      def from_string(string)
      end
   end

   # return true if <em>self</em> is the same as <em>other</em>, i.e. all
   # the points are the same
   def ==(other)
   end

   # return the center of <em>self</em>, i.e. create a circle and return its 
   # center
   def center
   end

   # return true if <em>self</em> contains <em>other</em>
   #
   # <em>other</em> can be a Point or a Polygon
   def contain?(other)
   end

   # return true if <em>self</em> is contained in <em>other</em> by determining
   # if <em>self</em> bounding box is contained by <em>other</em>'s bounding box.
   def contained?(other)
   end

   # return true if <em>self</em> is contained in <em>other</em> by determining
   # if <em>self</em> bounding box is contained by <em>other</em>'s bounding box.
   def in?(other)
   end

   # create a new Polygon object from the Array of Point <em>points</em>
   def initialize(points, closed = false)
   end

   # return true if <em>self</em> is strictly left of <em>other</em>, i.e.
   # the right most point of <em>self</em> is left of the left
   # most point of <em>other</em>
   def left?(other)
   end

   # return true if <em>self</em> is overlapping or left of <em>other</em>,
   # i.e. the left most point of <em>self</em> is left of the right
   # most point of <em>other</em>
   def overleft?(other)
   end

   # return true if <em>self</em> is overlapping or right of <em>other</em>,
   # i.e. the right most point of <em>self</em> is right of the left
   # most point of <em>other</em>
   def overright?(other)
   end

   # return true if <em>self</em> and <em>other</em> overlap by determining if
   # their bounding boxes overlap.
   def overlap?(other)
   end

   # return the number of points in <em>self</em>
   def npoints
   end

   # return true if <em>self</em> is strictly right of <em>other</em>, i.e.
   # the left most point of <em>self</em> is right of the left
   # most point of <em>other</em>
   def right?(other)
   end

   # return true if <em>self</em> is the same as <em>other</em>, i.e. all 
   # the points are the same
   def same?(other)
   end

   # convert <em>self</em> to a Box
   def to_box
   end

   # convert <em>self</em> to a Circle
   def to_circle
   end

   # convert <em>self</em> to a Path
   def to_path
   end

   # convert <em>self</em> to a Point by returning its center
   def to_point
   end
end

#
# The class PLRuby::Circle implement the PostgreSQL type <em>circle</em>
#
class PLRuby::Circle
   include Comparable

   class << self

      # Convert a <em>String</em> (PostgreSQL representation)
      # to a <em>Circle</em>
      def from_string(string)
      end
   end

   # translate (right, up) <em>self</em>
   def +(point)
   end

   # translate (left, down) <em>self</em>
   def -(point)
   end

   # scale and rotate <em>self</em>
   def *(point)
   end

   # scale and rotate <em>self</em>
   def /(point)
   end

   # comparison function based on area,
   # i.e. self.area <=> other.area
   def <=>(other)
   end

   # return the area
   def area
   end

   # return true if <em>self</em> is entirely above <em>other</em>
   def above?(other)
   end

   # return true if <em>self</em> is entirely below <em>other</em>
   def below?(other)
   end

   # return true if <em>self</em> contain <em>other</em>
   def contain?(other)
   end

   # return true if <em>self</em> is contained in <em>other</em>
   def contained?(other)
   end

   # return the diameter
   def diameter
   end

   # create a Circle object with <em>center</em> and <em>radius</em>
   #
   # <em>center</em> can be a Point or an Array [x, y]
   def initialize(center, radius)
   end

   # return true if <em>self</em> overlap <em>other</em>
   def overlap?(other)
   end

   # return true if the right edge of <em>self</em> is to the left of
   # the right edge of <em>other</em>
   def overleft?(other)
   end

   # return true if <em>self</em> is strictly left of <em>other</em>
   def left?(other)
   end

   # return true if the left edge of <em>self</em> is to the right of
   # the left edge of <em>other</em>
   def overright?(other)
   end

   # return the radius
   def radius
   end

   # return true if <em>self</em> is strictly right of <em>other</em>
   def right?(other)
   end

   # return true if <em>self</em> is the same than <em>other</em>, i.e.
   # self.center == other.center && self.radius == other.radius
   def same?(other)
   end

   # convert <em>self</em> to a Box
   def to_box
   end

   # convert <em>self</em> to a Point by returning its center
   def to_point
   end

   # convert <em>self</em> to a Polygon with <em>npts</em> Points
   def to_polygon(npts)
   end

end
