#!/usr/bin/ruby
ARGV.collect! {|x| 
   x = x.sub(/\A--with-pgsql-prefix=/, "--with-pgsql-dir=") 
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
   language, procedural = 'C', 'procedural'
   opaque = 'opaque'
   version = version.to_i
   safe = safe.to_i
   trusted = if safe >= 4
		"trusted"
	     else
		""
	     end

   case version
   when 70
      language = 'newC'
   when 71
      language = 'C'
   when 72
      language = 'C'
      procedural = ""
   when 73
      language = 'C'
      procedural = ""
      opaque = 'language_handler'
   when 74
      language = 'C'
      procedural = ""
      opaque = 'language_handler'
   end
   puts <<-EOT

 ========================================================================
 After the installation use *something like this* to create the language 
 plruby#{suffix}


   create function plruby#{suffix}_call_handler() returns #{opaque}
   as '#{Config::CONFIG["sitearchdir"]}/plruby#{suffix}.#{CONFIG["DLEXT"]}'
   language '#{language}';

   create #{trusted} #{procedural} language 'plruby#{suffix}'
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


libs = if CONFIG.key?("LIBRUBYARG_STATIC")
	  Config::expand(CONFIG["LIBRUBYARG_STATIC"].dup).sub(/^-l/, '')
       else
	  Config::expand(CONFIG["LIBRUBYARG"].dup).sub(/lib([^.]*).*/, '\\1')
       end

unknown = find_library(libs, "ruby_init", 
		       Config::expand(CONFIG["archdir"].dup))

if srcdir = with_config("pgsql-srcinc")
   $CFLAGS += " -Wall -I#{srcdir} "
end

include_dir, = dir_config("pgsql", "/usr/local/pgsql/include", "/usr/local/pgsql/lib")

$CFLAGS += if File.exist?("#{include_dir}/server")
	      " -I#{include_dir}/server"
	   elsif File.exist?("#{include_dir}/postgresql/server")
              " -I#{include_dir}/postgresql/server"
           else
              ""
	   end

if safe = with_config("safe-level")
   safe = Integer(safe)
   if safe < 0
      raise "invalid value for safe #{safe}"
   end
   $CFLAGS += " -DSAFE_LEVEL=#{safe}"
else
   safe = 12
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
if ! have_library("pq", "PQsetdbLogin")
    raise "libpq is missing"
end

if have_func("rb_hash_delete", "ruby.h")
   $CFLAGS += " -DHAVE_RB_HASH_DELETE"
end

if ! version = with_config("pgsql-version")
   for version_in in [
	 "#{include_dir}/config.h", 
	 "#{include_dir}/pg_config.h", 
	 "#{include_dir}/server/pg_config.h",
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
   version = "74"
   print <<-EOT
 ************************************************************************
 I can't find the version of PostgreSQL, the test will be make against
 the output of 7.4. If the test fail, verify the result in the directories
 test/plt and test/plp
 ************************************************************************
   EOT
end

if "aa".respond_to?(:initialize_copy, true)
   $CFLAGS += " -DHAVE_RB_INITIALIZE_COPY"
end

if version.to_i >= 74
   if !have_header("server/utils/array.h")
      if !have_header("utils/array.h")
         raise "I cant't find server/utils/array.h"
      end
      $CFLAGS += " -DPG_UTILS_ARRAY"
   end
end

enable_conversion = false
if version.to_i >= 72
   if enable_conversion = enable_config("conversion", true)
      $CFLAGS += " -DPLRUBY_ENABLE_CONVERSION"
      if check_autoload(safe.to_i)
         $CFLAGS += " -DRUBY_CAN_USE_AUTOLOAD"
      end
      if check_md
         $CFLAGS += " -DRUBY_CAN_USE_MARSHAL_DUMP"
      end
   end
else
   if enable_config("conversion")
      puts <<-EOT

====================== WARNING ==========

 --enable-conversion is disabled because it
 can work *only* with PostgreSQL >= 71

====================== WARNING ==========
      EOT
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
	 conversions.each do |key, val|
            conv.puts "#include \"conversions/#{key}/conversions.h\""
            subdirs << "src/conversions/#{key}"
         end
      end
   end
end

$CFLAGS += " -DPG_PL_VERSION=#{version}"

suffix = with_config('suffix').to_s
$CFLAGS += " -DPLRUBY_CALL_HANDLER=plruby#{suffix}_call_handler"

subdirs.each do |key|
   orig_argv << "with-cflags=\"#$CFLAGS -I.. -I ../..\""
   orig_argv << "with-ldflags=\"#$LDFLAGS\""
   cmd = "#{CONFIG['RUBY_INSTALL_NAME']} extconf.rb #{orig_argv.join(' ')}"
   system("cd #{key}; #{cmd}")
end

subdirs.unshift("src")

begin
   Dir.chdir("src")
   if CONFIG["LIBRUBYARG"] == "$(LIBRUBYARG_SHARED)" && 
         !enable_config("plruby-shared")
      $LIBRUBYARG = ""
   end
   $objs = ["plruby.o", "plplan.o", "plpl.o"] unless $objs
   create_makefile("plruby#{suffix}")
   version.sub!(/\.\d/, '')
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
\t-(cd test/plt ; sh ./runtest #{version} #{suffix})
\t-(cd test/plp ; sh ./runtest #{version} #{suffix})
EOF
if version >= "73"
   make.puts "\t-(cd test/range; sh ./runtest #{version} #{suffix})"
end

make.close

create_lang(version, suffix, safe)

