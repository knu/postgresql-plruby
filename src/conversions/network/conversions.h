{
    VALUE tmp;

#if RUBY_CAN_USE_AUTOLOAD
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("NetAddr"),
               rb_str_new2("plruby/plruby_network"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("MacAddr"),
               rb_str_new2("plruby/plruby_network"));
    tmp = INT2NUM(rb_intern("NetAddr"));
    rb_hash_aset(plruby_conversions, INT2NUM(INETOID), tmp);
    rb_hash_aset(plruby_conversions, INT2NUM(CIDROID), tmp);
    tmp = INT2NUM(rb_intern("MacAddr"));
    rb_hash_aset(plruby_conversions, INT2NUM(MACADDROID), tmp);
#else
    tmp = plruby_define_void_class("NetAddr", "plruby/plruby_network");
    rb_hash_aset(plruby_classes, INT2NUM(INETOID), tmp);
    rb_hash_aset(plruby_classes, INT2NUM(CIDROID), tmp);
    tmp = plruby_define_void_class("MacAddr", "plruby/plruby_network");
    rb_hash_aset(plruby_classes, INT2NUM(MACADDROID), tmp);
#endif
}    
