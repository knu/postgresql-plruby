#!/usr/bin/ruby
ARGV.collect! {|x| 
   x.sub(/\A--with-pgsql-prefix=/, "--with-pgsql-dir=") 
   x.sub(/\A--enable-conversion\z/, "--enable-basic")
}

orig_argv = ARGV.dup

require 'mkmf'

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
 After the installation use something like this to create the language 
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
   $CFLAGS = "-I#{srcdir} "
end

include_dir, = dir_config("pgsql", "/usr/local/pgsql/include", "/usr/local/pgsql/lib")

$CFLAGS += if File.exist?("#{include_dir}/server")
	      " -I#{include_dir}/server"
	   elsif File.exist?("#{include_dir}/postgresql/server")
              " -I#{include_dir}/postgresql/server"
	   end

if safe = with_config("safe-level")
    $CFLAGS += " -DSAFE_LEVEL=#{safe}"
else
   safe = 12
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

unless have_func("rb_hash_delete", "ruby.h")
   $CFLAGS += " -DPLRUBY_HASH_DELETE"
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

if version.to_i >= 74
   if !have_header("server/utils/array.h")
      if !have_header("utils/array.h")
         raise "I cant't find server/utils/array.h"
      end
      $CFLAGS += " -DPG_UTILS_ARRAY"
   end
end

conversions = {}
subdirs = []

if version.to_i >= 71
   Dir.foreach("src/conversions") do |dir| 
      next if dir[0] == ?.
      conversions[dir] = enable_config(dir)
   end

   if conversions.find {|k,v| v}
      $CFLAGS += " -DPLRUBY_ENABLE_CONVERSION"
      conversions["basic"] = true
      begin
	 conv = File.new("src/conversions.h", "w")
	 conversions.each do |key, val|
	    if val
	       conv.puts "#include \"conversions/#{key}/conversions.h\""
	       subdirs << "src/conversions/#{key}"
	    end
	 end
      ensure
	 conv.close if conv
      end
   end
end

$CFLAGS += " -DPG_PL_VERSION=#{version}"

suffix = with_config('suffix').to_s
$CFLAGS += " -DPLRUBY_CALL_HANDLER=plruby#{suffix}_call_handler"

if safe.to_i >= 3 
   $objs = ["plruby.o"]
   subdirs.each do |key|
      Dir.foreach(key) do |f|
         next if /\.c\z/ !~ f
         $objs << f.sub(/\.c\z/, '.o')
      end
   end
   subdirs = []
else
   subdirs.each do |key|
      orig_argv << "with-cflags=\"#$CFLAGS\""
      orig_argv << "with-ldflags=\"#$LDFLAGS\""
      cmd = "#{CONFIG['RUBY_INSTALL_NAME']} extconf.rb #{orig_argv.join(' ')}"
      system("cd #{key}; #{cmd}")
   end
end

subdirs.unshift("src")

begin
   Dir.chdir("src")
   $objs = ["plruby.o"] unless $objs
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

