/*
 * plruby.c - ruby as a procedural language for PostgreSQL
 * copied from pltcl.c by Guy Decoux <ts@moulon.inra.fr>
 * 
 * You can redistribute it and/or modify it under the same term as
 * Ruby.
 *
 * Original copyright from pltcl.c
 */
/**********************************************************************
 * pltcl.c              - PostgreSQL support for Tcl as
 *                        procedural language (PL)
 *
 * IDENTIFICATION
 *        $Header: /usr/local/cvsroot/pgsql/src/pl/tcl/pltcl.c,v 1.12 1999/05/26 12:57:23 momjian Exp $
 *
 *        This software is copyrighted by Jan Wieck - Hamburg.
 *
 *        The author hereby grants permission  to  use,  copy,  modify,
 *        distribute,  and      license this software and its documentation
 *        for any purpose, provided that existing copyright notices are
 *        retained      in      all  copies  and  that  this notice is included
 *        verbatim in any distributions. No written agreement, license,
 *        or  royalty  fee      is required for any of the authorized uses.
 *        Modifications to this software may be  copyrighted  by  their
 *        author  and  need  not  follow  the licensing terms described
 *        here, provided that the new terms are  clearly  indicated  on
 *        the first page of each file where they apply.
 *
 *        IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY
 *        PARTY  FOR  DIRECT,   INDIRECT,       SPECIAL,   INCIDENTAL,   OR
 *        CONSEQUENTIAL   DAMAGES  ARISING      OUT  OF  THE  USE  OF  THIS
 *        SOFTWARE, ITS DOCUMENTATION, OR ANY DERIVATIVES THEREOF, EVEN
 *        IF  THE  AUTHOR  HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH
 *        DAMAGE.
 *
 *        THE  AUTHOR  AND      DISTRIBUTORS  SPECIFICALLY       DISCLAIM       ANY
 *        WARRANTIES,  INCLUDING,  BUT  NOT  LIMITED  TO,  THE  IMPLIED
 *        WARRANTIES  OF  MERCHANTABILITY,      FITNESS  FOR  A  PARTICULAR
 *        PURPOSE,      AND NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON
 *        AN "AS IS" BASIS, AND THE AUTHOR      AND  DISTRIBUTORS  HAVE  NO
 *        OBLIGATION   TO       PROVIDE   MAINTENANCE,   SUPPORT,  UPDATES,
 *        ENHANCEMENTS, OR MODIFICATIONS.
 *
 **********************************************************************/

#include "plruby.h"

#ifdef PLRUBY_TIMEOUT
static int pl_in_progress = 0;
static int pl_interrupted = 0;
#endif

static ID id_to_s, id_raise, id_kill, id_alive, id_value, id_call, id_thr;

#ifdef PLRUBY_HASH_DELETE
static ID id_delete;

#define rb_hash_delete(a, b) rb_funcall((a), id_delete, 1, (b))

#endif

#ifdef NEW_STYLE_FUNCTION

#define DFC1(a_, b_) DirectFunctionCall1((a_), (b_))

#else

#define DFC1(a_, b_) (*fmgr_faddr(a_)(b_))

#endif

#ifndef BoolGetDatum
#define BoolGetDatum(a_) (a_)
#define DatumGetBool(a_) (a_)
#endif

#if PG_PL_VERSION < 72
#define SPI_freetuptable(a_) 
#define SPI_freeplan(a_) 
#endif

static int      pl_firstcall = 1;
static int      pl_call_level = 0;
static VALUE    pl_mPL, pl_mPLtemp, pl_cPLPlan, pl_cPLCursor;
static VALUE    pl_ePLruby, pl_eCatch;
static VALUE    PLruby_hash, pl_sPLtemp;
static int      pl_fatal = 0;

#ifdef PLRUBY_ENABLE_CONVERSION

static ID id_from_datum, id_to_datum;
static VALUE plruby_classes;
static VALUE plruby_conversions;

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
        pl_fatal = 1;
        elog(ERROR, "can't find %s : try first `make install'", name);
    }
}

static void
pl_init_conversions()
{
#include "conversions.h"
}

VALUE
plruby_s_new(int argc, VALUE *argv, VALUE obj)
{
    VALUE res = rb_funcall2(obj, rb_intern("allocate"), 0, 0);
    rb_obj_call_init(res, argc, argv);
    return res;
}

#ifndef HAVE_RB_DEFINE_ALLOC_FUNC

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

#if PG_PL_VERSION >= 73
static MemoryContext spi_context;
#endif

static void
pl_proc_free(proc)
    pl_proc_desc *proc;
{
    if (proc->proname) {
        free(proc->proname);
    }
    free(proc);
}

static char *definition = "def PLtemp.%s(%s)\n%s\nend";

VALUE
plruby_to_s(VALUE obj)
{
    if (TYPE(obj) != T_STRING) {
        obj = rb_funcall2(obj, id_to_s, 0, 0);
    }
    if (TYPE(obj) != T_STRING || !RSTRING(obj)->ptr) {
        rb_raise(pl_ePLruby, "Expected a String");
    }
    return obj;
}

static char *names = "SELECT a.attname FROM pg_class c, pg_attribute a"
" WHERE c.relname = '%s' AND a.attnum > 0 AND a.attrelid = c.oid"
" ORDER BY a.attnum";

static VALUE
pl_column_name(VALUE obj, VALUE table)
{
    VALUE *query, res;
    char *tmp;

    if (TYPE(table) != T_STRING) {
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

    if (TYPE(table) != T_STRING) {
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(tmp) != T_DATA || 
        RDATA(tmp)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tmp, struct pl_tuple, tpl);
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
    if (TYPE(res) != T_DATA || 
        RDATA(res)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(res, struct pl_tuple, tpl);
    tpl->cxt = rsi->econtext->ecxt_per_query_memory;
    tpl->dsc = rsi->expectedDesc;
    tpl->att = TupleDescGetAttInMetadata(rsi->expectedDesc);
    tpl->pro = prodesc;
    rb_thread_local_aset(rb_thread_current(), id_thr, res);
    return res;
}

#if PG_PL_VERSION >= 74

#ifndef MAXDIM
#define MAXDIM 12
#endif

#endif

static Datum pl_value_to_datum _((VALUE, FmgrInfo *, Oid, Oid, int));

static HeapTuple
pl_tuple_heap(VALUE c, VALUE tuple)
{
    HeapTuple retval;
    struct pl_tuple *tpl;
    Datum *dvalues;
    char *nulls;
    int i;

    if (TYPE(tuple) != T_DATA || 
        RDATA(tuple)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tuple, struct pl_tuple, tpl);
    if (tpl->pro->result_type == 'b' && TYPE(c) != T_ARRAY) {
        VALUE tmp;

        tmp = rb_ary_new2(1);
        rb_ary_push(tmp, c);
        c = tmp;
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
#if PG_PL_VERSION >= 74
            if (tpl->att->tupdesc->attrs[i]->attndims != 0) {
                static Datum return_array_type _((VALUE, pl_proc_desc *));
                pl_proc_desc prodesc;

                MEMZERO(&prodesc, pl_proc_desc, 1);
                prodesc.result_func = tpl->pro->arg_func[i];
                prodesc.result_elem = tpl->pro->arg_elem[i];
                prodesc.result_oid = tpl->pro->arg_type[i];
                prodesc.result_len = tpl->pro->arg_len[i];
                prodesc.result_val = tpl->pro->arg_val[i];
                prodesc.result_align = tpl->pro->arg_align[i];
                dvalues[i] = return_array_type(RARRAY(c)->ptr[i], &prodesc);
            }
            else
#endif
            {
                Oid typoid = tpl->att->tupdesc->attrs[i]->atttypid;

                dvalues[i] = pl_value_to_datum(RARRAY(c)->ptr[i],
                                               &tpl->att->attinfuncs[i],
                                               typoid,
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

    if (TYPE(tuple) != T_DATA || 
        RDATA(tuple)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tuple, struct pl_tuple, tpl);
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

    if (TYPE(tuple) != T_DATA || 
        RDATA(tuple)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
        rb_raise(pl_ePLruby, "invalid thread local variable");
    }
    Data_Get_Struct(tuple, struct pl_tuple, tpl);
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

static VALUE pl_plan_s_new _((int, VALUE *, VALUE));
static VALUE pl_plan_each _((int, VALUE *, VALUE));

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
    plan = pl_plan_s_new(2, tmp, pl_cPLPlan);
    rb_funcall2(plan, rb_intern("each"), 0, 0);
    return Qnil;
}
 
#endif

#if PG_PL_VERSION >= 73
static void
perm_fmgr_info(Oid functionId, FmgrInfo *finfo)
{
        fmgr_info_cxt(functionId, finfo, TopMemoryContext);
}

#define fmgr_info perm_fmgr_info

#endif

static void pl_result_mark(VALUE obj) {}

static VALUE
pl_protect(plth)
    struct pl_thread_st *plth;
{
    Datum retval;
    VALUE result;

    if (sigsetjmp(Warn_restart, 1) != 0) {
        return pl_eCatch;
    }
#ifdef NEW_STYLE_FUNCTION
    if (CALLED_AS_TRIGGER(plth->fcinfo)) {
        retval = PointerGetDatum(pl_trigger_handler(plth->fcinfo));
    }
    else {
        retval = pl_func_handler(plth->fcinfo);
    }
#else
    if (CurrentTriggerData == NULL) {
        retval = pl_func_handler(plth->proinfo, plth->proargs, plth->isNull);
    }
    else {
        retval = (Datum) pl_trigger_handler(plth->proinfo);
    }
#endif
    result = Data_Wrap_Struct(rb_cObject, pl_result_mark, 0, (void *)retval);
    return result;
}

#ifdef PLRUBY_TIMEOUT

static VALUE
pl_thread_raise(VALUE th)
{
    VALUE exc = rb_exc_new2(pl_ePLruby, "timeout");
    return rb_funcall(th, id_raise, 1, exc);
}

static VALUE
pl_thread_kill(VALUE th)
{
    return rb_funcall2(th, id_kill, 0, 0);
}

static VALUE
pl_timer(VALUE th)
{
    struct timeval time;

    rb_thread_sleep(PLRUBY_TIMEOUT);
    pl_interrupted = 1;
    time.tv_sec = 0;
    time.tv_usec = 50000;
    while (1) {
        if (!RTEST(rb_funcall2(th, id_alive, 0, 0))) {
            return Qnil;
        }
        if (!pl_in_progress) {
            rb_protect(pl_thread_raise, th, 0);
        }
        rb_thread_wait_for(time);
    }
    return Qnil;
}

static VALUE
pl_thread_value(VALUE th)
{
    return rb_funcall2(th, id_value, 0, 0);
}

#endif

static VALUE
pl_error(VALUE v)
{
    VALUE result;

    result = rb_gv_get("$!");
    if (rb_obj_is_kind_of(result, pl_eCatch)) {
        result = pl_eCatch;
    }
    else if (rb_obj_is_kind_of(result, rb_eException)) {
        result = plruby_to_s(result);
    }
    return result;
}

#if defined(PLRUBY_TIMEOUT) && PG_PL_VERSION >= 73
struct extra_args {
    VALUE result;
    struct Node *context;
    SetFunctionReturnMode returnMode;
    Tuplestorestate *setResult;
    ExprDoneCond isDone;
};

static void
extra_args_mark(struct extra_args *exa)
{
    rb_gc_mark(exa->result);
}
#endif

static VALUE
pl_real_handler(struct pl_thread_st *plth)
{
    VALUE result;
    int state;

#ifdef PLRUBY_TIMEOUT
    if (plth->timeout) {
        VALUE curr = rb_thread_current();
        rb_thread_create(pl_timer, (void *)curr);
        rb_funcall(curr, rb_intern("priority="), 1, INT2NUM(0));
        rb_set_safe_level(SAFE_LEVEL);
    }
#endif

    state = 0;
    pl_call_level++;
    result = rb_protect(pl_protect, (VALUE)plth, &state);
    pl_call_level--;
    if (state) {
        state = 0;
        result = rb_protect(pl_error, 0, &state);
        if (state || (result != pl_eCatch && TYPE(result) != T_STRING)) {
            result = rb_str_new2("Unknown Error");
        }
    }
#if defined(PLRUBY_TIMEOUT) && PG_PL_VERSION >= 73
    {
        VALUE res;
        struct extra_args *exa;
        ReturnSetInfo *rsi;

        res = Data_Make_Struct(rb_cData, struct extra_args, extra_args_mark, free, exa);
        exa->context = plth->fcinfo->context;
        rsi = (ReturnSetInfo *)plth->fcinfo->resultinfo;
        if (rsi) {
            exa->setResult = rsi->setResult; 
            exa->returnMode = rsi->returnMode;
            exa->isDone = rsi->isDone;
        }
        exa->result = result;
        return res;
    }
#endif
    return result;
}

Datum
#ifdef NEW_STYLE_FUNCTION
PLRUBY_CALL_HANDLER(PG_FUNCTION_ARGS)
#else
PLRUBY_CALL_HANDLER(FmgrInfo *proinfo,
                    FmgrValues *proargs,
                    bool *isNull)
#endif
{
    VALUE result;
    sigjmp_buf save_restart;
    struct pl_thread_st plth;
    volatile void *tmp;
#if PG_PL_VERSION >= 73
    MemoryContext orig_context;
    volatile VALUE orig_id;
#endif

    if (pl_firstcall) {
        pl_init_all();
    }
    if (!pl_call_level) {
        extern void Init_stack();
        Init_stack(&tmp);
    }

#if PG_PL_VERSION >= 73
    orig_context = CurrentMemoryContext;
    orig_id = rb_thread_local_aref(rb_thread_current(), id_thr);
    rb_thread_local_aset(rb_thread_current(), id_thr, Qnil);
#endif
    if (SPI_connect() != SPI_OK_CONNECT) {
        if (pl_call_level) {
            rb_raise(pl_ePLruby, "cannot connect to SPI manager");
        }
        else {
            elog(ERROR, "cannot connect to SPI manager");
        }
    }
#if PG_PL_VERSION >= 73
    spi_context =  MemoryContextSwitchTo(orig_context);
#endif
#ifdef NEW_STYLE_FUNCTION
    plth.fcinfo = fcinfo;
#else
    plth.proinfo = proinfo;
    plth.proargs = proargs;
    plth.isNull = isNull;
#endif
    plth.timeout = 0;

    memcpy(&save_restart, &Warn_restart, sizeof(save_restart));
#ifdef PLRUBY_TIMEOUT
    if (!pl_call_level) {
        VALUE th;
        int state;

        pl_interrupted = pl_in_progress = 0;
        plth.timeout = 1;
        th = rb_thread_create(pl_real_handler, (void *)&plth);
        result = rb_protect(pl_thread_value, th, &state);
        pl_interrupted = pl_in_progress = pl_call_level = 0;
        if (state) {
            result = rb_str_new2("Unknown error");
        }
    }
    else 
#endif
    {
        PLRUBY_BEGIN(0);
        result = pl_real_handler(&plth);
        PLRUBY_END;
    }
#if defined(PLRUBY_TIMEOUT) && PG_PL_VERSION >= 73
    if (TYPE(result) == T_DATA &&
        RDATA(result)->dmark == (RUBY_DATA_FUNC)extra_args_mark) {
        ReturnSetInfo *rsi;
        struct extra_args *exa;

        Data_Get_Struct(result, struct extra_args, exa);
        fcinfo->context = exa->context;
        rsi = (ReturnSetInfo *)fcinfo->resultinfo;
        if (rsi) {
            rsi->setResult = exa->setResult; 
            rsi->returnMode = exa->returnMode;
            rsi->isDone = exa->isDone;
        }
        result = exa->result;
    }
#endif
    memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));

#ifdef PLRUBY_TIMEOUT
    if (!pl_call_level) {
        int i, in_progress;
        VALUE thread, threads;
        VALUE main_th = rb_thread_main();
        
        in_progress = pl_in_progress;
        pl_in_progress = 1;
        while (1) {
            threads = rb_thread_list();
            if (RARRAY(threads)->len <= 1) break;
            for (i = 0; i < RARRAY(threads)->len; i++) {
                thread = RARRAY(threads)->ptr[i];
                if (thread != main_th) {
                    rb_protect(pl_thread_kill, thread, 0);
                }
            }
        }
        pl_call_level = 0;
        pl_in_progress = in_progress;
    }
#endif

#if PG_PL_VERSION >= 73
    rb_thread_local_aset(rb_thread_current(), id_thr, orig_id);
#endif

    if (result == pl_eCatch) {
        if (pl_call_level) {
            rb_raise(pl_eCatch, "SPI ERROR");
        }
        else {
            siglongjmp(Warn_restart, 1);
        }
    }
    if (TYPE(result) == T_STRING && RSTRING(result)->ptr) {
        if (pl_call_level) {
            rb_raise(pl_ePLruby, "%.*s", 
                     (int)RSTRING(result)->len, RSTRING(result)->ptr);
        }
        else {
            elog(ERROR, "%.*s", 
                 (int)RSTRING(result)->len, RSTRING(result)->ptr);
        }
    }
    if (TYPE(result) == T_DATA && 
        RDATA(result)->dmark == (RUBY_DATA_FUNC)pl_result_mark) {
        return ((Datum)DATA_PTR(result));
    }
    if (pl_call_level) {
        rb_raise(pl_ePLruby, "Invalid return value %d", TYPE(result));
    }
    else {
        elog(ERROR, "Invalid return value %d", TYPE(result));
    }
    return ((Datum)0);
}

static Datum
pl_value_to_datum(VALUE obj, FmgrInfo *finfo, Oid typoid, Oid typelem, int typlen)
{
    Datum d;

    if (typoid == BOOLOID) {
        return BoolGetDatum(RTEST(obj));
    }
#ifdef PLRUBY_ENABLE_CONVERSION
    if (rb_respond_to(obj, id_to_datum)) {
        VALUE res = rb_funcall(obj, id_to_datum, 2, INT2NUM(typoid), 
                               INT2NUM(typlen));
        if (TYPE(res) == T_DATA) {
            d = (Datum)RDATA(res)->data;
            rb_gc_force_recycle(res);
            return d;
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

static Datum
return_array_type(VALUE ary, pl_proc_desc *p)
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
        values[i] = pl_value_to_datum(RARRAY(ary)->ptr[i], &p->result_func,
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
        retval = return_array_type(c, prodesc);
    }
    else
#endif
    {
        retval = pl_value_to_datum(c, &prodesc->result_func,
                                   prodesc->result_oid, prodesc->result_elem,
                                   prodesc->result_len);
    }
    return retval;
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
            VALUE val, res;

            val = Data_Wrap_Struct(rb_cData, 0, 0, (void *)value);
            res = rb_funcall(klass, id_from_datum, 2, val, vid);
            rb_gc_force_recycle(val);
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

static Datum
#ifdef NEW_STYLE_FUNCTION
pl_func_handler(PG_FUNCTION_ARGS)
#else
pl_func_handler(proinfo, proargs, isNull)
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
{
    int i;
    char internal_proname[512];
    int proname_len;
#ifndef NEW_STYLE_FUNCTION
    char *stroid;
#endif
    HeapTuple procTup;
    Form_pg_proc procStruct;
    pl_proc_desc *prodesc;
    VALUE value_proc_desc;
    VALUE value_proname;
    VALUE ary, c;
    static char *argf = "args";
    Oid result_oid, arg_type[RUBY_ARGS_MAXFMGR];
    int nargs, expr_multiple;
    
#ifdef NEW_STYLE_FUNCTION
    sprintf(internal_proname, "proc_%u", fcinfo->flinfo->fn_oid);
#else
    stroid = oidout(proinfo->fn_oid);
    strcpy(internal_proname, "proc_");
    strcat(internal_proname, stroid);
    pfree(stroid);
#endif

    proname_len = strlen(internal_proname);
    value_proname = rb_tainted_str_new(internal_proname, proname_len);
    value_proc_desc = rb_hash_aref(PLruby_hash, value_proname);

    PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
    procTup = SearchSysCacheTuple(RUBY_PROOID,
                                  ObjectIdGetDatum(fcinfo->flinfo->fn_oid),
                                  0, 0, 0);
#else
    procTup = SearchSysCacheTuple(RUBY_PROOID,
                                  ObjectIdGetDatum(proinfo->fn_oid),
                                  0, 0, 0);
#endif
    PLRUBY_END;
    if (!HeapTupleIsValid(procTup))     {
        rb_raise(pl_ePLruby, "cache lookup from pg_proc failed");
    }
    procStruct = (Form_pg_proc) GETSTRUCT(procTup);

#if PG_PL_VERSION >= 74
    if (procStruct->prorettype == ANYARRAYOID ||
        procStruct->prorettype == ANYELEMENTOID) {
        result_oid = get_fn_expr_rettype(fcinfo->flinfo);
        if (result_oid == InvalidOid) {
            result_oid = procStruct->prorettype;
        }
    }
    else
#endif
    {
        result_oid = procStruct->prorettype;
    }

    nargs = procStruct->pronargs;
    for (i = 0; i < nargs; ++i) {
#if PG_PL_VERSION >= 74
        if (procStruct->proargtypes[i] == ANYARRAYOID ||
            procStruct->proargtypes[i] == ANYELEMENTOID) {
            arg_type[i] = get_fn_expr_argtype(fcinfo->flinfo, i);
            if (arg_type[i] == InvalidOid) {
                arg_type[i] = procStruct->proargtypes[i];
            }
        }
        else 
#endif
        {
            arg_type[i] = procStruct->proargtypes[i];
        }
    }

#if PG_PL_VERSION >= 73
    if (!NIL_P(value_proc_desc)) {
        int uptodate;

        Data_Get_Struct(value_proc_desc, pl_proc_desc, prodesc);
        uptodate = 
            (prodesc->fn_xmin == HeapTupleHeaderGetXmin(procTup->t_data)) &&
            (prodesc->fn_cmin == HeapTupleHeaderGetCmin(procTup->t_data));
#if PG_PL_VERSION >= 74
        if (uptodate) {
            uptodate = result_oid == prodesc->result_oid;
        }
        if (uptodate) {
            int i;

            for (i = 0; i < nargs; ++i) {
                if (arg_type[i] != prodesc->arg_type[i]) {
                    uptodate = 0;
                    break;
                }
            }
        }
#endif
        if (!uptodate) {
            rb_remove_method(pl_sPLtemp, internal_proname);
            value_proc_desc = Qnil;
        }
    }
#endif

    if (NIL_P(value_proc_desc)) {
        HeapTuple typeTup;
        Form_pg_type typeStruct;
        char proc_internal_args[4096];
        char *proc_source, *proc_internal_def;
        int status;
#if PG_PL_VERSION >= 73
        MemoryContext oldcontext;
#endif

        value_proc_desc = Data_Make_Struct(rb_cObject, pl_proc_desc, 0, pl_proc_free, prodesc);
        prodesc->result_oid = result_oid;
        PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
        oldcontext = MemoryContextSwitchTo(TopMemoryContext);
        prodesc->fn_xmin = HeapTupleHeaderGetXmin(procTup->t_data);
        prodesc->fn_cmin = HeapTupleHeaderGetCmin(procTup->t_data);
#endif
        typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                      ObjectIdGetDatum(result_oid),
                                      0, 0, 0);
        PLRUBY_END;
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "cache lookup for return type failed");
        }
        typeStruct = (Form_pg_type) GETSTRUCT(typeTup);

#if PG_PL_VERSION >= 73
        if (typeStruct->typtype == 'p') {
            switch (result_oid) {
            case RECORDOID:
            case VOIDOID:
                break;
            default:
                rb_raise(pl_ePLruby,  "functions cannot return type %s",
                         format_type_be(result_oid));
                break;
            }
        }
#endif

#if PG_PL_VERSION < 73
        if (typeStruct->typrelid != InvalidOid) {
            rb_raise(pl_ePLruby, "return types of tuples supported only for >= 7.3");
        }
#else
        if (procStruct->proretset) {
            Oid funcid, functypeid;
            char functyptype;

            funcid = fcinfo->flinfo->fn_oid;
            PLRUBY_BEGIN(1);
            functypeid = get_func_rettype(funcid);
            functyptype = get_typtype(functypeid);
            PLRUBY_END;
            if (functyptype == 'c' || functyptype == 'b' ||
                (functyptype == 'p' && functypeid == RECORDOID)) {
                prodesc->result_type = functyptype;
            }
            else {
                rb_raise(pl_ePLruby, "Invalid kind of return type");
            }
        }
        else {
            if (result_oid == REFCURSOROID) {
                prodesc->result_type = 'x';
            }
            else {
                Oid     funcid, functypeid;
                char functyptype;

                funcid = fcinfo->flinfo->fn_oid;
                PLRUBY_BEGIN(1);
                functypeid = get_func_rettype(funcid);
                functyptype = get_typtype(functypeid);
                PLRUBY_END;
                if (functyptype == 'c' ||
                    (functyptype == 'p' && functypeid == RECORDOID)) {
                    prodesc->result_type = 'y';
                }
            }
        }
#endif

        prodesc->result_elem = (Oid)typeStruct->typelem;
#if PG_PL_VERSION >= 74
        prodesc->result_is_array = 0;
        if (NameStr(typeStruct->typname)[0] == '_') {
            FmgrInfo  inputproc;
            HeapTuple typeTuple;
            Form_pg_type typeStruct;

            PLRUBY_BEGIN(1);
            typeTuple = SearchSysCache
                (TYPEOID,
                 ObjectIdGetDatum(prodesc->result_elem), 0, 0, 0);
                        
            if (!HeapTupleIsValid(typeTuple)) {
                rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                         prodesc->result_elem);
            }
            typeStruct = (Form_pg_type) GETSTRUCT(typeTuple);
            fmgr_info(typeStruct->typinput, &inputproc);
            prodesc->result_is_array = 1;
            prodesc->result_func = inputproc;
            prodesc->result_val = typeStruct->typbyval;
            prodesc->result_len = typeStruct->typlen;
            prodesc->result_align = typeStruct->typalign;
            ReleaseSysCache(typeTuple);
            PLRUBY_END;
        }
        else
#endif
        {
            PLRUBY_BEGIN(1);
            fmgr_info(typeStruct->typinput, &(prodesc->result_func));
            prodesc->result_len = typeStruct->typlen;
            PLRUBY_END;
        }

#if PG_PL_VERSION >= 71
        ReleaseSysCache(typeTup);
#endif

        prodesc->nargs = nargs;
        proc_internal_args[0] = '\0';
        for (i = 0; i < prodesc->nargs; i++)    {

            PLRUBY_BEGIN(1);
            typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                          ObjectIdGetDatum(arg_type[i]),
                                          0, 0, 0);
            PLRUBY_END;

            if (!HeapTupleIsValid(typeTup)) {
                rb_raise(pl_ePLruby, "cache lookup for argument type failed");
            }
            typeStruct = (Form_pg_type) GETSTRUCT(typeTup);
            prodesc->arg_type[i] = arg_type[i];

#if PG_PL_VERSION >= 73
            if (typeStruct->typtype == 'p') {
                rb_raise(pl_ePLruby, "argument can't have the type %s",
                         format_type_be(arg_type[i]));
            }
#endif
            prodesc->arg_elem[i] = (Oid) (typeStruct->typelem);
            if (typeStruct->typrelid != InvalidOid) {
                prodesc->arg_is_rel[i] = 1;
#if PG_PL_VERSION >= 71
                ReleaseSysCache(typeTup);
#endif
            }
            else {
                prodesc->arg_is_rel[i] = 0;
            }

            PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 74
            prodesc->arg_is_array[i] = 0;
            if (NameStr(typeStruct->typname)[0] == '_') {
                FmgrInfo  outputproc;
                HeapTuple typeTuple;
                Form_pg_type typeStruct;
                    
                typeTuple = SearchSysCache
                    (TYPEOID,
                     ObjectIdGetDatum(prodesc->arg_elem[i]), 0, 0, 0);
                    
                if (!HeapTupleIsValid(typeTuple)) {
                    rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                             prodesc->arg_elem[i]);
                }
                typeStruct = (Form_pg_type) GETSTRUCT(typeTuple);
                fmgr_info(typeStruct->typoutput, &outputproc);
                prodesc->arg_is_array[i] = 1;
                prodesc->arg_func[i] = outputproc;
                prodesc->arg_val[i] = typeStruct->typbyval;
                prodesc->arg_len[i] = typeStruct->typlen;
                prodesc->arg_align[i] = typeStruct->typalign;
                ReleaseSysCache(typeTuple);
            }
            else
#endif
            {
                fmgr_info(typeStruct->typoutput, &(prodesc->arg_func[i]));
                prodesc->arg_len[i] = typeStruct->typlen;
            }
#if PG_PL_VERSION >= 71
            ReleaseSysCache(typeTup);
#endif
            PLRUBY_END;
        }

        PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
        proc_source = DatumGetCString(DFC1(textout,
                                           PointerGetDatum(&procStruct->prosrc)));
#else
        proc_source = textout(&(procStruct->prosrc));
#endif
        proc_internal_def = ALLOCA_N(char, strlen(definition) + proname_len +
                                     strlen(argf) + strlen(proc_source) + 1);
        sprintf(proc_internal_def, definition, internal_proname, argf, proc_source);
        pfree(proc_source);
        PLRUBY_END;

        rb_eval_string_protect(proc_internal_def, &status);
        if (status) {
            VALUE s = plruby_to_s(rb_gv_get("$!"));
            rb_raise(pl_ePLruby, "cannot create internal procedure\n%s\n<<===%s\n===>>",
                 RSTRING(s)->ptr, proc_internal_def);
        }
        prodesc->proname = ALLOC_N(char, strlen(internal_proname) + 1);
        strcpy(prodesc->proname, internal_proname);
        rb_hash_aset(PLruby_hash, value_proname, value_proc_desc); 
        PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
        MemoryContextSwitchTo(oldcontext);
#endif
        PLRUBY_END;
    }

#if PG_PL_VERSION >= 71
    ReleaseSysCache(procTup);
#endif

    Data_Get_Struct(value_proc_desc, pl_proc_desc, prodesc);
#if PG_PL_VERSION >= 73
    {
        VALUE res;
        struct pl_tuple *tpl;

        res = rb_thread_local_aref(rb_thread_current(), id_thr);
        if (NIL_P(res)) {
            res = Data_Make_Struct(rb_cData, struct pl_tuple, pl_thr_mark, free, tpl);
        } 
        if (TYPE(res) != T_DATA || 
           RDATA(res)->dmark != (RUBY_DATA_FUNC)pl_thr_mark) {
            rb_raise(pl_ePLruby, "invalid thread local variable");
        }
        Data_Get_Struct(res, struct pl_tuple, tpl);
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
            rb_ary_push(ary, pl_build_tuple(slot->val,
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
            char *tmp;

#ifdef NEW_STYLE_FUNCTION           
            res = pl_convert_arg(fcinfo->arg[i], prodesc->arg_type[i],
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

        oldcxt = MemoryContextSwitchTo(spi_context);
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
        {
#if PG_PL_VERSION >= 73
            if (expr_multiple) {
                pl_context_remove();
                fcinfo->context = NULL;
                ((ReturnSetInfo *)fcinfo->resultinfo)->isDone = ExprEndResult;
            }
#endif
            PG_RETURN_NULL();
        }
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
        struct PLportal *portal;
        Datum retval;

        if (TYPE(c) != T_DATA ||
            RDATA(c)->dfree != (RUBY_DATA_FUNC)portal_free) {
            rb_raise(pl_ePLruby, "expected a cursor");
        }
        GetPortal(c, portal);
        PLRUBY_BEGIN(1);
        retval = DFC1(textin, CStringGetDatum(portal->portal->name));
        PLRUBY_END;
        return retval;
    }
        
#endif
    return return_base_type(c, prodesc);
}

static VALUE
pl_build_tuple(HeapTuple tuple, TupleDesc tupdesc, int type_ret)
{
    int i;
    VALUE output, res = Qnil;
    Datum attr;
    bool isnull;
    char *attname, *outputstr, *typname;
    HeapTuple typeTup;
    Oid typoutput;
    Oid typelem;
    Form_pg_type fpgt;
    int alen;
    
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
            if (NameStr(fpgt->typname)[0] != '_')
#endif
            {
                FmgrInfo finfo;
                
                fmgr_info(typoutput, &finfo);
                
                s = pl_convert_arg(attr, tupdesc->attrs[i]->atttypid,
                                   &finfo, typelem,tupdesc->attrs[i]->attlen);
            }
#if PG_PL_VERSION >= 74 
            else {
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

                    typeTuple = SearchSysCache(TYPEOID,
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
#endif
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

struct foreach_fmgr {
    TupleDesc tupdesc;
    int *modattrs;
    Datum *modvalues;
    char *modnulls;
}; 

#ifndef VARLENA_FIXED_SIZE

#define VARLENA_FIXED_SIZE(a) (1)

#endif

static VALUE
for_numvals(obj, argobj)
    VALUE obj, argobj;
{
    int                 attnum;
    HeapTuple   typeTup;
    Oid                 typinput;
    Oid                 typelem;
    FmgrInfo    finfo;
    VALUE key, value;
    struct foreach_fmgr *arg;

    Data_Get_Struct(argobj, struct foreach_fmgr, arg);
    key = plruby_to_s(rb_ary_entry(obj, 0));
    value = rb_ary_entry(obj, 1);
    if ((RSTRING(key)->ptr)[0]  == '.' || NIL_P(value)) {
        return Qnil;
    }
    attnum = SPI_fnumber(arg->tupdesc, RSTRING(key)->ptr);
    if (attnum == SPI_ERROR_NOATTRIBUTE) {
        rb_raise(pl_ePLruby, "invalid attribute '%s'", RSTRING(key)->ptr);
    }

    PLRUBY_BEGIN(1);
    typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                  ObjectIdGetDatum(arg->tupdesc->attrs[attnum - 1]->atttypid),
                                  0, 0, 0);
    PLRUBY_END;

    if (!HeapTupleIsValid(typeTup)) {   
        rb_raise(pl_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
             RSTRING(key)->ptr,
             ObjectIdGetDatum(arg->tupdesc->attrs[attnum - 1]->atttypid));
    }
    typinput = (Oid) (((Form_pg_type) GETSTRUCT(typeTup))->typinput);
    typelem = (Oid) (((Form_pg_type) GETSTRUCT(typeTup))->typelem);

    PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 71
    ReleaseSysCache(typeTup);
#endif

    arg->modnulls[attnum - 1] = ' ';
    fmgr_info(typinput, &finfo);
    arg->modvalues[attnum - 1] = 
        pl_value_to_datum(value, &finfo, 
                          arg->tupdesc->attrs[attnum - 1]->atttypid, typelem,
                          (!VARLENA_FIXED_SIZE(arg->tupdesc->attrs[attnum-1]))
                          ? arg->tupdesc->attrs[attnum - 1]->attlen
                          : arg->tupdesc->attrs[attnum - 1]->atttypmod);
    return Qnil;
}
 

static HeapTuple
#ifdef NEW_STYLE_FUNCTION
pl_trigger_handler(PG_FUNCTION_ARGS)
#else
pl_trigger_handler(FmgrInfo *proinfo)
#endif
{
    TriggerData *trigdata;
    char internal_proname[512];
    char *stroid;
    pl_proc_desc *prodesc;
    HeapTuple procTup;
    Form_pg_proc procStruct;
    TupleDesc tupdesc;
    HeapTuple rettup;
    int i;
    int *modattrs;
    Datum *modvalues;
    char *modnulls;
    VALUE tg_new, tg_old, args, TG, c, tmp;
    int proname_len, status;
    VALUE value_proname, value_proc_desc;
    char *proc_internal_def;
    static char *argt = "new, old, args, tg";
    
#ifdef NEW_STYLE_FUNCTION
    trigdata = (TriggerData *) fcinfo->context;

    sprintf(internal_proname, "proc_%u_trigger", fcinfo->flinfo->fn_oid);
#else
    trigdata = CurrentTriggerData;
    CurrentTriggerData = NULL;

    stroid = oidout(proinfo->fn_oid);
    strcpy(internal_proname, "proc_");
    strcat(internal_proname, stroid);
    strcat(internal_proname, "_trigger");
    pfree(stroid);
#endif

    proname_len = strlen(internal_proname);
    value_proname = rb_tainted_str_new(internal_proname, proname_len);
    value_proc_desc = rb_hash_aref(PLruby_hash, value_proname);

    PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
    procTup = SearchSysCacheTuple(RUBY_PROOID,
                                  ObjectIdGetDatum(fcinfo->flinfo->fn_oid),
                                  0, 0, 0);
#else
    procTup = SearchSysCacheTuple(RUBY_PROOID,
                                  ObjectIdGetDatum(proinfo->fn_oid),
                                  0, 0, 0);
#endif
    PLRUBY_END;

    if (!HeapTupleIsValid(procTup)) {
        rb_raise(pl_ePLruby, "cache lookup from pg_proc failed");
    }

    procStruct = (Form_pg_proc) GETSTRUCT(procTup);

#if PG_PL_VERSION >= 73
    if (!NIL_P(value_proc_desc)) {
        Data_Get_Struct(value_proc_desc, pl_proc_desc, prodesc);
        if (prodesc->fn_xmin != HeapTupleHeaderGetXmin(procTup->t_data) ||
            prodesc->fn_cmin != HeapTupleHeaderGetCmin(procTup->t_data)) {
            rb_remove_method(pl_sPLtemp, internal_proname);
            value_proc_desc = Qnil;
        }
    }
#endif

    if (NIL_P(value_proc_desc)) {
        char *proc_source;
        
        value_proc_desc = Data_Make_Struct(rb_cObject, pl_proc_desc, 0, pl_proc_free, prodesc);
#if PG_PL_VERSION >= 73
        prodesc->fn_xmin = HeapTupleHeaderGetXmin(procTup->t_data);
        prodesc->fn_cmin = HeapTupleHeaderGetCmin(procTup->t_data);
#endif
        PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
        proc_source = DatumGetCString(
            DFC1(textout, PointerGetDatum(&procStruct->prosrc)));
#else
        proc_source = textout(&(procStruct->prosrc));
#endif
        proc_internal_def = ALLOCA_N(char, strlen(definition) + proname_len +
                                     strlen(argt) + strlen(proc_source) + 1);
        sprintf(proc_internal_def, definition, internal_proname, argt, proc_source);
        pfree(proc_source);
        PLRUBY_END;

        rb_eval_string_protect(proc_internal_def, &status);
        if (status) {
            VALUE s = plruby_to_s(rb_gv_get("$!"));
            rb_raise(pl_ePLruby, "cannot create internal procedure %s\n<<===%s\n===>>",
                 RSTRING(s)->ptr, proc_internal_def);
        }
        prodesc->proname = ALLOC_N(char, strlen(internal_proname) + 1);
        strcpy(prodesc->proname, internal_proname);
        rb_hash_aset(PLruby_hash, value_proname, value_proc_desc); 
    }

#if PG_PL_VERSION >= 71
        ReleaseSysCache(procTup);
#endif

    Data_Get_Struct(value_proc_desc, pl_proc_desc, prodesc);

    tupdesc = trigdata->tg_relation->rd_att;

    TG = rb_hash_new();

    rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("name")), 
                 rb_str_freeze(rb_tainted_str_new2(trigdata->tg_trigger->tgname)));

#if PG_PL_VERSION > 70
    {
        char *s;
        
        PLRUBY_BEGIN(1);
        s = DatumGetCString(
            DFC1(nameout, NameGetDatum(
                     &(trigdata->tg_relation->rd_rel->relname))));
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("relname")), 
                     rb_str_freeze(rb_tainted_str_new2(s)));
        pfree(s);
        PLRUBY_END;
    }
#else
    rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("relname")), 
                 rb_str_freeze(rb_tainted_str_new2(nameout(&(trigdata->tg_relation->rd_rel->relname)))));
#endif

    PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
    stroid = DatumGetCString(DirectFunctionCall1(oidout,
						 ObjectIdGetDatum(trigdata->tg_relation->rd_id)));
#else
    stroid = oidout(trigdata->tg_relation->rd_id);
#endif
    rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("relid")), 
                 rb_str_freeze(rb_tainted_str_new2(stroid)));
    pfree(stroid);
    PLRUBY_END;

    tmp = rb_ary_new2(tupdesc->natts);
    for (i = 0; i < tupdesc->natts; i++) {
        rb_ary_push(tmp, rb_str_freeze(rb_tainted_str_new2(tupdesc->attrs[i]->attname.data)));
    }
    rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("relatts")), rb_ary_freeze(tmp));

    if (TRIGGER_FIRED_BEFORE(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("when")), INT2FIX(TG_BEFORE)); 
    }
    else if (TRIGGER_FIRED_AFTER(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("when")), INT2FIX(TG_AFTER)); 
    }
    else {
        rb_raise(pl_ePLruby, "unknown WHEN event (%u)", trigdata->tg_event);
    }
    
    if (TRIGGER_FIRED_FOR_ROW(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("level")),INT2FIX(TG_ROW));
    }
    else if (TRIGGER_FIRED_FOR_STATEMENT(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("level")), INT2FIX(TG_STATEMENT)); 
    }
    else {
        rb_raise(pl_ePLruby, "unknown LEVEL event (%u)", trigdata->tg_event);
    }

    if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_INSERT));
        tg_new = pl_build_tuple(trigdata->tg_trigtuple, tupdesc, RET_HASH);
        tg_old = rb_hash_new();
        rettup = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_DELETE));
        tg_old = pl_build_tuple(trigdata->tg_trigtuple, tupdesc, RET_HASH);
        tg_new = rb_hash_new();

        rettup = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_UPDATE)); 
        tg_new = pl_build_tuple(trigdata->tg_newtuple, tupdesc, RET_HASH);
        tg_old = pl_build_tuple(trigdata->tg_trigtuple, tupdesc, RET_HASH);
        rettup = trigdata->tg_newtuple;
    }
    else {
        rb_raise(pl_ePLruby, "unknown OP event (%u)", trigdata->tg_event);
    }
    rb_hash_freeze(TG);

    args = rb_ary_new2(trigdata->tg_trigger->tgnargs);
    for (i = 0; i < trigdata->tg_trigger->tgnargs; i++) {
        rb_ary_push(args, rb_str_freeze(rb_tainted_str_new2(trigdata->tg_trigger->tgargs[i])));
    }
    rb_ary_freeze(args);

    c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING(value_proname)->ptr),
                   4, tg_new, tg_old, args, TG);

    PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
    MemoryContextSwitchTo(spi_context);
#endif
    if (SPI_finish() != SPI_OK_FINISH) {
        rb_raise(pl_ePLruby, "SPI_finish() failed");
    }
    PLRUBY_END;

    switch (TYPE(c)) {
    case T_TRUE:
        return rettup;
        break;
    case T_FALSE:
        return (HeapTuple) NULL;
        break;
    case T_FIXNUM:
        if (NUM2INT(c) == TG_OK) {
            return rettup;
        }
        if (NUM2INT(c) == TG_SKIP) {
            return (HeapTuple) NULL;
        }
        rb_raise(pl_ePLruby, "Invalid return code");
        break;
    case T_STRING:
        if (strcmp(RSTRING(c)->ptr, "OK") == 0) {
            return rettup;
        }
        if (strcmp(RSTRING(c)->ptr, "SKIP") == 0) {
            return (HeapTuple) NULL;
        }
        rb_raise(pl_ePLruby, "unknown response %s", RSTRING(c)->ptr);
        break;
    case T_HASH:
        break;
    default:
        rb_raise(pl_ePLruby, "Invalid return value");
        break;
    }

    modattrs = ALLOCA_N(int, tupdesc->natts);
    modvalues = ALLOCA_N(Datum, tupdesc->natts);
    for (i = 0; i < tupdesc->natts; i++) {
        modattrs[i] = i + 1;
        modvalues[i] = (Datum) NULL;
    }

    modnulls = ALLOCA_N(char, tupdesc->natts + 1);
    memset(modnulls, 'n', tupdesc->natts);
    modnulls[tupdesc->natts] = '\0';
    {
        struct foreach_fmgr *mgr;
        VALUE res;

        res = Data_Make_Struct(rb_cObject, struct foreach_fmgr, 0, free, mgr);
        mgr->tupdesc = tupdesc;
        mgr->modattrs = modattrs;
        mgr->modvalues = modvalues;
        mgr->modnulls = modnulls;
        rb_iterate(rb_each, c, for_numvals, res);
    }

    PLRUBY_BEGIN(1);
    rettup = SPI_modifytuple(trigdata->tg_relation, rettup, tupdesc->natts,
                             modattrs, modvalues, modnulls);
    PLRUBY_END;
    
    if (rettup == NULL) {
        rb_raise(pl_ePLruby, "SPI_modifytuple() failed - RC = %d\n", SPI_result);
    }

    return rettup;
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
        case NOTICE:
#ifdef DEBUG
        case DEBUG:
#endif
#ifdef DEBUG1
        case DEBUG1:
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

    if (TYPE(mes) != T_STRING) {
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

static void
exec_output(VALUE option, int compose, int *result)
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
        rb_iterate(rb_each, argv[argc - 1], pl_i_each, (VALUE)&po);
        comp = po.output;
        count = po.count;
        argc--;
    }
    switch (rb_scan_args(argc, argv, "12", &a, &b, &c)) {
    case 3:
        exec_output(c, RET_HASH, &comp);
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
            pl_build_tuple(tuples[0], tupdesc, array);
        }
        else {
            for (i = 0; i < ntuples; i++) {
                rb_yield(pl_build_tuple(tuples[i], tupdesc, array));
            }
        }
        result = Qtrue;
    }
    else {
        if (count == 1) {
            result = pl_build_tuple(tuples[0], tupdesc, array);
        }
        else {
            result = rb_ary_new2(ntuples);
            for (i = 0; i < ntuples; i++) {
                rb_ary_push(result, pl_build_tuple(tuples[i], tupdesc, array));
            }
        }
    }
    SPI_freetuptable(SPI_tuptable);
    return result;
}

static void
query_free(qdesc)
    pl_query_desc *qdesc;
{
    if (qdesc->argtypes) free(qdesc->argtypes);
    if (qdesc->arginfuncs) free(qdesc->arginfuncs);
    if (qdesc->argtypelems) free(qdesc->argtypelems);
    if (qdesc->arglen) free(qdesc->arglen);
#if PG_PL_VERSION >= 74
    if (qdesc->arg_is_array) free(qdesc->arg_is_array);
    if (qdesc->arg_val) free(qdesc->arg_val);
    if (qdesc->arg_align) free(qdesc->arg_align);
#endif
    free(qdesc);
}

static void
query_mark(qdesc)
    pl_query_desc *qdesc;
{
    rb_gc_mark(qdesc->po.argsv);
}

static VALUE
pl_plan_s_new(int argc, VALUE *argv, VALUE obj)
{
    pl_query_desc *qdesc;
    VALUE result;
    
    result = Data_Make_Struct(obj, pl_query_desc, query_mark, 
                              query_free, qdesc);
    rb_obj_call_init(result, argc, argv);
    return result;
}

static VALUE
pl_plan_prepare(int argc, VALUE *argv, VALUE obj)
{
    if (!argc || TYPE(argv[argc - 1]) != T_HASH) {
        argv[argc] = rb_hash_new();
        ++argc;
    }
    rb_hash_aset(argv[argc - 1], rb_str_new2("save"), Qtrue);
    return pl_plan_s_new(argc, argv, pl_cPLPlan);
}

static VALUE
pl_plan_save(VALUE obj)
{
    pl_query_desc *qdesc;
    void *tmp;

    GetPlan(obj, qdesc);
    PLRUBY_BEGIN(1);
    tmp = qdesc->plan;
    qdesc->plan = SPI_saveplan(tmp);
    SPI_freeplan(tmp);
    PLRUBY_END;

    if (qdesc->plan == NULL) {
        char buf[128];
        char *reason;
            
        switch (SPI_result) {
        case SPI_ERROR_ARGUMENT:
            reason = "SPI_ERROR_ARGUMENT";
            break;
        case SPI_ERROR_UNCONNECTED:
            reason = "SPI_ERROR_UNCONNECTED";
            break;
        default:
            sprintf(buf, "unknown RC %d", SPI_result);
            reason = buf;
            break;
        }
        rb_raise(pl_ePLruby, "SPI_saveplan() failed - %s", reason);
    }
    return obj;
}
 
static VALUE
pl_plan_init(int argc, VALUE *argv, VALUE obj)
{
    pl_query_desc *qdesc;
    void *plan;
    int i;
    HeapTuple typeTup;
    VALUE a, b, c, d;
    
    Data_Get_Struct(obj, pl_query_desc, qdesc);
    if (argc && TYPE(argv[argc - 1]) == T_HASH) {
        rb_iterate(rb_each, argv[argc - 1], pl_i_each, (VALUE)&(qdesc->po));
        argc--;
    }
    switch (rb_scan_args(argc, argv, "13", &a, &b, &c, &d)) {
    case 4:
        exec_output(d, RET_ARRAY, &(qdesc->po.output));
        /* ... */
    case 3:
        if (!NIL_P(c)) {
            qdesc->po.count = NUM2INT(c);
        }
        /* ... */
    case 2:
        if (!NIL_P(b)) {
            if (TYPE(b) != T_ARRAY) {
                rb_raise(pl_ePLruby, "second argument must be an ARRAY");
            }
            qdesc->po.argsv = b;
        }
        break;
    }
    if (TYPE(a) != T_STRING) {
        rb_raise(pl_ePLruby, "first argument must be a STRING");
    }
    sprintf(qdesc->qname, "%lx", (long) qdesc);
    if (RTEST(qdesc->po.argsv)) {
        if (TYPE(qdesc->po.argsv) != T_ARRAY) {
            rb_raise(pl_ePLruby, "expected an Array");
        }
        qdesc->nargs = RARRAY(qdesc->po.argsv)->len;
    }
    qdesc->argtypes = NULL;
    if (qdesc->nargs) {
        qdesc->argtypes = ALLOC_N(Oid, qdesc->nargs);
        qdesc->arginfuncs = ALLOC_N(FmgrInfo, qdesc->nargs);
        qdesc->argtypelems = ALLOC_N(Oid, qdesc->nargs);
        qdesc->arglen = ALLOC_N(int, qdesc->nargs);
#if PG_PL_VERSION >= 74
        qdesc->arg_is_array = ALLOC_N(bool, qdesc->nargs);
        qdesc->arg_val = ALLOC_N(bool, qdesc->nargs);
        qdesc->arg_align = ALLOC_N(char, qdesc->nargs);
#endif
 
        for (i = 0; i < qdesc->nargs; i++)      {
#if PG_PL_VERSION >= 73
            char *argcopy;
            List *names = NIL;
            List  *lp;
            TypeName *typename;
#endif
#if PG_PL_VERSION >= 74
            Form_pg_type fpgt;
#endif
            int arg_is_array = 0;
            VALUE args = plruby_to_s(RARRAY(qdesc->po.argsv)->ptr[i]);

            PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
            argcopy  = pstrdup(RSTRING(args)->ptr);
            SplitIdentifierString(argcopy, '.', &names);
            typename = makeNode(TypeName);
            foreach (lp, names)
                typename->names = lappend(typename->names, makeString(lfirst(lp)));
            typeTup = typenameType(typename);
            qdesc->argtypes[i] = HeapTupleGetOid(typeTup);
#if PG_PL_VERSION >= 74
            fpgt = (Form_pg_type) GETSTRUCT(typeTup);
            arg_is_array = qdesc->arg_is_array[i] = NameStr(fpgt->typname)[0] == '_';
            if (qdesc->arg_is_array[i]) {
                HeapTuple typeTuple = SearchSysCache
                    (TYPEOID,
                     ObjectIdGetDatum(fpgt->typelem), 0, 0, 0);

                if (!HeapTupleIsValid(typeTuple)) {
                    rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                             fpgt->typelem);
                }
                fpgt = (Form_pg_type) GETSTRUCT(typeTuple);
                fmgr_info(fpgt->typinput, &(qdesc->arginfuncs[i]));
                qdesc->arglen[i] = fpgt->typlen;
                qdesc->arg_val[i] = fpgt->typbyval;
                qdesc->arg_align[i] = fpgt->typalign;
                ReleaseSysCache(typeTuple);
            }
#endif
#else
            typeTup = SearchSysCacheTuple(RUBY_TYPNAME,
                                          PointerGetDatum(RSTRING(args)->ptr),
                                          0, 0, 0);
            if (!HeapTupleIsValid(typeTup)) {
                rb_raise(pl_ePLruby, "Cache lookup of type '%s' failed", RSTRING(args)->ptr);
            }
            qdesc->argtypes[i] = typeTup->t_data->t_oid;
#endif
            qdesc->argtypelems[i] = ((Form_pg_type) GETSTRUCT(typeTup))->typelem;
            if (!arg_is_array) {
                fmgr_info(((Form_pg_type) GETSTRUCT(typeTup))->typinput,
                          &(qdesc->arginfuncs[i]));
                qdesc->arglen[i] = (int) (((Form_pg_type) GETSTRUCT(typeTup))->typlen);
            }
#if PG_PL_VERSION >= 71
            ReleaseSysCache(typeTup);
#if PG_PL_VERSION >= 73
            freeList(typename->names);
            pfree(typename);
            freeList(names);
            pfree(argcopy);
#endif
#endif
            PLRUBY_END;

        }
    }

    {
        extern bool InError;
        sigjmp_buf save_restart;

        memcpy(&save_restart, &Warn_restart, sizeof(save_restart));
        if (sigsetjmp(Warn_restart, 1) == 0) {
            PLRUBY_BEGIN(1);
            plan = SPI_prepare(RSTRING(a)->ptr, qdesc->nargs, qdesc->argtypes);
            memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));
            PLRUBY_END;
        }
        else {
            memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));
            InError = 0;
            plan = NULL;
        }
    }

    if (plan == NULL) {
        char            buf[128];
        char       *reason;

        switch (SPI_result) {
        case SPI_ERROR_ARGUMENT:
            reason = "SPI_ERROR_ARGUMENT";
            break;
        case SPI_ERROR_UNCONNECTED:
            reason = "SPI_ERROR_UNCONNECTED";
            break;
        case SPI_ERROR_COPY:
            reason = "SPI_ERROR_COPY";
            break;
        case SPI_ERROR_CURSOR:
            reason = "SPI_ERROR_CURSOR";
            break;
        case SPI_ERROR_TRANSACTION:
            reason = "SPI_ERROR_TRANSACTION";
            break;
        case SPI_ERROR_OPUNKNOWN:
            reason = "SPI_ERROR_OPUNKNOWN";
            break;
        case 0:
            reason = "SPI_PARSE_ERROR";
            break;
        default:
            sprintf(buf, "unknown RC %d", SPI_result);
            reason = buf;
            break;
        }
        rb_raise(pl_ePLruby, "SPI_prepare() failed - %s\n%s",
                 reason, RSTRING(a)->ptr);
    }
    qdesc->plan = plan;
    if (qdesc->po.save) {
        pl_plan_save(obj);
    }
    return obj;
}

static void
process_args(pl_query_desc *qdesc, VALUE vortal)
{
    struct PLportal *portal;
    int callnargs, j;
    VALUE argsv;

    Data_Get_Struct(vortal, struct PLportal, portal);
    if (qdesc->nargs > 0) {
        argsv = portal->po.argsv;
        if (TYPE(argsv) != T_ARRAY) {
            rb_raise(pl_ePLruby, "array expected for arguments");
        }
        if (RARRAY(argsv)->len != qdesc->nargs) {
            rb_raise(pl_ePLruby, "length of arguments doesn't match # of arguments");
        }
        callnargs = RARRAY(argsv)->len;
        portal->nargs = callnargs;
        portal->nulls = ALLOC_N(char, callnargs + 1);
        portal->argvalues = ALLOC_N(Datum, callnargs);
        MEMZERO(portal->argvalues, Datum, callnargs);
        portal->arglen = ALLOC_N(int, callnargs);
        MEMZERO(portal->arglen, int, callnargs);
        for (j = 0; j < callnargs; j++) {
            if (NIL_P(RARRAY(argsv)->ptr[j])) {
                portal->nulls[j] = 'n';
                portal->argvalues[j] = (Datum)NULL;
            }
            else {
#if PG_PL_VERSION >= 74
                if (qdesc->arg_is_array[j]) {
                    pl_proc_desc prodesc;

                    MEMZERO(&prodesc, pl_proc_desc, 1);
                    prodesc.result_func  = qdesc->arginfuncs[j];
                    prodesc.result_oid   = qdesc->argtypes[j];
                    prodesc.result_elem  = qdesc->argtypelems[j];
                    prodesc.result_len   = qdesc->arglen[j];
                    prodesc.result_val   = qdesc->arg_val[j];
                    prodesc.result_align = qdesc->arg_align[j];

                    portal->nulls[j] = ' ';
                    portal->arglen[j] = qdesc->arglen[j];

                    portal->argvalues[j] =
                        return_array_type(RARRAY(argsv)->ptr[j], &prodesc);
                } 
                else
#endif
                {
                    VALUE args = RARRAY(argsv)->ptr[j];
                    portal->nulls[j] = ' ';
                    portal->arglen[j] = qdesc->arglen[j];
                    portal->argvalues[j] =
                        pl_value_to_datum(args, &qdesc->arginfuncs[j],
                                          qdesc->argtypes[j], 
                                          qdesc->argtypelems[j],
                                          qdesc->arglen[j]);
                }

            }
        }
        portal->nulls[callnargs] = '\0';
    }
    return;
}

static void
free_args(struct PLportal *portal)
{
    int j;

    for (j = 0; j < portal->nargs; j++) {
        if (portal->arglen[j] < 0 && 
            portal->argvalues[j] != (Datum) NULL) {
            pfree((char *) (portal->argvalues[j]));
            portal->argvalues[j] = (Datum) NULL;
        }
    }
    if (portal->argvalues) {
        free(portal->argvalues);
        portal->argvalues = 0;
    }
    if (portal->arglen) {
        free(portal->arglen);
        portal->arglen = 0;
    }
    if (portal->nulls) {
        free(portal->nulls);
        portal->nulls = 0;
    }
}

static void
portal_free(struct PLportal *portal)
{
    portal->nargs = 0;
    free_args(portal);
    free(portal);
}

static void
portal_mark(struct PLportal *portal)
{
    rb_gc_mark(portal->po.argsv);
}

static VALUE
pl_i_each(VALUE obj, struct portal_options *po)
{
    VALUE key, value;
    char *options;

    key = rb_ary_entry(obj, 0);
    value = rb_ary_entry(obj, 1);
    key = rb_obj_as_string(key);
    options = RSTRING(key)->ptr;
    if (strcmp(options, "values") == 0 ||
        strcmp(options, "types") == 0) {
        po->argsv = value;
    }
    else if (strcmp(options, "count") == 0) {
        po->count = NUM2INT(value);
    }
    else if (strcmp(options, "output") == 0) {
        exec_output(value, RET_ARRAY, &(po->output));
    }
    else if (strcmp(options, "block") == 0) {
        po->block = NUM2INT(value);
    }
    else if (strcmp(options, "save") == 0) {
        po->save = RTEST(value);
    }
    return Qnil;
}

static VALUE
create_vortal(int argc, VALUE *argv, VALUE obj)
{
    VALUE vortal, argsv, countv, c;
    struct PLportal *portal;
    pl_query_desc *qdesc;

    GetPlan(obj, qdesc);
    vortal = Data_Make_Struct(pl_cPLCursor, struct PLportal, portal_mark,
                              portal_free, portal);

    MEMCPY(&(portal->po), &(qdesc->po), struct portal_options, 1);
    portal->po.argsv = Qnil;
    if (!portal->po.output) {
        portal->po.output = RET_HASH;
    }
    if (argc && TYPE(argv[argc - 1]) == T_HASH) {
        rb_iterate(rb_each, argv[argc - 1], pl_i_each, (VALUE)&portal->po);
        argc--;
    }
    switch (rb_scan_args(argc, argv, "03", &argsv, &countv, &c)) {
    case 3:
        exec_output(c, RET_ARRAY, &(portal->po.output));
        /* ... */
    case 2:
        if (!NIL_P(countv)) {
            portal->po.count = NUM2INT(countv);
        }
        /* ... */
    case 1:
        portal->po.argsv = argsv;
    }
#if PG_PL_VERSION < 71
    portal->po.output = RET_HASH;
#endif
    process_args(qdesc, vortal);
    portal->po.argsv = 0;
    return vortal;
}

static VALUE
pl_plan_execp(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int i, spi_rc, count, typout;
    VALUE result;
    VALUE vortal;
    pl_query_desc *qdesc;
    int ntuples;
    HeapTuple *tuples = NULL;
    TupleDesc tupdesc = NULL;
    struct PLportal *portal;

    GetPlan(obj, qdesc);
    vortal = create_vortal(argc, argv, obj);
    Data_Get_Struct(vortal, struct PLportal, portal);
    PLRUBY_BEGIN(1);
    spi_rc = SPI_execp(qdesc->plan, portal->argvalues,
                       portal->nulls, portal->po.count);
    Data_Get_Struct(vortal, struct PLportal, portal);
    free_args(portal);
    PLRUBY_END;
    count = portal->po.count;
    typout = portal->po.output;

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
        if (rb_block_given_p() || count == 1) {
            return Qfalse;
        }
        else {
            return rb_ary_new2(0);
        }
    }
    tuples = SPI_tuptable->vals;
    tupdesc = SPI_tuptable->tupdesc;
    if (rb_block_given_p()) {
        if (count == 1) {
            int form = typout;
            if (!(form & RET_DESC)) {
                form |= RET_BASIC;
            }
            pl_build_tuple(tuples[0], tupdesc, form);
        }
        else {
            for (i = 0; i < ntuples; i++) {
                rb_yield(pl_build_tuple(tuples[i], tupdesc, typout));
            }
        }
        result = Qtrue;
    }
    else {
        if (count == 1) {
            result = pl_build_tuple(tuples[0], tupdesc, typout);
        }
        else {
            result = rb_ary_new2(ntuples);
            for (i = 0; i < ntuples; i++) {
                rb_ary_push(result, 
                            pl_build_tuple(tuples[i], tupdesc, typout));
            }
        }
    }
    SPI_freetuptable(SPI_tuptable);
    return result;
}

#if PG_PL_VERSION >= 72

#if PG_PL_VERSION >= 74
#define PORTAL_ACTIVE(port) ((port)->portalActive)
#else
#define PORTAL_ACTIVE(port) 0
#endif

static VALUE
pl_close(VALUE vortal)
{
    struct PLportal *portal;

    GetPortal(vortal, portal);
    PLRUBY_BEGIN(1);
    if (!PORTAL_ACTIVE(portal->portal)) {
        SPI_cursor_close(portal->portal);
    }
    portal->portal = 0;
    PLRUBY_END;
    return Qnil;
}

static VALUE
pl_fetch(VALUE vortal)
{
    struct PLportal *portal;
    HeapTuple *tuples = NULL;
    TupleDesc tupdesc = NULL;
    SPITupleTable *tuptab;
    int i, proces, pcount, block, count;

    GetPortal(vortal, portal);
    count = 0;
    block = portal->po.block + 1;
    if (portal->po.count) pcount = portal->po.count;
    else pcount = -1;
    while (count != pcount) {
        PLRUBY_BEGIN(1);
        SPI_cursor_fetch(portal->portal, true, block);
        PLRUBY_END;
        if (SPI_processed <= 0) {
            return Qnil;
        }
        proces = SPI_processed;
        tuptab = SPI_tuptable;
        tuples = tuptab->vals;
        tupdesc = tuptab->tupdesc;
        for (i = 0; i < proces && count != pcount; ++i, ++count) {
            rb_yield(pl_build_tuple(tuples[i], tupdesc, portal->po.output));
        }
        SPI_freetuptable(tuptab);
    }
    return Qnil;
}


static VALUE
pl_plan_each(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    pl_query_desc *qdesc;
    Portal pgportal;
    struct PLportal *portal;
    VALUE vortal;

    if (!rb_block_given_p()) {
        rb_raise(pl_ePLruby, "a block must be given");
    }
    GetPlan(obj, qdesc);
    vortal = create_vortal(argc, argv, obj);
    Data_Get_Struct(vortal, struct PLportal, portal);
    PLRUBY_BEGIN(1);
    pgportal = SPI_cursor_open(NULL, qdesc->plan, 
                               portal->argvalues, portal->nulls);
    Data_Get_Struct(vortal, struct PLportal, portal);
    free_args(portal);
    PLRUBY_END;
    if (pgportal == NULL) {
        rb_raise(pl_ePLruby,  "SPI_cursor_open() failed");
    }
    portal->portal = pgportal;
    rb_ensure(pl_fetch, vortal, pl_close, vortal);
    return Qnil;
}

static VALUE
pl_plan_cursor(int argc, VALUE *argv, VALUE obj)
{
    char *name = NULL;
    pl_query_desc *qdesc;
    Portal pgportal;
    struct PLportal *portal;
    VALUE vortal;

    GetPlan(obj, qdesc);
    if (argc && TYPE(argv[0]) != T_HASH) {
        if (!NIL_P(argv[0])) {
            if (TYPE(argv[0]) != T_STRING) {
                rb_raise(pl_ePLruby, "invalid cursor name");
            }
            name = RSTRING(argv[0])->ptr;
        }
        --argc; ++argv;
    }
    vortal = create_vortal(argc, argv, obj);
    Data_Get_Struct(vortal, struct PLportal, portal);
    PLRUBY_BEGIN(1);
    pgportal = SPI_cursor_open(name, qdesc->plan, 
                               portal->argvalues, portal->nulls);
    PLRUBY_END;
    if (pgportal == NULL) {
        rb_raise(pl_ePLruby,  "SPI_cursor_open() failed");
    }
    portal->portal = pgportal;
    return vortal;
}

static VALUE
pl_plan_release(VALUE obj)
{
    pl_query_desc *qdesc;
    int spi_rc;

    GetPlan(obj, qdesc);
    PLRUBY_BEGIN(1);
    spi_rc = SPI_freeplan(qdesc->plan);
    qdesc->plan = 0;
    PLRUBY_END;
    if (spi_rc) {
        rb_raise(pl_ePLruby, "SPI_freeplan() failed");
    }
    return Qnil;
}

static VALUE
pl_cursor_move(VALUE obj, VALUE a)
{
    struct PLportal *portal;
    int forward, count;

    GetPortal(obj, portal);
    count = NUM2INT(a);
    if (count) {
        if (count < 0) {
            forward = 0;
            count *= -1;
        }
        else {
            forward = 1;
        }
        PLRUBY_BEGIN(1);
        SPI_cursor_move(portal->portal, forward, count);
        PLRUBY_END;
    }
    return obj;
}

static VALUE
pl_cursor_fetch(int argc, VALUE *argv, VALUE obj)
{
    struct PLportal *portal;
    SPITupleTable *tup;
    int proces, forward, count, i;
    VALUE a, res;

    GetPortal(obj, portal);
    forward = count = 1;
    if (rb_scan_args(argc, argv, "01", &a)) {
        if (!NIL_P(a)) {
            count = NUM2INT(a);
        }
        if (count < 0) {
            forward = 0;
            count *= -1;
        }
    }
    if (!count) {
        return Qnil;
    }
    PLRUBY_BEGIN(1);
    SPI_cursor_fetch(portal->portal, forward, count);
    PLRUBY_END;
    proces = SPI_processed;
    tup = SPI_tuptable;
    if (proces <= 0) {
        return Qnil;
    }
    if (proces == 1) {
        res = pl_build_tuple(tup->vals[0], tup->tupdesc, portal->po.output);
    }
    else {
        res = rb_ary_new2(proces);
        for (i = 0; i < proces; ++i) {
            rb_ary_push(res, pl_build_tuple(tup->vals[i], tup->tupdesc, 
                                             portal->po.output));
        }
    }
    SPI_freetuptable(tup);
    return res;
}

static VALUE
cursor_i_fetch(VALUE obj)
{
    VALUE res;

    while (1) {
        res = rb_funcall2(obj, rb_intern("fetch"), 0, 0);
        if (NIL_P(res)) break;
        rb_yield(res);
    }
    return obj;
}

static VALUE
pl_cursor_each(VALUE obj)
{
    if (!rb_block_given_p()) {
        rb_raise(pl_ePLruby, "called without a block");
    }
    rb_iterate(cursor_i_fetch, obj, rb_yield, 0);
    return obj;
}

static VALUE
cursor_r_fetch(VALUE obj)
{
    VALUE res;

    while (1) {
        res = rb_funcall(obj, rb_intern("fetch"), 1, INT2NUM(-1));
        if (NIL_P(res)) break;
        rb_yield(res);
    }
    return obj;
}

static VALUE
pl_cursor_rev_each(VALUE obj)
{
    if (!rb_block_given_p()) {
        rb_raise(pl_ePLruby, "called without a block");
    }
    rb_iterate(cursor_r_fetch, obj, rb_yield, 0);
    return obj;
}

static VALUE
pl_cursor_rewind(VALUE obj)
{
    struct PLportal *portal;
    int proces = 12;

    GetPortal(obj, portal);
    while (proces) {
        PLRUBY_BEGIN(1);
        SPI_cursor_move(portal->portal, 0, 12);
        PLRUBY_END;
        proces = SPI_processed;
    }
    return obj;
}

#endif

static int pl_convert_function = 0;

static int
pl_exist_singleton()
{
    int spi_rc;

    pl_convert_function = 0;
    spi_rc = SPI_exec("select 1 from pg_class where relname = 'plruby_singleton_methods'", 1);
    SPI_freetuptable(SPI_tuptable);
    if (spi_rc != SPI_OK_SELECT || SPI_processed == 0) {
        return 0;
    }
    spi_rc = SPI_exec("select name from plruby_singleton_methods", 0);
    SPI_freetuptable(SPI_tuptable);
    if (spi_rc != SPI_OK_SELECT || SPI_processed == 0) {
        return 0;
    }
#ifdef PLRUBY_ENABLE_CONVERSION
    spi_rc = SPI_exec("select name from plruby_singleton_methods where name = '***'", 1);
    if (spi_rc == SPI_OK_SELECT && SPI_processed != 0) {
        pl_convert_function = 1;
    }
#endif
    return 1;
}

static char *recherche = 
    "select name, args, body from plruby_singleton_methods where name = '%s'";

static char *singleton = "select prosrc from pg_proc,pg_language,pg_type"
" where proname = '%s' and pg_proc.prolang = pg_language.oid"
" and pg_language.lanname = 'plruby'"
" and prorettype = pg_type.oid and typname != 'trigger'";

static char *def_singleton = "def PLtemp.%s(*args)\n%s\nend";

static VALUE
pl_each(tmp)
    VALUE *tmp;
{
    return rb_funcall2(pl_mPLtemp, (ID)tmp[0], (int)tmp[1], (VALUE *)tmp[2]);
}

static VALUE
pl_load_singleton(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int spi_rc, status;
    ID id;
    char *nom, *buff;
    int fname, fargs, fbody;
    char *name, *args, *body;
    char *sinm;
    int in_singleton = 0;

    if (argc == 0) { 
        rb_raise(rb_eArgError, "no id given");
    }
 
    id = SYM2ID(argv[0]);
    argc--; argv++;
    nom = rb_id2name(id);
    buff = ALLOCA_N(char, 1 + strlen(recherche) + strlen(nom));
    sprintf(buff, recherche, nom);

    PLRUBY_BEGIN(1);
    spi_rc = SPI_exec(buff, 0);
    PLRUBY_END;

    if (spi_rc != SPI_OK_SELECT || SPI_processed == 0) {
        SPI_freetuptable(SPI_tuptable);
        if (pl_convert_function) {
            buff = ALLOCA_N(char, 1 + strlen(singleton) + strlen(nom));
            sprintf(buff, singleton, nom);

            PLRUBY_BEGIN(1);
            spi_rc = SPI_exec(buff, 1);
            PLRUBY_END;
            if (spi_rc != SPI_OK_SELECT || SPI_processed == 0) {
                SPI_freetuptable(SPI_tuptable);
                rb_raise(rb_eNameError, 
                         "undefined method `%s' for PLtemp:Module", nom);
            }
            in_singleton = 1;
        }
        else {
            rb_raise(rb_eNameError, "undefined method `%s' for PLtemp:Module", nom);
        }
    }
    if (!in_singleton) {
        fname = SPI_fnumber(SPI_tuptable->tupdesc, "name");
        fargs = SPI_fnumber(SPI_tuptable->tupdesc, "args");
        fbody = SPI_fnumber(SPI_tuptable->tupdesc, "body");
        name = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fname);
        args = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fargs);
        body = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fbody);
        SPI_freetuptable(SPI_tuptable);
        sinm = ALLOCA_N(char, 1 + strlen(definition) + strlen(name) + 
                        strlen(args) + strlen(body));
        sprintf(sinm, definition, name, args, body);
    }
    else {
        fbody = SPI_fnumber(SPI_tuptable->tupdesc, "prosrc");
        body = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fbody);
        SPI_freetuptable(SPI_tuptable);
        sinm = ALLOCA_N(char, 1 + strlen(def_singleton) + strlen(nom) + strlen(body));
        sprintf(sinm, def_singleton, nom, body);
    }
    rb_eval_string_protect(sinm, &status);
    if (status) {
        VALUE s = plruby_to_s(rb_gv_get("$!"));
        rb_raise(pl_ePLruby, "cannot create internal procedure\n%s\n<<===%s\n===>>",
                 RSTRING(s)->ptr, sinm);
    }
    if (rb_block_given_p()) {
        VALUE tmp[3];
        tmp[0] = (VALUE)id;
        tmp[1] = (VALUE)argc;
        tmp[2] = (VALUE)argv;
        return rb_iterate(pl_each, (VALUE)tmp, rb_yield, 0);
    }
    return rb_funcall2(pl_mPLtemp, id, argc, argv);
}

static VALUE plans;

static void
pl_init_all(void)
{
    if (pl_fatal) {
        elog(ERROR, "initialization not possible");
    }
    if (!pl_firstcall) {
        return;
    }
    ruby_init();
    if (MAIN_SAFE_LEVEL < 3) {
        ruby_init_loadpath();
    }
    rb_define_global_const("NOTICE", INT2FIX(NOTICE));
#ifdef DEBUG
    rb_define_global_const("DEBUG", INT2FIX(DEBUG));
#else
#ifdef DEBUG1
    rb_define_global_const("DEBUG", INT2FIX(DEBUG1));
#endif
#endif
#ifdef DEBUG1
    rb_define_global_const("DEBUG1", INT2FIX(DEBUG1));
#endif
#ifdef WARNING
    rb_define_global_const("WARNING", INT2FIX(WARNING));
#endif
#ifdef WARN
    rb_define_global_const("WARN", INT2FIX(WARN));
#endif
#ifdef FATAL
    rb_define_global_const("FATAL", INT2FIX(FATAL));
#endif
#ifdef ERROR
    rb_define_global_const("ERROR", INT2FIX(ERROR));
#endif
#ifdef NOIND
    rb_define_global_const("NOIND", INT2FIX(NOIND));
#endif
    if (rb_const_defined_at(rb_cObject, rb_intern("PL")) ||
        rb_const_defined_at(rb_cObject, rb_intern("PLtemp"))) {
        pl_fatal = 1;
        elog(ERROR, "module already defined");
    }
    pl_mPL = rb_define_module("PL");
    rb_const_set(rb_cObject, rb_intern("PLruby"), pl_mPL);
#ifdef PLRUBY_ENABLE_CONVERSION
    id_from_datum = rb_intern("from_datum");
    id_to_datum = rb_intern("to_datum");
    plruby_classes = rb_hash_new();
    rb_global_variable(&plruby_classes);
    plruby_conversions = rb_hash_new();
    rb_global_variable(&plruby_conversions);
    pl_init_conversions();
#endif
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
    /* deprecated */
    rb_define_module_function(pl_mPL, "spi_prepare", pl_plan_prepare, -1);
    rb_define_module_function(pl_mPL, "prepare", pl_plan_prepare, -1);
    /* ... */
    pl_ePLruby = rb_define_class_under(pl_mPL, "Error", rb_eStandardError);
    pl_eCatch = rb_define_class_under(pl_mPL, "Catch", rb_eStandardError);
    pl_cPLPlan = rb_define_class_under(pl_mPL, "Plan", rb_cObject);
    rb_include_module(pl_cPLPlan, rb_mEnumerable);
    rb_const_set(rb_cObject, rb_intern("PLrubyPlan"), pl_cPLPlan);
    rb_define_singleton_method(pl_cPLPlan, "new", pl_plan_s_new, -1);
    rb_define_private_method(pl_cPLPlan, "initialize", pl_plan_init, -1);
    rb_define_method(pl_cPLPlan, "save", pl_plan_save, 0);
    rb_define_method(pl_cPLPlan, "spi_execp", pl_plan_execp, -1);
    rb_define_method(pl_cPLPlan, "execp", pl_plan_execp, -1);
    rb_define_method(pl_cPLPlan, "exec", pl_plan_execp, -1);
#if PG_PL_VERSION >= 72
    rb_define_method(pl_cPLPlan, "spi_fetch", pl_plan_each, -1);
    rb_define_method(pl_cPLPlan, "each", pl_plan_each, -1);
    rb_define_method(pl_cPLPlan, "fetch", pl_plan_each, -1);
    rb_define_method(pl_cPLPlan, "cursor", pl_plan_cursor, -1);
    rb_define_method(pl_cPLPlan, "release", pl_plan_release, 0);
    pl_cPLCursor = rb_define_class_under(pl_mPL, "Cursor", rb_cObject);
    rb_include_module(pl_cPLCursor, rb_mEnumerable);
    rb_undef_method(CLASS_OF(pl_cPLCursor), "new");
    rb_define_method(pl_cPLCursor, "each", pl_cursor_each, 0);
    rb_define_method(pl_cPLCursor, "reverse_each", pl_cursor_rev_each, 0);
    rb_define_method(pl_cPLCursor, "close", pl_close, 0);
    rb_define_method(pl_cPLCursor, "fetch", pl_cursor_fetch, -1);
    rb_define_method(pl_cPLCursor, "row", pl_cursor_fetch, -1);
    rb_define_method(pl_cPLCursor, "move", pl_cursor_move, 1);
    rb_define_method(pl_cPLCursor, "rewind", pl_cursor_rewind, 0);
#endif
    id_to_s = rb_intern("to_s");
    id_raise = rb_intern("raise");
    id_kill = rb_intern("kill");
    id_alive = rb_intern("alive?");
    id_value = rb_intern("value");
    id_call = rb_intern("call");
    id_thr = rb_intern("__functype__");
#ifdef PLRUBY_TIMEOUT
    rb_funcall(rb_thread_main(), rb_intern("priority="), 1, INT2NUM(10));
    rb_undef_method(CLASS_OF(rb_cThread), "new"); 
    rb_undef_method(CLASS_OF(rb_cThread), "start"); 
    rb_undef_method(CLASS_OF(rb_cThread), "fork"); 
    rb_undef_method(CLASS_OF(rb_cThread), "critical="); 
#endif
    pl_mPLtemp = rb_define_module("PLtemp");
    pl_sPLtemp = rb_singleton_class(pl_mPLtemp);
    if (MAIN_SAFE_LEVEL >= 3) {
        rb_obj_taint(pl_mPLtemp);
        rb_obj_taint(pl_sPLtemp);
    }
    rb_set_safe_level(MAIN_SAFE_LEVEL);
    plans = rb_hash_new();
    rb_define_variable("$Plans", &plans);
    PLruby_hash = rb_hash_new();
    rb_global_variable(&PLruby_hash);
#if PG_PL_VERSION >= 73
    PLcontext = rb_hash_new();
    rb_global_variable(&PLcontext);
#endif
#ifdef PLRUBY_HASH_DELETE
    id_delete = rb_intern("delete");
#endif
    if (SPI_connect() != SPI_OK_CONNECT) {
        pl_fatal = 1;
        elog(ERROR, "plruby_singleton_methods : SPI_connect failed");
    }
    if (pl_exist_singleton()) {
        rb_define_module_function(pl_mPLtemp, "method_missing", pl_load_singleton, -1);
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        pl_fatal = 1;
        elog(ERROR, "plruby_singleton_methods : SPI_finish failed");
    }
    pl_firstcall = 0;
    return;
}
