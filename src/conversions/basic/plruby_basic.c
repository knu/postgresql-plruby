#include "package.h"
#include <ruby.h>

#ifndef StringValuePtr
#define StringValuePtr(x) STR2CSTR(x)
#endif

#include "package.h"
#include <postgres.h>
#include <catalog/pg_type.h>
#include <utils/builtins.h>
#include <utils/date.h>
#include <utils/nabstime.h>

#define DFC1(a, b) DirectFunctionCall1((a), (b))

extern VALUE plruby_s_new _((int, VALUE *, VALUE));
#ifndef HAVE_RB_INITIALIZE_COPY
extern VALUE plruby_clone _((VALUE));
#endif

extern Oid plruby_datum_oid _((VALUE, int *));
extern VALUE plruby_datum_set _((VALUE, Datum));
extern VALUE plruby_datum_get _((VALUE, Oid *));

static VALUE
pl_fixnum_s_datum(VALUE obj, VALUE a)
{
    Oid typoid;
    Datum value;

    value = plruby_datum_get(a, &typoid);
    switch (typoid) {
    case OIDOID:
	return INT2NUM(DatumGetObjectId(value));
	break;
    case INT2OID:
	return INT2NUM(DatumGetInt16(value));
	break;
    case INT4OID:
	return INT2NUM(DatumGetInt32(value));
	break;
    case INT8OID:
	return INT2NUM(DatumGetInt64(value));
	break;
    default:
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    return Qnil;
}

static VALUE
pl_fixnum_to_datum(VALUE obj, VALUE a)
{
    int typoid, typlen;
    int value;
    Datum d;

    value = NUM2INT(obj);
    typoid = plruby_datum_oid(a, &typlen);
    switch (typoid) {
    case OIDOID:
    case INT2OID:
    case INT4OID:
	break;
    default:
	return Qnil;
    }
    if (typlen == 2) {
	d = Int16GetDatum(value);
    }
    else {
	d = Int32GetDatum(value);
    }
    return plruby_datum_set(a, d);
}

static VALUE
pl_float_s_datum(VALUE obj, VALUE a)
{
    Oid typoid;
    Datum value;

    value = plruby_datum_get(a, &typoid);
    switch (typoid) {
    case FLOAT4OID:
	return rb_float_new(DatumGetFloat4(value));
	break;

    case FLOAT8OID:
	return rb_float_new(DatumGetFloat8(value));
	break;

    case CASHOID:
    case NUMERICOID:
	return rb_float_new(DatumGetFloat8(DFC1(numeric_float8, value)));
	break;
    default:
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    return Qnil;
}

static VALUE
pl_float_to_datum(VALUE obj, VALUE a)
{
    int typoid, typlen;
    double value;
    Datum d;

    typoid = plruby_datum_oid(a, &typlen);
    switch (typoid) {
    case FLOAT4OID:
    case FLOAT8OID:
    case CASHOID:
    case NUMERICOID:
	break;
    default:
	return Qnil;
    }
    if (typlen == 4) {
	d = Float4GetDatum((float4)RFLOAT(obj)->value);
    }
    else {
	d = Float8GetDatum((float8)RFLOAT(obj)->value);
    }
    return plruby_datum_set(a, d);
}

static VALUE
pl_time_s_datum(VALUE obj, VALUE a)
{
    Oid typoid;
    Datum value;
    VALUE result = Qnil;

    value = plruby_datum_get(a, &typoid);
    switch (typoid) {
    case TIMESTAMPOID:
	result = rb_dbl2big(DatumGetTimestamp(value));
	goto big_time;

    case TIMESTAMPTZOID:
	result = rb_dbl2big(DatumGetTimestampTz(value));
	goto big_time;

    case ABSTIMEOID:
	result = rb_dbl2big(DatumGetTimestamp(DFC1(abstime_timestamp, value)));
	goto big_time;

    case DATEOID:
	result = rb_dbl2big(DatumGetTimestamp(DFC1(date_timestamp, value)));
	goto big_time;

    case RELTIMEOID:
	value = DFC1(reltime_interval, value);
	/* ... */
    case INTERVALOID:
	value = DFC1(time_interval, value);
	goto time_big;

    case TIMETZOID:
	value = DFC1(timetz_time, value);
	/* ... */
    case TIMEOID:
    time_big:
	result = rb_dbl2big(DatumGetTimeADT(value));
    big_time:
	result = rb_funcall(rb_cTime, rb_intern("at"), 1, result);
	break;
    default:
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    return result;
}

static VALUE pl_cTinter;

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
    d0 = rb_dbl2big(DatumGetTimestamp(DFC1(date_timestamp, 
					   DFC1(date_in, (Datum)first))));
    d1 = rb_dbl2big(DatumGetTimestamp(DFC1(date_timestamp, 
					   DFC1(date_in, (Datum)second))));
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
    tmp = rb_dbl2big(DatumGetTimestamp(DFC1(abstime_timestamp, 
					    interval->data[0])));
    tmp = rb_funcall(rb_cTime, rb_intern("at"), 1, tmp);
    OBJ_TAINT(tmp);
    rb_ary_push(res, tmp);
    tmp = rb_dbl2big(DatumGetTimestamp(DFC1(abstime_timestamp, 
					    interval->data[1])));
    tmp = rb_funcall(rb_cTime, rb_intern("at"), 1, tmp);
    OBJ_TAINT(tmp);
    rb_ary_push(res, tmp);
    OBJ_TAINT(res);
    return res;
}
	

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
    MEMCPY(t1, t0, struct pl_tint, 1);
    return copy;
}

void Init_plruby_basic()
{
    rb_define_singleton_method(rb_cFixnum, "from_datum", pl_fixnum_s_datum, 1);
    rb_define_method(rb_cFixnum, "to_datum", pl_fixnum_to_datum, 1);
    rb_define_singleton_method(rb_cFloat, "from_datum", pl_float_s_datum, 1);
    rb_define_method(rb_cFloat, "to_datum", pl_float_to_datum, 1);
    rb_define_singleton_method(rb_cTime, "from_datum", pl_time_s_datum, 1);
    pl_cTinter = rb_define_class("Tinterval", rb_cObject);
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
    rb_define_method(pl_cTinter, "low", pl_tint_low, 0);
    rb_define_method(pl_cTinter, "low=", pl_tint_lowset, 1);
    rb_define_method(pl_cTinter, "high", pl_tint_high, 0);
    rb_define_method(pl_cTinter, "high=", pl_tint_highset, 1);
    rb_define_method(pl_cTinter, "to_s", pl_tint_to_s, 0);
}
