#!/usr/bin/ruby
ARGV.collect! {|x| x.sub(/^--with-pgsql-prefix=/, "--with-pgsql-dir=") }

require 'mkmf'
libs = if CONFIG.key?("LIBRUBYARG_STATIC")
	  Config::expand(CONFIG["LIBRUBYARG_STATIC"].dup).sub(/^-l/, '')
       else
	  Config::expand(CONFIG["LIBRUBYARG"].dup).sub(/lib([^.]*).*/, '\\1')
       end

unknown = find_library(libs, "ruby_init", 
		       Config::expand(CONFIG["archdir"].dup))

if srcdir = with_config("pgsql-srcinc")
   $CFLAGS = "-I#{srcdir} "
end

include_dir, = dir_config("pgsql", "/usr/local/pgsql/include", "/usr/local/pgsql/lib")

$CFLAGS += if File.exist?("#{include_dir}/server")
	      " -I#{include_dir}/server"
	   else 
	      " -I#{include_dir}/postgresql/server"
	   end

if safe = with_config("safe-level")
    $CFLAGS += " -DSAFE_LEVEL=#{safe}"
end

if timeout = with_config("timeout")
   timeout = timeout.to_i
   if timeout < 2
      raise "Invalid value for timeout #{timeout}"
   end
   $CFLAGS += " -DPLRUBY_TIMEOUT=#{timeout}"
   if safe = with_config("main-safe-level")
      $CFLAGS += " -DMAIN_SAFE_LEVEL=#{safe}"
   end
end

if ! have_header("catalog/pg_proc.h")
    raise  "Some include file are missing (see README for the installation)"
end
if ! have_library("pq", "PQsetdbLogin")
    raise "libpq is missing"
end

if ! version = with_config("pgsql-version")
   for version_in in [
	 "#{include_dir}/config.h", 
	 "#{include_dir}/pg_config.h", 
	 "#{srcdir}/version.h.in",
	 "#{srcdir}/pg_config.h"
      ]
      version = nil
      version_regexp = /(?:PG_RELEASE|PG_VERSION)\s+"(\d(\.\d)?)/
      begin
	 IO.foreach(version_in) do |line|
	    if version_regexp =~ line
	       version = $1
	       if ! version.sub!(/\./, '')
		  version += "0"
	       end
	       break
	    end
	 end
	 break if version
      rescue
      end
   end
end
unless version
   version = "73"
   print <<-EOT
 ************************************************************************
 I can't find the version of PostgreSQL, the test will be make against
 the output of 7.3. If the test fail, verify the result in the directories
 test/plt and test/plp
 ************************************************************************
   EOT
end

$CFLAGS += " -DPG_PL_VERSION=#{version}"
if RUBY_VERSION >= "1.8.0"
   $DLDFLAGS = $LDFLAGS
end
create_makefile("plruby")
version.sub!(/\.\d/, '')
open("Makefile", "a") do |make|
    make.print <<-EOF

test: $(DLLIB)
\t(cd test/plt ; sh ./runtest #{version})
\t(cd test/plp ; sh ./runtest #{version})
    EOF
    if version >= "73"
        make.puts "\t(cd test/range; sh ./runtest #{version})"
    end
end

