#include <unistd.h>
#include <fcntl.h>
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
#include "utils/lsyscache.h"
#include "funcapi.h"
#endif

#if PG_PL_VERSION >= 74
#include "server/utils/array.h"
#endif

#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif

#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif

#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif

#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif

#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif

#include <ruby.h>

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

extern VALUE rb_thread_list();

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

#ifdef PLRUBY_TIMEOUT

#define PLRUBY_BEGIN(lvl)			\
    do {					\
        int in_progress = pl_in_progress;	\
         if (pl_interrupted) {			\
	    rb_raise(pl_ePLruby, "timeout");	\
        }					\
        pl_in_progress = lvl

#define PLRUBY_END				\
        pl_in_progress = in_progress;		\
        if (pl_interrupted) {			\
	    rb_raise(pl_ePLruby, "timeout");	\
        }					\
    } while (0)

#else
#define PLRUBY_BEGIN(lvl)
#define PLRUBY_END
#endif

enum { TG_OK, TG_SKIP };
enum { TG_BEFORE, TG_AFTER, TG_ROW, TG_STATEMENT, TG_INSERT,
       TG_DELETE, TG_UPDATE, TG_UNKNOWN }; 

struct pl_thread_st {
#ifdef NEW_STYLE_FUNCTION
    PG_FUNCTION_ARGS;
#else
    FmgrInfo *proinfo;
    FmgrValues *proargs;
    bool *isNull;
#endif
    int timeout;
};

typedef struct pl_proc_desc
{
    char	   *proname;
#if PG_PL_VERSION >= 73
    TransactionId  fn_xmin;
    CommandId      fn_cmin;
#endif
    FmgrInfo	result_func;
    Oid		result_elem;
    Oid		result_oid;
    int		result_len;
#if PG_PL_VERSION >= 74
    bool	result_is_array;
    bool	result_val;
    char	result_align;
#endif
    int		nargs;
    FmgrInfo	arg_func[RUBY_ARGS_MAXFMGR];
    Oid		arg_elem[RUBY_ARGS_MAXFMGR];
    Oid		arg_type[RUBY_ARGS_MAXFMGR];
    int		arg_len[RUBY_ARGS_MAXFMGR];
#if PG_PL_VERSION >= 74
    bool	arg_is_array[RUBY_ARGS_MAXFMGR];
    bool	arg_val[RUBY_ARGS_MAXFMGR];
    char	arg_align[RUBY_ARGS_MAXFMGR];
#endif
    int		arg_is_rel[RUBY_ARGS_MAXFMGR];
    char result_type;
} pl_proc_desc;

struct portal_options {
    VALUE argsv;
    int count, output;
    int block, save;
};

typedef struct pl_query_desc
{
    char qname[20];
    void *plan;
    int	 nargs;
    Oid	*argtypes;
    FmgrInfo *arginfuncs;
    Oid *argtypelems;
    int	*arglen;
    int cursor;
    struct portal_options po;
} pl_query_desc;

struct PLportal {
    Portal portal;
    char *nulls;
    Datum *argvalues;
    int *arglen;
    int nargs;
    struct portal_options po;
};

#define GetPortal(obj, portal) do {			\
    Data_Get_Struct(obj, struct PLportal, portal);	\
    if (!portal->portal) {				\
	rb_raise(pl_ePLruby, "cursor closed");		\
    }							\
} while (0)

#define GetPlan(obj, qdesc) do {					\
    Data_Get_Struct(obj, pl_query_desc, qdesc);				\
    if (!qdesc->plan) {							\
	rb_raise(pl_ePLruby, "plan was dropped during the session");	\
    }									\
} while (0)

#define RET_HASH      1
#define RET_ARRAY     2
#define RET_DESC      4
#define RET_DESC_ARR 12
#define RET_BASIC    16

static VALUE pl_i_each _((VALUE, struct portal_options *));
static VALUE pl_build_tuple(HeapTuple, TupleDesc, int);
static Datum return_base_type(VALUE, pl_proc_desc *);
static VALUE pl_SPI_exec _((int, VALUE *, VALUE));
static void pl_init_all(void);
static void portal_free _((struct PLportal *));

#ifdef NEW_STYLE_FUNCTION
#if PG_PL_VERSION >= 71
PG_FUNCTION_INFO_V1(plruby_call_handler);
#else
Datum plruby_call_handler(PG_FUNCTION_ARGS);
#endif
static Datum pl_func_handler(PG_FUNCTION_ARGS);
static HeapTuple pl_trigger_handler(PG_FUNCTION_ARGS);
#else
Datum plruby_call_handler(FmgrInfo *, FmgrValues *, bool *);

static Datum pl_func_handler(FmgrInfo *, FmgrValues *, bool *);

static HeapTuple pl_trigger_handler(FmgrInfo *);
#endif


