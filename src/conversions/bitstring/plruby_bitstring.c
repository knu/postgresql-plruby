#include "convcommon.h"
#include "utils/varbit.h"

static void pl_bit_mark(VarBit *p) {}

static VALUE
pl_bit_s_alloc(VALUE obj)
{
    VarBit *inst;

    inst = (VarBit *)ALLOC_N(char, VARBITTOTALLEN(0));
#ifdef SET_VARSIZE
    SET_VARSIZE(inst, VARBITTOTALLEN(0));
#else
    VARATT_SIZEP(inst) = VARBITTOTALLEN(0);
#endif
    VARBITLEN(inst) = 0;
    return Data_Wrap_Struct(obj, pl_bit_mark, free, inst);
}

static VALUE
pl_bit_init_copy(VALUE copy, VALUE orig)
{
    VarBit *t0, *t1;
    int s0, s1;

    if (copy == orig) return copy;
    if (TYPE(orig) != T_DATA ||
        RDATA(orig)->dmark != (RUBY_DATA_FUNC)pl_bit_mark) {
        rb_raise(rb_eTypeError, "wrong argument type to clone");
    }
    Data_Get_Struct(orig, VarBit, t0);
    Data_Get_Struct(copy, VarBit, t1);
    s0 = VARSIZE(t0);
    s1 = VARSIZE(t1);
    if (s0 != s1) {
        free(t1);
        DATA_PTR(copy) = 0;
        t1 = (VarBit *)ALLOC_N(char, s0);
        DATA_PTR(copy) = t1;
    }
    memcpy(t1, t0, s0);
    return copy;
}

static VALUE
pl_bit_s_datum(VALUE obj, VALUE a)
{
    VarBit *ip0, *ip1;
    Oid typoid;
    VALUE res;

    ip0 = (VarBit *)plruby_datum_get(a, &typoid);
    if (typoid != BITOID && typoid != VARBITOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    ip1 = (VarBit *)ALLOC_N(char, VARSIZE(ip0));
    memcpy(ip1, ip0, VARSIZE(ip0));
    res = Data_Wrap_Struct(obj, pl_bit_mark, free, ip1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_bit_to_datum(VALUE obj, VALUE a)
{
    VarBit *ip0, *ip1;
    int length;
    Oid typoid;

    typoid = plruby_datum_oid(a, &length);
    switch (typoid) {
    case BITOID:
    case VARBITOID:
        Data_Get_Struct(obj, VarBit, ip0);
        ip1 = (VarBit *)PLRUBY_DFC3(bit, ip0, Int32GetDatum(length), true);
        break;
    default:
        /* a faire */
        return Qnil;
    }
    return plruby_datum_set(a, (Datum)ip1);
}

PL_MLOADVAR(pl_bit_mload, varbit_recv, VarBit, VARSIZE);
PL_MDUMP(pl_bit_mdump, varbit_send);

static VALUE
pl_bit_init(int argc, VALUE *argv, VALUE obj)
{
    VarBit *inst;
    VALUE a, b;
    void *v = 0;
    int length = -1;
    int taint = 0;

    if (rb_scan_args(argc, argv, "11", &a, &b) == 2) {
        length = NUM2INT(b);
    }
    taint = OBJ_TAINTED(a);
    if (rb_respond_to(a, rb_intern("to_int"))) {
        a = rb_funcall2(a, rb_intern("to_int"), 0, 0);
#if PG_PL_VERSION >= 75
        v = (void *)PLRUBY_DFC2(bitfromint4, Int32GetDatum(NUM2LONG(a)),
                                Int32GetDatum(length));
#else
        v = (void *)PLRUBY_DFC1(bitfromint4, Int32GetDatum(NUM2LONG(a)));
        if (length > 0) {
	    void *v1;

            int ll = DatumGetInt32(PLRUBY_DFC1(bitlength, v));
            if (length != ll) {
                if (length < ll) {
                    v1 = (void *)PLRUBY_DFC2(bitshiftleft, v,
                                             Int32GetDatum(ll - length));
                    pfree(v);
                }
                else {
                    v1 = v;
                }
                v = (void *)PLRUBY_DFC3(bit, v1, Int32GetDatum(length), true);
                pfree(v1);
            }
        }
#endif
    }
    if (!v) {
        a = plruby_to_s(a);
        v = (void *)PLRUBY_DFC3(bit_in, RSTRING_PTR(a), ObjectIdGetDatum(0),
                                Int32GetDatum(length));
    }
    Data_Get_Struct(obj, VarBit, inst);
    free(inst);
    inst = (VarBit *)ALLOC_N(char, VARSIZE(v));
    CPY_FREE(inst, v, VARSIZE(v));
    RDATA(obj)->data = inst;
    if (taint) OBJ_TAINT(obj);
    return obj;
}

static VALUE
pl_bit_cmp(VALUE a, VALUE b)
{
    VarBit *inst0, *inst1;
    int result;

    if (!rb_obj_is_kind_of(b, rb_obj_class(a))) {
	return Qnil;
    }
    Data_Get_Struct(a, VarBit, inst0);
    Data_Get_Struct(b, VarBit, inst1);
    result = DatumGetInt32(PLRUBY_DFC2(bitcmp, inst0, inst1));
    return INT2FIX(result);
}

static VALUE
pl_bit_to_s(VALUE obj)
{
    VarBit *src;
    char *str;
    VALUE res;

    Data_Get_Struct(obj, VarBit, src);
    str = (char *)PLRUBY_DFC1(bit_out, src);
    if (OBJ_TAINTED(obj)) {
	res = rb_tainted_str_new2(str);
    }
    else {
	res = rb_str_new2(str);
    }
    pfree(str);
    return res;
}

#define BIT_OPERATOR(name_, function_)                                  \
static VALUE                                                            \
name_(VALUE obj, VALUE a)                                               \
{                                                                       \
    VarBit *v0, *v1, *vp, *vr;                                          \
    VALUE res;                                                          \
                                                                        \
    if (TYPE(a) != T_DATA ||                                            \
        RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_bit_mark) {               \
        rb_raise(rb_eArgError, "invalid argument for %s",               \
                 rb_id2name(rb_frame_last_func()));                     \
    }                                                                   \
    Data_Get_Struct(obj, VarBit, v0);                                   \
    Data_Get_Struct(a, VarBit, v1);                                     \
    vp = (VarBit *)PLRUBY_DFC2(function_, v0, v1);                      \
    vr = (VarBit *)ALLOC_N(char, VARSIZE(vp));                          \
    CPY_FREE(vr, vp, VARSIZE(vp));                                      \
    res = Data_Wrap_Struct(rb_class_of(obj), pl_bit_mark, free, vr);    \
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);             \
    return res;                                                         \
}

BIT_OPERATOR(pl_bit_add, bitcat);
BIT_OPERATOR(pl_bit_and, bitand);
BIT_OPERATOR(pl_bit_or, bitor);
BIT_OPERATOR(pl_bit_xor, bitxor);

static VALUE
pl_bit_push(VALUE obj, VALUE a)
{
    VarBit *v0, *v1, *vp;

    if (TYPE(a) != T_DATA ||
        RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_bit_mark) {
        rb_raise(rb_eArgError, "invalid argument for %s",
                 rb_id2name(rb_frame_last_func()));
    }
    Data_Get_Struct(obj, VarBit, v0);
    Data_Get_Struct(a, VarBit, v1);
    vp = (VarBit *)PLRUBY_DFC2(bitcat, v0, v1);
    free(v0);
    v0 = (VarBit *)ALLOC_N(char, VARSIZE(vp));
    CPY_FREE(v0, vp, VARSIZE(vp));
    DATA_PTR(obj) = v0;
    return obj;
}

static VALUE
pl_bit_not(VALUE obj)
{
    VarBit *v0, *vp, *vr;
    VALUE res;

    Data_Get_Struct(obj, VarBit, v0);
    vp = (VarBit *)PLRUBY_DFC1(bitnot, v0);
    vr = (VarBit *)ALLOC_N(char, VARSIZE(vp));
    CPY_FREE(vr, vp, VARSIZE(vp));
    res = Data_Wrap_Struct(rb_class_of(obj), pl_bit_mark, free, vr);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

#define SHIFT_OPERATOR(name_, function_)                                \
static VALUE                                                            \
name_(VALUE obj, VALUE a)                                               \
{                                                                       \
    VarBit *v0, *vp, *vr;                                               \
    VALUE res;                                                          \
                                                                        \
    Data_Get_Struct(obj, VarBit, v0);                                   \
    a = rb_Integer(a);                                                  \
    vp = (VarBit *)PLRUBY_DFC2(function_, v0, Int32GetDatum(NUM2INT(a)));\
    vr = (VarBit *)ALLOC_N(char, VARSIZE(vp));                          \
    CPY_FREE(vr, vp, VARSIZE(vp));                                      \
    res = Data_Wrap_Struct(rb_class_of(obj), pl_bit_mark, free, vr);    \
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);             \
    return res;                                                         \
}

SHIFT_OPERATOR(pl_bit_left_shift, bitshiftleft);
SHIFT_OPERATOR(pl_bit_right_shift, bitshiftright);

static VALUE
pl_bit_length(VALUE obj)
{
    VarBit *v;
    int l;

    Data_Get_Struct(obj, VarBit, v);
    l = DatumGetInt32(PLRUBY_DFC1(bitlength, v));
    return INT2NUM(l);
}

static VALUE
pl_bit_octet_length(VALUE obj)
{
    VarBit *v;
    int l;

    Data_Get_Struct(obj, VarBit, v);
    l = DatumGetInt32(PLRUBY_DFC1(bitoctetlength, v));
    return INT2NUM(l);
}

static VALUE
pl_bit_to_i(VALUE obj)
{
    VarBit *v;
    int l;

    Data_Get_Struct(obj, VarBit, v);
    l = DatumGetInt32(PLRUBY_DFC1(bittoint4, v));
    return INT2NUM(l);
}

/* This is varbit_out() from utils/adt/varbit.c */

#ifndef IS_HIGHBIT_SET
#define IS_HIGHBIT_SET(x) ((x) & BITHIGH)
#endif

static VALUE
pl_bit_each(VALUE obj)
{
    VarBit *s;
    VALUE i1, i0;
    bits8 *sp, x;
    int i, k, len;

    i1 = INT2FIX(1);
    i0 = INT2FIX(0);
    Data_Get_Struct(obj, VarBit, s);
    len = VARBITLEN(s);
    sp = VARBITS(s);
    for (i = 0; i < len - BITS_PER_BYTE; i += BITS_PER_BYTE, sp++) {
        x = *sp;
        for (k = 0; k < BITS_PER_BYTE; k++) {
            if (IS_HIGHBIT_SET(x)) {
                rb_yield(i1);
            }
            else {
                rb_yield(i0);
            }
            x <<= 1;
        }
    }
    x = *sp;
    for (k = i; k < len; k++){
        if (IS_HIGHBIT_SET(x)) {
            rb_yield(i1);
        }
        else {
            rb_yield(i0);
        }
        x <<= 1;
    }
    return Qnil;
}

static VALUE
pl_bit_index(VALUE obj, VALUE a)
{
    VarBit *v0, *v1;
    int i;

    if (TYPE(a) != T_DATA ||
        RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_bit_mark) {
        rb_raise(rb_eArgError, "invalid argument for %s",
                 rb_id2name(rb_frame_last_func()));
    }
    Data_Get_Struct(obj, VarBit, v0);
    Data_Get_Struct(a, VarBit, v1);
    i = DatumGetInt32(PLRUBY_DFC2(bitposition, v0, v1));
    i -= 1;
    if (i < 0) return Qnil;
    return INT2NUM(i);
}

static VALUE
pl_bit_include(VALUE obj, VALUE a)
{
    if (NIL_P(pl_bit_index(obj, a))) {
        return Qfalse;
    }
    return Qtrue;
}

extern long rb_reg_search();

static VALUE
pl_bit_subpat(VALUE obj, VALUE a, int nth)
{
    VALUE res;

    obj = pl_bit_to_s(obj);
    if (rb_reg_search(a, obj, 0, 0) >= 0) {
	res = rb_reg_nth_match(nth, rb_backref_get());
        return rb_funcall(rb_obj_class(obj), rb_intern("new"), 1, res);
    }
    return Qnil;
}

static VALUE
pl_bit_substr(VALUE obj, long beg, long len)
{
    VarBit *v, *v0, *v1;
    long ll;
    VALUE res;
    
    Data_Get_Struct(obj, VarBit, v);
    ll = DatumGetInt32(PLRUBY_DFC1(bitlength, v));
    if (len < 0) return Qnil;
    if (beg > ll) return Qnil;
    if (beg < 0) {
        beg += ll;
        if (beg < 0) return Qnil;
    }
    if (beg + len > ll) {
        len = ll - beg;
    }
    if (len < 0) {
        len = 0;
    }
    if (len == 0) {
        res = rb_funcall2(rb_obj_class(obj), rb_intern("allocate"), 0, 0);
        if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
        return res;
    }
    v0 = (VarBit *)PLRUBY_DFC3(bitsubstr, v, Int32GetDatum(beg + 1), 
                               Int32GetDatum(len));
    v1 = (VarBit *)ALLOC_N(char, VARSIZE(v0));
    CPY_FREE(v1, v0, VARSIZE(v0));
    res = Data_Wrap_Struct(rb_obj_class(obj), pl_bit_mark, free, v1);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_bit_aref(VALUE obj, VALUE a)
{
    VarBit *v, *v0, *v1;
    long l, idx;
    bits8 *sp, x;
    VALUE res;

    Data_Get_Struct(obj, VarBit, v);
    l = DatumGetInt32(PLRUBY_DFC1(bitlength, v));

    switch (TYPE(a)) {
    case T_FIXNUM:
        idx = FIX2LONG(a);

    num_index:
        if (idx < 0) {
            idx = l + idx;
        }
        if (idx < 0 || l <= idx) {
            return Qnil;
        }
        sp = VARBITS(v);
        sp += (idx / BITS_PER_BYTE);
        x = *sp <<= (idx % BITS_PER_BYTE);
        if (IS_HIGHBIT_SET(x)) return INT2FIX(1);
        return INT2FIX(0);

    case T_REGEXP:
        return pl_bit_subpat(obj, a, 0);

    case T_STRING:
        a = plruby_to_s(a);
        v0 = (void *)PLRUBY_DFC3(bit_in, RSTRING_PTR(a), 
                                 ObjectIdGetDatum(0), Int32GetDatum(-1));
        if (DatumGetInt32(PLRUBY_DFC2(bitposition, v, v0)) > 0) {
            v1 = (VarBit *)ALLOC_N(char, VARSIZE(v0));
            CPY_FREE(v1, v0, VARSIZE(v0));
            res = Data_Wrap_Struct(CLASS_OF(obj), pl_bit_mark, free, v1);
            if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
            return res;
        }
        pfree(v0);
        return Qnil;

    case T_DATA:
        if (RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_bit_mark) {
            rb_raise(rb_eArgError, "expected a BitString object");
        }
        Data_Get_Struct(a, VarBit, v0);
        if (DatumGetInt32(PLRUBY_DFC2(bitposition, v, v0)) > 0) {
            return rb_funcall2(a, rb_intern("dup"), 0, 0);
        }
        return Qnil;

    default:
    {
        long beg, len;

        switch (rb_range_beg_len(a, &beg, &len, l, 0)) {
        case Qfalse:
            break;
        case Qnil:
            return Qnil;
        default:
            return pl_bit_substr(obj, beg, len);
        }
    }
    idx = NUM2LONG(a);
    goto num_index;
    }
    return Qnil;
}

static VALUE
pl_bit_aref_m(int argc, VALUE *argv, VALUE obj)
{
    if (argc == 2) {
	if (TYPE(argv[0]) == T_REGEXP) {
	    return pl_bit_subpat(obj, argv[0], NUM2INT(argv[1]));
	}
        return pl_bit_substr(obj, NUM2LONG(argv[0]), NUM2LONG(argv[1]));
    }
    if (argc != 1) {
	rb_raise(rb_eArgError, "wrong number of arguments(%d for 1)", argc);
    }
    return pl_bit_aref(obj, argv[0]);
}

static VALUE
pl_bit_aset(int argc, VALUE *argv, VALUE obj)
{
    VALUE res;
    int i;
    void *v;
    VarBit *inst;

    for (i = 0; i < argc; ++i) {
        if (TYPE(argv[i]) == T_DATA &&
            RDATA(argv[i])->dmark == (RUBY_DATA_FUNC)pl_bit_mark) {
            argv[i] = pl_bit_to_s(argv[i]);
        }
    }
    res = rb_funcall2(pl_bit_to_s(obj), rb_intern("[]="), argc, argv);
    if (NIL_P(res)) return res;
    res = plruby_to_s(res);
    v = (void *)PLRUBY_DFC3(bit_in, RSTRING_PTR(res), ObjectIdGetDatum(0),
                            Int32GetDatum(-1));
    Data_Get_Struct(obj, VarBit, inst);
    free(inst);
    inst = (VarBit *)ALLOC_N(char, VARSIZE(v));
    CPY_FREE(inst, v, VARSIZE(v));
    RDATA(obj)->data = inst;
    return obj;
}

void Init_plruby_bitstring()
{
    VALUE pl_cBit;

    pl_cBit = rb_define_class("BitString", rb_cObject);
    rb_include_module(pl_cBit, rb_mComparable);
    rb_include_module(pl_cBit, rb_mEnumerable);
    rb_undef_method(CLASS_OF(pl_cBit), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cBit, pl_bit_s_alloc);
#else
    rb_define_singleton_method(pl_cBit, "allocate", pl_bit_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cBit, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cBit, "from_string", plruby_s_new, -1);
    rb_define_singleton_method(pl_cBit, "from_datum", pl_bit_s_datum, 1);
    rb_define_method(pl_cBit, "to_datum", pl_bit_to_datum, 1);
    rb_define_method(pl_cBit, "initialize", pl_bit_init, -1);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cBit, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cBit, "initialize_copy", pl_bit_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cBit, "marshal_load", pl_bit_mload, 1);
    rb_define_method(pl_cBit, "marshal_dump", pl_bit_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cBit, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cBit, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cBit, "<=>", pl_bit_cmp, 1);
    rb_define_method(pl_cBit, "each", pl_bit_each, 0);
    rb_define_method(pl_cBit, "+", pl_bit_add, 1);
    rb_define_method(pl_cBit, "concat", pl_bit_push, 1);
    rb_define_method(pl_cBit, "push", pl_bit_push, 1);
    rb_define_method(pl_cBit, "index", pl_bit_index, 1);
    rb_define_method(pl_cBit, "include?", pl_bit_include, 1);
    rb_define_method(pl_cBit, "&", pl_bit_and, 1);
    rb_define_method(pl_cBit, "|", pl_bit_or, 1);
    rb_define_method(pl_cBit, "^", pl_bit_xor, 1);
    rb_define_method(pl_cBit, "~", pl_bit_not, 0);
    rb_define_method(pl_cBit, "<<", pl_bit_left_shift, 1);
    rb_define_method(pl_cBit, ">>", pl_bit_right_shift, 1);
    rb_define_method(pl_cBit, "[]", pl_bit_aref_m, -1);
    rb_define_method(pl_cBit, "[]=", pl_bit_aset, -1);
    rb_define_method(pl_cBit, "length", pl_bit_length, 0);
    rb_define_method(pl_cBit, "size", pl_bit_length, 0);
    rb_define_method(pl_cBit, "octet_length", pl_bit_octet_length, 0);
    rb_define_method(pl_cBit, "octet_size", pl_bit_octet_length, 0);
    rb_define_method(pl_cBit, "to_s", pl_bit_to_s, 0);
    rb_define_method(pl_cBit, "to_i", pl_bit_to_i, 0);
}
