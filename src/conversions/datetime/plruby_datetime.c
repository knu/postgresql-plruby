#include "convcommon.h"

#include <utils/date.h>
#include <utils/nabstime.h>

static VALUE pl_cTinter, pl_mMarshal;

static char *
pl_dequote(char *src)
{
    char *origin;

    while (*src && *src != '"') ++src;
    if (*src != '"') {
	rb_raise(rb_eArgError, "Invalid Tinterval");
    }
    ++src;
    origin = src;
    while (*src && *src != '"') ++src;
    if (*src != '"') {
	rb_raise(rb_eArgError, "Invalid Tinterval");
    }
    *src = 0;
    return origin;
}

struct pl_tint {
    VALUE low, high;
};

static void
pl_tint_mark(struct pl_tint *tint)
{
    rb_gc_mark(tint->low);
    rb_gc_mark(tint->high);
}

static VALUE
pl_tint_s_alloc(VALUE obj)
{
    struct pl_tint *tint;
    return Data_Make_Struct(obj, struct pl_tint, pl_tint_mark, free, tint);
}

static VALUE
pl_tint_s_from_string(VALUE obj, VALUE str)
{
    char *first, *second, *tmp;
    VALUE d0, d1;
    struct pl_tint *tint;
    VALUE res;

    tmp = StringValuePtr(str);
    first = pl_dequote(tmp);
    second = pl_dequote(first + strlen(first) + 1);
    d0 = rb_dbl2big(DatumGetTimestamp(PLRUBY_DFC1(date_timestamp, 
                                                  PLRUBY_DFC1(date_in, first))));
    d1 = rb_dbl2big(DatumGetTimestamp(PLRUBY_DFC1(date_timestamp, 
                                                  PLRUBY_DFC1(date_in, second))));
    res = Data_Make_Struct(obj, struct pl_tint, pl_tint_mark, free, tint);
    tint->low = rb_funcall(rb_cTime, rb_intern("at"), 1, d0);
    tint->high = rb_funcall(rb_cTime, rb_intern("at"), 1, d1);
    if (OBJ_TAINTED(str)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_tint_s_datum(VALUE obj, VALUE a)
{
    TimeIntervalData *interval;
    Oid typoid;
    VALUE tmp, res;

    interval = (TimeIntervalData *)plruby_datum_get(a, &typoid);
    if (typoid != TINTERVALOID) {
        rb_raise(rb_eArgError, "invalid argument");
    }
    res = rb_ary_new2(2);
    tmp = rb_dbl2big(DatumGetTimestamp(PLRUBY_DFC1(abstime_timestamp, 
                                                   interval->data[0])));
    tmp = rb_funcall(rb_cTime, rb_intern("at"), 1, tmp);
    OBJ_TAINT(tmp);
    rb_ary_push(res, tmp);
    tmp = rb_dbl2big(DatumGetTimestamp(PLRUBY_DFC1(abstime_timestamp, 
                                                   interval->data[1])));
    tmp = rb_funcall(rb_cTime, rb_intern("at"), 1, tmp);
    OBJ_TAINT(tmp);
    rb_ary_push(res, tmp);
    OBJ_TAINT(res);
    return res;
}

#if PG_PL_VERSION >= 74
	
static VALUE
pl_tint_mdump(int argc, VALUE *argv, VALUE obj)
{
    struct pl_tint *tint;
    VALUE ary;

    Data_Get_Struct(obj, struct pl_tint, tint);
    ary = rb_ary_new2(2);
    rb_ary_push(ary, tint->low);
    rb_ary_push(ary, tint->high);
    return rb_funcall(pl_mMarshal, rb_intern("dump"), 1, ary);
}

static VALUE
pl_tint_mload(VALUE obj, VALUE a)
{
    struct pl_tint *tint;

    if (TYPE(a) != T_STRING || !RSTRING(a)->len) {
        rb_raise(rb_eArgError, "expected a String object");
    }
    a = rb_funcall(pl_mMarshal, rb_intern("load"), 1, a);
    if (TYPE(a) != T_ARRAY || RARRAY(a)->len != 2) {
        rb_raise(rb_eArgError, "expected an Array with 2 elements");
    }
    if (!rb_obj_is_kind_of(RARRAY(a)->ptr[0], rb_cTime) ||
        !rb_obj_is_kind_of(RARRAY(a)->ptr[1], rb_cTime)) {
	rb_raise(rb_eArgError, "need 2 Times objects");
    }
    Data_Get_Struct(obj, struct pl_tint, tint);
    tint->low = RARRAY(a)->ptr[0];
    tint->high = RARRAY(a)->ptr[1];
    return obj;
}

#endif
    
static VALUE
pl_tint_init(VALUE obj, VALUE a, VALUE b)
{
    struct pl_tint *tint;

    if (!rb_obj_is_kind_of(a, rb_cTime) || !rb_obj_is_kind_of(b, rb_cTime)) {
	rb_raise(rb_eArgError, "need 2 Times objects");
    }
    Data_Get_Struct(obj, struct pl_tint, tint);
    tint->low = a;
    tint->high = b;
    if (OBJ_TAINTED(a) || OBJ_TAINTED(b)) OBJ_TAINT(obj);
    return obj;
}

static VALUE
pl_tint_low(VALUE obj)
{
    struct pl_tint *tint;
    VALUE res;

    Data_Get_Struct(obj, struct pl_tint, tint);
    res = rb_obj_dup(tint->low);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_tint_lowset(VALUE obj, VALUE a)
{
    struct pl_tint *tint;

    Data_Get_Struct(obj, struct pl_tint, tint);
    if (!rb_obj_is_kind_of(a, rb_cTime)) {
	rb_raise(rb_eArgError, "need a Time object");
    }
    tint->low = a;
    if (OBJ_TAINTED(a)) OBJ_TAINT(obj);
    return a;
}

static VALUE
pl_tint_high(VALUE obj)
{
    struct pl_tint *tint;
    VALUE res;

    Data_Get_Struct(obj, struct pl_tint, tint);
    res = rb_obj_dup(tint->high);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_tint_highset(VALUE obj, VALUE a)
{
    struct pl_tint *tint;

    Data_Get_Struct(obj, struct pl_tint, tint);
    if (!rb_obj_is_kind_of(a, rb_cTime)) {
	rb_raise(rb_eArgError, "need a Time object");
    }
    tint->high = a;
    if (OBJ_TAINTED(a)) OBJ_TAINT(obj);
    return a;
}

#define tinterval_str "[\"%s\" \"%s\"]"

static VALUE
pl_tint_to_s(VALUE obj)
{
    char *tmp, *t0, *t1;
    VALUE v0, v1;
    struct pl_tint *tint;

    Data_Get_Struct(obj, struct pl_tint, tint);
    v0 = plruby_to_s(tint->low);
    t0 = StringValuePtr(v0);
    v1 = plruby_to_s(tint->high);
    t1 = StringValuePtr(v1);
    tmp = ALLOCA_N(char, strlen(tinterval_str) + strlen(t0) + strlen(t1) + 1);
    sprintf(tmp, tinterval_str, t0, t1);
    if (OBJ_TAINTED(obj)) {
	return rb_tainted_str_new2(tmp);
    }
    return rb_str_new2(tmp);
}

static VALUE
pl_tint_init_copy(VALUE copy, VALUE orig)
{
    struct pl_tint *t0, *t1;

    if (copy == orig) return copy;
    if (TYPE(orig) != T_DATA ||
        RDATA(orig)->dmark != (RUBY_DATA_FUNC)pl_tint_mark) {
        rb_raise(rb_eTypeError, "wrong argument type to clone");
    }
    Data_Get_Struct(orig, struct pl_tint, t0);
    Data_Get_Struct(copy, struct pl_tint, t1);
    t1->low = rb_obj_dup(t0->low);
    t1->high = rb_obj_dup(t0->high);
    return copy;
}

void Init_plruby_datetime()
{
    pl_mMarshal = rb_const_get(rb_cObject, rb_intern("Marshal"));
    pl_cTinter = rb_define_class("Tinterval", rb_cObject);
    rb_undef_method(CLASS_OF(pl_cTinter), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cTinter, pl_tint_s_alloc);
#else
    rb_define_singleton_method(pl_cTinter, "allocate", pl_tint_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cTinter, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cTinter, "from_string", pl_tint_s_from_string, 1);
    rb_define_singleton_method(pl_cTinter, "from_datum", pl_tint_s_datum, 1);
    rb_define_method(pl_cTinter, "initialize", pl_tint_init, 2);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cTinter, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cTinter, "initialize_copy", pl_tint_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cTinter, "marshal_load", pl_tint_mload, 1);
    rb_define_method(pl_cTinter, "marshal_dump", pl_tint_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cTinter, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cTinter, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cTinter, "low", pl_tint_low, 0);
    rb_define_method(pl_cTinter, "low=", pl_tint_lowset, 1);
    rb_define_method(pl_cTinter, "high", pl_tint_high, 0);
    rb_define_method(pl_cTinter, "high=", pl_tint_highset, 1);
    rb_define_method(pl_cTinter, "to_s", pl_tint_to_s, 0);
}
