#include "convcommon.h"

#include <utils/date.h>
#include <utils/nabstime.h>

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
	return rb_float_new(DatumGetFloat8(plruby_dfc1(numeric_float8, value)));
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
	result = rb_dbl2big(DatumGetTimestamp(plruby_dfc1(abstime_timestamp, value)));
	goto big_time;

    case DATEOID:
	result = rb_dbl2big(DatumGetTimestamp(plruby_dfc1(date_timestamp, value)));
	goto big_time;

    case RELTIMEOID:
	value = plruby_dfc1(reltime_interval, value);
	/* ... */
    case INTERVALOID:
	value = plruby_dfc1(time_interval, value);
	goto time_big;

    case TIMETZOID:
	value = plruby_dfc1(timetz_time, value);
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

void Init_plruby_basic()
{
    rb_define_singleton_method(rb_cFixnum, "from_datum", pl_fixnum_s_datum, 1);
    rb_define_method(rb_cFixnum, "to_datum", pl_fixnum_to_datum, 1);
    rb_define_singleton_method(rb_cFloat, "from_datum", pl_float_s_datum, 1);
    rb_define_method(rb_cFloat, "to_datum", pl_float_to_datum, 1);
    rb_define_singleton_method(rb_cTime, "from_datum", pl_time_s_datum, 1);
}
