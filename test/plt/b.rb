#!/usr/bin/ruby
require 'rbconfig'
include Config

pwd = Dir.pwd
pwd.sub!(%r{[^/]+/[^/]+$}, "")

language, extension, procedural = 'C', "_new_trigger_array", ''
opaque = 'language_handler'

suffix = ARGV[1].to_s

begin
   f = File.new("test_setup.sql", "w")
   IO.foreach("test_setup#{extension}.sql") do |x| 
      x.gsub!(/language\s+'plruby'/, "language 'plruby#{suffix}'")
      f.print x
   end
   f.close
   f = File.new("test_mklang.sql", "w")
   f.print <<EOF

   create function plruby#{suffix}_call_handler() returns #{opaque}
   as '#{pwd}src/plruby#{suffix}.#{CONFIG["DLEXT"]}'
   language '#{language}';

   create trusted #{procedural} language 'plruby#{suffix}'
	handler plruby#{suffix}_call_handler
	lancompiler 'PL/Ruby';
EOF
   f.close
rescue
   raise "Why I can't write #$!"
end


    
