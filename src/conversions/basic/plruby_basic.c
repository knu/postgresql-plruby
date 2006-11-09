#include "convcommon.h"

#include <utils/cash.h>
#include <utils/date.h>
#include <utils/nabstime.h>
#include <utils/pg_locale.h>
#include <utils/timestamp.h>
#include <math.h>

static double cash_divisor;
static Timestamp epoch;
static ID id_at, id_to_f, id_to_i, id_usec;

static VALUE
pl_fixnum_s_datum(VALUE obj, VALUE a)
{
    Oid typoid;
    Datum value;

    value = plruby_datum_get(a, &typoid);
    switch (typoid) {
    case OIDOID:
	return UINT2NUM(DatumGetObjectId(value));

    case INT2OID:
	return INT2NUM(DatumGetInt16(value));

    case INT4OID:
	return INT2NUM(DatumGetInt32(value));

    case INT8OID:
	return LL2NUM(DatumGetInt64(value));

    default:
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
}

static VALUE
pl_fixnum_to_datum(VALUE obj, VALUE a)
{
    Datum d;

    switch (plruby_datum_oid(a, NULL)) {
    case OIDOID:
	d = ObjectIdGetDatum(NUM2UINT(obj));
	break;

    case INT2OID:
	d = Int16GetDatum(NUM2INT(obj));
	break;

    case INT4OID:
	d = Int32GetDatum(NUM2INT(obj));
	break;

    case INT8OID:
	d = Int64GetDatum(NUM2LL(obj));
	break;

    default:
	return Qnil;
    }
    return plruby_datum_set(a, d);
}

static VALUE
pl_float_s_datum(VALUE obj, VALUE a)
{
    Oid typoid;
    Datum value;
    double result;

    value = plruby_datum_get(a, &typoid);
    switch (typoid) {
    case FLOAT4OID:
	result = DatumGetFloat4(value);
	break;

    case FLOAT8OID:
	result = DatumGetFloat8(value);
	break;

    case CASHOID:
        result = (double) *(Cash *) DatumGetPointer(value) / cash_divisor;
        break;

    case NUMERICOID:
	result = DatumGetFloat8(plruby_dfc1(numeric_float8, value));
	break;

    default:
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    return rb_float_new(result);
}

extern double round();

static VALUE
pl_float_to_datum(VALUE obj, VALUE a)
{
    double value;
    Datum d;

    value = RFLOAT(obj)->value;
    switch (plruby_datum_oid(a, NULL)) {
    case FLOAT4OID:
        d = Float4GetDatum((float4)value);
        break;

    case FLOAT8OID:
        d = Float8GetDatum((float8)value);
        break;

    case CASHOID:
    {
        Cash *cash = (Cash *) palloc(sizeof(Cash));
        *cash = (Cash) round(value * cash_divisor);
        d = PointerGetDatum(cash);
        break;
    }
       
    case NUMERICOID:
        d = plruby_dfc1(float8_numeric, Float8GetDatum((float8)value));
	break;

    default:
	return Qnil;
    }
    return plruby_datum_set(a, d);
}

static VALUE
pl_str_s_datum(VALUE klass, VALUE a)
{
    bytea *data;
    Oid typoid;
    Datum value;

    value = plruby_datum_get(a, &typoid);
    if (typoid != BYTEAOID) {
        return Qnil;
    }
    data = DatumGetByteaP(value);
    return rb_str_new(VARDATA(data), VARSIZE(data) - VARHDRSZ);
}

static VALUE
pl_str_to_datum(VALUE obj, VALUE a)
{
    bytea      *data;
    size_t     len;

    /* Converts BYTEA only. */
    if (plruby_datum_oid(a, NULL) != BYTEAOID)
       return Qnil;

    len = RSTRING_LEN(obj);
    data = palloc(VARHDRSZ + len);
    memcpy(VARDATA(data), RSTRING_PTR(obj), len);
    VARATT_SIZEP(data) = VARHDRSZ + len;

    return plruby_datum_set(a, PointerGetDatum(data));
}

static VALUE
pl_time_s_datum(VALUE klass, VALUE a)
{
    Timestamp	ts;
    Oid		typoid;
    Datum	value;

    /*
     * INTERVAL and RELTIME are converted to Float (number of seconds).
     * For INTERVALs containing nonzero month/year component, duration of one
     * month is assumed to be 30*24*60*60 seconds.  A special type has to be
     * created for this, because of the months/year components and also because
     * long or short enough numbers do not convert back right (exponential
     * notation of INTERVALs is not accepted by Postgres).
     *
     * TIMESTAMP, TIMESTAMP WITH TIME ZONE, ABSTIME, DATE are converted to klass
     * (Time), naturally.
     *
     * TIME and TIME WITH TIME ZONE are also converted to klass (Time), as in
     * the (totally broken anyway) 0.4.3 implementation.  The result is that
     * specific time since Unix epoch.  That makes little sense (the reverse
     * conversion of the result breaks anyway), but some at least.  A special
     * type has to be created for this.
     */
    value = plruby_datum_get(a, &typoid);
    switch (typoid) {
	/* Time interval types. */

    case RELTIMEOID:
	value = plruby_dfc1(reltime_interval, value);
	/* ... */
    case INTERVALOID:
	{
	    Interval *iv = DatumGetIntervalP(value);

	    return rb_float_new((double) iv->month * 30*24*60*60 + 
		    iv->time
#ifdef HAVE_INT64_TIMESTAMP
		    / 1E6
#endif
		);
	}

	/*
	 * Time of day types.
	 *
	 * No separate conversion code is written, abusing the coincidence of C
	 * types used for TimeADT and Timestamp (int64 or double, depending on
	 * HAVE_INT64_TIMESTAMP).  The proper implementation would use a special
	 * type anyway, see above.
	 */

    case TIMETZOID:
	{
	    TimeTzADT *timetz = DatumGetTimeTzADTP(value);

	    /* Shift according to the timezone. */
	    ts = timetz->time + (Timestamp) timetz->zone
#ifdef HAVE_INT64_TIMESTAMP
		* 1000000
#endif
		;
	}
	goto convert;

    case TIMEOID:
	ts = (Timestamp) DatumGetTimeADT(value);
	goto convert;

	/* The rest of types end up as a Timestamp in `value'. */

    case ABSTIMEOID:
	value = plruby_dfc1(abstime_timestamptz, value);
	break;

    case DATEOID:
	value = plruby_dfc1(date_timestamptz, value);
	break;

    case TIMESTAMPOID:
    case TIMESTAMPTZOID:
	break;

    default:
	rb_raise(rb_eTypeError, "%s: incompatible type OID %u",
		rb_class2name(klass), typoid);
    }

    ts = DatumGetTimestamp(value) - epoch;

convert:
    return rb_funcall(klass, id_at,
#ifndef HAVE_INT64_TIMESTAMP
	    1, rb_float_new(ts)
#else
	    2, LONG2NUM(ts / 1000000), ULONG2NUM(ts % 1000000)
#endif
	);
}

static VALUE
pl_time_to_datum(VALUE obj, VALUE a)
{
    PGFunction	conv;
    Datum	d;
    int		typoid;

    typoid = plruby_datum_oid(a, NULL);
    switch (typoid) {
    case ABSTIMEOID:
    case DATEOID:
    case TIMEOID:
    case TIMESTAMPOID:
    case TIMESTAMPTZOID:
    case TIMETZOID:
	break;

    default:
	return Qnil;
    }

    /* Convert Time to TimestampTz first. */
#ifndef HAVE_INT64_TIMESTAMP
    d = TimestampTzGetDatum(epoch + NUM2DBL(rb_funcall(obj, id_to_f, 0)));
#else
    d = TimestampTzGetDatum(epoch + (TimestampTz) 
                            NUM2LONG(rb_funcall(obj, id_to_i, 0)) * 1000000 +
                            NUM2ULONG(rb_funcall(obj, id_usec, 0)));
#endif

    conv = NULL;
    switch (typoid) {
    case ABSTIMEOID:
	conv = timestamptz_abstime;
	break;

    case DATEOID:
	conv = timestamptz_date;
	break;

    case TIMEOID:
	conv = timestamptz_time;
	break;

    case TIMESTAMPOID:
	conv = timestamptz_timestamp;
	break;

    case TIMESTAMPTZOID:
	break;

    case TIMETZOID:
	conv = timestamptz_timetz;
	break;
    }

    if (conv == NULL) {
        return Qnil;
    }
    d = plruby_dfc1(conv, d);
    return plruby_datum_set(a, d);
}

void Init_plruby_basic()
{
    int fpoint;
    struct lconv *lconvert = PGLC_localeconv();

    fpoint = lconvert->frac_digits;
    if (fpoint < 0 || fpoint > 10) {
        fpoint = 2;
    }
    cash_divisor = pow(10.0, fpoint);
    epoch = SetEpochTimestamp();
    id_at = rb_intern("at");
    id_to_f = rb_intern("to_f");
    id_to_i = rb_intern("to_i");
    id_usec = rb_intern("usec");

    rb_define_singleton_method(rb_cFixnum, "from_datum", pl_fixnum_s_datum, 1);
    rb_define_method(rb_cFixnum, "to_datum", pl_fixnum_to_datum, 1);
    rb_define_singleton_method(rb_cFloat, "from_datum", pl_float_s_datum, 1);
    rb_define_method(rb_cFloat, "to_datum", pl_float_to_datum, 1);
    rb_define_singleton_method(rb_cString, "from_datum", pl_str_s_datum, 1);
    rb_define_method(rb_cString, "to_datum", pl_str_to_datum, 1);
    rb_define_singleton_method(rb_cTime, "from_datum", pl_time_s_datum, 1);
    rb_define_method(rb_cTime, "to_datum", pl_time_to_datum, 1);
}
