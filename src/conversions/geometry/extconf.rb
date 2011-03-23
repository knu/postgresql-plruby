require 'mkmf'

if CONFIG["LIBRUBYARG"] == "$(LIBRUBYARG_SHARED)" && 
      !enable_config("plruby-shared")
   $LIBRUBYARG = ""
end
have_library('ruby18', 'ruby_init')
create_makefile('plruby/plruby_geometry')
