{
    VALUE tmp;
#if MAIN_SAFE_LEVEL >= 3 && !defined(PLRUBY_ENABLE_AUTOLOAD)
    extern void Init_plruby_bitstring();

    Init_plruby_bitstring();

    tmp = rb_const_get(rb_cObject, rb_intern("BitString"));
    rb_hash_aset(plruby_classes, INT2NUM(BITOID), tmp);
    rb_hash_aset(plruby_classes, INT2NUM(VARBITOID), tmp);
#else
#if MAIN_SAFE_LEVEL >= 3
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("BitString"),
               rb_str_new2("plruby_bitstring"));
#else
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("BitString"),
               rb_str_new2("plruby/plruby_bitstring"));
#endif
    tmp = INT2NUM(rb_intern("BitString"));
    rb_hash_aset(plruby_conversions, INT2NUM(BITOID), tmp);
    rb_hash_aset(plruby_conversions, INT2NUM(VARBITOID), tmp);
#endif
}    
