#!/usr/bin/ruby
require 'mkmf'

stat_lib = if CONFIG.key?("LIBRUBYARG_STATIC")
	      $LDFLAGS += " -L#{CONFIG['libdir']}"
	      CONFIG["LIBRUBYARG_STATIC"]
	   else
	      "-lruby"
	   end.sub(/^-l/, '')
            
if srcdir = with_config("pgsql-srcinc-dir")
   $CFLAGS = "-I#{srcdir}"
end
include_dir = ""
if prefix = with_config("pgsql-prefix")
   if File.exist?("#{prefix}/include/server")
      $CFLAGS += " -I#{prefix}/include -I#{prefix}/include/server"
   else 
      $CFLAGS += " -I#{prefix}/include -I#{prefix}/include/postgresql/server"
   end
   $LDFLAGS += " -L#{prefix}/lib"
   include_dir = "#{prefix}/include"
end
if incdir = with_config("pgsql-include-dir")
   $CFLAGS += " -I#{incdir} -I#{incdir}/server"
   include_dir = incdir
else
   $CFLAGS += " -I/usr/include/postgresql -I/usr/include/postgresql/server"
end
if  libdir = with_config("pgsql-lib-dir")
    $LDFLAGS += " -L#{libdir}"
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
$libs = append_library($libs, stat_lib)
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
   version = "72"
   print <<-EOT
 ************************************************************************
 I can't find the version of PostgreSQL, the test will be make against
 the output of 7.2. If the test fail, verify the result in the directories
 test/plt and test/plp
 ************************************************************************
   EOT
end

$CFLAGS += " -DPG_PL_VERSION=#{version}"
create_makefile("plruby")
version.sub!(/\.\d/, '')
open("Makefile", "a") do |make|
    make.print <<-EOF

test: $(DLLIB)
\t(cd test/plt ; sh ./runtest #{version})
\t(cd test/plp ; sh ./runtest #{version})
    EOF
end

