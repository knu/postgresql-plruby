#!/usr/bin/ruby
require 'rbconfig'
include Config
pwd = Dir.pwd
pwd.sub!(%r{[^/]+/[^/]+$}, "")
language, extension = 'C', '_new'
opaque = 'opaque'
case ARGV[0].to_i
when 70
   language = 'newC'
when 73, 74
   extension = "_new_trigger"
   opaque = 'language_handler'
end
begin
   f = File.new("test_setup.sql", "w")
   IO.foreach("test_setup#{extension}.sql") {|x| f.print x }
   f.close
   f = File.new("test_mklang.sql", "w")
   f.print <<EOF
 
create function plruby_call_handler() returns #{opaque}
    as '#{pwd}plruby.#{CONFIG["DLEXT"]}'
   language '#{language}';
 
create trusted procedural language 'plruby'
        handler plruby_call_handler
        lancompiler 'PL/Ruby';
EOF
   f.close
rescue
   raise "Why I can't write #$!"
end
