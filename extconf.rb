#!/usr/bin/ruby
require 'mkmf'
if srcdir = with_config("pgsql-srcinc-dir")
    $CFLAGS = "-I#{srcdir}"
else
    srcdir = "/var/postgres/postgresl-6.5/src/include"
    $CFLAGS = "-I/var/postgres/postgresl-6.5/src/include"
end
if prefix = with_config("pgsql-prefix")
    $CFLAGS += " -I#{prefix}/include"
    $LDFLAGS += " -L#{prefix}/lib"
end
if incdir = with_config("pgsql-include-dir")
    $CFLAGS += " -I#{incdir}"
else
    $CFLAGS += " -I/usr/include/postgresql"
end
if  libdir = with_config("pgsql-lib-dir")
    $LDFLAGS += " -L#{libdir}"
end
if safe = with_config("safe-level")
    $CFLAGS += " -DSAFE_LEVEL=#{safe}"
end
if ! have_header("catalog/pg_proc.h")
    raise  "Some include file are missing (see README for the installation)"
end
if ! have_library("pq", "PQsetdbLogin")
    raise "libpq is missing"
end
$libs = append_library($libs, "ruby")
if ! version = with_config("pgsql-version")
   version = nil
   version_in = "#{srcdir}/version.h.in"
   version_regexp = /PG_RELEASE\s+"(\d)/
   retry_version = true
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
      raise if ! version
   rescue
      if retry_version
	 version_in = "#{prefix}/include/config.h"
	 version_regexp = /PG_VERSION\s+"(\d(\.\d)?)/
	 retry_version = false
	 retry
      end
      version = "70"
      print <<-EOT
 ************************************************************************
 I can't find the version of PostgreSQL, the test will be make against
 the output of 7.0. If the test fail, verify the result in the directories
 test/plt and test/plp
 ************************************************************************
      EOT
   end
end
$CFLAGS += " -DPG_PL_VERSION=#{version}"
create_makefile("plruby")
version.sub!(/\.\d/, '')
open("Makefile", "a") do |make|
    make.print <<-EOF

test: $(DLLIB)
\t(cd test/plt ; ./runtest #{version})
\t(cd test/plp ; ./runtest #{version})
    EOF
end

