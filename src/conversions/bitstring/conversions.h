{
    VALUE tmp;

#if RUBY_CAN_USE_AUTOLOAD
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("BitString"),
               rb_str_new2("plruby/plruby_bitstring"));
    tmp = INT2NUM(rb_intern("BitString"));
    rb_hash_aset(plruby_conversions, INT2NUM(BITOID), tmp);
    rb_hash_aset(plruby_conversions, INT2NUM(VARBITOID), tmp);
#else
    tmp = plruby_define_void_class("BitString", "plruby/plruby_bitstring");
    rb_hash_aset(plruby_classes, INT2NUM(BITOID), tmp);
    rb_hash_aset(plruby_classes, INT2NUM(VARBITOID), tmp);
#endif
}    
