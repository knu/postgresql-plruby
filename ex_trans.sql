create table tu (a int, b int);

create or replace function uu(abort bool) returns bool as '
   transaction do |txn|
      PL.exec("insert into tu values (1, 2)")
      transaction do |txn1|
         PL.exec("insert into tu values (3, 4)")
         txn1.abort
      end
      PL.exec("insert into tu values (5, 6)")
      txn.abort if abort
   end
   abort
' language 'plruby';

      
create or replace function uu() returns bool as '
   transaction do |txn1|
      PL.exec("insert into tu values (3, 4)")
      txn1.abort
   end
   true
' language 'plruby';

create or replace function tt(abort bool) returns bool as '
   transaction do |txn|
      PL.exec("insert into tu values (1, 2)")
      PL.exec("select uu()")
      PL.exec("insert into tu values (5, 6)")
      txn.abort if abort
   end
   abort
' language 'plruby';
