#!/usr/bin/ruby
require 'rbconfig'
include Config
pwd = Dir.pwd
pwd.sub!("[^/]+/[^/]+$", "")
language, extension = 'C', '_old'
case ARGV[0].to_i
when 70
   language = 'newC'
   extension = "_new"
when 71
   language = 'C'
   extension = "_new"
end
begin
   f = File.new("test_setup.sql", "w")
   IO.foreach("test_setup#{extension}.sql") {|x| f.print x }
   f.close
   f = File.new("test_mklang.sql", "w")
   f.print <<EOF

create function plruby_call_handler() returns opaque
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


    
