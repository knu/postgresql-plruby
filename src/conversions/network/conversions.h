{
    VALUE tmp;
#if MAIN_SAFE_LEVEL >= 3 && !defined(PLRUBY_ENABLE_AUTOLOAD)
    extern void Init_plruby_network();

    Init_plruby_network();

    tmp = rb_const_get(rb_cObject, rb_intern("NetAddr"));
    rb_hash_aset(plruby_classes, INT2NUM(INETOID), tmp);
    rb_hash_aset(plruby_classes, INT2NUM(CIDROID), tmp);
    tmp = rb_const_get(rb_cObject, rb_intern("MacAddr"));
    rb_hash_aset(plruby_classes, INT2NUM(MACADDROID), tmp);
#else
#if MAIN_SAFE_LEVEL >= 3
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("NetAddr"),
               rb_str_new2("plruby_network"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("MacAddr"),
               rb_str_new2("plruby_network"));
#else
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("NetAddr"),
               rb_str_new2("plruby/plruby_network"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("MacAddr"),
               rb_str_new2("plruby/plruby_network"));
#endif
    tmp = INT2NUM(rb_intern("NetAddr"));
    rb_hash_aset(plruby_conversions, INT2NUM(INETOID), tmp);
    rb_hash_aset(plruby_conversions, INT2NUM(CIDROID), tmp);
    tmp = INT2NUM(rb_intern("MacAddr"));
    rb_hash_aset(plruby_conversions, INT2NUM(MACADDROID), tmp);
#endif
}    
