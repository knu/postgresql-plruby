#!/usr/bin/ruby
require 'rbconfig'
include Config
pwd = Dir.pwd
pwd.sub!("[^/]+/[^/]+$", "")
begin
    f = File.new("test_mklang.sql", "w")
    f.print <<EOF

create function plruby_call_handler() returns opaque
    as '#{pwd}plruby.#{CONFIG["DLEXT"]}'
	language 'C';

create trusted procedural language 'plruby'
	handler plruby_call_handler
	lancompiler 'PL/Ruby';
EOF
    f.close
rescue
    raise "Why I can't write $!"
end


    
