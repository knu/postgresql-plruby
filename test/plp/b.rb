#!/usr/bin/ruby
require 'rbconfig'
include Config
pwd = Dir.pwd
pwd.sub!("[^/]+/[^/]+$", "")
language = 'C'
if system("nm #{pwd}plruby.#{CONFIG['DLEXT']} | grep OidFunctionCall3 2>&1 > /dev/null")
   language = 'newC'
end
begin
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


    
