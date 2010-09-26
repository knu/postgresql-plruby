#include "plruby.h"

static VALUE pl_ePLruby, pl_mPLtemp;
static VALUE pl_mPL, pl_cPLPlan, pl_eCatch;

static ID id_thr;

static VALUE pl_SPI_exec _((int, VALUE *, VALUE));

#ifndef HAVE_RB_HASH_DELETE
static ID id_delete;

#define rb_hash_delete(a, b) rb_funcall((a), id_delete, 1, (b))

#endif

static char *names = 
"SELECT a.attname FROM pg_class c, pg_attribute a, pg_namespace n"
" WHERE c.relname = '%s' AND a.attnum > 0 AND NOT a.attisdropped AND a.attrelid = c.oid"
" AND c.relnamespace = n.oid AND n.nspname = '%s'"
" ORDER BY a.attnum";

static VALUE
pl_column_name(VALUE obj, VALUE table)
{
    VALUE *query, res;
    char *tmp;
    char *nsp, *tbl, *c;

    if (TYPE(table) != T_STRING || !RSTRING_PTR(table)) {
        rb_raise(pl_ePLruby, "expected a String");
    }
    tmp = ALLOCA_N(char, strlen(names) + RSTRING_LEN(table) + 1);
    nsp = ALLOCA_N(char, RSTRING_LEN(table) + 1);
    tbl = ALLOCA_N(char, RSTRING_LEN(table) + 1);
    strcpy(nsp, RSTRING_PTR(table));
    if ((c = strchr(nsp, '.')) != NULL) {
	*c = 0;
	strcpy(tbl, c + 1);
    }
    else {
	strcpy(tbl, nsp);
	strcpy(nsp, "public");
    }
    sprintf(tmp, names, tbl, nsp);
    query = ALLOCA_N(VALUE, 3);
    MEMZERO(query, VALUE, 3);
    query[0] = rb_str_new2(tmp);
    query[1] = Qnil;
    query[2] = rb_str_new2("value");
    res = pl_SPI_exec(3, query, pl_mPL);
    rb_funcall2(res, rb_intern("flatten!"), 0, 0);
    return res;
}

static char *types = 
"SELECT t.typname FROM pg_class c, pg_attribute a, pg_type t, pg_namespace n"
" WHERE c.relname = '%s' and a.attnum > 0 AND NOT a.attisdropped"
" AND a.attrelid = c.oid and a.atttypid = t.oid"
" AND c.relnamespace = n.oid AND n.nspname = '%s'"
" ORDER BY a.attnum";

static VALUE
pl_column_type(VALUE obj, VALUE table)
{
    VALUE *query, res;
    char *tmp;
    char *nsp, *tbl, *c;

    if (TYPE(table) != T_STRING || !RSTRING_PTR(table)) {
        rb_raise(pl_ePLruby, "expected a String");
    }
    tmp = ALLOCA_N(char, strlen(types) + RSTRING_LEN(table) + 1);
    nsp = ALLOCA_N(char, RSTRING_LEN(table) + 1);
    tbl = ALLOCA_N(char, RSTRING_LEN(table) + 1);
    strcpy(nsp, RSTRING_PTR(table));
    if ((c = strchr(nsp, '.')) != NULL) {
	*c = 0;
	strcpy(tbl, c + 1);
    }
    else {
	strcpy(tbl, nsp);
	strcpy(nsp, "public");
    }
    sprintf(tmp, types, tbl, nsp);
    query = ALLOCA_N(VALUE, 3);
    MEMZERO(query, VALUE, 3);
    query[0] = rb_str_new2(tmp);
    query[1] = Qnil;
    query[2] = rb_str_new2("value");
    res = pl_SPI_exec(3, query, pl_mPL);
    rb_funcall2(res, rb_intern("flatten!"), 0, 0);
    return res;
}

struct pl_tuple {
    MemoryContext cxt;
    AttInMetadata *att;
    pl_proc_desc *pro;
    TupleDesc dsc;
    Tuplestorestate *out;
    PG_FUNCTION_ARGS;
};

#if PG_PL_VERSION >= 75
#define SortMem work_mem
#endif

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
        if (tpl->dsc->attrs[i]->attisdropped) {
            attname = "";
        }
        else {
            attname = NameStr(tpl->dsc->attrs[i]->attname);
        }
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
        typeTup = SearchSysCache(TYPEOID, OidGD(tpl->pro->result_oid),0,0,0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for result type %ld failed",
                     tpl->pro->result_oid);
        }
        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        res = rb_tainted_str_new2(NameStr(fpgt->typname));
        ReleaseSysCache(typeTup);
        return res;
    }
    res = rb_ary_new2(tpl->dsc->natts);
    for (i = 0; i < tpl->dsc->natts; i++) {
        if (tpl->dsc->attrs[i]->attisdropped)
            continue;
        PLRUBY_BEGIN(1);
        attname = NameStr(tpl->dsc->attrs[i]->attname);
        typeTup = SearchSysCache(TYPEOID, OidGD(tpl->dsc->attrs[i]->atttypid),
                                 0, 0, 0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
                     attname, OidGD(tpl->dsc->attrs[i]->atttypid));
        }
        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        rb_ary_push(res, rb_tainted_str_new2(NameStr(fpgt->typname)));
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
        RARRAY_LEN(name) != RARRAY_LEN(types)) {
        rb_raise(pl_ePLruby, "unknown error");
    }
    res = rb_tainted_str_new2("");
    for (i = 0; i < RARRAY_LEN(name); ++i) {
        rb_str_concat(res, RARRAY_PTR(name)[i]);
        rb_str_concat(res, tt_blc);
        rb_str_concat(res, RARRAY_PTR(types)[i]);
        if (i != (RARRAY_LEN(name) - 1)) {
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
        typeTup = SearchSysCache(TYPEOID, OidGD(tpl->pro->arg_type[i]),0,0,0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for type %ld failed",
                     OidGD(tpl->pro->arg_type[i]));
        }
        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        rb_ary_push(res, rb_tainted_str_new2(NameStr(fpgt->typname)));
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
    tpl->att = TupleDescGetAttInMetadata(tpl->dsc);
    tpl->pro = prodesc;
    rb_thread_local_aset(rb_thread_current(), id_thr, res);
    return res;
}

#ifdef PLRUBY_ENABLE_CONVERSION

static ID id_from_datum;
static ID id_to_datum;

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
    VALUE tmp;

    tmp = rb_attr_get(obj, rb_intern("plruby_tuple"));
    if (TYPE(tmp) == T_DATA) {
	return (Datum)DATA_PTR(tmp);
    }
    if (typoid == BOOLOID) {
        return BoolGD(RTEST(obj));
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
    PLRUBY_BEGIN_PROTECT(1);
    d = FunctionCall3(finfo, PointerGD(RSTRING_PTR(obj)),
                      OidGD(typelem), IntGD(typlen));
    PLRUBY_END_PROTECT;
    return d;
}

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
    MEMZERO(dim, int, MAXDIM);
    lbs = ALLOCA_N(int, MAXDIM);
    MEMZERO(lbs, int, MAXDIM);
    i = 0;
    while (TYPE(tmp) == T_ARRAY) {
        lbs[i] = 1;
        dim[i++] = RARRAY_LEN(tmp);
        if (i == MAXDIM) {
            rb_raise(pl_ePLruby, "too many dimensions -- max %d", MAXDIM);
        }
        if (RARRAY_LEN(tmp)) {
            total *= RARRAY_LEN(tmp);
        }
        tmp = RARRAY_PTR(tmp)[0];
    }
    ndim = i;
#if PG_PL_VERSION < 74
    if (ndim != 1) {
        rb_raise(rb_eNotImpError, "multi-dimensional array only for >= 7.4");
    }
#endif
    ary = rb_funcall2(ary, rb_intern("flatten"), 0, 0);
    if (RARRAY_LEN(ary) != total) {
#ifdef WARNING
        elog(WARNING, "not a regular array");
#else
        elog(NOTICE, "not a regular array");
#endif
    }
    values = (Datum *)palloc(RARRAY_LEN(ary) * sizeof(Datum));
    for (i = 0; i < RARRAY_LEN(ary); ++i) {
        values[i] = plruby_to_datum(RARRAY_PTR(ary)[i], 
                                    &p->result_func,
                                    p->result_oid, p->result_elem,
                                    -1);
    }
    PLRUBY_BEGIN_PROTECT(1);
#if PG_PL_VERSION >= 74
#if PG_PL_VERSION >= 82
    array = construct_md_array(values, NULL, ndim, dim, lbs,
                               p->result_elem, p->result_len,
                               p->result_val, p->result_align);
#else
    array = construct_md_array(values, ndim, dim, lbs,
                               p->result_elem, p->result_len,
                               p->result_val, p->result_align);
#endif
#else
    array = construct_array(values, dim[0], p->result_elem, p->result_len,
                            p->result_val, p->result_align);
#endif
    PLRUBY_END_PROTECT;
    return PointerGD(array);
}

static Datum
return_base_type(VALUE c, pl_proc_desc *prodesc)
{
    Datum retval;

    if (prodesc->result_is_array) {
        retval = plruby_return_array(c, prodesc);
    }
    else {
        retval = plruby_to_datum(c, &prodesc->result_func,
                                 prodesc->result_oid, 
                                 prodesc->result_elem,
                                 -1);
    }
    return retval;
}

struct each_st {
    VALUE res;
    TupleDesc tup;
};

static VALUE
pl_each(VALUE obj, struct each_st *st)
{
    VALUE key, value;
    char *column;
    int attn;

    key = rb_ary_entry(obj, 0);
    value = rb_ary_entry(obj, 1);
    key = plruby_to_s(key);
    column = RSTRING_PTR(key);
    attn = SPI_fnumber(st->tup, column);
    if (attn <= 0 || st->tup->attrs[attn - 1]->attisdropped) {
	rb_raise(pl_ePLruby, "Invalid column name '%s'", column);
    }
    attn -= 1;
    if (TYPE(st->res) != T_ARRAY || !RARRAY_PTR(st->res)) {
        rb_raise(pl_ePLruby, "expected an Array");
    }
    if (attn >= RARRAY_LEN(st->res)) {
	rb_raise(pl_ePLruby, "Invalid column position '%d'", attn);
    }
    RARRAY_PTR(st->res)[attn] = value;
    return Qnil;
}

static HeapTuple
pl_tuple_heap(VALUE c, VALUE tuple)
{
    HeapTuple retval;
    struct pl_tuple *tpl;
    TupleDesc tupdesc = 0;
    Datum *dvalues;
    Oid typid;
    char *nulls;
    int i;

    
    GetTuple(tuple, tpl);
    if (tpl->att) {
	tupdesc = tpl->att->tupdesc;
    }
    if (!tupdesc) {
	rb_raise(pl_ePLruby, "Invalid descriptor");
    }
    if (TYPE(c) != T_ARRAY) {
	if (NIL_P(c) || (TYPE(c) == T_STRING && !RSTRING_LEN(c))) {
	    c = rb_ary_new2(1);
	    rb_ary_push(c, rb_str_new2(""));
	}
	else {
	    c = rb_Array(c);
	}
    }
    if (TYPE(c) != T_ARRAY || !RARRAY_PTR(c)) {
        rb_raise(pl_ePLruby, "expected an Array");
    }
    if (tupdesc->natts != RARRAY_LEN(c)) {
        rb_raise(pl_ePLruby, "Invalid number of rows (%d expected %d)",
                 RARRAY_LEN(c), tupdesc->natts);
    }
    dvalues = ALLOCA_N(Datum, RARRAY_LEN(c));
    MEMZERO(dvalues, Datum, RARRAY_LEN(c));
    nulls = ALLOCA_N(char, RARRAY_LEN(c));
    MEMZERO(nulls, char, RARRAY_LEN(c));
    for (i = 0; i < RARRAY_LEN(c); i++) {
        if (NIL_P(RARRAY_PTR(c)[i]) || 
            tupdesc->attrs[i]->attisdropped) {
            dvalues[i] = (Datum)0;
            nulls[i] = 'n';
        }
        else {
            nulls[i] = ' ';
            typid =  tupdesc->attrs[i]->atttypid;
            if (tupdesc->attrs[i]->attndims != 0 ||
		tpl->att->attinfuncs[i].fn_addr == (PGFunction)array_in) {
                pl_proc_desc prodesc;
                FmgrInfo func;
                HeapTuple hp;
                Form_pg_type fpg;

                MEMZERO(&prodesc, pl_proc_desc, 1);
                PLRUBY_BEGIN(1);
                hp = SearchSysCache(TYPEOID, OidGD(typid), 0, 0, 0);
                if (!HeapTupleIsValid(hp)) {
                    rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                             typid);
                }
                fpg = (Form_pg_type) GETSTRUCT(hp);
#if PG_PL_VERSION >= 75
                typid = getTypeIOParam(hp);
#else
                typid = fpg->typelem;
#endif
                ReleaseSysCache(hp);
                hp = SearchSysCache(TYPEOID, OidGD(typid), 0, 0, 0);
                PLRUBY_END;
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
                dvalues[i] = plruby_return_array(RARRAY_PTR(c)[i], &prodesc);
            }
            else  {
#if PG_PL_VERSION >= 75
		dvalues[i] = plruby_to_datum(RARRAY_PTR(c)[i],
					     &tpl->att->attinfuncs[i],
					     typid,
					     tpl->att->attioparams[i],
					     tpl->att->atttypmods[i]);
#else
                dvalues[i] = plruby_to_datum(RARRAY_PTR(c)[i],
                                             &tpl->att->attinfuncs[i],
                                             typid,
                                             tpl->att->attelems[i],
                                             tpl->att->atttypmods[i]);
#endif
            }
        }
    }
    PLRUBY_BEGIN_PROTECT(1);
    retval = heap_formtuple(tupdesc, dvalues, nulls);
    PLRUBY_END_PROTECT;
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
    PLRUBY_BEGIN_PROTECT(1);
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
    PLRUBY_END_PROTECT;
    return Qnil;
}

static VALUE
pl_ary_collect(VALUE c, VALUE ary)
{
    PLRUBY_BEGIN_PROTECT(1);
    rb_ary_push(ary,c);
    PLRUBY_END_PROTECT;
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
    PLRUBY_BEGIN_PROTECT(1);
    retval = TupleGD(TupleDescGetSlot(tpl->att->tupdesc), tmp);
    PLRUBY_END_PROTECT;
    return retval;
}

struct pl_arg {
    ID id;
    int named;
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
#if HAVE_RB_BLOCK_CALL
    return Qtrue;
#else
    struct pl_arg *args;

    Data_Get_Struct(arg, struct pl_arg, args);
    if (args->named) {
        return rb_funcall2(pl_mPLtemp, args->id, RARRAY_LEN(args->ary),
                          RARRAY_PTR(args->ary));
    }
    return rb_funcall(pl_mPLtemp, args->id, 1, args->ary);
#endif
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
#if HAVE_RB_BLOCK_CALL
    return plan;
#else
    rb_funcall2(plan, rb_intern("each"), 0, 0);
    return Qnil;
#endif
}
 
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
#if !defined(DEBUG) || LOG != DEBUG
        case LOG:
#endif
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
    PLRUBY_BEGIN_PROTECT(1);
    elog(level, RSTRING_PTR(res));
    PLRUBY_END_PROTECT;
    return Qnil;
}

static VALUE
pl_quote(obj, mes)
    VALUE obj, mes;
{    
    char *tmp, *cp1, *cp2;

    if (TYPE(mes) != T_STRING || !RSTRING_PTR(mes)) {
        rb_raise(pl_ePLruby, "quote: string expected");
    }
    tmp = ALLOCA_N(char, RSTRING_LEN(mes) * 2 + 1);
    cp1 = RSTRING_PTR(mes);
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
    if (TYPE(option) != T_STRING || RSTRING_PTR(option) == 0 || !result) {
        rb_raise(pl_ePLruby, "string expected for optional output");
    }
    if (strcmp(RSTRING_PTR(option), "array") == 0) {
        *result = compose|RET_DESC_ARR;
    }
    else if (strcmp(RSTRING_PTR(option), "hash") == 0) {
        *result = compose|RET_DESC;
    }
    else if (strcmp(RSTRING_PTR(option), "value") == 0) {
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
    array = comp;
    PLRUBY_BEGIN_PROTECT(1);
    spi_rc = SPI_exec(RSTRING_PTR(a), count);
    PLRUBY_END_PROTECT;

    switch (spi_rc) {
    case SPI_OK_UTILITY:
	if (SPI_tuptable == NULL) {
	    SPI_freetuptable(SPI_tuptable);
	    return Qtrue;
	}
	break;
    case SPI_OK_SELINTO:
    case SPI_OK_INSERT:
    case SPI_OK_DELETE:
    case SPI_OK_UPDATE:
        SPI_freetuptable(SPI_tuptable);
        return INT2NUM(SPI_processed);
    case SPI_OK_SELECT:
#ifdef SPI_OK_INSERT_RETURNING
    case SPI_OK_INSERT_RETURNING:
    case SPI_OK_DELETE_RETURNING:
    case SPI_OK_UPDATE_RETURNING:
#endif
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
                st_insert(RHASH_TBL(plruby_classes), vid, Qfalse);
            }
            else {
                klass = rb_const_get(rb_cObject, NUM2INT(klass));
                st_insert(RHASH_TBL(plruby_classes), vid, klass);
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
    PLRUBY_BEGIN_PROTECT(1);
    outstr = DatumGetCString(FunctionCall3(finfo, value, OidGD(typelem),
                                           IntGD(attlen)));
    result = rb_tainted_str_new2(outstr);
    pfree(outstr);
    PLRUBY_END_PROTECT;
    return result;
}

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
#ifdef att_addlength_pointer
	    *p = att_addlength_pointer(*p, prodesc->arg_len[curr], PointerGD(*p));
	    *p = (char *) att_align_nominal(*p, prodesc->arg_align[curr]);
#else
            *p = att_addlength(*p, prodesc->arg_len[curr], PointerGD(*p));
            *p = (char *) att_align(*p, prodesc->arg_align[curr]);
#endif
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
        if (tupdesc->attrs[i]->attisdropped)
            continue;
        PLRUBY_BEGIN(1);
        attname = NameStr(tupdesc->attrs[i]->attname);
        attr = heap_getattr(tuple, i + 1, tupdesc, &isnull);
        typeTup = SearchSysCache(TYPEOID, OidGD(tupdesc->attrs[i]->atttypid),
                                 0, 0, 0);
        PLRUBY_END;

        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
                     attname, OidGD(tupdesc->attrs[i]->atttypid));
        }

        fpgt = (Form_pg_type) GETSTRUCT(typeTup);
        typoutput = (Oid) (fpgt->typoutput);
#if PG_PL_VERSION >= 75
        typelem = getTypeIOParam(typeTup);
#else
        typelem = (Oid) (fpgt->typelem);
#endif
        if (type_ret & RET_DESC) {
            Oid typeid;
            char *typname;
            int alen;

            typname = NameStr(fpgt->typname);
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
        if (!isnull && OidIsValid(typoutput)) {
            VALUE s;

            PLRUBY_BEGIN_PROTECT(1);
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
                    Oid elemtyp;
                    char *p = ARR_DATA_PTR(array);

                    typeTuple = 
                        SearchSysCache(TYPEOID, OidGD(typelem), 0, 0, 0);
                    if (!HeapTupleIsValid(typeTuple)) {
                        elog(ERROR, "cache lookup failed for type %u",
                             typelem);
                    }

                    typeStruct = (Form_pg_type) GETSTRUCT(typeTuple);

                    fmgr_info(typeStruct->typoutput, &(prodesc.arg_func[0]));
                    prodesc.arg_val[0] = typeStruct->typbyval;
                    prodesc.arg_len[0] = typeStruct->typlen;
                    prodesc.arg_align[0] = typeStruct->typalign;
                    elemtyp = ARR_ELEMTYPE(array);
                    ReleaseSysCache(typeTuple);
                    s = create_array(0, ndim, dim, &p, &prodesc, 0, elemtyp); 
                }
            }
            else {
                FmgrInfo finfo;
                
                fmgr_info(typoutput, &finfo);
                
                s = pl_convert_arg(attr, tupdesc->attrs[i]->atttypid,
                                   &finfo, typelem,tupdesc->attrs[i]->attlen);
            }
            PLRUBY_END_PROTECT;

            if (type_ret & RET_DESC) {
                if (TYPE(res) == T_ARRAY) {
                    RARRAY_PTR(res)[1] = s;
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
            else {
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
                else {
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
    PG_FUNCTION_ARGS;
    
    fcinfo = plth->fcinfo;
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

    ary = rb_ary_new2(prodesc->nargs);
    for (i = 0; i < prodesc->nargs; i++) {
        if (fcinfo->argnull[i]) {
            rb_ary_push(ary, Qnil);
        }
        else if (prodesc->arg_is_rel[i]) {
            VALUE tmp;

#if PG_PL_VERSION >= 75
            HeapTupleHeader td;
            Oid tupType;
            int32 tupTypmod;
            TupleDesc tupdesc;
            HeapTupleData tmptup;

            td = DatumGetHeapTupleHeader(fcinfo->arg[i]);
            tupType = HeapTupleHeaderGetTypeId(td);
            tupTypmod = HeapTupleHeaderGetTypMod(td);
            tupdesc = lookup_rowtype_tupdesc(tupType, tupTypmod);
            tmptup.t_len = HeapTupleHeaderGetDatumLength(td);
            tmptup.t_data = td;
	    tmp = plruby_build_tuple(&tmptup, tupdesc, RET_HASH);
#else
            TupleTableSlot *slot = (TupleTableSlot *) fcinfo->arg[i];
            tmp = plruby_build_tuple(slot->val, slot->ttc_tupleDescriptor, RET_HASH);
#endif
	    rb_iv_set(tmp, "plruby_tuple", 
		      Data_Wrap_Struct(rb_cData, 0, 0, (void *)fcinfo->arg[i]));
	    rb_ary_push(ary, tmp);
        } 
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
                Oid elemtyp;
                elemtyp = ARR_ELEMTYPE(array);
                p = ARR_DATA_PTR(array);
                rb_ary_push(ary, create_array(0, ndim, dim, &p, prodesc, i,
                                              elemtyp));
            }
        }
        else {
            VALUE res;

            res = pl_convert_arg(fcinfo->arg[i],
                                 prodesc->arg_type[i],
                                 &prodesc->arg_func[i],
                                 prodesc->arg_elem[i],
                                 prodesc->arg_len[i]);
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
    PG_FUNCTION_ARGS;
    
    fcinfo = plth->fcinfo;
    expr_multiple = 0;
    if (prodesc->result_type && prodesc->result_type != 'x' &&
        prodesc->result_type != 'y') {
        ReturnSetInfo *rsi;

        if (!fcinfo || !fcinfo->resultinfo) {
            rb_raise(pl_ePLruby, "no description given");
        }
        rsi = (ReturnSetInfo *)fcinfo->resultinfo;
        if (prodesc->result_is_setof && !rsi->expectedDesc) {
            VALUE  res, retary, arg;
            struct pl_arg *args;
            TupleDesc tupdesc;
            FuncCallContext *funcctx;
            Datum result;

            arg = Data_Make_Struct(rb_cObject, struct pl_arg, pl_arg_mark, free, args);
            args->id = rb_intern(RSTRING_PTR(value_proname));
            args->ary = ary;
#if PG_PL_VERSION >= 75
            args->named = prodesc->named_args;
#endif

            if (SRF_IS_FIRSTCALL())
            {
                MemoryContext oldcontext;

                funcctx = SRF_FIRSTCALL_INIT();

                oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

                /* Build a tuple descriptor for our result type */
                if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
                    ereport(ERROR,
                            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                             errmsg("function returning record called in context "
                                 "that cannot accept type record")));
                /*
                 * generate attribute metadata needed later to produce tuples from raw
                 * C strings
                 */
                funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

                MemoryContextSwitchTo(oldcontext);

                retary = rb_ary_new();
#if HAVE_RB_BLOCK_CALL
                if (args->named) {
                    res = rb_block_call(pl_mPLtemp, args->id,
                            RARRAY_LEN(args->ary),
                            RARRAY_PTR(args->ary),
                            pl_ary_collect, retary);
                }
                else {
                    res = rb_block_call(pl_mPLtemp, args->id,
                            1, &args->ary,
                            pl_ary_collect, retary);
                }
#else
                res = rb_iterate(pl_func, arg, pl_ary_collect, retary);
#endif
                elog(NOTICE, "returned array len is: %ld", RARRAY_LEN(retary) );

                funcctx->max_calls = RARRAY_LEN(retary) ;
                funcctx->user_fctx = (void *)retary;

            }
            funcctx = SRF_PERCALL_SETUP();

            retary = (VALUE)funcctx->user_fctx;

            if (funcctx->call_cntr < funcctx->max_calls)    /* do when there is more left to send */
            {
                char         ** values;
                HeapTuple    tuple;
                size_t       idx;
                VALUE        resary;

                resary = RARRAY_PTR(retary)[funcctx->call_cntr];
                values = (char **)palloc(RARRAY_LEN(resary) * sizeof(char *));

                for ( idx = 0; idx < RARRAY_LEN(resary); idx++ )
                {
                    VALUE str = rb_ary_entry( resary, idx );
                    if (TYPE(str) != T_STRING) {
                        str = rb_obj_as_string(str);
                    }
                    values[idx] = pstrdup( StringValueCStr( str ) );
                }

                /* build a tuple */
                tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);

                /* make the tuple into a datum */
                result = HeapTupleGetDatum(tuple);
            }

            PLRUBY_BEGIN_PROTECT(1);
            {
                MemoryContext oldcxt;
                int rc;

                oldcxt = MemoryContextSwitchTo(plruby_spi_context);
                if ((rc = SPI_finish()) != SPI_OK_FINISH) {
                    elog(ERROR, "SPI_finish() failed : %d", rc);
                }
                MemoryContextSwitchTo(oldcxt);
            }
            PLRUBY_END_PROTECT;

            if ( funcctx->call_cntr < funcctx->max_calls )
            {
                SRF_RETURN_NEXT(funcctx, result);
            }
            else
            {
                SRF_RETURN_DONE(funcctx);
            }

        } else if ((rsi->allowedModes & SFRM_Materialize) && rsi->expectedDesc) {
            VALUE tuple, res, arg;
            struct pl_arg *args;
            struct pl_tuple *tpl;
            VALUE (*pl_call)(VALUE);

            tuple = pl_tuple_s_new(fcinfo, prodesc);
            arg = Data_Make_Struct(rb_cObject, struct pl_arg, pl_arg_mark, free, args);
            args->id = rb_intern(RSTRING_PTR(value_proname));
            args->ary = ary;
#if PG_PL_VERSION >= 75
            args->named = prodesc->named_args;
#endif
            pl_call = pl_func;
            while (1) {
#if HAVE_RB_BLOCK_CALL
		if (pl_call == pl_func) {
		    if (args->named) {
			res = rb_block_call(pl_mPLtemp, args->id, 
					    RARRAY_LEN(args->ary),
					    RARRAY_PTR(args->ary),
					    pl_tuple_put, tuple);
		    }
		    else {
			res = rb_block_call(pl_mPLtemp, args->id,
					    1, &args->ary,
					    pl_tuple_put, tuple);
		    }
		}
		else {
		    res = rb_block_call(pl_string(arg), rb_intern("each"),
					0, 0, pl_tuple_put, tuple);
		}
#else
                res = rb_iterate(pl_call, arg, pl_tuple_put, tuple);
#endif
                Data_Get_Struct(tuple, struct pl_tuple, tpl);
                if (NIL_P(res) && !tpl->out) {
                    MemoryContext oldcxt;
                    
                    PLRUBY_BEGIN_PROTECT(1);
                    oldcxt = MemoryContextSwitchTo(tpl->cxt);
#if PG_PL_VERSION >= 74
                    tpl->out = tuplestore_begin_heap(true, false, SortMem);
#else
                    tpl->out = tuplestore_begin_heap(true, SortMem);
#endif
                    MemoryContextSwitchTo(oldcxt);
                    PLRUBY_END_PROTECT;
                }
                if (tpl->out) {
                    MemoryContext oldcxt;
                    
                    PLRUBY_BEGIN_PROTECT(1);
                    oldcxt = MemoryContextSwitchTo(tpl->cxt);
                    tuplestore_donestoring(tpl->out);
                    MemoryContextSwitchTo(oldcxt);
                    PLRUBY_END_PROTECT;
                    ((ReturnSetInfo *)fcinfo->resultinfo)->setResult = tpl->out;
                    ((ReturnSetInfo *)fcinfo->resultinfo)->returnMode = SFRM_Materialize;
                    break;
                }
                if (NIL_P(res)) {
                    break;
                }
                if (TYPE(res) != T_STRING || RSTRING_PTR(res) == 0) {
                    rb_raise(pl_ePLruby, "invalid return type for a SET");
                }
                args->ary = res;
                pl_call = pl_string;
            }
            c = Qnil;
        }
        else if (IsA(rsi, ReturnSetInfo)) {
            expr_multiple = 1;
#if PG_PL_VERSION >= 75
            if (prodesc->named_args) {
                c = rb_funcall2(pl_mPLtemp, rb_intern(RSTRING_PTR(value_proname)),
                                RARRAY_LEN(ary), RARRAY_PTR(ary));
            }
            else {
                c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING_PTR(value_proname)),
                               1, ary);
            }
#else
            c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING_PTR(value_proname)),
                           1, ary);
#endif
        }
        else {
            rb_raise(pl_ePLruby, "context don't accept set");
        }
    }
    else {
#if PG_PL_VERSION >= 75
        if (prodesc->named_args) {
            c = rb_funcall2(pl_mPLtemp, rb_intern(RSTRING_PTR(value_proname)),
                            RARRAY_LEN(ary), RARRAY_PTR(ary));
        }
        else {
            c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING_PTR(value_proname)),
                           1, ary);
        }
#else
        c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING_PTR(value_proname)),
                       1, ary);
#endif
    }

    PLRUBY_BEGIN_PROTECT(1);
    {
        MemoryContext oldcxt;
        int rc;

        oldcxt = MemoryContextSwitchTo(plruby_spi_context);
        if ((rc = SPI_finish()) != SPI_OK_FINISH) {
            elog(ERROR, "SPI_finish() failed : %d", rc);
        }
        MemoryContextSwitchTo(oldcxt);
    }
    PLRUBY_END_PROTECT;

    if (c == Qnil) {
        if (expr_multiple) {
            pl_context_remove();
            fcinfo->context = NULL;
            ((ReturnSetInfo *)fcinfo->resultinfo)->isDone = ExprEndResult;
        }
        PG_RETURN_NULL();
    }
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
        PLRUBY_BEGIN_PROTECT(1);
        retval = DFC1(textin, CStringGD(RSTRING_PTR(res)));
        PLRUBY_END_PROTECT;
        return retval;
    }
#if PG_PL_VERSION >= 81
    if (prodesc->result_type == 'y') {
	TupleDesc tupdesc;
	
	if (get_call_result_type(fcinfo, NULL, &tupdesc) == TYPEFUNC_COMPOSITE) {
	    VALUE tmp;
	    struct pl_tuple *tpl;

	    tmp = Data_Make_Struct(rb_cData, struct pl_tuple, pl_thr_mark, 
				   free, tpl);
	    GetTuple(tmp, tpl);
	    tpl->pro = prodesc;
	    tpl->dsc = tupdesc;
	    tpl->att = TupleDescGetAttInMetadata(tupdesc);
	    return pl_tuple_datum(c, tmp);
	}
    }
#endif
    return return_base_type(c, prodesc);
}

extern void Init_plruby_plan();

void Init_plruby_pl()
{
    VALUE pl_sPLtemp;

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
    rb_define_module_function(pl_mPL, "result_name", pl_query_name, 0);
    rb_define_module_function(pl_mPL, "result_type", pl_query_type, 0);
    rb_define_module_function(pl_mPL, "result_size", pl_query_lgth, 0);
    rb_define_module_function(pl_mPL, "result_description", pl_query_description, 0);
    rb_define_module_function(pl_mPL, "args_type", pl_args_type, 0);
    rb_define_module_function(pl_mPL, "context", pl_context_get, 0);
    rb_define_module_function(pl_mPL, "context=", pl_context_set, 1);
    pl_ePLruby = rb_define_class_under(pl_mPL, "Error", rb_eStandardError);
    pl_eCatch = rb_define_class_under(pl_mPL, "Catch", rb_eStandardError);
    pl_mPLtemp = rb_define_module("PLtemp");
    pl_sPLtemp = rb_singleton_class(pl_mPLtemp);
    PLcontext = rb_hash_new();
    rb_global_variable(&PLcontext);
    if (MAIN_SAFE_LEVEL >= 3) {
        rb_obj_taint(pl_mPLtemp);
        rb_obj_taint(pl_sPLtemp);
        rb_obj_taint(PLcontext);
    }
    id_thr = rb_intern("__functype__");
#ifndef HAVE_RB_HASH_DELETE
    id_delete = rb_intern("delete");
#endif
#ifdef PLRUBY_ENABLE_CONVERSION
    id_from_datum = rb_intern("from_datum");
    id_to_datum = rb_intern("to_datum");
#endif
    Init_plruby_plan();
    pl_cPLPlan = rb_const_get(pl_mPL, rb_intern("Plan"));
}
