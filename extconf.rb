#!/usr/bin/ruby
require 'mkmf'
if srcdir = with_config("pgsql-srcinc-dir")
    $CFLAGS = "-I#{srcdir}"
else
    $CFLAGS = "-I/var/postgres/postgresl-6.5/src/include"
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
if ! have_library("ruby", "rb_gvar_get")
    raise "ruby must be > 1.4.3"
end
create_makefile("plruby")
open("Makefile", "a") do |make|
    make.print <<-EOF

test: $(DLLIB)
\t(cd test/plt ; ./runtest)
\t(cd test/plp ; ./runtest)
    EOF
end

