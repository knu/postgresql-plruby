{
    VALUE pl_cTint;

#if SAFE_LEVEL >= 3
    {
	extern void Init_plruby_basic();

	Init_plruby_basic();
    }
#else
    {
        extern void plruby_require(char *);

        plruby_require("plruby/plruby_basic");
    }
#endif

    rb_hash_aset(plruby_classes, INT2NUM(OIDOID), rb_cFixnum);
    rb_hash_aset(plruby_classes, INT2NUM(INT2OID), rb_cFixnum);
    rb_hash_aset(plruby_classes, INT2NUM(INT4OID), rb_cFixnum);
    rb_hash_aset(plruby_classes, INT2NUM(INT8OID), rb_cFixnum);

    rb_hash_aset(plruby_classes, INT2NUM(FLOAT4OID), rb_cFloat);
    rb_hash_aset(plruby_classes, INT2NUM(FLOAT8OID), rb_cFloat);
    rb_hash_aset(plruby_classes, INT2NUM(CASHOID), rb_cFloat);
    rb_hash_aset(plruby_classes, INT2NUM(NUMERICOID), rb_cFloat);

    rb_hash_aset(plruby_classes, INT2NUM(TIMESTAMPOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(TIMESTAMPTZOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(ABSTIMEOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(DATEOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(RELTIMEOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(INTERVALOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(TIMETZOID), rb_cTime);
    rb_hash_aset(plruby_classes, INT2NUM(TIMEOID), rb_cTime);

    pl_cTint = rb_const_get(rb_cObject, rb_intern("Tinterval"));
    rb_hash_aset(plruby_classes, INT2NUM(TINTERVALOID), pl_cTint);
}
