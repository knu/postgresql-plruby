{
    VALUE tmp;

#if RUBY_CAN_USE_AUTOLOAD
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Tinterval"),
               rb_str_new2("plruby/plruby_datetime"));
    tmp = INT2NUM(rb_intern("Tinterval"));
    rb_hash_aset(plruby_conversions, INT2NUM(TINTERVALOID), tmp);
#else
    tmp = plruby_define_void_class("Tinterval", "plruby/plruby_datetime");
    rb_hash_aset(plruby_classes, INT2NUM(TINTERVALOID), tmp);
#endif
}    
