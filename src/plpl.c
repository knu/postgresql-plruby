#include "plruby.h"

static VALUE pl_ePLruby, pl_mPLtemp;
static VALUE pl_mPL, pl_cPLPlan;

static ID id_thr;

static VALUE pl_SPI_exec _((int, VALUE *, VALUE));

#ifdef PLRUBY_HASH_DELETE
static ID id_delete;

#define rb_hash_delete(a, b) rb_funcall((a), id_delete, 1, (b))

#endif

#ifdef PLRUBY_ENABLE_CONVERSION

static VALUE plruby_classes;
static VALUE plruby_conversions;
static ID id_from_datum;
static ID id_to_datum;

static VALUE
protect_require(VALUE name)
{
    return rb_require((char *)name);
}

static void
plruby_require(char *name)
{
    int status;

    rb_protect(protect_require, (VALUE)name, &status);
    if (status) {
        plruby_fatal = 1;
        elog(ERROR, "can't find %s : try first `make install'", name);
    }
}

static void
pl_init_conversions()
{
#include "conversions.h"
}

#ifndef HAVE_RB_INITIALIZE_COPY

VALUE
plruby_clone(VALUE obj)
{
    VALUE res = rb_funcall2(rb_obj_class(obj), rb_intern("allocate"), 0, 0);
    CLONESETUP(res, obj);
    rb_funcall(res, rb_intern("initialize_copy"), 1, obj);
    return res;
}

#endif

#endif

static char *names = "SELECT a.attname FROM pg_class c, pg_attribute a"
" WHERE c.relname = '%s' AND a.attnum > 0 AND a.attrelid = c.oid"
" ORDER BY a.attnum";

static VALUE
pl_column_name(VALUE obj, VALUE table)
{
    VALUE *query, res;
    char *tmp;

    if (TYPE(table) != T_STRING || !RSTRING(table)->ptr) {
        rb_raise(pl_ePLruby, "expected a String");
    }
    tmp = ALLOCA_N(char, strlen(names) + RSTRING(table)->len + 1);
    sprintf(tmp, names, RSTRING(table)->ptr);
    query = ALLOCA_N(VALUE, 3);
    query[0] = rb_str_new2(tmp);
    query[1] = Qnil;
    query[2] = rb_str_new2("value");
    res = pl_SPI_exec(3, query, pl_mPL);
    rb_funcall2(res, rb_intern("flatten!"), 0, 0);
    return res;
}

static char *types = 
"SELECT t.typname FROM pg_class c, pg_attribute a, pg_type t"
" WHERE c.relname = '%s' and a.attnum > 0"
" and a.attrelid = c.oid and a.atttypid = t.oid"
" ORDER BY a.attnum";

static VALUE
pl_column_type(VALUE obj, VALUE table)
{
    VALUE *query, res;
    char *tmp;

    if (TYPE(table) != T_STRING || !RSTRING(table)->ptr) {
        rb_raise(pl_ePLruby, "expected a String");
    }
    tmp = ALLOCA_N(char, strlen(types) + RSTRING(table)->len + 1);
    sprintf(tmp, types, RSTRING(table)->ptr);
    query = ALLOCA_N(VALUE, 3);
    query[0] = rb_str_new2(tmp);
    query[1] = Qnil;
    query[2] = rb_str_new2("value");
    res = pl_SPI_exec(3, query, pl_mPL);
    rb_funcall2(res, rb_intern("flatten!"), 0, 0);
    return res;
}

#if PG_PL_VERSION >= 73

struct pl_tuple {
    MemoryContext cxt;
    AttInMetadata *att;
    pl_proc_desc *pro;
    TupleDesc dsc;
    Tuplestorestate *out;
    PG_FUNCTION_ARGS;
};

extern int SortMem;

static void pl_thr_mark(struct pl_tuple *tpl) {}

#define GetTuple(tmp_, tpl_) do {                               \
    if (TYPE(tmp_) != T_DATA ||                                 \
        RDATA(tmp_)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {    \
        rb_raise(pl_ePLruby, "invalid thread local variable");  \
    }                                                           \
    Data_Get_Struct(tmp_, struct pl_tuple, tpl_);               \
} while(0)


static VALUE
pl_query_name(VALUE obj)
{
    VALUE res, tmp;
    struct pl_tuple *tpl;
    char * attname;
    int i;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    if (NIL_P(tmp)) {
        return Qnil;
    }
    GetTuple(tmp, tpl);
    if (!tpl->dsc) {
        return Qnil;
    }
    res = rb_ary_new2(tpl->dsc->natts);
    for (i = 0; i < tpl->dsc->natts; i++) {
        PLRUBY_BEGIN(1);
        attname = NameStr(tpl->dsc->attrs[i]->attname);
        PLRUBY_END;
        rb_ary_push(res, rb_tainted_str_new2(attname));
    }
    return res;
}

static VALUE
pl_query_type(VALUE obj)
{
    struct pl_tuple *tpl;
    VALUE res, tmp;
    char * attname;
    HeapTuple typeTup;
    Form_pg_type fpgt;
    int i;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    if (NIL_P(tmp)) {
        return Qnil;
    }
    GetTuple(tmp, tpl);
    if (!tpl->dsc) {
        PLRUBY_BEGIN(1);
        typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                      ObjectIdGetDatum(tpl->pro->result_oid),
                                      0, 0, 0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for result type %ld failed",
                     tpl->pro->result_oid);
        }
        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        res = rb_tainted_str_new2(fpgt->typname.data);
        ReleaseSysCache(typeTup);
        return res;
    }
    res = rb_ary_new2(tpl->dsc->natts);
    for (i = 0; i < tpl->dsc->natts; i++) {
        PLRUBY_BEGIN(1);
        attname = NameStr(tpl->dsc->attrs[i]->attname);
        typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                      ObjectIdGetDatum(tpl->dsc->attrs[i]->atttypid),
                                      0, 0, 0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
                 attname, ObjectIdGetDatum(tpl->dsc->attrs[i]->atttypid));
        }
        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        rb_ary_push(res, rb_tainted_str_new2(fpgt->typname.data));
        ReleaseSysCache(typeTup);
    }
    return res;
}

static VALUE
pl_query_description(VALUE obj)
{
    VALUE name, types, res;
    VALUE tt_virg, tt_blc;
    int i;

    tt_virg = rb_str_new2(", ");
    tt_blc = rb_str_new2(" ");
    name = pl_query_name(obj);
    if (NIL_P(name)) {
        return Qnil;
    }
    types = pl_query_type(obj);
    if (TYPE(name) != T_ARRAY || TYPE(types) != T_ARRAY ||
        RARRAY(name)->len != RARRAY(types)->len) {
        rb_raise(pl_ePLruby, "unknown error");
    }
    res = rb_tainted_str_new2("");
    for (i = 0; i < RARRAY(name)->len; ++i) {
        rb_str_concat(res, RARRAY(name)->ptr[i]);
        rb_str_concat(res, tt_blc);
        rb_str_concat(res, RARRAY(types)->ptr[i]);
        if (i != (RARRAY(name)->len - 1)) {
            rb_str_concat(res, tt_virg);
        }
    }
    return res;
}

static VALUE
pl_query_lgth(VALUE obj)
{
    VALUE tmp;
    struct pl_tuple *tpl;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    if (NIL_P(tmp)) {
        return Qnil;
    }
    GetTuple(tmp, tpl);
    if (!tpl->dsc) {
        return Qnil;
    }
    return INT2NUM(tpl->dsc->natts);
}

static VALUE
pl_args_type(VALUE obj)
{
    struct pl_tuple *tpl;
    VALUE res, tmp;
    HeapTuple typeTup;
    Form_pg_type fpgt;
    int i;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    if (NIL_P(tmp)) {
        return Qnil;
    }
    GetTuple(tmp, tpl);
    res = rb_ary_new2(tpl->pro->nargs);
    for (i = 0; i < tpl->pro->nargs; i++) {
        PLRUBY_BEGIN(1);
        typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                      ObjectIdGetDatum(tpl->pro->arg_type[i]),
                                      0, 0, 0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for type %ld failed",
                     ObjectIdGetDatum(tpl->pro->arg_type[i]));
        }
        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        rb_ary_push(res, rb_tainted_str_new2(fpgt->typname.data));
        ReleaseSysCache(typeTup);
    }
    return res;
}

static VALUE PLcontext;

struct PL_node
{
    NodeTag type;
    VALUE value;
};

static VALUE
pl_context_get(VALUE obj)
{
    struct pl_tuple *tpl;
    VALUE tmp;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    if (NIL_P(tmp)) {
        return Qnil;
    }
    GetTuple(tmp, tpl);
    if (!tpl->fcinfo || !tpl->fcinfo->context || 
        !IsA(tpl->fcinfo->context, Invalid)) {
        return Qnil;
    }
    return ((struct PL_node *)tpl->fcinfo->context)->value;
}

static VALUE
pl_context_set(VALUE obj, VALUE a)
{
    struct pl_tuple *tpl;
    VALUE tmp;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    GetTuple(tmp, tpl);
    if (tpl->fcinfo && tpl->fcinfo->context) {
        if (!IsA(tpl->fcinfo->context, Invalid)) {
            rb_raise(pl_ePLruby, "trying to change a valid context");
        }  
        rb_hash_delete(PLcontext, ((struct PL_node *)tpl->fcinfo->context)->value);
    }
    else {
        if (!tpl->fcinfo) {
            rb_raise(pl_ePLruby, "no function info");
        }
        tpl->fcinfo->context = newNode(sizeof(struct PL_node), T_Invalid);
    }
    ((struct PL_node *)tpl->fcinfo->context)->value = a;
    rb_hash_aset(PLcontext, a, Qnil);
    return a;
}

static void
pl_context_remove()
{
    struct pl_tuple *tpl;
    VALUE tmp;

    tmp = rb_thread_local_aref(rb_thread_current(), id_thr);
    GetTuple(tmp, tpl);
    if (tpl->fcinfo && tpl->fcinfo->context) {
        rb_hash_delete(PLcontext, ((struct PL_node *)tpl->fcinfo->context)->value);
        pfree(tpl->fcinfo->context);
    }
}

static VALUE
pl_tuple_s_new(PG_FUNCTION_ARGS, pl_proc_desc *prodesc)
{
    VALUE res;
    ReturnSetInfo *rsi;
    struct pl_tuple *tpl;

    if (!fcinfo || !fcinfo->resultinfo) {
        rb_raise(pl_ePLruby, "no description given");
    }
    rsi = (ReturnSetInfo *)fcinfo->resultinfo;
    if ((rsi->allowedModes & SFRM_Materialize) == 0 || !rsi->expectedDesc) {
        rb_raise(pl_ePLruby, "context don't accept set");
    }
    res = rb_thread_local_aref(rb_thread_current(), id_thr);
    if (NIL_P(res)) {
        res = Data_Make_Struct(rb_cData, struct pl_tuple, pl_thr_mark, free, tpl);
    }
    GetTuple(res, tpl);
    tpl->cxt = rsi->econtext->ecxt_per_query_memory;
    tpl->dsc = rsi->expectedDesc;
    tpl->att = TupleDescGetAttInMetadata(rsi->expectedDesc);
    tpl->pro = prodesc;
    rb_thread_local_aset(rb_thread_current(), id_thr, res);
    return res;
}

#endif

#ifdef PLRUBY_ENABLE_CONVERSION

struct datum_value {
    Datum d;
    Oid typoid;
    int typlen;
};

static void pl_conv_mark() {}

Oid plruby_datum_oid(VALUE obj, int *typlen)
{
    struct datum_value *dv;

    if (TYPE(obj) != T_DATA ||
        RDATA(obj)->dmark != (RUBY_DATA_FUNC)pl_conv_mark) {
        rb_raise(pl_ePLruby, "invalid Datum value");
    }
    Data_Get_Struct(obj, struct datum_value, dv);
    if (typlen) *typlen = dv->typlen;
    return dv->typoid;
}

VALUE plruby_datum_set(VALUE obj, Datum d)
{
    struct datum_value *dv;

    if (TYPE(obj) != T_DATA ||
        RDATA(obj)->dmark != (RUBY_DATA_FUNC)pl_conv_mark) {
        rb_raise(pl_ePLruby, "invalid Datum value");
    }
    Data_Get_Struct(obj, struct datum_value, dv);
    dv->d = d;
    return obj;
}

Datum plruby_datum_get(VALUE obj, Oid *typoid)
{
    struct datum_value *dv;

    if (TYPE(obj) != T_DATA ||
        RDATA(obj)->dmark != (RUBY_DATA_FUNC)pl_conv_mark) {
        rb_raise(pl_ePLruby, "invalid Datum value");
    }
    Data_Get_Struct(obj, struct datum_value, dv);
    if (typoid) *typoid = dv->typoid;
    return dv->d;
}

#endif

Datum
plruby_to_datum(VALUE obj, FmgrInfo *finfo, Oid typoid,
                Oid typelem, int typlen)
{
    Datum d;

    if (typoid == BOOLOID) {
        return BoolGetDatum(RTEST(obj));
    }
#ifdef PLRUBY_ENABLE_CONVERSION
    if (rb_respond_to(obj, id_to_datum)) {
        struct datum_value *dv;
        VALUE res;
        
        res = Data_Make_Struct(rb_cData, struct datum_value, pl_conv_mark, free, dv);
        dv->typoid = typoid;
        dv->typlen = typlen;
        res = rb_funcall(obj, id_to_datum, 1, res);
        if (TYPE(res) == T_DATA &&
            RDATA(res)->dmark == (RUBY_DATA_FUNC)pl_conv_mark) {
            Data_Get_Struct(res, struct datum_value, dv);
            if (dv->typoid == typoid && dv->typlen == typlen && dv->d) {
                return dv->d;
            }
        }
    }
#endif
    obj = plruby_to_s(obj);
    PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
    d = FunctionCall3(finfo, PointerGetDatum(RSTRING(obj)->ptr),
                      ObjectIdGetDatum(typelem), Int32GetDatum(typlen));
#else
    d = (Datum)(*fmgr_faddr(finfo))(RSTRING(obj)->ptr, typelem, typlen);
#endif
    PLRUBY_END;
    return d;
}

#if PG_PL_VERSION >= 74

Datum
plruby_return_array(VALUE ary, pl_proc_desc *p)
{
    VALUE tmp;
    int i, total, ndim, *dim, *lbs;
    Datum *values;
    ArrayType *array;

    tmp = rb_Array(ary);
    total = 1;
    dim = ALLOCA_N(int, MAXDIM);
    lbs = ALLOCA_N(int, MAXDIM);
    i = 0;
    while (TYPE(tmp) == T_ARRAY) {
        lbs[i] = 1;
        dim[i++] = RARRAY(tmp)->len;
        if (i == MAXDIM) {
            rb_raise(pl_ePLruby, "too many dimensions -- max %d", MAXDIM);
        }
        if (RARRAY(tmp)->len) {
            total *= RARRAY(tmp)->len;
        }
        tmp = RARRAY(tmp)->ptr[0];
    }
    ndim = i;
    ary = rb_funcall2(ary, rb_intern("flatten"), 0, 0);
    if (RARRAY(ary)->len != total) {
        elog(WARNING, "not a regular array");
    }
    values = (Datum *)palloc(RARRAY(ary)->len * sizeof(Datum));
    for (i = 0; i < RARRAY(ary)->len; ++i) {
        values[i] = plruby_to_datum(RARRAY(ary)->ptr[i], 
                                    &p->result_func,
                                    p->result_oid, p->result_elem,
                                    p->result_len);
    }
    PLRUBY_BEGIN(1);
    array = construct_md_array(values, ndim, dim, lbs,
                               p->result_elem,  p->result_len,
                               p->result_val,  p->result_align);
    PLRUBY_END;
    return PointerGetDatum(array);
}

#endif

static Datum
return_base_type(VALUE c, pl_proc_desc *prodesc)
{
    Datum retval;

#if PG_PL_VERSION >= 74
    if (prodesc->result_is_array) {
        retval = plruby_return_array(c, prodesc);
    }
    else
#endif
    {
        retval = plruby_to_datum(c, &prodesc->result_func,
                                 prodesc->result_oid, 
                                 prodesc->result_elem,
                                 prodesc->result_len);
    }
    return retval;
}

#if PG_PL_VERSION >= 73

static HeapTuple
pl_tuple_heap(VALUE c, VALUE tuple)
{
    HeapTuple retval;
    struct pl_tuple *tpl;
    Datum *dvalues;
    Oid typid;
    char *nulls;
    int i;

    GetTuple(tuple, tpl);
    if (tpl->pro->result_type == 'b' && TYPE(c) != T_ARRAY) {
        c = rb_Array(c);
    }
    if (TYPE(c) != T_ARRAY || !RARRAY(c)->ptr) {
        rb_raise(pl_ePLruby, "expected an Array");
    }
    if (tpl->att->tupdesc->natts != RARRAY(c)->len) {
        rb_raise(pl_ePLruby, "Invalid number of rows (%d expected %d)",
                 tpl->att->tupdesc->natts, RARRAY(c)->len);
    }
    dvalues = ALLOCA_N(Datum, RARRAY(c)->len);
    nulls = ALLOCA_N(char, RARRAY(c)->len);
    for (i = 0; i < RARRAY(c)->len; i++) {
        if (NIL_P(RARRAY(c)->ptr[i]) || 
            tpl->att->tupdesc->attrs[i]->attisdropped) {
            dvalues[i] = (Datum)0;
            nulls[i] = 'n';
        }
        else {
            nulls[i] = ' ';
            typid =  tpl->att->tupdesc->attrs[i]->atttypid;
#if PG_PL_VERSION >= 74
            if (tpl->att->tupdesc->attrs[i]->attndims != 0) {
                pl_proc_desc prodesc;
                FmgrInfo func;
                HeapTuple hp;
                Form_pg_type fpg;

                MEMZERO(&prodesc, pl_proc_desc, 1);
                PLRUBY_BEGIN(1);
                hp = SearchSysCacheTuple(RUBY_TYPOID, ObjectIdGetDatum(typid), 0, 0, 0);
                if (!HeapTupleIsValid(hp)) {
                    rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                             typid);
                }
                fpg = (Form_pg_type) GETSTRUCT(hp);
                typid = fpg->typelem;
                ReleaseSysCache(hp);
                hp = SearchSysCacheTuple(RUBY_TYPOID, ObjectIdGetDatum(typid), 0, 0, 0);
                if (!HeapTupleIsValid(hp)) {
                    rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                             typid);
                }
                fpg = (Form_pg_type) GETSTRUCT(hp);
                fmgr_info(fpg->typinput, &func);
                prodesc.result_func = func;
                prodesc.result_oid = typid;
                prodesc.result_elem = typid;
                prodesc.result_val = fpg->typbyval;
                prodesc.result_len = fpg->typlen;
                prodesc.result_align = fpg->typalign;
                ReleaseSysCache(hp);
                PLRUBY_END;
                dvalues[i] = plruby_return_array(RARRAY(c)->ptr[i], &prodesc);
            }
            else
#endif
            {
                dvalues[i] = plruby_to_datum(RARRAY(c)->ptr[i],
                                             &tpl->att->attinfuncs[i],
                                             typid,
                                             tpl->att->attelems[i],
                                             tpl->att->atttypmods[i]);
            }
        }
    }
    PLRUBY_BEGIN(1);
    retval = heap_formtuple(tpl->att->tupdesc, dvalues, nulls);
    PLRUBY_END;
    return retval;
}

static VALUE
pl_tuple_put(VALUE c, VALUE tuple)
{
    HeapTuple retval;
    MemoryContext oldcxt;
    struct pl_tuple *tpl;

    GetTuple(tuple, tpl);
    retval = pl_tuple_heap(c, tuple);
    PLRUBY_BEGIN(1);
    oldcxt = MemoryContextSwitchTo(tpl->cxt);
    if (!tpl->out) {
#if PG_PL_VERSION >= 74
        tpl->out = tuplestore_begin_heap(true, false, SortMem);
#else
        tpl->out = tuplestore_begin_heap(true, SortMem);
#endif
    }
    tuplestore_puttuple(tpl->out, retval);
    MemoryContextSwitchTo(oldcxt);
    PLRUBY_END;
    return Qnil;
}

static Datum
pl_tuple_datum(VALUE c, VALUE tuple)
{
    Datum retval;
    HeapTuple tmp;
    struct pl_tuple *tpl;

    GetTuple(tuple, tpl);
    tmp = pl_tuple_heap(c, tuple);
    PLRUBY_BEGIN(1);
    retval = TupleGetDatum(TupleDescGetSlot(tpl->att->tupdesc), tmp);
    PLRUBY_END;
    return retval;
}

struct pl_arg {
    ID id;
    VALUE ary;
};

static void
pl_arg_mark(struct pl_arg *args)
{
    rb_gc_mark(args->ary);
}

static VALUE
pl_func(VALUE arg)
{
    struct pl_arg *args;

    Data_Get_Struct(arg, struct pl_arg, args);
    return rb_funcall(pl_mPLtemp, args->id, 1, args->ary);
}

static VALUE
pl_string(VALUE arg)
{
    struct pl_arg *args;
    VALUE tmp[2], plan;

    Data_Get_Struct(arg, struct pl_arg, args);
    tmp[0] = args->ary;
    tmp[1] = rb_hash_new();
    rb_hash_aset(tmp[1], rb_str_new2("block"), INT2NUM(50));
    rb_hash_aset(tmp[1], rb_str_new2("output"), rb_str_new2("value"));
    plan = plruby_s_new(2, tmp, pl_cPLPlan);
    rb_funcall2(plan, rb_intern("each"), 0, 0);
    return Qnil;
}
 
#endif

static VALUE
pl_warn(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int level, indice;
    VALUE res;

    level = NOTICE;
    indice = 0;
    switch (argc) {
    case 2:
        indice  = 1;
        switch (level = NUM2INT(argv[0])) {
#ifdef DEBUG
        case DEBUG:
#endif
#ifdef DEBUG1
        case DEBUG1:
#endif
#ifdef DEBUG2
        case DEBUG2:
#endif
#ifdef DEBUG3
        case DEBUG3:
#endif
#ifdef DEBUG4
        case DEBUG4:
#endif
#ifdef DEBUG5
        case DEBUG5:
#endif
#ifdef NOTICE
        case NOTICE:
#endif
#ifdef LOG
        case LOG:
#endif
#ifdef NOIND
        case NOIND:
#endif
#ifdef WARNING
        case WARNING:
#endif
#ifdef WARN
        case WARN:
#endif
#ifdef ERROR
        case ERROR:
#endif
#ifdef FATAL
        case FATAL:
#endif
            break;
        default:
            rb_raise(pl_ePLruby, "invalid level %d", level);
        }
    case 1:
        res = argv[indice];
        if (NIL_P(res)) {
            return Qnil;
        }
        res = plruby_to_s(res);
        break;
    default:
        rb_raise(pl_ePLruby, "invalid syntax");
    }
    PLRUBY_BEGIN(1);
    elog(level, RSTRING(res)->ptr);
    PLRUBY_END;
    return Qnil;
}

static VALUE
pl_quote(obj, mes)
    VALUE obj, mes;
{    
    char *tmp, *cp1, *cp2;

    if (TYPE(mes) != T_STRING || !RSTRING(mes)->ptr) {
        rb_raise(pl_ePLruby, "quote: string expected");
    }
    tmp = ALLOCA_N(char, RSTRING(mes)->len * 2 + 1);
    cp1 = RSTRING(mes)->ptr;
    cp2 = tmp;
    while (*cp1) {
        if (*cp1 == '\'')
            *cp2++ = '\'';
        else {
            if (*cp1 == '\\')
                *cp2++ = '\\';
        }
        *cp2++ = *cp1++;
    }
    *cp2 = '\0';
    return rb_tainted_str_new2(tmp);
}

void
plruby_exec_output(VALUE option, int compose, int *result)
{
    if (TYPE(option) != T_STRING || RSTRING(option)->ptr == 0 || !result) {
        rb_raise(pl_ePLruby, "string expected for optional output");
    }
    if (strcmp(RSTRING(option)->ptr, "array") == 0) {
        *result = compose|RET_DESC_ARR;
    }
    else if (strcmp(RSTRING(option)->ptr, "hash") == 0) {
        *result = compose|RET_DESC;
    }
    else if (strcmp(RSTRING(option)->ptr, "value") == 0) {
        *result = RET_ARRAY;
    }
}

static VALUE
pl_SPI_exec(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int spi_rc, count, array;
    int i, comp, ntuples;
    struct portal_options po;
    VALUE a, b, c, result;
    HeapTuple *tuples;
    TupleDesc tupdesc = NULL;

    count = 0;
    array = comp = RET_HASH;
    if (argc && TYPE(argv[argc - 1]) == T_HASH) {
        MEMZERO(&po, struct portal_options, 1);
        rb_iterate(rb_each, argv[argc - 1], plruby_i_each, (VALUE)&po);
        comp = po.output;
        count = po.count;
        argc--;
    }
    switch (rb_scan_args(argc, argv, "12", &a, &b, &c)) {
    case 3:
        plruby_exec_output(c, RET_HASH, &comp);
        /* ... */
    case 2:
        if (!NIL_P(b)) {
            count = NUM2INT(b);
        }
    }
    if (TYPE(a) != T_STRING) {
        rb_raise(pl_ePLruby, "exec: first argument must be a string");
    }
#if PG_PL_VERSION >= 71
    array = comp;
#endif

    PLRUBY_BEGIN(1);
    spi_rc = SPI_exec(RSTRING(a)->ptr, count);
    PLRUBY_END;

    switch (spi_rc) {
    case SPI_OK_UTILITY:
        SPI_freetuptable(SPI_tuptable);
        return Qtrue;
    case SPI_OK_SELINTO:
    case SPI_OK_INSERT:
    case SPI_OK_DELETE:
    case SPI_OK_UPDATE:
        SPI_freetuptable(SPI_tuptable);
        return INT2NUM(SPI_processed);
    case SPI_OK_SELECT:
        break;
    case SPI_ERROR_ARGUMENT:
        rb_raise(pl_ePLruby, "SPI_exec() failed - SPI_ERROR_ARGUMENT");
    case SPI_ERROR_UNCONNECTED:
        rb_raise(pl_ePLruby, "SPI_exec() failed - SPI_ERROR_UNCONNECTED");
    case SPI_ERROR_COPY:
        rb_raise(pl_ePLruby, "SPI_exec() failed - SPI_ERROR_COPY");
    case SPI_ERROR_CURSOR:
        rb_raise(pl_ePLruby, "SPI_exec() failed - SPI_ERROR_CURSOR");
    case SPI_ERROR_TRANSACTION:
        rb_raise(pl_ePLruby, "SPI_exec() failed - SPI_ERROR_TRANSACTION");
    case SPI_ERROR_OPUNKNOWN:
        rb_raise(pl_ePLruby, "SPI_exec() failed - SPI_ERROR_OPUNKNOWN");
    default:
        rb_raise(pl_ePLruby, "SPI_exec() failed - unknown RC %d", spi_rc);
    }

    ntuples = SPI_processed;
    if (ntuples <= 0) {
        SPI_freetuptable(SPI_tuptable);
        if (rb_block_given_p() || count == 1)
            return Qfalse;
        else
            return rb_ary_new2(0);
    }
    tuples = SPI_tuptable->vals;
    tupdesc = SPI_tuptable->tupdesc;
    if (rb_block_given_p()) {
        if (count == 1) {
            if (!(array & RET_DESC)) {
                array |= RET_BASIC;
            }
            plruby_build_tuple(tuples[0], tupdesc, array);
        }
        else {
            for (i = 0; i < ntuples; i++) {
                rb_yield(plruby_build_tuple(tuples[i], tupdesc, array));
            }
        }
        result = Qtrue;
    }
    else {
        if (count == 1) {
            result = plruby_build_tuple(tuples[0], tupdesc, array);
        }
        else {
            result = rb_ary_new2(ntuples);
            for (i = 0; i < ntuples; i++) {
                rb_ary_push(result, plruby_build_tuple(tuples[i], tupdesc, array));
            }
        }
    }
    SPI_freetuptable(SPI_tuptable);
    return result;
}

static VALUE
pl_convert_arg(Datum value, Oid typoid, FmgrInfo *finfo, Oid typelem, 
               int attlen)
{
    VALUE result;
    char *outstr;

    if (typoid == BOOLOID) {
        return DatumGetBool(value)?Qtrue:Qfalse;
    }
#ifdef PLRUBY_ENABLE_CONVERSION
    {
        VALUE vid, klass;

        vid = INT2NUM(typoid);
        klass = rb_hash_aref(plruby_classes, vid);
        if (NIL_P(klass)) {
            klass = rb_hash_aref(plruby_conversions, vid);
            if (NIL_P(klass)) {
                st_insert(RHASH(plruby_classes)->tbl, vid, Qfalse);
            }
            else {
                klass = rb_const_get(rb_cObject, NUM2INT(klass));
                st_insert(RHASH(plruby_classes)->tbl, vid, klass);
            }
        }
        if (RTEST(klass)) {
            struct datum_value *dv;
            VALUE res;


            res = Data_Make_Struct(rb_cData, struct datum_value,
                                   pl_conv_mark, free, dv);
            dv->d = value;
            dv->typoid = typoid;
            dv->typlen = attlen;
            res = rb_funcall(klass, id_from_datum, 1, res);
            return res;
        }
    }
#endif
    PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
    outstr = DatumGetCString(FunctionCall3(finfo, value,
                                           ObjectIdGetDatum(typelem),
                                           Int32GetDatum(attlen)));
#else
    outstr = (*fmgr_faddr(finfo))(value, typelem, attlen);
#endif
    result = rb_tainted_str_new2(outstr);
    pfree(outstr);
    PLRUBY_END;
    return result;
}

#if PG_PL_VERSION >= 74

static VALUE
create_array(index, ndim, dim, p, prodesc, curr, typoid)
    int index, ndim, *dim, curr;
    Oid typoid;
    char **p;
    pl_proc_desc *prodesc;
{
    VALUE res, tmp;
    Datum itemvalue;
    int i;

    res = rb_ary_new2(dim[index]);
    for (i = 0; i < dim[index]; ++i) {
        if (index == ndim - 1) {
            itemvalue = fetch_att(*p, prodesc->arg_val[curr], 
                                  prodesc->arg_len[curr]);
            tmp = pl_convert_arg(itemvalue, typoid,
                                 &prodesc->arg_func[curr], (Datum)0, -1);
            *p = att_addlength(*p, prodesc->arg_len[curr], PointerGetDatum(*p));
            *p = (char *) att_align(*p, prodesc->arg_align[curr]);
            rb_ary_push(res, tmp); 
        }
        else {
            for (i = 0; i < dim[index]; ++i) {
                rb_ary_push(res, create_array(index + 1, ndim, dim, p, 
                                              prodesc, curr, typoid));
            }
        }
    }
    return res;
}

#endif

VALUE
plruby_build_tuple(HeapTuple tuple, TupleDesc tupdesc, int type_ret)
{
    int i;
    VALUE output, res = Qnil;
    Datum attr;
    bool isnull;
    char *attname;
    HeapTuple typeTup;
    Oid typoutput;
    Oid typelem;
    Form_pg_type fpgt;
    
    output = Qnil;
    if (type_ret & RET_ARRAY) {
        output = rb_ary_new();
    }
    else if (type_ret & RET_HASH) {
        output = rb_hash_new();
    }
    if (!tuple) {
        return output;
    }

    for (i = 0; i < tupdesc->natts; i++) {
        PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
        attname = NameStr(tupdesc->attrs[i]->attname);
#else
        attname = tupdesc->attrs[i]->attname.data;
#endif

        attr = heap_getattr(tuple, i + 1, tupdesc, &isnull);
        typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                      ObjectIdGetDatum(tupdesc->attrs[i]->atttypid),
                                      0, 0, 0);
        PLRUBY_END;

        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
                 attname, ObjectIdGetDatum(tupdesc->attrs[i]->atttypid));
        }

        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        typoutput = (Oid) (fpgt->typoutput);
        typelem = (Oid) (fpgt->typelem);
#if PG_PL_VERSION >= 71
        if (type_ret & RET_DESC) {
            Oid typeid;
            char *typname;
            int alen;

            typname = fpgt->typname.data;
            alen = tupdesc->attrs[i]->attlen;
            typeid = tupdesc->attrs[i]->atttypid;
            if (strcmp(typname, "text") == 0) {
                alen = -1;
            }
            else if (strcmp(typname, "bpchar") == 0 ||
                     strcmp(typname, "varchar") == 0) {
                if (tupdesc->attrs[i]->atttypmod == -1) {
                    alen = 0;
                }
                else {
                    alen = tupdesc->attrs[i]->atttypmod - 4;
                }
            }
            if ((type_ret & RET_DESC_ARR) == RET_DESC_ARR) {
                res = rb_ary_new();
                rb_ary_push(res, rb_tainted_str_new2(attname));
                rb_ary_push(res, Qnil);
                rb_ary_push(res, rb_tainted_str_new2(typname));
                rb_ary_push(res, INT2FIX(alen));
                rb_ary_push(res, INT2FIX(typeid));
            }
            else {
                res = rb_hash_new();
                rb_hash_aset(res, rb_tainted_str_new2("name"), rb_tainted_str_new2(attname));
                rb_hash_aset(res, rb_tainted_str_new2("type"), rb_tainted_str_new2(typname));
                rb_hash_aset(res, rb_tainted_str_new2("typeid"), INT2FIX(typeid));
                rb_hash_aset(res, rb_tainted_str_new2("len"), INT2FIX(alen));
            }
        }
        ReleaseSysCache(typeTup);
#endif
        if (!isnull && OidIsValid(typoutput)) {
            VALUE s;

            PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 74
            if (NameStr(fpgt->typname)[0] == '_') {
                ArrayType *array;
                int ndim, *dim;
                
                array = (ArrayType *)attr;
                ndim = ARR_NDIM(array);
                dim = ARR_DIMS(array);
                if (ArrayGetNItems(ndim, dim) == 0) {
                    s = rb_ary_new2(0);
                }
                else {
                    pl_proc_desc prodesc;
                    HeapTuple typeTuple;
                    Form_pg_type typeStruct;
                    char *p = ARR_DATA_PTR(array);

                    typeTuple = 
                        SearchSysCacheTuple(RUBY_TYPOID,
                                            ObjectIdGetDatum((Oid) fpgt->typelem), 0, 0, 0);
                    if (!HeapTupleIsValid(typeTuple)) {
                        rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                                 (Oid) fpgt->typelem);
                    }

                    typeStruct = (Form_pg_type) GETSTRUCT(typeTuple);

                    fmgr_info(typeStruct->typoutput, &(prodesc.arg_func[0]));
                    prodesc.arg_val[0] = typeStruct->typbyval;
                    prodesc.arg_len[0] = typeStruct->typlen;
                    prodesc.arg_align[0] = typeStruct->typalign;
                    ReleaseSysCache(typeTuple);
                    
                    s = create_array(0, ndim, dim, &p, &prodesc, 0, 
                                     ARR_ELEMTYPE(array));
                }
            }
            else
#endif
            {
                FmgrInfo finfo;
                
                fmgr_info(typoutput, &finfo);
                
                s = pl_convert_arg(attr, tupdesc->attrs[i]->atttypid,
                                   &finfo, typelem,tupdesc->attrs[i]->attlen);
            }
            PLRUBY_END;

#if PG_PL_VERSION >= 71
            if (type_ret & RET_DESC) {
                if (TYPE(res) == T_ARRAY) {
                    RARRAY(res)->ptr[1] = s;
                }
                else {
                    rb_hash_aset(res, rb_tainted_str_new2("value"), s);
                }
                if (TYPE(output) == T_ARRAY) {
                    rb_ary_push(output, res);
                }
                else {
                    rb_yield(res);
                }
            }
            else 
#endif
            {
                if (type_ret & RET_BASIC) {
                    rb_yield(rb_assoc_new(rb_tainted_str_new2(attname), s));
                }
                else {
                    switch (TYPE(output)) {
                    case T_HASH:
                        rb_hash_aset(output, rb_tainted_str_new2(attname), s);
                        break;
                    case T_ARRAY:
                        rb_ary_push(output, s);
                        break;
                    }
                }
            }
        } 
        else {
            if (isnull) {
#if PG_PL_VERSION >= 71
                if (type_ret & RET_DESC) {
                    if (TYPE(res) == T_HASH) {
                        rb_hash_aset(res, rb_tainted_str_new2("value"), Qnil);
                    }                   
                    if (TYPE(output) == T_ARRAY) {
                        rb_ary_push(output, res);
                    }
                    else {
                        rb_yield(res);
                    }
                }
                else 
#endif
                {
                    if (type_ret & RET_BASIC) {
                        rb_yield(rb_assoc_new(rb_tainted_str_new2(attname), Qnil));
                    }
                    else {
                        switch (TYPE(output)) {
                        case T_HASH:
                            rb_hash_aset(output, rb_tainted_str_new2(attname), Qnil);
                            break;
                        case T_ARRAY:
                            rb_ary_push(output, Qnil);
                            break;
                        }
                    }    
                }
            }
        }
    }
    return output;
}

VALUE
plruby_create_args(struct pl_thread_st *plth, pl_proc_desc *prodesc)
{
    VALUE ary;
    int i;
#ifdef NEW_STYLE_FUNCTION
    PG_FUNCTION_ARGS;
#else
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
    
#ifdef NEW_STYLE_FUNCTION
    fcinfo = plth->fcinfo;
#else
    proinfo = plth->proinfo;
    proargs = plth->proargs;
    isNull = plth->isNull;
#endif

#if PG_PL_VERSION >= 73
    {
        VALUE res;
        struct pl_tuple *tpl;

        res = rb_thread_local_aref(rb_thread_current(), id_thr);
        if (NIL_P(res)) {
            res = Data_Make_Struct(rb_cData, struct pl_tuple, pl_thr_mark, free, tpl);
        } 
        GetTuple(res, tpl);
        tpl->fcinfo = fcinfo;
        tpl->pro = prodesc;
        rb_thread_local_aset(rb_thread_current(), id_thr, res);
    }
#endif

    ary = rb_ary_new2(prodesc->nargs);
    for (i = 0; i < prodesc->nargs; i++) {
        if (prodesc->arg_is_rel[i]) {
#ifdef NEW_STYLE_FUNCTION
            TupleTableSlot *slot = (TupleTableSlot *) fcinfo->arg[i];
#else
            TupleTableSlot *slot = (TupleTableSlot *) proargs->data[i];
#endif
            rb_ary_push(ary, plruby_build_tuple(slot->val,
                                                slot->ttc_tupleDescriptor, 
                                                RET_HASH));
        } 
#ifdef NEW_STYLE_FUNCTION
        else if (fcinfo->argnull[i]) {
            rb_ary_push(ary, Qnil);
        }
#endif
#if PG_PL_VERSION >= 74
        else if (prodesc->arg_is_array[i]) {
            ArrayType *array;
            int ndim, *dim;
            char *p;

            array = (ArrayType *)fcinfo->arg[i];
            ndim = ARR_NDIM(array);
            dim = ARR_DIMS(array);
            if (ArrayGetNItems(ndim, dim) == 0) {
                rb_ary_push(ary, rb_ary_new2(0));
            }
            else {
                p = ARR_DATA_PTR(array);
                rb_ary_push(ary, create_array(0, ndim, dim, &p, prodesc, i,
                                              ARR_ELEMTYPE(array)));
            }
        }
#endif
        else {
            VALUE res;

#ifdef NEW_STYLE_FUNCTION           
            res = pl_convert_arg(fcinfo->arg[i],
                                 prodesc->arg_type[i],
                                 &prodesc->arg_func[i],
                                 prodesc->arg_elem[i],
                                 prodesc->arg_len[i]);
#else
            res = pl_convert_arg((Datum)proargs->data[i], 
                                 prodesc->arg_type[i],
                                 &prodesc->arg_func[i],
                                 prodesc->arg_elem[i],
                                 prodesc->arg_len[i]);
#endif
            rb_ary_push(ary, res);
        }
    }
    return ary;
}

Datum
plruby_return_value(struct pl_thread_st *plth, pl_proc_desc *prodesc, 
                    VALUE value_proname, VALUE ary)
{
    VALUE c;
    int expr_multiple;
#ifdef NEW_STYLE_FUNCTION
    PG_FUNCTION_ARGS;
#else
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
    
#ifdef NEW_STYLE_FUNCTION
    fcinfo = plth->fcinfo;
#else
    proinfo = plth->proinfo;
    proargs = plth->proargs;
    isNull = plth->isNull;
#endif

    expr_multiple = 0;

#if PG_PL_VERSION >= 73
    if (prodesc->result_type && prodesc->result_type != 'x' &&
        prodesc->result_type != 'y') {
        ReturnSetInfo *rsi;

        if (!fcinfo || !fcinfo->resultinfo) {
            rb_raise(pl_ePLruby, "no description given");
        }
        rsi = (ReturnSetInfo *)fcinfo->resultinfo;
        if ((rsi->allowedModes & SFRM_Materialize) && rsi->expectedDesc) {
            VALUE tuple, res, arg;
            struct pl_arg *args;
            struct pl_tuple *tpl;
            VALUE (*pl_call)(VALUE);

            tuple = pl_tuple_s_new(fcinfo, prodesc);
            arg = Data_Make_Struct(rb_cObject, struct pl_arg, pl_arg_mark, free, args);
            args->id = rb_intern(RSTRING(value_proname)->ptr);
            args->ary = ary;
            pl_call = pl_func;
            while (1) {
                res = rb_iterate(pl_call, arg, pl_tuple_put, tuple);
                Data_Get_Struct(tuple, struct pl_tuple, tpl);
                if (NIL_P(res) && !tpl->out) {
                    MemoryContext oldcxt;
                    
                    PLRUBY_BEGIN(1);
                    oldcxt = MemoryContextSwitchTo(tpl->cxt);
#if PG_PL_VERSION >= 74
                    tpl->out = tuplestore_begin_heap(true, false, SortMem);
#else
                    tpl->out = tuplestore_begin_heap(true, SortMem);
#endif
                    MemoryContextSwitchTo(oldcxt);
                    PLRUBY_END;
                }
                if (tpl->out) {
                    MemoryContext oldcxt;
                    
                    PLRUBY_BEGIN(1);
                    oldcxt = MemoryContextSwitchTo(tpl->cxt);
                    tuplestore_donestoring(tpl->out);
                    MemoryContextSwitchTo(oldcxt);
                    PLRUBY_END;
                    ((ReturnSetInfo *)fcinfo->resultinfo)->setResult = tpl->out;
                    ((ReturnSetInfo *)fcinfo->resultinfo)->returnMode = SFRM_Materialize;
                    break;
                }
                if (NIL_P(res)) {
                    break;
                }
                if (TYPE(res) != T_STRING || RSTRING(res)->ptr == 0) {
                    rb_raise(pl_ePLruby, "invalid return type for a SET");
                }
                args->ary = res;
                pl_call = pl_string;
            }
            c = Qnil;
        }
        else if (IsA(rsi, ReturnSetInfo)) {
            expr_multiple = 1;
            c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING(value_proname)->ptr),
                           1, ary);
        }
        else {
            rb_raise(pl_ePLruby, "context don't accept set");
        }
    }
    else
#endif
    {
        c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING(value_proname)->ptr),
                       1, ary);
    }

    PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
    {
        MemoryContext oldcxt;

        oldcxt = MemoryContextSwitchTo(plruby_spi_context);
#endif
        if (SPI_finish() != SPI_OK_FINISH) {
            rb_raise(pl_ePLruby, "SPI_finish() failed");
        }
#if PG_PL_VERSION >= 73
        MemoryContextSwitchTo(oldcxt);
    }
#endif
    PLRUBY_END;

    if (c == Qnil) {
#ifdef NEW_STYLE_FUNCTION
#if PG_PL_VERSION >= 73
        if (expr_multiple) {
            pl_context_remove();
            fcinfo->context = NULL;
            ((ReturnSetInfo *)fcinfo->resultinfo)->isDone = ExprEndResult;
        }
#endif
        PG_RETURN_NULL();
#else
        *isNull = true;
        return (Datum)0;
#endif
    }
#if PG_PL_VERSION >= 73
    if (fcinfo->resultinfo) {
        if (fcinfo->flinfo->fn_retset) {
            ((ReturnSetInfo *)fcinfo->resultinfo)->isDone = ExprMultipleResult;
            return return_base_type(c, prodesc);
        }
        if (!prodesc->result_type) {
            return return_base_type(c, prodesc);
        }
        return pl_tuple_datum(c, pl_tuple_s_new(fcinfo, prodesc));
    }
    if (prodesc->result_type == 'x') {
        VALUE res;
        Datum retval;

        res = rb_funcall2(c, rb_intern("portal_name"), 0, 0);
        res = plruby_to_s(res);
        PLRUBY_BEGIN(1);
        retval = DFC1(textin, CStringGetDatum(RSTRING(res)->ptr));
        PLRUBY_END;
        return retval;
    }
        
#endif
    return return_base_type(c, prodesc);
}

extern void Init_plruby_plan();

void Init_plruby_pl()
{
    VALUE pl_sPLtemp, pl_eCatch;

    pl_mPL = rb_define_module("PL");
    rb_const_set(rb_cObject, rb_intern("PLruby"), pl_mPL);
    rb_define_const(pl_mPL, "OK", INT2FIX(TG_OK));
    rb_define_const(pl_mPL, "SKIP", INT2FIX(TG_SKIP));
    rb_define_const(pl_mPL, "BEFORE", INT2FIX(TG_BEFORE)); 
    rb_define_const(pl_mPL, "AFTER", INT2FIX(TG_AFTER)); 
    rb_define_const(pl_mPL, "ROW", INT2FIX(TG_ROW)); 
    rb_define_const(pl_mPL, "STATEMENT", INT2FIX(TG_STATEMENT)); 
    rb_define_const(pl_mPL, "INSERT", INT2FIX(TG_INSERT));
    rb_define_const(pl_mPL, "DELETE", INT2FIX(TG_DELETE)); 
    rb_define_const(pl_mPL, "UPDATE", INT2FIX(TG_UPDATE));
    rb_define_const(pl_mPL, "UNKNOWN", INT2FIX(TG_UNKNOWN));
    rb_define_global_function("warn", pl_warn, -1);
    rb_define_module_function(pl_mPL, "quote", pl_quote, 1);
    rb_define_module_function(pl_mPL, "spi_exec", pl_SPI_exec, -1);
    rb_define_module_function(pl_mPL, "exec", pl_SPI_exec, -1);
    rb_define_module_function(pl_mPL, "column_name", pl_column_name, 1);
    rb_define_module_function(pl_mPL, "column_type", pl_column_type, 1);
#if PG_PL_VERSION >= 73
    rb_define_module_function(pl_mPL, "result_name", pl_query_name, 0);
    rb_define_module_function(pl_mPL, "result_type", pl_query_type, 0);
    rb_define_module_function(pl_mPL, "result_size", pl_query_lgth, 0);
    rb_define_module_function(pl_mPL, "result_description", pl_query_description, 0);
    rb_define_module_function(pl_mPL, "args_type", pl_args_type, 0);
    rb_define_module_function(pl_mPL, "context", pl_context_get, 0);
    rb_define_module_function(pl_mPL, "context=", pl_context_set, 1);
#endif
    pl_ePLruby = rb_define_class_under(pl_mPL, "Error", rb_eStandardError);
    pl_eCatch = rb_define_class_under(pl_mPL, "Catch", rb_eStandardError);
    pl_mPLtemp = rb_define_module("PLtemp");
    pl_sPLtemp = rb_singleton_class(pl_mPLtemp);
#if PG_PL_VERSION >= 73
    PLcontext = rb_hash_new();
    rb_global_variable(&PLcontext);
#endif
    if (MAIN_SAFE_LEVEL >= 3) {
        rb_obj_taint(pl_mPLtemp);
        rb_obj_taint(pl_sPLtemp);
#if PG_PL_VERSION >= 73
        rb_obj_taint(PLcontext);
#endif
    }
    id_thr = rb_intern("__functype__");
#ifdef PLRUBY_HASH_DELETE
    id_delete = rb_intern("delete");
#endif
#ifdef PLRUBY_ENABLE_CONVERSION
    id_from_datum = rb_intern("from_datum");
    id_to_datum = rb_intern("to_datum");
    plruby_classes = rb_hash_new();
    rb_global_variable(&plruby_classes);
    plruby_conversions = rb_hash_new();
    rb_global_variable(&plruby_conversions);
    pl_init_conversions();
#endif
    Init_plruby_plan();
    pl_cPLPlan = rb_const_get(pl_mPL, rb_intern("Plan"));
}
