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
 * pltcl.c		- PostgreSQL support for Tcl as
 *			  procedural language (PL)
 *
 * IDENTIFICATION
 *	  $Header: /usr/local/cvsroot/pgsql/src/pl/tcl/pltcl.c,v 1.12 1999/05/26 12:57:23 momjian Exp $
 *
 *	  This software is copyrighted by Jan Wieck - Hamburg.
 *
 *	  The author hereby grants permission  to  use,  copy,	modify,
 *	  distribute,  and	license this software and its documentation
 *	  for any purpose, provided that existing copyright notices are
 *	  retained	in	all  copies  and  that	this notice is included
 *	  verbatim in any distributions. No written agreement, license,
 *	  or  royalty  fee	is required for any of the authorized uses.
 *	  Modifications to this software may be  copyrighted  by  their
 *	  author  and  need  not  follow  the licensing terms described
 *	  here, provided that the new terms are  clearly  indicated  on
 *	  the first page of each file where they apply.
 *
 *	  IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTORS BE LIABLE TO ANY
 *	  PARTY  FOR  DIRECT,	INDIRECT,	SPECIAL,   INCIDENTAL,	 OR
 *	  CONSEQUENTIAL   DAMAGES  ARISING	OUT  OF  THE  USE  OF  THIS
 *	  SOFTWARE, ITS DOCUMENTATION, OR ANY DERIVATIVES THEREOF, EVEN
 *	  IF  THE  AUTHOR  HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH
 *	  DAMAGE.
 *
 *	  THE  AUTHOR  AND	DISTRIBUTORS  SPECIFICALLY	 DISCLAIM	ANY
 *	  WARRANTIES,  INCLUDING,  BUT	NOT  LIMITED  TO,  THE	IMPLIED
 *	  WARRANTIES  OF  MERCHANTABILITY,	FITNESS  FOR  A  PARTICULAR
 *	  PURPOSE,	AND NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON
 *	  AN "AS IS" BASIS, AND THE AUTHOR	AND  DISTRIBUTORS  HAVE  NO
 *	  OBLIGATION   TO	PROVIDE   MAINTENANCE,	 SUPPORT,  UPDATES,
 *	  ENHANCEMENTS, OR MODIFICATIONS.
 *
 **********************************************************************/

#ifndef SAFE_LEVEL
#define SAFE_LEVEL 12
#endif

#ifndef MAIN_SAFE_LEVEL
#ifdef PLRUBY_TIMEOUT
#define MAIN_SAFE_LEVEL 3
#else
#define MAIN_SAFE_LEVEL SAFE_LEVEL
#endif
#endif

#if SAFE_LEVEL <= MAIN_SAFE_LEVEL
#define MAIN_SAFE_LEVEL SAFE_LEVEL
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <setjmp.h>

#include "executor/spi.h"
#include "commands/trigger.h"
#include "utils/elog.h"
#include "utils/builtins.h"
#include "fmgr.h"
#include "access/heapam.h"

#include "tcop/tcopprot.h"
#include "utils/syscache.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_language.h"
#include "catalog/pg_type.h"

#if PG_PL_VERSION >= 73
#include "nodes/makefuncs.h"
#include "parser/parse_type.h"
#endif


#ifndef MAXFMGRARGS
#define RUBY_ARGS_MAXFMGR FUNC_MAX_ARGS
#define RUBY_TYPOID TYPEOID
#define RUBY_PROOID PROCOID
#define RUBY_TYPNAME TYPENAME
#else
#define RUBY_ARGS_MAXFMGR MAXFMGRARGS
#define RUBY_TYPOID TYPOID
#define RUBY_PROOID PROOID
#define RUBY_TYPNAME TYPNAME
#endif

#ifdef PG_FUNCTION_ARGS
#define NEW_STYLE_FUNCTION
#endif

#if PG_PL_VERSION >= 71
#define SearchSysCacheTuple SearchSysCache
#endif

#include <ruby.h>

static int plr_in_progress = 0;

#ifdef PLRUBY_TIMEOUT

static int plr_interrupted = 0;

#define PLRUBY_BEGIN(lvl)			\
    do {					\
        int in_progress = plr_in_progress;	\
         if (plr_interrupted) {			\
	    rb_raise(pg_ePLruby, "timeout");	\
        }					\
        plr_in_progress = lvl

#define PLRUBY_END				\
        plr_in_progress = in_progress;		\
        if (plr_interrupted) {			\
	    rb_raise(pg_ePLruby, "timeout");	\
        }					\
    } while (0)

#else
#define PLRUBY_BEGIN(lvl)
#define PLRUBY_END
#endif

enum { TG_OK, TG_SKIP };
enum { TG_BEFORE, TG_AFTER, TG_ROW, TG_STATEMENT, TG_INSERT,
       TG_DELETE, TG_UPDATE, TG_UNKNOWN }; 

static ID id_to_s, id_raise, id_kill, id_alive, id_value;

struct plr_thread_st {
#ifdef NEW_STYLE_FUNCTION
    PG_FUNCTION_ARGS;
#else
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
    int timeout;
};

typedef struct plr_proc_desc
{
    char	   *proname;
    FmgrInfo	result_in_func;
    Oid			result_in_elem;
    int			result_in_len;
    int			nargs;
    FmgrInfo	arg_out_func[RUBY_ARGS_MAXFMGR];
    Oid			arg_out_elem[RUBY_ARGS_MAXFMGR];
    int			arg_out_len[RUBY_ARGS_MAXFMGR];
    int			arg_is_rel[RUBY_ARGS_MAXFMGR];
} plr_proc_desc;

static void
plr_proc_free(proc)
    plr_proc_desc *proc;
{
    if (proc->proname)
	free(proc->proname);
    free(proc);
}

typedef struct plr_query_desc
{
    char qname[20];
    void *plan;
    int	 nargs;
    Oid	*argtypes;
    FmgrInfo   *arginfuncs;
    Oid	 *argtypelems;
    int	*arglen;
} plr_query_desc;

struct PLportal {
    Portal portal;
    VALUE argsv;
    VALUE array;
    char *nulls;
    Datum *argvalues;
    int *arglen;
    int count, nargs;
};

static int	plr_firstcall = 1;
static int	plr_call_level = 0;
static VALUE    pg_mPLruby, pg_mPLtemp, pg_cPLrubyPlan;
static VALUE    pg_ePLruby, pg_eCatch;
static VALUE    PLruby_hash, PLruby_portal;

static char *definition = "
def PLtemp.%s(%s)
    %s
end
";

static void plr_init_all(void);

#ifdef NEW_STYLE_FUNCTION
#if PG_PL_VERSION >= 71
PG_FUNCTION_INFO_V1(plruby_call_handler);
#else
Datum plruby_call_handler(PG_FUNCTION_ARGS);
#endif
static Datum plr_func_handler(PG_FUNCTION_ARGS);
static HeapTuple plr_trigger_handler(PG_FUNCTION_ARGS);
#else
Datum plruby_call_handler(FmgrInfo *, FmgrValues *, bool *);

static Datum plr_func_handler(FmgrInfo *, FmgrValues *, bool *);

static HeapTuple plr_trigger_handler(FmgrInfo *);
#endif

static VALUE plr_build_tuple(HeapTuple, TupleDesc, int, int);

#if PG_PL_VERSION >= 73
static void
perm_fmgr_info(Oid functionId, FmgrInfo *finfo)
{
	fmgr_info_cxt(functionId, finfo, TopMemoryContext);
}

#define fmgr_info perm_fmgr_info

#endif

static VALUE
plr_protect(args)
    VALUE *args;
{
    Datum retval;

    if (sigsetjmp(Warn_restart, 1) != 0) {
	return pg_eCatch;
    }
#ifdef NEW_STYLE_FUNCTION
    if (CALLED_AS_TRIGGER((FunctionCallInfo)args)) {
	retval = PointerGetDatum(plr_trigger_handler((FunctionCallInfo)args));
    }
    else {
	retval = plr_func_handler((FunctionCallInfo)args);
    }
#else
    if (CurrentTriggerData == NULL) {
	retval = plr_func_handler((FmgrInfo *)args[0], (FmgrValues *)args[1],
				  (bool *)args[2]);
    }
    else {
	retval = (Datum) plr_trigger_handler((FmgrInfo *)args[0]);
    }
#endif
    return Data_Wrap_Struct(rb_cObject, 0, 0, (void *)retval);
}

#ifdef PLRUBY_TIMEOUT

static VALUE
plr_thread_raise(VALUE th)
{
    VALUE exc = rb_exc_new2(pg_ePLruby, "timeout");
    return rb_funcall(th, id_raise, 1, exc);
}

static VALUE
plr_thread_kill(VALUE th)
{
    return rb_funcall2(th, id_kill, 0, 0);
}
extern VALUE rb_thread_list();

static VALUE
plr_timer(VALUE th)
{
    struct timeval time;

    rb_thread_sleep(PLRUBY_TIMEOUT);
    plr_interrupted = 1;
    if (!plr_in_progress) {
	rb_protect(plr_thread_raise, th, 0);
    }
    time.tv_sec = 0;
    time.tv_usec = 50000;
    while (1) {
	if (!RTEST(rb_funcall2(th, id_alive, 0, 0))) {
	    return Qnil;
	}
	if (!plr_in_progress) {
	    rb_protect(plr_thread_kill, th, 0);
	}
	rb_thread_wait_for(time);
    }
    return Qnil;
}

static VALUE
plr_thread_value(VALUE th)
{
    return rb_funcall2(th, id_value, 0, 0);
}

#endif

static void free_args(struct PLportal *);

static VALUE
plr_error(VALUE v)
{
    VALUE result;

    result = rb_gv_get("$!");
    if (rb_obj_is_kind_of(result, pg_eCatch)) {
	result = pg_eCatch;
    }
    else if (rb_obj_is_kind_of(result, rb_eException)) {
	result = rb_funcall2(result, id_to_s, 0, 0);
    }
    return result;
}

static VALUE
plr_real_handler(struct plr_thread_st *plst)
{
    VALUE *args, result;
    int state;

#ifdef NEW_STYLE_FUNCTION
    args = (VALUE *)plst->fcinfo;
#else
    args = ALLOCA_N(VALUE, 3);
    args[0] = (VALUE)plst->proinfo;
    args[1] = (VALUE)plst->proargs;
    args[2] = (VALUE)plst->isNull;
#endif

#ifdef PLRUBY_TIMEOUT
    if (plst->timeout) {
	VALUE curr = rb_thread_current();
	rb_thread_create(plr_timer, (void *)curr);
	rb_funcall(curr, rb_intern("priority="), 1, INT2NUM(0));
	rb_set_safe_level(SAFE_LEVEL);
    }
#endif

    state = 0;
    plr_call_level++;
    result = rb_protect(plr_protect, (VALUE)args, &state);
    plr_call_level--;
    if (state) {
	state = 0;
	result = rb_protect(plr_error, 0, &state);
	if (state || (result != pg_eCatch && TYPE(result) != T_STRING)) {
	    result = rb_str_new2("Unknown Error");
	}
    }
    return result;
}

Datum
#ifdef NEW_STYLE_FUNCTION
plruby_call_handler(PG_FUNCTION_ARGS)
#else
plruby_call_handler(FmgrInfo *proinfo,
		    FmgrValues *proargs,
		    bool *isNull)
#endif
{
    VALUE result;
    sigjmp_buf save_restart;
    int portal_len;
    struct plr_thread_st plth;

    if (plr_firstcall) {
	plr_init_all();
    }

    if (SPI_connect() != SPI_OK_CONNECT) {
	if (plr_call_level) {
	    rb_raise(pg_ePLruby, "cannot connect to SPI manager");
	}
	else {
	    elog(ERROR, "cannot connect to SPI manager");
	}
    }

#ifdef NEW_STYLE_FUNCTION
    plth.fcinfo = fcinfo;
#else
    plth.proinfo = proinfo;
    plth.proargs = proargs;
    plth.isNull = isNull;
#endif
    plth.timeout = 0;

    portal_len = RARRAY(PLruby_portal)->len;
    memcpy(&save_restart, &Warn_restart, sizeof(save_restart));
#ifdef PLRUBY_TIMEOUT
    if (!plr_call_level) {
	VALUE th;
	int state;

	plr_interrupted = plr_in_progress = 0;
	plth.timeout = 1;
	th = rb_thread_create(plr_real_handler, &plth);
	result = rb_protect(plr_thread_value, th, &state);
	if (state) {
	    result = rb_str_new2("Unknown error");
	}
    }
    else 
#endif
    {
	PLRUBY_BEGIN(0);
	result = plr_real_handler(&plth);
	PLRUBY_END;
    }
    memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));

    {
	int in_progress = plr_in_progress;
	plr_in_progress = 1;
	if (portal_len != RARRAY(PLruby_portal)->len) {
	    int i;
	    VALUE vortal;
	    struct PLportal *portal;
	    
	    for (i = RARRAY(PLruby_portal)->len; i > portal_len; --i) {
		vortal = rb_ary_pop(PLruby_portal);
		Data_Get_Struct(vortal, struct PLportal, portal);
		free_args(portal);
	    }
	}

#ifdef PLRUBY_TIMEOUT
	if (!plr_call_level) {
	    int i;
	    VALUE thread, threads;
	    VALUE main_th = rb_thread_main();

	    while (1) {
		threads = rb_thread_list();
		if (RARRAY(threads)->len <= 1) break;
		for (i = 0; i < RARRAY(threads)->len; i++) {
		    thread = RARRAY(threads)->ptr[i];
		    if (thread != main_th) {
			rb_protect(plr_thread_kill, thread, 0);
		    }
		}
	    }
	}
#endif
	plr_in_progress = in_progress;
    }

    if (result == pg_eCatch) {
	if (plr_call_level) {
	    rb_raise(pg_eCatch, "SPI ERROR");
	}
	else {
	    siglongjmp(Warn_restart, 1);
	}
    }
    if (TYPE(result) == T_STRING) {
	if (plr_call_level) {
	    rb_raise(pg_ePLruby, "%.*s", 
		     (int)RSTRING(result)->len, RSTRING(result)->ptr);
	}
	else {
	    elog(ERROR, "%.*s", 
		 (int)RSTRING(result)->len, RSTRING(result)->ptr);
	}
    }
    if (TYPE(result) != T_DATA) {
	if (plr_call_level) {
	    rb_raise(pg_ePLruby, "Invalid return value %d", TYPE(result));
	}
	else {
	    elog(ERROR, "Invalid return value %d", TYPE(result));
	}
    }
    return ((Datum)DATA_PTR(result));
}

static Datum
#ifdef NEW_STYLE_FUNCTION
plr_func_handler(PG_FUNCTION_ARGS)
#else
plr_func_handler(proinfo, proargs, isNull)
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
{
    int			i;
    char		internal_proname[512];
    int		proname_len;
#ifndef NEW_STYLE_FUNCTION
    char	   *stroid;
#endif
    plr_proc_desc *prodesc;
    VALUE value_proc_desc;
    Datum    retval;
    VALUE value_proname;
    VALUE rubyret;
    VALUE ary, c;
    static char *argf = "args";
    
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
    if ((value_proc_desc = rb_hash_aref(PLruby_hash, value_proname)) == Qnil) {
	HeapTuple	procTup;
	HeapTuple	typeTup;
	Form_pg_proc procStruct;
	Form_pg_type typeStruct;
	char		proc_internal_args[4096];
	char	   *proc_source;
	char *proc_internal_def;
	int status;

	value_proc_desc = Data_Make_Struct(rb_cObject, plr_proc_desc, 0, plr_proc_free, prodesc);
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
	if (!HeapTupleIsValid(procTup))	{
	    rb_raise(pg_ePLruby, "cache lookup from pg_proc failed");
	}
	procStruct = (Form_pg_proc) GETSTRUCT(procTup);
	
	PLRUBY_BEGIN(1);
	typeTup = SearchSysCacheTuple(RUBY_TYPOID,
				      ObjectIdGetDatum(procStruct->prorettype),
				      0, 0, 0);
	PLRUBY_END;
	if (!HeapTupleIsValid(typeTup))	{
	    rb_raise(pg_ePLruby, "cache lookup for return type failed");
	}
	typeStruct = (Form_pg_type) GETSTRUCT(typeTup);

#if PG_PL_VERSION >= 73
	if (typeStruct->typtype == 'p' && 
	    procStruct->prorettype != VOIDOID) {
	    rb_raise(pg_ePLruby,  "functions cannot return type %s",
		     format_type_be(procStruct->prorettype));
	}
#endif

	if (typeStruct->typrelid != InvalidOid)	{
	    rb_raise(pg_ePLruby, "return types of tuples not supported yet");
	}

	PLRUBY_BEGIN(1);
	fmgr_info(typeStruct->typinput, &(prodesc->result_in_func));
	prodesc->result_in_elem = (Oid) (typeStruct->typelem);
#if PG_PL_VERSION >= 71
	ReleaseSysCache(typeTup);
#endif
	PLRUBY_END;

	prodesc->result_in_len = typeStruct->typlen;
	prodesc->nargs = procStruct->pronargs;
	proc_internal_args[0] = '\0';
	for (i = 0; i < prodesc->nargs; i++)	{

	    PLRUBY_BEGIN(1);
	    typeTup = SearchSysCacheTuple(RUBY_TYPOID,
					  ObjectIdGetDatum(procStruct->proargtypes[i]),
					  0, 0, 0);
	    PLRUBY_END;

	    if (!HeapTupleIsValid(typeTup)) {
		rb_raise(pg_ePLruby, "cache lookup for argument type failed");
	    }
	    typeStruct = (Form_pg_type) GETSTRUCT(typeTup);

#if PG_PL_VERSION >= 73
	    if (typeStruct->typtype == 'p') { 
		rb_raise(pg_ePLruby, "argument can't have the type %s",
			 format_type_be(procStruct->proargtypes[i]));
	    }
#endif

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
	    fmgr_info(typeStruct->typoutput, &(prodesc->arg_out_func[i]));
	    prodesc->arg_out_elem[i] = (Oid) (typeStruct->typelem);
	    prodesc->arg_out_len[i] = typeStruct->typlen;
#if PG_PL_VERSION >= 71
	    ReleaseSysCache(typeTup);
#endif
	    PLRUBY_END;
	}

	PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
	proc_source = DatumGetCString(DirectFunctionCall1(textout,
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
	    VALUE s = rb_funcall(rb_gv_get("$!"), id_to_s, 0);
	    rb_raise(pg_ePLruby, "cannot create internal procedure\n%s\n<<===%s\n===>>",
		 RSTRING(s)->ptr, proc_internal_def);
	}
	
	prodesc->proname = malloc(strlen(internal_proname) + 1);
	strcpy(prodesc->proname, internal_proname);
	rb_hash_aset(PLruby_hash, value_proname, value_proc_desc); 
#if PG_PL_VERSION >= 71
	ReleaseSysCache(procTup);
#endif
    }

    Data_Get_Struct(value_proc_desc, plr_proc_desc, prodesc);

    ary = rb_ary_new2(prodesc->nargs);
    for (i = 0; i < prodesc->nargs; i++) {
	if (prodesc->arg_is_rel[i]) {
#ifdef NEW_STYLE_FUNCTION
	    TupleTableSlot *slot = (TupleTableSlot *) fcinfo->arg[i];
#else
	    TupleTableSlot *slot = (TupleTableSlot *) proargs->data[i];
#endif
	    rb_ary_push(ary, plr_build_tuple(slot->val,
						slot->ttc_tupleDescriptor, 
						0, Qnil));
	} 
	else {
#ifdef NEW_STYLE_FUNCTION
	    if (fcinfo->argnull[i]) {
		rb_ary_push(ary, Qnil);
 	    }
	    else
#endif
	    {
		char *tmp;

		PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
		tmp = DatumGetCString(FunctionCall3(&prodesc->arg_out_func[i],
						    fcinfo->arg[i],
						    ObjectIdGetDatum(prodesc->arg_out_elem[i]),
						    Int32GetDatum(prodesc->arg_out_len[i])));
#else
		tmp = (*fmgr_faddr(&(prodesc->arg_out_func[i])))
		    (proargs->data[i],
		     prodesc->arg_out_elem[i],
		     prodesc->arg_out_len[i]);
#endif
		rb_ary_push(ary, rb_tainted_str_new2(tmp));
		pfree(tmp);
		PLRUBY_END;
	    }
	}
    }

    c = rb_funcall(pg_mPLtemp, rb_intern(RSTRING(value_proname)->ptr), 1, ary);

    PLRUBY_BEGIN(1);
    if (SPI_finish() != SPI_OK_FINISH) {
	rb_raise(pg_ePLruby, "SPI_finish() failed");
    }
    PLRUBY_END;
    
    if (c == Qnil) {
#ifdef NEW_STYLE_FUNCTION
	PG_RETURN_NULL();
#else
	*isNull = true;
	return (Datum)0;
#endif
    }

    rubyret = rb_funcall(c, id_to_s, 0);

    PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
    retval = FunctionCall3(&prodesc->result_in_func,
			   PointerGetDatum(RSTRING(rubyret)->ptr),
			   ObjectIdGetDatum(prodesc->result_in_elem),
			   Int32GetDatum(prodesc->result_in_len));
#else
    retval = (Datum) (*fmgr_faddr(&prodesc->result_in_func))
	(RSTRING(rubyret)->ptr,
	 prodesc->result_in_elem,
	 prodesc->result_in_len);
#endif
    PLRUBY_END;

    return retval;

}

static VALUE
plr_build_tuple(HeapTuple tuple, TupleDesc tupdesc, 
		int iterat, int array)
{
    int	i;
    VALUE output, res;
    Datum attr;
    bool isnull;
    char *attname, *outputstr, *typname;
    HeapTuple typeTup;
    Oid	typoutput;
    Oid	typelem;
    Form_pg_type fpgt;
    int alen;
    
    output = Qtrue;
    if (!iterat) {
	if (array == Qnil) {
	    output = rb_hash_new();
	}
	else {
	    output = rb_ary_new();
	}
    }

    for (i = 0; i < tupdesc->natts; i++) {
#ifdef NEW_STYLE_FUNCTION
	attname = NameStr(tupdesc->attrs[i]->attname);
#else
	attname = tupdesc->attrs[i]->attname.data;
#endif

	PLRUBY_BEGIN(1);
	attr = heap_getattr(tuple, i + 1, tupdesc, &isnull);
	typeTup = SearchSysCacheTuple(RUBY_TYPOID,
				      ObjectIdGetDatum(tupdesc->attrs[i]->atttypid),
				      0, 0, 0);
	PLRUBY_END;

	if (!HeapTupleIsValid(typeTup))	{
	    rb_raise(pg_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
		 attname, ObjectIdGetDatum(tupdesc->attrs[i]->atttypid));
	}

	fpgt = (Form_pg_type) GETSTRUCT(typeTup);
	typoutput = (Oid) (fpgt->typoutput);
	typelem = (Oid) (fpgt->typelem);
#if PG_PL_VERSION >= 71
	if (array != Qnil) {
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
	    if (array == Qtrue) {
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
#ifdef NEW_STYLE_FUNCTION
	    outputstr = DatumGetCString(OidFunctionCall3(typoutput,
							 attr,
							 ObjectIdGetDatum(typelem),
							 Int32GetDatum(tupdesc->attrs[i]->attlen)));
#else
	    FmgrInfo	finfo;
	    
	    fmgr_info(typoutput, &finfo);
	    
	    outputstr = (*fmgr_faddr(&finfo))(attr, typelem, tupdesc->attrs[i]->attlen);
#endif
	    s = rb_tainted_str_new2(outputstr);
	    pfree(outputstr);
	    PLRUBY_END;

#if PG_PL_VERSION >= 71
	    if (array != Qnil) {
		if (array == Qtrue) {
		    RARRAY(res)->ptr[1] = s;
		}
		else {
		    rb_hash_aset(res, rb_tainted_str_new2("value"), s);
		}
		if (iterat) {
		    rb_yield(res);
		}
		else {
		    rb_ary_push(output, res);
		}
	    }
	    else 
#endif
	    {
		if (iterat) {
		    rb_yield(rb_assoc_new(rb_tainted_str_new2(attname), s));
		}
		else {
		    rb_hash_aset(output, rb_tainted_str_new2(attname), s);
		}
	    }
	} 
	else {
	    if (isnull) {
#if PG_PL_VERSION >= 71
		if (array != Qnil) {
		    if (array == Qfalse) {
			rb_hash_aset(res, rb_tainted_str_new2("value"), Qnil);
		    }			
		    if (iterat) {
			rb_yield(res);
		    }
		    else {
			rb_ary_push(output, res);
		    }
		}
		else 
#endif
		{
		    if (iterat) {
			rb_yield(rb_assoc_new(rb_tainted_str_new2(attname), Qnil));
		    }
		    else {
			rb_hash_aset(output, rb_tainted_str_new2(attname), Qnil);
		    }
		}
	    }
	}
    }
    return output;
}

struct foreach_fmgr {
    TupleDesc	tupdesc;
    int		   *modattrs;
    Datum	   *modvalues;
    char	   *modnulls;
}; 

static VALUE
for_numvals(obj, argobj)
    VALUE obj, argobj;
{
    int			attnum;
    HeapTuple	typeTup;
    Oid			typinput;
    Oid			typelem;
    FmgrInfo	finfo;
    VALUE key, value;
    struct foreach_fmgr *arg;

    Data_Get_Struct(argobj, struct foreach_fmgr, arg);
    key = rb_funcall(rb_ary_entry(obj, 0), id_to_s, 0);
    value = rb_ary_entry(obj, 1);
    if ((RSTRING(key)->ptr)[0]  == '.' || NIL_P(value)) {
	return Qnil;
    }
    value = rb_funcall(value, id_to_s, 0);
    if (RSTRING(value)->len == 0) {
	return Qnil;
    }

    attnum = SPI_fnumber(arg->tupdesc, RSTRING(key)->ptr);
    if (attnum == SPI_ERROR_NOATTRIBUTE) {
	rb_raise(pg_ePLruby, "invalid attribute '%s'", RSTRING(key)->ptr);
    }

    PLRUBY_BEGIN(1);
    typeTup = SearchSysCacheTuple(RUBY_TYPOID,
				  ObjectIdGetDatum(arg->tupdesc->attrs[attnum - 1]->atttypid),
				  0, 0, 0);
    PLRUBY_END;

    if (!HeapTupleIsValid(typeTup)) {	
	rb_raise(pg_ePLruby, "Cache lookup for attribute '%s' type %ld failed",
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
#ifdef NEW_STYLE_FUNCTION
#if PG_PL_VERSION >= 71
    arg->modvalues[attnum - 1] =
	FunctionCall3(&finfo,
		      CStringGetDatum(RSTRING(value)->ptr),
		      ObjectIdGetDatum(typelem),
		      Int32GetDatum(arg->tupdesc->attrs[attnum - 1]->atttypmod));
#else
    arg->modvalues[attnum - 1] =
	FunctionCall3(&finfo,
		      CStringGetDatum(RSTRING(value)->ptr),
		      ObjectIdGetDatum(typelem),
		      Int32GetDatum((!VARLENA_FIXED_SIZE(arg->tupdesc->attrs[attnum - 1]))
		      ? arg->tupdesc->attrs[attnum - 1]->attlen
		      : arg->tupdesc->attrs[attnum - 1]->atttypmod));
#endif
#else
    arg->modvalues[attnum - 1] = (Datum) (*fmgr_faddr(&finfo))
	(RSTRING(value)->ptr,
	 typelem,
	 (!VARLENA_FIXED_SIZE(arg->tupdesc->attrs[attnum - 1]))
	 ? arg->tupdesc->attrs[attnum - 1]->attlen
	 : arg->tupdesc->attrs[attnum - 1]->atttypmod
	    );
#endif
    PLRUBY_END;

    return Qnil;
}
 

static HeapTuple
#ifdef NEW_STYLE_FUNCTION
plr_trigger_handler(PG_FUNCTION_ARGS)
#else
plr_trigger_handler(FmgrInfo *proinfo)
#endif
{
    TriggerData *trigdata;
    char		internal_proname[512];
    char	   *stroid;
    plr_proc_desc *prodesc;
    TupleDesc	tupdesc;
    volatile HeapTuple	rettup;
    int			i;
    int		   *modattrs;
    Datum	   *modvalues;
    char	   *modnulls;
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
    if ((value_proc_desc = rb_hash_aref(PLruby_hash, value_proname)) == Qnil) {
	HeapTuple	procTup;
	Form_pg_proc procStruct;
	char	   *proc_source;
	
	value_proc_desc = Data_Make_Struct(rb_cObject, plr_proc_desc, 0, plr_proc_free, prodesc);

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
	    rb_raise(pg_ePLruby, "cache lookup from pg_proc failed");
	}

	procStruct = (Form_pg_proc) GETSTRUCT(procTup);

	PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
	proc_source = DatumGetCString(DirectFunctionCall1(textout,
							  PointerGetDatum(&procStruct->prosrc)));
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
	    VALUE s = rb_funcall(rb_gv_get("$!"), id_to_s, 0);
	    rb_raise(pg_ePLruby, "cannot create internal procedure %s\n<<===%s\n===>>",
		 RSTRING(s)->ptr, proc_internal_def);
	}
	prodesc->proname = malloc(strlen(internal_proname) + 1);
	strcpy(prodesc->proname, internal_proname);
	rb_hash_aset(PLruby_hash, value_proname, value_proc_desc); 
#if PG_PL_VERSION >= 71
	ReleaseSysCache(procTup);
#endif
    }
    Data_Get_Struct(value_proc_desc, plr_proc_desc, prodesc);

    tupdesc = trigdata->tg_relation->rd_att;

    TG = rb_hash_new();

    rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("name")), 
		 rb_str_freeze(rb_tainted_str_new2(trigdata->tg_trigger->tgname)));

#if PG_PL_VERSION > 70
    {
	char *s = 
	    DatumGetCString(
		DirectFunctionCall1(nameout,
				    NameGetDatum(
					&(trigdata->tg_relation->rd_rel->relname))));
	
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("relname")), 
		     rb_str_freeze(rb_tainted_str_new2(s)));
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

    if (TRIGGER_FIRED_BEFORE(trigdata->tg_event))
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("when")), INT2FIX(TG_BEFORE)); 
    else if (TRIGGER_FIRED_AFTER(trigdata->tg_event))
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("when")), INT2FIX(TG_AFTER)); 
    else
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("when")), INT2FIX(TG_UNKNOWN)); 
    
    if (TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("level")),INT2FIX(TG_ROW));
    else if (TRIGGER_FIRED_FOR_STATEMENT(trigdata->tg_event))
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("level")), INT2FIX(TG_STATEMENT)); 
    else
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("level")), INT2FIX(TG_UNKNOWN)); 

    if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event)) {
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_INSERT));
	tg_new = plr_build_tuple(trigdata->tg_trigtuple, tupdesc, 0, Qnil);
	tg_old = rb_ary_new2(0);
	rettup = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event)) {
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_DELETE));
	tg_old = plr_build_tuple(trigdata->tg_trigtuple, tupdesc, 0, Qnil);
	tg_new = rb_ary_new2(0);

	rettup = trigdata->tg_trigtuple;
    }
    else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event)) {
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_UPDATE)); 
	tg_new = plr_build_tuple(trigdata->tg_newtuple, tupdesc, 0, Qnil);
	tg_old = plr_build_tuple(trigdata->tg_trigtuple, tupdesc, 0, Qnil);
	rettup = trigdata->tg_newtuple;
    }
    else {
	rb_hash_aset(TG, rb_str_freeze(rb_tainted_str_new2("op")), INT2FIX(TG_UNKNOWN));
	tg_new = plr_build_tuple(trigdata->tg_trigtuple, tupdesc, 0, Qnil);
	tg_old = plr_build_tuple(trigdata->tg_trigtuple, tupdesc, 0, Qnil);
	rettup = trigdata->tg_trigtuple;
    }
    rb_hash_freeze(TG);

    args = rb_ary_new2(trigdata->tg_trigger->tgnargs);
    for (i = 0; i < trigdata->tg_trigger->tgnargs; i++) {
	rb_ary_push(args, rb_str_freeze(rb_tainted_str_new2(trigdata->tg_trigger->tgargs[i])));
    }
    rb_ary_freeze(args);

    c = rb_funcall(pg_mPLtemp, rb_intern(RSTRING(value_proname)->ptr),
		   4, tg_new, tg_old, args, TG);

    PLRUBY_BEGIN(1);
    if (SPI_finish() != SPI_OK_FINISH) {
	rb_raise(pg_ePLruby, "SPI_finish() failed");
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
	rb_raise(pg_ePLruby, "Invalid return code");
	break;
    case T_STRING:
	if (strcmp(RSTRING(c)->ptr, "OK") == 0) {
	    return rettup;
	}
	if (strcmp(RSTRING(c)->ptr, "SKIP") == 0) {
	    return (HeapTuple) NULL;
	}
	rb_raise(pg_ePLruby, "unknown response %s", RSTRING(c)->ptr);
	break;
    case T_HASH:
	break;
    default:
	rb_raise(pg_ePLruby, "Invalid return value");
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
	rb_raise(pg_ePLruby, "SPI_modifytuple() failed - RC = %d\n", SPI_result);
    }

    return rettup;
}

static VALUE
plr_warn(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int level, indice;

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
	    rb_raise(pg_ePLruby, "invalid level %d", level);
	}
    case 1:
	if (NIL_P(argv[indice]))
	    return Qnil;
	if (TYPE(argv[indice]) != T_STRING)
	    rb_raise(pg_ePLruby, "warn: string expected");
	break;
    default:
	rb_raise(pg_ePLruby, "invalid syntax");
    }
    PLRUBY_BEGIN(1);
    elog(level, RSTRING(argv[indice])->ptr);
    PLRUBY_END;
    return Qnil;
}

static VALUE
plr_quote(obj, mes)
    VALUE obj, mes;
{    
    char	   *tmp;
    char	   *cp1;
    char	   *cp2;

    if (TYPE(mes) != T_STRING) {
	rb_raise(pg_ePLruby, "quote: string expected");
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

static VALUE
plr_SPI_exec(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int	spi_rc;
    volatile int count = 0;
    volatile int array = Qnil;
    int	i, comp;
    int	ntuples;
    VALUE a, b, c, result;
    HeapTuple  *tuples;
    TupleDesc	tupdesc = NULL;

    comp = Qnil;
    switch (rb_scan_args(argc, argv, "12", &a, &b, &c)) {
    case 3:
	if (TYPE(c) != T_STRING) {
	    rb_raise(pg_ePLruby, "string expected for optionnal output");
	}
	if (strcmp(RSTRING(c)->ptr, "array") == 0) {
	    comp = Qtrue;
	}
	else if (strcmp(RSTRING(c)->ptr, "hash") == 0) {
	    comp = Qfalse;
	}
	/* ... */
    case 2:
	if (!NIL_P(b)) {
	    count = NUM2INT(b);
	}
    }
    if (TYPE(a) != T_STRING) {
	rb_raise(pg_ePLruby, "exec: first argument must be a string");
    }
#if PG_PL_VERSION >= 71
    array = comp;
#endif

    PLRUBY_BEGIN(1);
    spi_rc = SPI_exec(RSTRING(a)->ptr, count);
    PLRUBY_END;

    switch (spi_rc) {
    case SPI_OK_UTILITY:
	return Qtrue;
    case SPI_OK_SELINTO:
    case SPI_OK_INSERT:
    case SPI_OK_DELETE:
    case SPI_OK_UPDATE:
	return INT2NUM(SPI_processed);
    case SPI_OK_SELECT:
	break;
    case SPI_ERROR_ARGUMENT:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_ARGUMENT");
    case SPI_ERROR_UNCONNECTED:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_UNCONNECTED");
    case SPI_ERROR_COPY:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_COPY");
    case SPI_ERROR_CURSOR:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_CURSOR");
    case SPI_ERROR_TRANSACTION:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_TRANSACTION");
    case SPI_ERROR_OPUNKNOWN:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_OPUNKNOWN");
    default:
	rb_raise(pg_ePLruby, "SPI_exec() failed - unknown RC %d", spi_rc);
    }

    ntuples = SPI_processed;
    if (ntuples <= 0) {
	if (rb_block_given_p() || count == 1)
	    return Qfalse;
	else
	    return rb_ary_new2(0);
    }
    tuples = SPI_tuptable->vals;
    tupdesc = SPI_tuptable->tupdesc;
    if (rb_block_given_p()) {
	if (count == 1) {
	    plr_build_tuple(tuples[0], tupdesc, 1, array);
	}
	else {
	    for (i = 0; i < ntuples; i++) {
		rb_yield(plr_build_tuple(tuples[i], tupdesc, 0, array));
	    }
	}
	result = Qtrue;
    }
    else {
	if (count == 1) {
	    result = plr_build_tuple(tuples[0], tupdesc, 0, array);
	}
	else {
	    result = rb_ary_new2(ntuples);
	    for (i = 0; i < ntuples; i++) {
		rb_ary_push(result, plr_build_tuple(tuples[i], tupdesc, 0, array));
	    }
	}
    }
    return result;
}

static void
plr_query_free(qdesc)
    plr_query_desc *qdesc;
{
    if (qdesc->argtypes) free(qdesc->argtypes);
    if (qdesc->arginfuncs) free(qdesc->arginfuncs);
    if (qdesc->argtypelems) free(qdesc->argtypelems);
    if (qdesc->arglen) free(qdesc->arglen);
    free(qdesc);
}

static VALUE
plr_SPI_prepare(int argc, VALUE *argv, VALUE obj)
{
    int	nargs;
    plr_query_desc *qdesc;
    void *plan;
    int	i;
    HeapTuple	typeTup;
    VALUE a, b, result;
    
    nargs = 0;
    if (rb_scan_args(argc, argv, "11", &a, &b) == 2) {
	if (TYPE(b) != T_ARRAY) {
	    rb_raise(pg_ePLruby, "second argument must be an ARRAY");
	}
	nargs = RARRAY(b)->len;
    }
    if (TYPE(a) != T_STRING) {
	rb_raise(pg_ePLruby, "first argument must be a STRING");
    }

    result = Data_Make_Struct(pg_cPLrubyPlan, plr_query_desc, 0, 
			      plr_query_free, qdesc);
    sprintf(qdesc->qname, "%lx", (long) qdesc);
    qdesc->nargs = nargs;
    qdesc->argtypes = NULL;
    if (nargs) {
	qdesc->argtypes = ALLOC_N(Oid, nargs);
	qdesc->arginfuncs = ALLOC_N(FmgrInfo ,nargs);
	qdesc->argtypelems = ALLOC_N(Oid ,nargs);
	qdesc->arglen = ALLOC_N(int, nargs);
 
	for (i = 0; i < nargs; i++)	{
	    VALUE args = rb_funcall(RARRAY(b)->ptr[i], id_to_s, 0);

	    PLRUBY_BEGIN(1);
#if PG_PL_VERSION >= 73
	    typeTup = typenameType(makeTypeName(RSTRING(args)->ptr));
	    qdesc->argtypes[i] = HeapTupleGetOid(typeTup);
#else
	    typeTup = SearchSysCacheTuple(RUBY_TYPNAME,
					  PointerGetDatum(RSTRING(args)->ptr),
					  0, 0, 0);
	    if (!HeapTupleIsValid(typeTup)) {
		rb_raise(pg_ePLruby, "Cache lookup of type '%s' failed", RSTRING(args)->ptr);
	    }
	    qdesc->argtypes[i] = typeTup->t_data->t_oid;
#endif
	    fmgr_info(((Form_pg_type) GETSTRUCT(typeTup))->typinput,
		      &(qdesc->arginfuncs[i]));
	    qdesc->argtypelems[i] = ((Form_pg_type) GETSTRUCT(typeTup))->typelem;
	    qdesc->arglen[i] = (int) (((Form_pg_type) GETSTRUCT(typeTup))->typlen);
#if PG_PL_VERSION >= 71
	    ReleaseSysCache(typeTup);
#endif
	    PLRUBY_END;

	}
    }

    PLRUBY_BEGIN(1);
    plan = SPI_prepare(RSTRING(a)->ptr, nargs, qdesc->argtypes);
    PLRUBY_END;

    if (plan == NULL) {
	char		buf[128];
	char	   *reason;

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
	default:
	    sprintf(buf, "unknown RC %d", SPI_result);
	    reason = buf;
	    break;
	}
	rb_raise(pg_ePLruby, "SPI_prepare() failed - %s", reason);
    }

    PLRUBY_BEGIN(1);
    qdesc->plan = SPI_saveplan(plan);
    PLRUBY_END;

    if (qdesc->plan == NULL) {
	char		buf[128];
	char	   *reason;

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
	rb_raise(pg_ePLruby, "SPI_saveplan() failed - %s", reason);
    }
    return result;
}

static void
process_args(qdesc, portal)
    plr_query_desc *qdesc;
    struct PLportal *portal;
{
    int callnargs, j;
    VALUE argsv;

    if (qdesc->nargs > 0) {
	argsv = portal->argsv;
	if (TYPE(argsv) != T_ARRAY) {
	    rb_raise(pg_ePLruby, "array expected for arguments");
	}
	if (RARRAY(argsv)->len != qdesc->nargs) {
	    rb_raise(pg_ePLruby, "length of arguments doesn't match # of arguments");
	}
	callnargs = RARRAY(argsv)->len;
	portal->nargs = callnargs;
	portal->nulls = ALLOC_N(char, callnargs + 1);
	portal->argvalues = ALLOC_N(Datum, callnargs);
	MEMZERO(portal->argvalues, Datum, callnargs);
	portal->arglen = ALLOC_N(int, callnargs);
	MEMZERO(portal->arglen, int, callnargs);
	for (j = 0; j < callnargs; j++)	{
	    if (NIL_P(RARRAY(argsv)->ptr[j])) {
		portal->nulls[j] = 'n';
		portal->argvalues[j] = (Datum)NULL;
	    }
	    else {
		VALUE args = rb_funcall(RARRAY(argsv)->ptr[j], id_to_s, 0);
		portal->nulls[j] = ' ';
		portal->arglen[j] = qdesc->arglen[j];

		PLRUBY_BEGIN(1);
#ifdef NEW_STYLE_FUNCTION
		portal->argvalues[j] =
		    FunctionCall3(&qdesc->arginfuncs[j],
				  CStringGetDatum(RSTRING(args)->ptr),
				  ObjectIdGetDatum(qdesc->argtypelems[j]),
				  Int32GetDatum(qdesc->arglen[j]));
#else
		portal->argvalues[j] = (Datum) (*fmgr_faddr(&qdesc->arginfuncs[j]))
		    (RSTRING(args)->ptr, qdesc->argtypelems[j], qdesc->arglen[j]);
#endif
		PLRUBY_END;

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

    if (portal->nargs) {
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
	portal->nargs = 0;
    }
}

static void
portal_free(struct PLportal *portal)
{
   if (portal->nargs) {
	MEMZERO(portal->argvalues, Datum, portal->nargs);
    }
    free_args(portal);
    free(portal);
}

static VALUE
plr_i_each(obj, portal)
    VALUE obj;
    struct PLportal *portal;
{
    VALUE key, value;
    char *options;

    key = rb_ary_entry(obj, 0);
    value = rb_ary_entry(obj, 1);
    key = rb_obj_as_string(key);
    options = RSTRING(key)->ptr;
    if (strcmp(options, "values") == 0) {
	portal->argsv = value;
    }
    else if (strcmp(options, "count") == 0) {
	portal->count = NUM2INT(value);
    }
    else if (strcmp(options, "output") == 0) {
	if (TYPE(value) != T_STRING) {
	    rb_raise(pg_ePLruby, "string expected for optionnal output");
	}
	if (strcmp(RSTRING(value)->ptr, "array") == 0) {
	    portal->array = Qtrue;
	}
	else if (strcmp(RSTRING(value)->ptr, "hash") == 0) {
	    portal->array = Qfalse;
	}
    }
    return Qnil;
}

static VALUE
plr_SPI_execp(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int	i, spi_rc;
    VALUE result;
    volatile VALUE vortal;
    plr_query_desc *qdesc;
    int	ntuples;
    HeapTuple *tuples = NULL;
    TupleDesc tupdesc = NULL;
    VALUE argsv, countv, c;
    struct PLportal *portal;

    Data_Get_Struct(obj, plr_query_desc, qdesc);
    vortal = Data_Make_Struct(rb_cObject, struct PLportal, 0, portal_free,
			      portal);

    portal->argsv = portal->array = Qnil;
    portal->count = 0;
    if (argc && TYPE(argv[argc - 1]) == T_HASH) {
	rb_iterate(rb_each, argv[argc - 1], plr_i_each, (VALUE)portal);
	argc--;
    }
    switch (rb_scan_args(argc, argv, "03", &argsv, &countv, &c)) {
    case 3:
	if (TYPE(c) != T_STRING) {
	    rb_raise(pg_ePLruby, "string expected for optional output");
	}
	if (strcmp(RSTRING(c)->ptr, "array") == 0) {
	    portal->array = Qtrue;
	}
	else if (strcmp(RSTRING(c)->ptr, "hash") == 0) {
	    portal->array = Qfalse;
	}
	/* ... */
    case 2:
	if (!NIL_P(countv)) {
	    portal->count = NUM2INT(countv);
	}
	/* ... */
    case 1:
	portal->argsv = argsv;
    }
#if PG_PL_VERSION < 71
    portal->array = Qnil;
#endif
    
    process_args(qdesc, portal);
    rb_ary_push(PLruby_portal, vortal);
    PLRUBY_BEGIN(1);
    spi_rc = SPI_execp(qdesc->plan, portal->argvalues,
		       portal->nulls, portal->count);
    rb_ary_pop(PLruby_portal);
    free_args(portal);
    PLRUBY_END;

    switch (spi_rc) {
    case SPI_OK_UTILITY:
	return Qtrue;
    case SPI_OK_SELINTO:
    case SPI_OK_INSERT:
    case SPI_OK_DELETE:
    case SPI_OK_UPDATE:
	return INT2NUM(SPI_processed);
    case SPI_OK_SELECT:
	break;

    case SPI_ERROR_ARGUMENT:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_ARGUMENT");
    case SPI_ERROR_UNCONNECTED:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_UNCONNECTED");
    case SPI_ERROR_COPY:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_COPY");
    case SPI_ERROR_CURSOR:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_CURSOR");
    case SPI_ERROR_TRANSACTION:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_TRANSACTION");
    case SPI_ERROR_OPUNKNOWN:
	rb_raise(pg_ePLruby, "SPI_exec() failed - SPI_ERROR_OPUNKNOWN");
    default:
	rb_raise(pg_ePLruby, "SPI_exec() failed - unknown RC %d", spi_rc);
    }
    
    ntuples = SPI_processed;
    if (ntuples <= 0) {
	if (rb_block_given_p() || portal->count == 1) {
	    return Qfalse;
	}
	else {
	    return rb_ary_new2(0);
	}
    }
    tuples = SPI_tuptable->vals;
    tupdesc = SPI_tuptable->tupdesc;
    if (rb_block_given_p()) {
	if (portal->count == 1) {
	    plr_build_tuple(tuples[0], tupdesc, 1, portal->array);
	}
	else {
	    for (i = 0; i < ntuples; i++) {
		rb_yield(plr_build_tuple(tuples[i], tupdesc, 0, portal->array));
	    }
	}
	result = Qtrue;
    }
    else {
	if (portal->count == 1) {
	    result = plr_build_tuple(tuples[0], tupdesc, 0, portal->array);
	}
	else {
	    result = rb_ary_new2(ntuples);
	    for (i = 0; i < ntuples; i++) {
		rb_ary_push(result, 
			    plr_build_tuple(tuples[i], tupdesc, 0, portal->array));
	    }
	}
    }
    return result;
}

static VALUE
plr_cursor_close(portal)
    struct PLportal *portal;
{
    PLRUBY_BEGIN(1);
    SPI_cursor_close(portal->portal);
    PLRUBY_END;
    return Qnil;
}

static VALUE
plr_cursor_fetch(portal)
    struct PLportal *portal;
{
    HeapTuple *tuples = NULL;
    TupleDesc tupdesc = NULL;
    int count = 0;

    do {
	PLRUBY_BEGIN(1);
	SPI_cursor_fetch(portal->portal, true, 1);
	PLRUBY_END;
	if (SPI_processed <= 0) {
	    return Qfalse;
	}
	tuples = SPI_tuptable->vals;
	tupdesc = SPI_tuptable->tupdesc;
	rb_yield(plr_build_tuple(tuples[0], tupdesc, 0, portal->array));
    } while (++count != portal->count);
    return Qtrue;
}

static VALUE
plr_SPI_each(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    VALUE result;
    plr_query_desc *qdesc;
    Portal pgportal;
    struct PLportal *portal;
    VALUE volatile vortal;
    VALUE argsv, countv, c;

    if (!rb_block_given_p()) {
	rb_raise(pg_ePLruby, "a block must be given");
    }
    Data_Get_Struct(obj, plr_query_desc, qdesc);
    vortal = Data_Make_Struct(rb_cObject, struct PLportal, 0, portal_free,
			      portal);

    portal->argsv = portal->array = Qnil;
    portal->count = 0;
    if (argc && TYPE(argv[argc - 1]) == T_HASH) {
	rb_iterate(rb_each, argv[argc - 1], plr_i_each, (VALUE)portal);
	argc--;
    }
    switch (rb_scan_args(argc, argv, "03", &argsv, &countv, &c)) {
    case 3:
	if (!NIL_P(c)) {
	    if (TYPE(c) != T_STRING) {
		rb_raise(pg_ePLruby, "string expected for optionnal output");
	    }
	    if (strcmp(RSTRING(c)->ptr, "array") == 0) {
		portal->array = Qtrue;
	    }
	    else if (strcmp(RSTRING(c)->ptr, "hash") == 0) {
		portal->array = Qfalse;
	    }
	}
	/* ... */
    case 2:
	if (!NIL_P(countv)) {
	    portal->count = NUM2INT(countv);
	}
	/* ... */
    case 1:
	portal->argsv = argsv;
    }
#if PG_PL_VERSION < 71
    portal->array = Qnil;
#endif
    
    pgportal = NULL;

    process_args(qdesc, portal);
    rb_ary_push(PLruby_portal, vortal);
    PLRUBY_BEGIN(1);
    pgportal = SPI_cursor_open(NULL, qdesc->plan, 
			       portal->argvalues, portal->nulls);
    rb_ary_pop(PLruby_portal);
    free_args(portal);
    PLRUBY_END;

    if (pgportal == NULL) {
	rb_raise(pg_ePLruby,  "SPI_cursor_open() failed");
    }
    portal->portal = pgportal;
    return rb_ensure(plr_cursor_fetch, (VALUE)portal,
		     plr_cursor_close, (VALUE)portal);
    return result;
}

static int
plr_exist_singleton()
{
    int spi_rc;

    spi_rc = SPI_exec("select 1 from pg_class where relname = 'plruby_singleton_methods'", 1);
    if (spi_rc != SPI_OK_SELECT || SPI_processed == 0)
	return 0;
    spi_rc = SPI_exec("select name from plruby_singleton_methods", 0);
    if (spi_rc != SPI_OK_SELECT || SPI_processed == 0)
	return 0;
    return SPI_processed;
}

static char *recherche = 
    "select name, args, body from plruby_singleton_methods where name = '%s'";

static VALUE
plr_each(tmp)
    VALUE *tmp;
{
    return rb_funcall2(pg_mPLtemp, (ID)tmp[0], (int)tmp[1], (VALUE *)tmp[2]);
}

static VALUE
plr_yield(i, a)
    VALUE i, a;
{
    rb_ary_push(a, rb_yield(i));
    return Qnil;
}

static VALUE
plr_load_singleton(argc, argv, obj)
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

    if (argc == 0) 
	rb_raise(rb_eArgError, "no id given");
 
    id = SYM2ID(argv[0]);
    argc--; argv++;
    nom = rb_id2name(id);
    buff = ALLOCA_N(char, 1 + strlen(recherche) + strlen(nom));
    sprintf(buff, recherche, nom);

    PLRUBY_BEGIN(1);
    spi_rc = SPI_exec(buff, 0);
    PLRUBY_END;

    if (spi_rc != SPI_OK_SELECT || SPI_processed == 0)
	rb_raise(rb_eNameError, "undefined method `%s' for PLtemp:Module", nom);
    fname = SPI_fnumber(SPI_tuptable->tupdesc, "name");
    fargs = SPI_fnumber(SPI_tuptable->tupdesc, "args");
    fbody = SPI_fnumber(SPI_tuptable->tupdesc, "body");
    name = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fname);
    args = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fargs);
    body = SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, fbody);
    sinm = ALLOCA_N(char, 1 + strlen(definition) + strlen(name) + 
		    strlen(args) + strlen(body));
    sprintf(sinm, definition, name, args, body);
    rb_eval_string_protect(sinm, &status);
    if (status) {
	VALUE s = rb_funcall(rb_gv_get("$!"), id_to_s, 0);
	rb_raise(pg_ePLruby, "cannot create internal procedure\n%s\n<<===%s\n===>>",
		 RSTRING(s)->ptr, sinm);
    }
    if (rb_block_given_p()) {
	VALUE tmp[3], res;
	tmp[0] = (VALUE)id;
	tmp[1] = (VALUE)argc;
	tmp[2] = (VALUE)argv;
	res = rb_ary_new();
	rb_iterate(plr_each, (VALUE)tmp, plr_yield, res);
	return res;
    }
    else {
	return rb_funcall2(pg_mPLtemp, id, argc, argv);
    }
}

static VALUE plans;

static void
plr_init_all(void)
{
    if (!plr_firstcall)
	return;
    plr_firstcall = 0;
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
    if (rb_const_defined_at(rb_cObject, rb_intern("PLruby")) ||
	rb_const_defined_at(rb_cObject, rb_intern("PLrubyError")) ||
	rb_const_defined_at(rb_cObject, rb_intern("PLrubyCatch")) ||
	rb_const_defined_at(rb_cObject, rb_intern("PLrubyPlan")) ||
	rb_const_defined_at(rb_cObject, rb_intern("PLtemp"))) {
	elog(ERROR, "class already defined");
    }
    pg_mPLruby = rb_define_module("PLruby");
    pg_ePLruby = rb_define_class("PLrubyError", rb_eStandardError);
    pg_eCatch = rb_define_class("PLrubyCatch", rb_eStandardError);
    rb_define_global_function("warn", plr_warn, -1);
    rb_define_module_function(pg_mPLruby, "quote", plr_quote, 1);
    rb_define_module_function(pg_mPLruby, "spi_exec", plr_SPI_exec, -1);
    rb_define_module_function(pg_mPLruby, "exec", plr_SPI_exec, -1);
    rb_define_const(pg_mPLruby, "OK", INT2FIX(TG_OK));
    rb_define_const(pg_mPLruby, "SKIP", INT2FIX(TG_SKIP));
    rb_define_const(pg_mPLruby, "BEFORE", INT2FIX(TG_BEFORE)); 
    rb_define_const(pg_mPLruby, "AFTER", INT2FIX(TG_AFTER)); 
    rb_define_const(pg_mPLruby, "ROW", INT2FIX(TG_ROW)); 
    rb_define_const(pg_mPLruby, "STATEMENT", INT2FIX(TG_STATEMENT)); 
    rb_define_const(pg_mPLruby, "INSERT", INT2FIX(TG_INSERT));
    rb_define_const(pg_mPLruby, "DELETE", INT2FIX(TG_DELETE)); 
    rb_define_const(pg_mPLruby, "UPDATE", INT2FIX(TG_UPDATE));
    rb_define_const(pg_mPLruby, "UNKNOWN", INT2FIX(TG_UNKNOWN));
    pg_cPLrubyPlan = rb_define_class("PLrubyPlan", rb_cObject);
    rb_undef_method(CLASS_OF(pg_cPLrubyPlan), "new");
    rb_define_module_function(pg_mPLruby, "spi_prepare", plr_SPI_prepare, -1);
    rb_define_module_function(pg_mPLruby, "prepare", plr_SPI_prepare, -1);
    rb_define_method(pg_cPLrubyPlan, "spi_execp", plr_SPI_execp, -1);
    rb_define_method(pg_cPLrubyPlan, "execp", plr_SPI_execp, -1);
    rb_define_method(pg_cPLrubyPlan, "exec", plr_SPI_execp, -1);
    rb_define_method(pg_cPLrubyPlan, "spi_fetch", plr_SPI_each, -1);
    rb_define_method(pg_cPLrubyPlan, "each", plr_SPI_each, -1);
    rb_define_method(pg_cPLrubyPlan, "fetch", plr_SPI_each, -1);
    id_to_s = rb_intern("to_s");
    id_raise = rb_intern("raise");
    id_kill = rb_intern("kill");
    id_alive = rb_intern("alive?");
    id_value = rb_intern("value");
#ifdef PLRUBY_TIMEOUT
    rb_funcall(rb_thread_main(), rb_intern("priority="), 1, INT2NUM(10));
    rb_undef_method(CLASS_OF(rb_cThread), "new"); 
    rb_undef_method(CLASS_OF(rb_cThread), "start"); 
    rb_undef_method(CLASS_OF(rb_cThread), "fork"); 
    rb_undef_method(CLASS_OF(rb_cThread), "critical="); 
#endif
    rb_set_safe_level(MAIN_SAFE_LEVEL);
    plans = rb_hash_new();
    rb_define_variable("$Plans", &plans);
    pg_mPLtemp = rb_define_module("PLtemp");
    PLruby_hash = rb_hash_new();
    rb_global_variable(&PLruby_hash);
    PLruby_portal = rb_ary_new();
    rb_global_variable(&PLruby_portal);
    if (SPI_connect() != SPI_OK_CONNECT) {
	elog(ERROR, "plruby_singleton_methods : SPI_connect failed");
    }
    if (plr_exist_singleton()) {
	rb_define_module_function(pg_mPLtemp, "method_missing", plr_load_singleton, -1);
    }
    if (SPI_finish() != SPI_OK_FINISH) {
	elog(ERROR, "plruby_singleton_methods : SPI_finish failed");
    }
    return;
}
