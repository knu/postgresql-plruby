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
print "#$CFLAGS\n"
if ! have_header("catalog/pg_proc.h")
    raise  "Some include file are missing (see README for the installation)"
end
if ! have_library("pq", "PQsetdbLogin")
    raise "libpq is missing"
end
if ! have_library("ruby", "rb_gvar_get")
    raise "ruby must be > 1.4.3"
end
create_makefile("plruby")
version = 6
begin
    IO.foreach("#{srcdir}/version.h.in") do |line|
	if /PG_RELEASE\s+\"(\d)\"/ =~ line
	    version = $1
	    break
	end
    end
rescue
    print <<-EOT
 ************************************************************************
 I can't find the version of PostgreSQL, the test will be make against
 the output of 6.*. If the test fail, verify the result in the directories
 test/plt and test/plp
 ************************************************************************
   EOT
end
open("Makefile", "a") do |make|
    make.print <<-EOF

test: $(DLLIB)
\t(cd test/plt ; ./runtest #{version})
\t(cd test/plp ; ./runtest #{version})
    EOF
end

