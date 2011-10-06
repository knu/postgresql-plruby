#!/usr/bin/ruby
require 'rbconfig'
include Config
pwd = Dir.pwd
pwd.sub!(%r{[^/]+/[^/]+$}, "")

language, extension = 'C', '_new_trigger'
opaque = 'language_handler'

version = ARGV[0].to_i
suffix = ARGV[1].to_s

begin
   f = File.new("test_setup.sql", "w")
   IO.foreach("test_setup.sql.in") do |x| 
      x.gsub!(/language\s+'plruby'/i, "language 'plruby#{suffix}'")
      f.print x
   end
   f.close
   f = File.new("test_mklang.sql", "w")
   f.print <<EOF
 
   create function plruby#{suffix}_call_handler() returns #{opaque}
    as '#{pwd}src/plruby#{suffix}.#{CONFIG["DLEXT"]}'
   language '#{language}';
 
   create trusted procedural language 'plruby#{suffix}'
        handler plruby#{suffix}_call_handler;
EOF
   f.close
rescue
   raise "Why I can't write #$!"
end
