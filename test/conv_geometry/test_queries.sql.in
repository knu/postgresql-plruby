set client_min_messages = 'WARNING';

create table pl_box (
  data box, barea float, boverlaps bool, boverleft bool, boverright bool,
  bleft bool, bright bool
);

create or replace function box_val(box[]) returns setof pl_box as '
  b1 = Box.new(2.5,2.5,1.0,1.0)
  b2 = Box.new(2.0,2.0,2.5,2.5)
  b3 = Box.new(3.0,3.0,5.0,5.0)
  args[0].each do |b|
     yield [b, b.area, b.overlap?(b1), b.overleft?(b2), 
            b.overright?(b2), b.left?(b3), b3.right?(b)]
  end
' language 'plruby';

select * from 
box_val('{(2.0,2.0,0.0,0.0);(1.0,1.0,3.0,3.0);(2.5,2.5,2.5,3.5);(3.0,3.0,3.0,3.0)}'::box[]);

drop table pl_box cascade;

create table pl_box (
   b box, bcmp1 int, bcmp2 int, bin bool, bcontain bool, bcenter point
);

create or replace function box_val(box[]) returns setof pl_box as '
  b1 = Box.new(3.0,3.0,5.0,5.0)
  b2 = Box.new(3.5,3.0,4.5,3.0)
  b3 = Box.new(0,0,3,3)
  args[0].each do |b|
     yield [b, b <=> b1, b <=> b2, b.in?(b3), b.contain?(b3), b.center]
  end
' language 'plruby';

select * from 
box_val('{(2.0,2.0,0.0,0.0);(1.0,1.0,3.0,3.0);(2.5,2.5,2.5,3.5);(3.0,3.0,3.0,3.0)}'::box[]);

drop table pl_box cascade;

create table pl_box (
   b0 box, b1 box, b2 box, b3 box
);

create or replace function box_val(box[]) returns setof pl_box as '
  p0 = [Point.new(-10.0,0.0), Point.new(-3.0,4.0),
        Point.new(5.1, 34.5), Point.new(-5.0,-12.0)]
  args[0].each do |b|
     p0.each do |p|
        yield [b + p, b - p, b * p, b / p]
     end
     p0.each do |p|
        yield [p + b, p - b, p * b, p / b]
     end
  end
' language 'plruby';

select * from 
box_val('{(2.0,2.0,0.0,0.0);(1.0,1.0,3.0,3.0);(2.5,2.5,2.5,3.5);(3.0,3.0,3.0,3.0)}'::box[]);

drop table pl_box cascade;

create table pl_box (
   p point, dp float, d0 float, c circle, w float, h float
);

create or replace function box_val(box[]) returns setof pl_box as '
   p = Point.new(6,4)
   args[0].each do |b| 
      p0 = b.center
      yield [p0, Geometry::distance(p, b),Geometry::distance(p, p0),
             b.to_circle, b.height, b.width]
   end
' language 'plruby';

select * from 
box_val('{(2.0,2.0,0.0,0.0);(1.0,1.0,3.0,3.0);(2.5,2.5,2.5,3.5);(3.0,3.0,3.0,3.0)}'::box[]);

drop table pl_box cascade;

create table pl_box (
   nb int, ll float, pc bool
);

create or replace function path_val(path[]) returns setof pl_box as '
   args[0].each do |b| 
      yield [b.npoints, b.length, b.closed?]
   end
' language 'plruby';

select * from 
path_val('{"[1,2,3,4]","(0,0),(3,0),(4,5),(1,6)","11,12,13,14,25,12"}'::path[]);

drop table pl_box cascade;

create table pl_box (
   p path, p0 path, p1 path
);

create or replace function path_val(path[]) returns setof pl_box as '
   p0 = Point.new(6,7)
   args[0].each do |b| 
      yield [b, b + p0, b - p0]
   end
' language 'plruby';

select * from 
path_val('{"[1,2,3,4]","(0,0),(3,0),(4,5),(1,6)","11,12,13,14,25,12"}'::path[]);

drop table pl_box cascade;

create table pl_box (
   p path, p0 path, p1 path
);

create or replace function path_val(path[]) returns setof pl_box as '
   p0 = Point.new(6,7)
   args[0].each do |b| 
      yield [b, b * p0, b / p0]
   end
' language 'plruby';

select * from 
path_val('{"[1,2,3,4]","(0,0),(3,0),(4,5),(1,6)","11,12,13,14,25,12"}'::path[]);

drop table pl_box cascade;

create table pl_box (
   center point, area float, radius float, diameter float
);

create or replace function circle_val(circle[]) returns setof pl_box as '
   args[0].each do |b|
      yield b.center, b.area, b.radius, b.diameter
   end
' language 'plruby';

select * from 
circle_val('{"<(5,1),3>","<(1,2),100>","<(100,200),10>","<(100,1),115>","1,3,5"}'::circle[]);


drop table pl_box cascade;

create table pl_box (
   p circle, p0 circle, p1 circle
);

create or replace function circle_val(circle[]) returns setof pl_box as '
   p0 = Point.new(6,7)
   args[0].each do |b| 
      yield [b, b + p0, b - p0]
   end
' language 'plruby';

select * from 
circle_val('{"<(5,1),3>","<(1,2),100>","<(100,200),10>","<(100,1),115>","1,3,5"}'::circle[]);

drop table pl_box cascade;

create table pl_box (
   p circle, p0 circle, p1 circle
);

create or replace function circle_val(circle[]) returns setof pl_box as '
   p0 = Point.new(6,7)
   args[0].each do |b| 
      yield [b, p0 * b, p0 / b ]
   end
' language 'plruby';

select * from 
circle_val('{"<(5,1),3>","<(1,2),100>","<(100,200),10>","<(100,1),115>","1,3,5"}'::circle[]);

drop table pl_box cascade;

create table pl_box (
  overlap bool, overleft bool, overright bool,
  bleft bool, bright bool, below bool, above bool
);

create or replace function circle_val(circle, circle) returns setof pl_box as '
    b0 = args[0]
    b1 = args[1]
    yield  b0.overlap?(b1), b0.overleft?(b1), b0.overright?(b1), 
           b0.left?(b1), b0.right?(b1), b0.below?(b1), b0.above?(b1)
' language 'plruby';


select * from 
circle_val('<(5,1),3>'::circle, '<(1,2),100>'::circle);

select * from 
circle_val('<(100,200),10>'::circle, '<(100,1),115>'::circle);

