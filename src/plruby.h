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

#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "parser/parse_type.h"
#include "utils/lsyscache.h"
#include "funcapi.h"

#include "utils/array.h"

#if PG_PL_VERSION >= 75
#include "nodes/pg_list.h"
#include "utils/typcache.h"
#include "access/xact.h"
#endif

#if PG_PL_VERSION >= 81
#include "utils/memutils.h"
#endif

#include "package.h"

#include <ruby.h>
#if HAVE_ST_H
#include <st.h>
#endif

#ifndef StringValuePtr
#define StringValuePtr(x) STR2CSTR(x)
#endif

#ifndef RSTRING_PTR
# define RSTRING_PTR(x_) RSTRING(x_)->ptr
# define RSTRING_LEN(x_) RSTRING(x_)->len
#endif

#ifndef RARRAY_PTR
# define RARRAY_PTR(x_) RARRAY(x_)->ptr
# define RARRAY_LEN(x_) RARRAY(x_)->len
#endif

#ifndef RHASH_TBL
#define RHASH_TBL(x_) (RHASH(x_)->tbl)
#endif

#ifndef RFLOAT_VALUE
#define RFLOAT_VALUE(x_) (RFLOAT(x_)->value)
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

extern int plruby_in_progress;
extern int plruby_interrupted;

#define PLRUBY_BEGIN(lvl_) do {                 \
    int in_progress = plruby_in_progress;       \
    if (plruby_interrupted) {                   \
	rb_raise(pl_ePLruby, "timeout");        \
    }                                           \
    plruby_in_progress = lvl_;

#define PLRUBY_END                                              \
    plruby_in_progress = in_progress;                           \
    if (plruby_interrupted) {                                   \
        rb_raise(pl_ePLruby, "timeout");                        \
    }                                                           \
} while (0)

#ifdef PG_PL_TRYCATCH

#define PLRUBY_BEGIN_PROTECT(lvl_) do {         \
    int in_progress = plruby_in_progress;       \
    if (plruby_interrupted) {                   \
        rb_raise(pl_ePLruby, "timeout");        \
    }                                           \
    plruby_in_progress = lvl_;                  \
    PG_TRY();                                   \
    {

extern int errorcode;

#define PLRUBY_END_PROTECT                      \
    }                                           \
    PG_CATCH();                                 \
    {                                           \
        plruby_in_progress = in_progress;       \
        rb_raise(pl_eCatch, "propagate");       \
    }                                           \
    PG_END_TRY();                               \
    plruby_in_progress = in_progress;           \
    if (plruby_interrupted) {                   \
        rb_raise(pl_ePLruby, "timeout");        \
    }                                           \
} while (0)

#else
    
#define PLRUBY_BEGIN_PROTECT(lvl_) do {                                 \
    sigjmp_buf save_restart;                                            \
    int in_progress = plruby_in_progress;                               \
    if (plruby_interrupted) {                                           \
	rb_raise(pl_ePLruby, "timeout");                                \
    }                                                                   \
    memcpy(&save_restart, &Warn_restart, sizeof(save_restart));         \
    if (sigsetjmp(Warn_restart, 1) != 0) {                              \
        plruby_in_progress = in_progress;                               \
        memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));     \
        rb_raise(pl_eCatch, "propagate");                               \
    }                                                                   \
    plruby_in_progress = lvl_;

#define PLRUBY_END_PROTECT                                      \
    plruby_in_progress = in_progress;                           \
    memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart)); \
    if (plruby_interrupted) {                                   \
        rb_raise(pl_ePLruby, "timeout");                        \
    }                                                           \
} while (0)

#endif

#else

#define PLRUBY_BEGIN(lvl_)
#define PLRUBY_END

#ifdef PG_PL_TRYCATCH

#define PLRUBY_BEGIN_PROTECT(lvl_) do {         \
    PG_TRY();                                   \
    {

#define PLRUBY_END_PROTECT                      \
    }                                           \
    PG_CATCH();                                 \
    {                                           \
        rb_raise(pl_eCatch, "propagate");       \
    }                                           \
    PG_END_TRY();                               \
} while (0)

#else

#define PLRUBY_BEGIN_PROTECT(lvl_) do {                                 \
    sigjmp_buf save_restart;                                            \
    memcpy(&save_restart, &Warn_restart, sizeof(save_restart));         \
    if (sigsetjmp(Warn_restart, 1) != 0) {                              \
        memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));     \
        rb_raise(pl_eCatch, "propagate");                               \
    }

#define PLRUBY_END_PROTECT                                              \
     memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));        \
} while (0)
   
#endif

#endif

   

enum { TG_OK, TG_SKIP };
enum { TG_BEFORE, TG_AFTER, TG_ROW, TG_STATEMENT, TG_INSERT,
       TG_DELETE, TG_UPDATE, TG_UNKNOWN }; 

struct pl_thread_st {
    PG_FUNCTION_ARGS;
    int timeout;
    Oid validator;
};

typedef struct pl_proc_desc
{
    char	   *proname;
    TransactionId  fn_xmin;
    CommandId      fn_cmin;
    FmgrInfo	result_func;
    Oid		result_elem;
    Oid		result_oid;
    int		result_len;
    bool	result_is_array;
    bool	result_val;
    char	result_align;
    int		nargs;
#if PG_PL_VERSION >= 75
    int         named_args;
#endif
    FmgrInfo	arg_func[FUNC_MAX_ARGS];
    Oid		arg_elem[FUNC_MAX_ARGS];
    Oid		arg_type[FUNC_MAX_ARGS];
    int		arg_len[FUNC_MAX_ARGS];
    bool	arg_is_array[FUNC_MAX_ARGS];
    bool	arg_val[FUNC_MAX_ARGS];
    char	arg_align[FUNC_MAX_ARGS];
    int		arg_is_rel[FUNC_MAX_ARGS];
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
    bool       *arg_is_array;
    bool       *arg_val;
    char       *arg_align;
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

extern VALUE plruby_s_new _((int, VALUE *, VALUE));
extern VALUE plruby_build_tuple _((HeapTuple, TupleDesc, int));
extern Datum plruby_to_datum _((VALUE, FmgrInfo *, Oid, Oid, int));
extern Datum plruby_return_value _((struct pl_thread_st *,  pl_proc_desc *,
                                    VALUE, VALUE));
extern VALUE plruby_create_args _((struct pl_thread_st *, pl_proc_desc *));
extern VALUE plruby_i_each _((VALUE, struct portal_options *));
extern void plruby_exec_output _((VALUE, int, int *));
extern VALUE plruby_to_s _((VALUE));

extern Datum plruby_return_array _((VALUE, pl_proc_desc *));
extern MemoryContext plruby_spi_context;

extern Datum plruby_dfc0 _((PGFunction));
extern Datum plruby_dfc1 _((PGFunction, Datum));
extern Datum plruby_dfc2 _((PGFunction, Datum, Datum));
extern Datum plruby_dfc3 _((PGFunction, Datum, Datum, Datum));

#ifdef PLRUBY_ENABLE_CONVERSION
extern VALUE plruby_classes, plruby_conversions;
extern Oid plruby_datum_oid _((VALUE, int *));
extern VALUE plruby_datum_set _((VALUE, Datum));
extern VALUE plruby_datum_get _((VALUE, Oid *));
extern VALUE plruby_define_void_class _((char *, char *));
#endif

#define DFC1(a_, b_) DirectFunctionCall1((a_), (b_))

#define OidGD(a_) ObjectIdGetDatum(a_)
#define PointerGD(a_) PointerGetDatum(a_)
#define NameGD(a_) NameGetDatum(a_)
#define BoolGD(a_) BoolGetDatum(a_)
#define IntGD(a_) Int32GetDatum(a_)
#define CStringGD(a_) CStringGetDatum(a_)
#define TupleGD(a_,b_) TupleGetDatum((a_),(b_))


