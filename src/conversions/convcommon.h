#include "package.h"

#include "package.h"
#include <ruby.h>
#include "package.h"
#include <postgres.h>
#include <catalog/pg_type.h>
#include <utils/builtins.h>
#include <lib/stringinfo.h>

#define CPY_FREE(p0_, p1_, size_) do {		\
    void *p2_ = (void *)p1_;			\
    memcpy((p0_), (p2_), (size_));		\
    pfree(p2_);					\
} while (0)

#if PG_PL_VERSION >= 74

#define PL_MDUMP(name_,func_)                                   \
static VALUE                                                    \
name_(int argc, VALUE *argv, VALUE obj)                         \
{                                                               \
    void *mac;                                                  \
    char *res;                                                  \
    VALUE result;                                               \
                                                                \
    Data_Get_Struct(obj, void, mac);                            \
    res = (char *)PLRUBY_DFC1(func_, mac);                      \
    result = rb_tainted_str_new(VARDATA(res), VARSIZE(res));    \
    pfree(res);                                                 \
    return result;                                              \
}

#define PL_MLOAD(name_,func_,type_)                                     \
static VALUE                                                            \
name_(VALUE obj, VALUE a)                                               \
{                                                                       \
    StringInfoData si;                                                  \
    type_ *mac0, *mac1;                                                 \
                                                                        \
    if (TYPE(a) != T_STRING || !RSTRING(a)->len) {                      \
        rb_raise(rb_eArgError, "expected a String object");             \
    }                                                                   \
    initStringInfo(&si);                                                \
    appendBinaryStringInfo(&si, RSTRING(a)->ptr, RSTRING(a)->len);      \
    mac1 = (type_ *)PLRUBY_DFC1(func_, &si);                            \
    pfree(si.data);                                                     \
    Data_Get_Struct(obj, type_, mac0);                                  \
    CPY_FREE(mac0, mac1, sizeof(type_));                                \
    return obj;                                                         \
}

#define PL_MLOADVAR(name_,func_,type_,size_)                            \
static VALUE                                                            \
name_(VALUE obj, VALUE a)                                               \
{                                                                       \
    StringInfoData si;                                                  \
    type_ *mac0, *mac1;                                                 \
    int szl;                                                            \
                                                                        \
    if (TYPE(a) != T_STRING || !RSTRING(a)->len) {                      \
        rb_raise(rb_eArgError, "expected a String object");             \
    }                                                                   \
    initStringInfo(&si);                                                \
    appendBinaryStringInfo(&si, RSTRING(a)->ptr, RSTRING(a)->len);      \
    mac1 = (type_ *)PLRUBY_DFC1(func_, &si);                            \
    pfree(si.data);                                                     \
    Data_Get_Struct(obj, type_, mac0);                                  \
    free(mac0);                                                         \
    szl = size_(mac1);                                                  \
    mac0 = (type_ *)ALLOC_N(char, szl);                                 \
    CPY_FREE(mac0, mac1, szl);                                          \
    RDATA(obj)->data = mac0;                                            \
    return obj;                                                         \
}

#ifndef RUBY_CAN_USE_MARSHAL_LOAD
extern VALUE plruby_s_load _((VALUE, VALUE));
#endif

#else

#define PL_MDUMP(name_,func_)
#define PL_MLOAD(name_,func_,type_) 
#define PL_MLOADVAR(name_,func_,type_,size_)

#endif

extern VALUE plruby_to_s _((VALUE));
extern VALUE plruby_s_new _((int, VALUE *, VALUE));
extern VALUE plruby_define_void_class _((char *, char *));
#ifndef HAVE_RB_INITIALIZE_COPY
extern VALUE plruby_clone _((VALUE));
#endif
extern Oid plruby_datum_oid _((VALUE, int *));
extern VALUE plruby_datum_set _((VALUE, Datum));
extern VALUE plruby_datum_get _((VALUE, Oid *));

#ifndef StringValuePtr
#define StringValuePtr(x) STR2CSTR(x)
#endif

extern Datum plruby_dfc0 _((PGFunction));
extern Datum plruby_dfc1 _((PGFunction, Datum));
extern Datum plruby_dfc2 _((PGFunction, Datum, Datum));
extern Datum plruby_dfc3 _((PGFunction, Datum, Datum, Datum));

#define PLRUBY_DFC0(a_) plruby_dfc0(a_)
#define PLRUBY_DFC1(a_,b_) plruby_dfc1(a_,PointerGetDatum(b_))
#define PLRUBY_DFC2(a_,b_,c_) plruby_dfc2(a_,PointerGetDatum(b_),PointerGetDatum(c_))
#define PLRUBY_DFC3(a_,b_,c_,d_) plruby_dfc3(a_,PointerGetDatum(b_),PointerGetDatum(c_),PointerGetDatum(d_))
