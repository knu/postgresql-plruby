drop function inet_val(inet);
drop table pl_inet;

create table pl_inet (
  host text, abbrev text, masklen int, family text,
  network inet, netmask inet, hostmask inet, first inet, last inet
);


create or replace function inet_val(inet) returns pl_inet as '
   a = args[0]
   [a.host, a.abbrev, a.masklen, a.family, a.network, a.netmask, 
    a.hostmask, a.first, a.last]
 ' language 'plruby';


select * from inet_val('192.168.1'::cidr);
select * from inet_val('192.168.1.226/24'::inet);
select * from inet_val('192.168.1.0/24'::cidr);
select * from inet_val('192.168.1.226'::inet);
select * from inet_val('192.168.1'::cidr);
select * from inet_val('192.168.1.0/24'::inet);
select * from inet_val('192.168.1'::cidr);
select * from inet_val('192.168.1.0/25'::inet);
select * from inet_val('192.168.1'::cidr);
select * from inet_val('192.168.1.255/24'::inet);
select * from inet_val('192.168.1'::cidr);
select * from inet_val('192.168.1.255/25'::inet);
select * from inet_val('10'::cidr);
select * from inet_val('10.1.2.3/8'::inet);
select * from inet_val('10.0.0.0'::cidr);
select * from inet_val('10.1.2.3/8'::inet);
select * from inet_val('10.1.2.3'::cidr);
select * from inet_val('10.1.2.3/32'::inet);
select * from inet_val('10.1.2'::cidr);
select * from inet_val('10.1.2.3/24'::inet);
select * from inet_val('10.1'::cidr);
select * from inet_val('10.1.2.3/16'::inet);
select * from inet_val('10'::cidr);
select * from inet_val('10.1.2.3/8'::inet);
select * from inet_val('10'::cidr);
select * from inet_val('11.1.2.3/8'::inet);
select * from inet_val('10'::cidr);
select * from inet_val('9.1.2.3/8'::inet);
select * from inet_val('10:23::f1'::cidr);
select * from inet_val('10:23::f1/64'::inet);
select * from inet_val('10:23::8000/113'::cidr);
select * from inet_val('10:23::ffff'::inet);
select * from inet_val('::ffff:1.2.3.4'::cidr);
select * from inet_val('::4.3.2.1/24'::inet);

create or replace function mac_cmp(macaddr, macaddr) returns int as '
   args[0] <=> args[1]
' language 'plruby';

select mac_cmp('00:07:E9:85:3E:C5'::macaddr, '00:E0:29:3E:E7:25'::macaddr);

create or replace function mac_trunc(macaddr) returns macaddr as '
   args[0].truncate
' language 'plruby';

select mac_trunc('00:07:E9:85:3E:C5'::macaddr);
select mac_trunc('00:E0:29:3E:E7:25'::macaddr);
