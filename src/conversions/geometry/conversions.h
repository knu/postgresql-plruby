{
    VALUE tmp;
#if MAIN_SAFE_LEVEL >= 3 && !defined(PLRUBY_ENABLE_AUTOLOAD)
    extern void Init_plruby_geometry();

    Init_plruby_geometry();

    tmp = rb_const_get(rb_cObject, rb_intern("Point"));
    rb_hash_aset(plruby_classes, INT2NUM(POINTOID), tmp);
    tmp = rb_const_get(rb_cObject, rb_intern("Segment"));
    rb_hash_aset(plruby_classes, INT2NUM(LSEGOID), tmp);
    tmp = rb_const_get(rb_cObject, rb_intern("Box"));
    rb_hash_aset(plruby_classes, INT2NUM(BOXOID), tmp);
    tmp = rb_const_get(rb_cObject, rb_intern("Path"));
    rb_hash_aset(plruby_classes, INT2NUM(PATHOID), tmp);
    tmp = rb_const_get(rb_cObject, rb_intern("Polygon"));
    rb_hash_aset(plruby_classes, INT2NUM(POLYGONOID), tmp);
    tmp = rb_const_get(rb_cObject, rb_intern("Circle"));
    rb_hash_aset(plruby_classes, INT2NUM(CIRCLEOID), tmp);
#else
#if MAIN_SAFE_LEVEL >= 3
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Point"),
               rb_str_new2("plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Segment"),
               rb_str_new2("plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Box"),
               rb_str_new2("plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Path"),
               rb_str_new2("plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Polygon"),
               rb_str_new2("plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Circle"),
               rb_str_new2("plruby_geometry"));
#else
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Point"),
               rb_str_new2("plruby/plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Segment"),
               rb_str_new2("plruby/plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Box"),
               rb_str_new2("plruby/plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Path"),
               rb_str_new2("plruby/plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Polygon"),
               rb_str_new2("plruby/plruby_geometry"));
    rb_funcall(rb_mKernel, rb_intern("autoload"), 2, rb_str_new2("Circle"),
               rb_str_new2("plruby/plruby_geometry"));
#endif
    rb_hash_aset(plruby_conversions, INT2NUM(POINTOID), INT2NUM(rb_intern("Point")));
    rb_hash_aset(plruby_conversions, INT2NUM(LSEGOID), INT2NUM(rb_intern("Segment")));
    rb_hash_aset(plruby_conversions, INT2NUM(BOXOID), INT2NUM(rb_intern("Box")));
    rb_hash_aset(plruby_conversions, INT2NUM(PATHOID), INT2NUM(rb_intern("Path")));
    rb_hash_aset(plruby_conversions, INT2NUM(POLYGONOID), INT2NUM(rb_intern("Polygon")));
    rb_hash_aset(plruby_conversions, INT2NUM(CIRCLEOID), INT2NUM(rb_intern("Circle")));
#endif
}
