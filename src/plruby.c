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

#ifdef NEW_STYLE_FUNCTION

#if PG_PL_VERSION >= 71
PG_FUNCTION_INFO_V1(PLRUBY_CALL_HANDLER);
#else
Datum PLRUBY_CALL_HANDLER(PG_FUNCTION_ARGS);
#endif

#else

Datum plruby_call_handler(FmgrInfo *, FmgrValues *, bool *);

#endif

static Datum pl_func_handler(struct pl_thread_st *);
static HeapTuple pl_trigger_handler(struct pl_thread_st *);

#ifdef PLRUBY_TIMEOUT
static int pl_in_progress = 0;
static int pl_interrupted = 0;
#endif

static ID id_to_s, id_raise, id_kill, id_alive, id_value, id_call, id_thr;

static int      pl_firstcall = 1;
static int      pl_call_level = 0;
static VALUE    pl_ePLruby, pl_eCatch;
static VALUE    pl_mPLtemp, pl_sPLtemp;
static VALUE    PLruby_hash;
int      plruby_fatal = 0;

VALUE
plruby_s_new(int argc, VALUE *argv, VALUE obj)
{
    VALUE res = rb_funcall2(obj, rb_intern("allocate"), 0, 0);
    rb_obj_call_init(res, argc, argv);
    return res;
}

static void
pl_proc_free(proc)
    pl_proc_desc *proc;
{
    if (proc->proname) {
        free(proc->proname);
    }
    free(proc);
}

#define GetProcDesc(value_, procdesc_) do {                     \
    if (TYPE(value_) != T_DATA ||                               \
        RDATA(value_)->dfree != (RUBY_DATA_FUNC)pl_proc_free) { \
        rb_raise(pl_ePLruby, "expected a proc object");         \
    }                                                           \
    Data_Get_Struct(value_, pl_proc_desc, procdesc_);           \
} while (0)

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
    if (CALLED_AS_TRIGGER(plth->fcinfo)) {
        retval = PointerGetDatum(pl_trigger_handler(plth));
    }
    else {
        retval = pl_func_handler(plth);
    }
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

static void pl_init_all _(());

#if PG_PL_VERSION >= 73
MemoryContext plruby_spi_context;
#endif

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
    plruby_spi_context =  MemoryContextSwitchTo(orig_context);
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

static char *definition = "def PLtemp.%s(%s)\n%s\nend";

static Datum
pl_func_handler(struct pl_thread_st *plth)
{
    int i;
    char internal_proname[512];
    int proname_len;
    HeapTuple procTup;
    Form_pg_proc procStruct;
    pl_proc_desc *prodesc;
    VALUE value_proc_desc;
    VALUE value_proname;
    VALUE ary;
    static char *argf = "args";
    Oid result_oid, arg_type[RUBY_ARGS_MAXFMGR];
    int nargs;
#ifdef NEW_STYLE_FUNCTION
    PG_FUNCTION_ARGS;
#else
    char *stroid;
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
    
#ifdef NEW_STYLE_FUNCTION
    fcinfo = plth->fcinfo;
    sprintf(internal_proname, "proc_%u", fcinfo->flinfo->fn_oid);
#else
    proinfo = plth->proinfo;
    proargs = plth->proargs;
    isNull = plth->isNull;
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

        GetProcDesc(value_proc_desc, prodesc);
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
            typeTuple = SearchSysCacheTuple(RUBY_TYPOID,
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
        ReleaseSysCache(typeTup);

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
                ReleaseSysCache(typeTup);
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
                    
                typeTuple = SearchSysCacheTuple(RUBY_TYPOID,
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
            ReleaseSysCache(typeTup);
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
    ReleaseSysCache(procTup);

    GetProcDesc(value_proc_desc, prodesc);
    ary = plruby_create_args(plth, prodesc);
    return plruby_return_value(plth, prodesc, value_proname, ary);
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
    int attnum;
    HeapTuple   typeTup;
    FmgrInfo    finfo;
    Form_pg_type fpg;
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
    attnum -= 1;

    PLRUBY_BEGIN(1);
    typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                  ObjectIdGetDatum(arg->tupdesc->attrs[attnum]->atttypid),
                                  0, 0, 0);
    PLRUBY_END;

    if (!HeapTupleIsValid(typeTup)) {   
        rb_raise(pl_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
             RSTRING(key)->ptr,
             ObjectIdGetDatum(arg->tupdesc->attrs[attnum]->atttypid));
    }
    fpg = (Form_pg_type) GETSTRUCT(typeTup);
    PLRUBY_BEGIN(1);
    ReleaseSysCache(typeTup);
    arg->modnulls[attnum] = ' ';
    fmgr_info(fpg->typinput, &finfo);
    PLRUBY_END;
#if PG_PL_VERSION >= 74
    if (fpg->typelem != 0 && fpg->typlen == -1) {
        pl_proc_desc prodesc;

        MEMZERO(&prodesc, pl_proc_desc, 1);
        prodesc.result_oid = fpg->typelem;
        PLRUBY_BEGIN(1);
        typeTup = SearchSysCacheTuple(RUBY_TYPOID,
                                      ObjectIdGetDatum(prodesc.result_oid),
                                      0, 0, 0);
        if (!HeapTupleIsValid(typeTup)) {
            rb_raise(pl_ePLruby, "cache lookup failed for type %u",
                     prodesc.result_elem);
        }
        fpg = (Form_pg_type) GETSTRUCT(typeTup);
        fmgr_info(fpg->typinput, &finfo);
        prodesc.result_func = finfo;
        prodesc.result_elem = prodesc.result_oid;
        prodesc.result_val = fpg->typbyval;
        prodesc.result_len = fpg->typlen;
        prodesc.result_align = fpg->typalign;
        ReleaseSysCache(typeTup);
        PLRUBY_END;
        arg->modvalues[attnum] = plruby_return_array(value, &prodesc);
    }
    else
#endif
    {
        arg->modvalues[attnum] = 
            plruby_to_datum(value, &finfo, 
                            arg->tupdesc->attrs[attnum]->atttypid, 
                            fpg->typelem,
                            (!VARLENA_FIXED_SIZE(arg->tupdesc->attrs[attnum]))
                            ? arg->tupdesc->attrs[attnum]->attlen
                            : arg->tupdesc->attrs[attnum]->atttypmod);
    }
    return Qnil;
}
 

#define rb_str_freeze_new2(a) rb_str_freeze(rb_tainted_str_new2(a))

static HeapTuple
pl_trigger_handler(struct pl_thread_st *plth)
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
    PG_FUNCTION_ARGS;

    fcinfo = plth->fcinfo;
#else
    FmgrInfo *proinfo;

    proinfo = plth->proinfo;
#endif

    
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
        GetProcDesc(value_proc_desc, prodesc);
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
    ReleaseSysCache(procTup);

    GetProcDesc(value_proc_desc, prodesc);
    tupdesc = trigdata->tg_relation->rd_att;
    TG = rb_hash_new();

    rb_hash_aset(TG, rb_str_freeze_new2("name"), 
                 rb_str_freeze_new2(trigdata->tg_trigger->tgname));

#if PG_PL_VERSION > 70
    {
        char *s;
        
        PLRUBY_BEGIN(1);
        s = DatumGetCString(
            DFC1(nameout, NameGetDatum(
                     &(trigdata->tg_relation->rd_rel->relname))));
        rb_hash_aset(TG, rb_str_freeze_new2("relname"),rb_str_freeze_new2(s));
        pfree(s);
        PLRUBY_END;
    }
#else
    rb_hash_aset(TG, rb_str_freeze_new2("relname"), 
                 rb_str_freeze_new2(nameout(&(trigdata->tg_relation->rd_rel->relname))));
#endif

    PLRUBY_BEGIN(1);
    stroid = DatumGetCString(DFC1(oidout,
                                  ObjectIdGetDatum(trigdata->tg_relation->rd_id)));
    rb_hash_aset(TG, rb_str_freeze_new2("relid"), rb_str_freeze_new2(stroid));
    pfree(stroid);
    PLRUBY_END;

    tmp = rb_ary_new2(tupdesc->natts);
    for (i = 0; i < tupdesc->natts; i++) {
        rb_ary_push(tmp, rb_str_freeze_new2(tupdesc->attrs[i]->attname.data));
    }
    rb_hash_aset(TG, rb_str_freeze_new2("relatts"), rb_ary_freeze(tmp));

    if (TRIGGER_FIRED_BEFORE(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("when"), INT2FIX(TG_BEFORE)); 
    }
    else if (TRIGGER_FIRED_AFTER(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("when"), INT2FIX(TG_AFTER)); 
    }
    else {
        rb_raise(pl_ePLruby, "unknown WHEN event (%u)", trigdata->tg_event);
    }
    
    if (TRIGGER_FIRED_FOR_ROW(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("level"),INT2FIX(TG_ROW));
    }
    else if (TRIGGER_FIRED_FOR_STATEMENT(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("level"), INT2FIX(TG_STATEMENT)); 
    }
    else {
        rb_raise(pl_ePLruby, "unknown LEVEL event (%u)", trigdata->tg_event);
    }

    if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("op"), INT2FIX(TG_INSERT));
        tg_new = plruby_build_tuple(trigdata->tg_trigtuple, tupdesc, RET_HASH);
        tg_old = rb_hash_new();
        rettup = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("op"), INT2FIX(TG_DELETE));
        tg_old = plruby_build_tuple(trigdata->tg_trigtuple, tupdesc, RET_HASH);
        tg_new = rb_hash_new();

        rettup = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event)) {
        rb_hash_aset(TG, rb_str_freeze_new2("op"), INT2FIX(TG_UPDATE)); 
        tg_new = plruby_build_tuple(trigdata->tg_newtuple, tupdesc, RET_HASH);
        tg_old = plruby_build_tuple(trigdata->tg_trigtuple, tupdesc, RET_HASH);
        rettup = trigdata->tg_newtuple;
    }
    else {
        rb_raise(pl_ePLruby, "unknown OP event (%u)", trigdata->tg_event);
    }
    rb_hash_freeze(TG);

    args = rb_ary_new2(trigdata->tg_trigger->tgnargs);
    for (i = 0; i < trigdata->tg_trigger->tgnargs; i++) {
        rb_ary_push(args, rb_str_freeze_new2(trigdata->tg_trigger->tgargs[i]));
    }
    rb_ary_freeze(args);

    c = rb_funcall(pl_mPLtemp, rb_intern(RSTRING(value_proname)->ptr),
                   4, tg_new, tg_old, args, TG);

    PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
    MemoryContextSwitchTo(plruby_spi_context);
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
        c = plruby_to_s(c);
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

extern void Init_plruby_pl();

static void
pl_init_all(void)
{
    VALUE pl_mPL;

    if (plruby_fatal) {
        elog(ERROR, "initialization not possible");
    }
    if (!pl_firstcall) {
        return;
    }
    plruby_fatal = 1;
    ruby_init();
#if defined(PLRUBY_ENABLE_AUTOLOAD) && MAIN_SAFE_LEVEL >= 3
    {
        VALUE path = rb_gv_get("$LOAD_PATH");
        rb_ary_push(path, rb_str_new2(PLRUBY_ENABLE_AUTOLOAD));
    }
#else
    if (MAIN_SAFE_LEVEL < 3) {
        ruby_init_loadpath();
    }
#endif
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
#ifdef DEBUG2
    rb_define_global_const("DEBUG2", INT2FIX(DEBUG2));
#endif
#ifdef DEBUG3
    rb_define_global_const("DEBUG3", INT2FIX(DEBUG3));
#endif
#ifdef DEBUG4
    rb_define_global_const("DEBUG4", INT2FIX(DEBUG4));
#endif
#ifdef DEBUG5
    rb_define_global_const("DEBUG5", INT2FIX(DEBUG5));
#endif
#ifdef INFO
    rb_define_global_const("INFO", INT2FIX(INFO));
#endif
#ifdef NOTICE
    rb_define_global_const("NOTICE", INT2FIX(NOTICE));
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
        elog(ERROR, "module already defined");
    }
    id_to_s = rb_intern("to_s");
    Init_plruby_pl();
    pl_mPL = rb_const_get(rb_cObject, rb_intern("PL"));
    pl_ePLruby = rb_const_get(pl_mPL, rb_intern("Error"));
    pl_eCatch = rb_const_get(pl_mPL, rb_intern("Catch"));
    pl_mPLtemp = rb_const_get(rb_cObject, rb_intern("PLtemp"));
    pl_sPLtemp = rb_singleton_class(pl_mPLtemp);
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
    rb_set_safe_level(MAIN_SAFE_LEVEL);
    PLruby_hash = rb_hash_new();
    rb_global_variable(&PLruby_hash);
    plans = rb_hash_new();
    rb_define_variable("$Plans", &plans);
    if (SPI_connect() != SPI_OK_CONNECT) {
        elog(ERROR, "plruby_singleton_methods : SPI_connect failed");
    }
    if (pl_exist_singleton()) {
        rb_define_module_function(pl_mPLtemp, "method_missing", pl_load_singleton, -1);
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        elog(ERROR, "plruby_singleton_methods : SPI_finish failed");
    }
    plruby_fatal = pl_firstcall = 0;
    return;
}
