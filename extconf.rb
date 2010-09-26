#!/usr/bin/ruby
ARGV.collect! {|x| 
   x = x.sub(/\A--((?:en|dis)able)-shared\z/) { "--#$1-plruby-shared" }
}

orig_argv = ARGV.dup

require 'mkmf'

class AX
   def marshal_dump
      "abc"
   end
end

def check_autoload(safe = 12)
   File.open("a.rb", "w") {|f| f.puts "class A; end" }
   autoload :A, "a.rb"
   Thread.new do
      begin
         $SAFE = safe
         A.new
      rescue Exception
         false
      end
   end.value
ensure
   File.unlink("a.rb")
end
      
def check_md
   Marshal.load(Marshal.dump(AX.new))
   false
rescue
   true
end

def create_lang(version = 74, suffix = '', safe = 0)
   language, opaque = 'C', 'language_handler'
   opaque = "opaque" if version == 72
   safe = safe.to_i
   trusted = if safe >= 4
		"trusted"
	     else
		""
	     end
   puts <<-EOT

 ========================================================================
 After the installation use *something like this* to create the language 
 plruby#{suffix}


   create function plruby#{suffix}_call_handler() returns #{opaque}
   as '#{Config::CONFIG["sitearchdir"]}/plruby#{suffix}.#{CONFIG["DLEXT"]}'
   language '#{language}';

   create #{trusted} language 'plruby#{suffix}'
   handler plruby#{suffix}_call_handler
   lancompiler 'PL/Ruby#{suffix}';

 ========================================================================
EOT
end

def rule(target, clean = nil)
   wr = "#{target}:
\t@for subdir in $(SUBDIRS); do \\
\t\t(cd $${subdir} && $(MAKE) #{target}); \\
\tdone;
"
   if clean != nil
      wr << "\t@-rm tmp/* tests/tmp/* 2> /dev/null\n"
      wr << "\t@rm Makefile\n" if clean
   end
   wr
end


include_dir, = dir_config("pgsql")
if include_dir
   raise "--with-pgsql-include/--with-pgsql-dir is obsolete.  Use --with-pg-config instead only if necessary."
end

pg_config = with_config('pg-config', 'pg_config')
include_dir = `#{pg_config} --includedir`.strip
$CFLAGS << " -I" << include_dir
$CFLAGS << " -I" << `#{pg_config} --includedir-server`.strip

if safe = with_config("safe-level")
   safe = Integer(safe)
   if safe < 0
      raise "invalid value for safe #{safe}"
   end
   $CFLAGS += " -DSAFE_LEVEL=#{safe}"
else
   safe = 12
end

if with_config("greenplum")
  $CFLAGS += " -I" << File.join( include_dir, "postgresql", "internal" )
  $CFLAGS += " -DWITH_GREENPLUM=1"
end

if timeout = with_config("timeout")
   timeout = Integer(timeout)
   if timeout < 2
      raise "Invalid value for timeout #{timeout}"
   end
   $CFLAGS += " -DPLRUBY_TIMEOUT=#{timeout}"
   if mainsafe = with_config("main-safe-level")
      $CFLAGS += " -DMAIN_SAFE_LEVEL=#{mainsafe}"
   end
end

if ! have_header("catalog/pg_proc.h")
    raise  "Some include file are missing (see README for the installation)"
end

if have_func("rb_hash_delete", "ruby.h")
   $CFLAGS += " -DHAVE_RB_HASH_DELETE"
end

case version_str = `#{pg_config} --version`
when /^PostgreSQL ([7-9])\.([0-9])(\.[0-9]+)?$/
   version = 10 * $1.to_i + $2.to_i
else
   version = 0
end
if version < 73
   raise <<-EOT

============================================================
#{version_str} is unsupported.  Try plruby-0.4.2.
============================================================
   EOT
end

if "aa".respond_to?(:initialize_copy, true)
   $CFLAGS += " -DHAVE_RB_INITIALIZE_COPY"
end

have_func("rb_block_call")
have_header("ruby/st.h")
have_header("st.h")

if version >= 74
   if !have_header("server/utils/array.h")
      if !have_header("utils/array.h")
         raise "I cant't find server/utils/array.h"
      end
      $CFLAGS += " -DPG_UTILS_ARRAY"
   end
end

if macro_defined?("PG_TRY", %Q{#include "c.h"\n#include "utils/elog.h"})
    $CFLAGS += " -DPG_PL_TRYCATCH"
end

enable_conversion = false
if enable_conversion = enable_config("conversion", true)
   $CFLAGS += " -DPLRUBY_ENABLE_CONVERSION"
   if check_autoload(safe.to_i)
      $CFLAGS += " -DRUBY_CAN_USE_AUTOLOAD"
   end
   if check_md
      $CFLAGS += " -DRUBY_CAN_USE_MARSHAL_DUMP"
   end
end

conversions = {}
subdirs = []

if enable_conversion
   Dir.foreach("src/conversions") do |dir| 
      next if dir[0] == ?. || !File.directory?("src/conversions/" + dir)
      conversions[dir] = true
   end
   if !conversions.empty?
      File.open("src/conversions.h", "w") do |conv|
	 conversions.sort.each do |key, val|
            conv.puts "#include \"conversions/#{key}/conversions.h\""
            subdirs << "src/conversions/#{key}"
         end
      end
   end
end

$CFLAGS += " -DPG_PL_VERSION=#{version}"

suffix = with_config('suffix').to_s
$CFLAGS += " -DPLRUBY_CALL_HANDLER=plruby#{suffix}_call_handler"
$CFLAGS += " -DPLRUBY_VALIDATOR=plruby#{suffix}_validator"

subdirs.each do |key|
   orig_argv << "--with-cflags='#$CFLAGS -I.. -I ../..'"
   orig_argv << "--with-ldflags='#$LDFLAGS'"
   orig_argv << "--with-cppflags='#$CPPFLAGS'"
   cmd = "#{CONFIG['RUBY_INSTALL_NAME']} extconf.rb #{orig_argv.join(' ')}"
   system("cd #{key}; #{cmd}")
end

subdirs.unshift("src")

begin
   Dir.chdir("src")
   if CONFIG["ENABLE_SHARED"] == "no"
      libs = if CONFIG.key?("LIBRUBYARG_STATIC")
                Config::expand(CONFIG["LIBRUBYARG_STATIC"].dup).sub(/^-l/, '')
             else
                Config::expand(CONFIG["LIBRUBYARG"].dup).sub(/lib([^.]*).*/, '\\1')
             end
      find_library(libs, "ruby_init", Config::expand(CONFIG["archdir"].dup))
   end
   $objs = ["plruby.o", "plplan.o", "plpl.o", "pltrans.o"] unless $objs
   create_makefile("plruby#{suffix}")
ensure
   Dir.chdir("..")
end

make = open("Makefile", "w")
make.print <<-EOF
SUBDIRS = #{subdirs.join(' ')}

#{rule('all')}
#{rule('clean', false)}
#{rule('distclean', true)}
#{rule('realclean', true)}
#{rule('install')}
#{rule('site-install')}

%.html: %.rd
\trd2 $< > ${<:%.rd=%.html}

plruby.html: plruby.rd

rd2: html

html: plruby.html

rdoc: docs/doc/index.html

docs/doc/index.html: $(RDOC)
\t@-(cd docs; rdoc plruby.rb)

ri:
\t@-(cd docs; rdoc -r plruby.rb)

ri-site:
\t@-(cd docs; rdoc -R plruby.rb)

test: src/$(DLLIB)
EOF
regexp = %r{\Atest/conv_(.*)}
Dir["test/*"].each do |dir|
   if regexp =~ dir
      next unless subdirs.include?("src/conversions/#{$1}")
   end
   make.puts "\t-(cd #{dir} ; RUBY='#{$ruby}' sh ./runtest #{version} #{suffix})"
end

make.close

create_lang(version, suffix, safe)

