create table bit_op (
   b0 bit(8), b1 bit(8), band bit(8), bor bit(8),
   bxor bit(8), bnot0 bit(8), bnot1 bit(8)
);

create function bt(integer, integer) returns bit_op as '
   b0 = BitString.new(args[0], 8)
   b1 = BitString.new(args[1], 8)
   [b0, b1, b0 & b1, b0 | b1, b0 ^ b1, ~b0, ~b1]
' language 'plruby';

select * from bt(12, 24);
select * from bt(12, 32);
select * from bt(15, 278);


create function be(integer) returns setof integer as '
   BitString.new(args[0], 8).each {|i| yield i}
' language 'plruby';

select * from be(12);
select * from be(257);

create function bx(integer, integer) returns bit varying as '
   BitString.new(*args)
' language 'plruby';

select bx(12, 6);
select bx(12, 8);

create table bit_sht (
   b0 bit(8), shft int, bl bit(8), br bit(8), bs text, bi integer,
   sz integer, osz integer
);

create function bs(integer, integer) returns bit_sht as '
   b0 = BitString.new(args[0], 8)
   [b0, args[1], b0 << args[1], b0 >> args[1], b0.to_s, b0.to_i,
    b0.size, b0.octet_size]
' language 'plruby';

select * from bs(12, 2);
select * from bs(277, -3);

create function ext(text, integer) returns integer as '
   b0 = BitString.new(args[0])
   b0[args[1]]
' language 'plruby';

select ext('011110', 0);
select ext('011110', -1);
select ext('011110', 1);
select ext('011110', 4);

create function ext2(text, integer, integer) returns bit varying(8) as '
   b0 = BitString.new(args[0])
   b0[args[1], args[2]]
' language 'plruby';

select ext2('0111101', 0, 2);
select ext2('0111101', -1, 3);
select ext2('0111101', 1, 2);
select ext2('0111101', 4, 1);
