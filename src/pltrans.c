#include "plruby.h"

#if PG_PL_VERSION >= 75

#define PG_PL_READ_UNCOMMITTED 0
#define PG_PL_READ_COMMITTED   1
#define PG_PL_REPETABLE_READ   2
#define PG_PL_SERIALIZABLE     3

#define PL_ELOG_DEBUG 0

#if PL_ELOG_DEBUG
#define pl_elog(a,b) elog(a,b)
#else
#define pl_elog(a,b)
#endif

static int      pl_in_transaction = 0;
static VALUE pl_eCatch, pl_ePLruby, pl_cTrans;

struct pl_trans {
    VALUE name;
    int commit;
};

static void
pl_trans_mark(void *trans)
{
}

#define GetTrans(obj_, trans_) do {                                     \
    if (TYPE(obj_) != T_DATA ||                                         \
        RDATA(obj_)->dmark != (RUBY_DATA_FUNC)pl_trans_mark) {          \
        rb_raise(rb_eArgError,                                           \
                 "transaction method called with a wrong object");      \
    }                                                                   \
    Data_Get_Struct(obj_, struct pl_trans, trans_);                     \
} while (0)

static char *savename = "savepoint_name";

static DefElem *
make_defelem(char *name, VALUE arg)
{
    DefElem *f = makeNode(DefElem);
    f->defname = name;
    f->arg = (Node *)makeString(RSTRING_PTR(arg));
    return f;
}

        
static VALUE
pl_intern_commit(VALUE obj)
{
    struct pl_trans *trans;
    int rc;

    pl_elog(NOTICE, "==> pl_intern_commit");
    GetTrans(obj, trans);
    PLRUBY_BEGIN_PROTECT(1);
    if (NIL_P(trans->name)) {
        if (!trans->commit) {
            pl_elog(NOTICE, "ReleaseCurrentSubTransaction");
            trans->commit = Qtrue;
            if ((rc =  SPI_finish()) != SPI_OK_FINISH) {
                elog(ERROR, "SPI_finish failed: %s", SPI_result_code_string(rc));
            }
            ReleaseCurrentSubTransaction();
        }
    }
    else {
	List *list;

        pl_elog(NOTICE, "ReleaseSavepoint");
	list = list_make1(make_defelem(savename, trans->name));
        trans->name = Qnil;
        trans->commit = Qtrue;
        ReleaseSavepoint(list);
        CommitTransactionCommand();
        StartTransactionCommand();
    }
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "<== pl_intern_commit");
    return Qnil;
}

struct pl_throw {
    VALUE txn;
    int commit;
};

static void
pl_throw_mark(struct pl_throw *plt)
{
    rb_gc_mark(plt->txn);
}

static VALUE
pl_commit(VALUE obj)
{
    VALUE res;
    struct pl_throw *plt;

    pl_elog(NOTICE, "pl_commit");
    if (!IsSubTransaction()) {
        rb_raise(pl_ePLruby, "outside a transaction");
    }
    res = Data_Make_Struct(pl_cTrans, struct pl_throw, pl_throw_mark, free, plt);
    plt->commit = Qtrue;
    plt->txn = obj;
    rb_throw("__plruby__transaction__", res);
    return Qnil;
}

static VALUE
pl_intern_abort(VALUE obj)
{
    struct pl_trans *trans;
    int rc;

    pl_elog(NOTICE, "==> pl_intern_abort");
    if (!IsSubTransaction()) {
        rb_raise(pl_ePLruby, "outside a transaction");
    }
    GetTrans(obj, trans);
    PLRUBY_BEGIN_PROTECT(1);
    if (NIL_P(trans->name)) {
        if (!trans->commit) {
            pl_elog(NOTICE, "RollbackAndReleaseCurrentSubTransaction");
            trans->commit = Qtrue;
            if ((rc =  SPI_finish()) != SPI_OK_FINISH) {
                elog(ERROR, "SPI_finish failed: %s", SPI_result_code_string(rc));
            }
            RollbackAndReleaseCurrentSubTransaction();
        }
    }
    else {
	List *list;

        pl_elog(NOTICE, "RollbackToSavepoint");
	list = list_make1(make_defelem(savename, trans->name));
        trans->name = Qnil;
        trans->commit = Qtrue;
        RollbackToSavepoint(list);
        CommitTransactionCommand();
        RollbackAndReleaseCurrentSubTransaction();
    }
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "<== pl_intern_abort");
    return Qnil;
}

static VALUE
pl_intern_error(VALUE obj)
{
    struct pl_trans *trans;

    pl_elog(NOTICE, "==> pl_intern_error");
    if (!IsSubTransaction()) {
        rb_raise(pl_ePLruby, "outside a transaction");
    }
    GetTrans(obj, trans);
    PLRUBY_BEGIN_PROTECT(1);
    pl_elog(NOTICE, "RollbackAndReleaseCurrentSubTransaction");
    trans->commit = Qtrue;
    RollbackAndReleaseCurrentSubTransaction();
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "<== pl_intern_error");
    return Qnil;
}

static VALUE
pl_abort(VALUE obj)
{
    VALUE res;
    struct pl_throw *plt;

    pl_elog(NOTICE, "pl_abort");
    if (!IsSubTransaction()) {
        rb_raise(pl_ePLruby, "outside a transaction");
    }
    res = Data_Make_Struct(pl_cTrans, struct pl_throw, pl_throw_mark, free, plt);
    plt->commit = Qfalse;
    plt->txn = obj;
    rb_throw("__plruby__transaction__", res);
    return Qnil;
}

static VALUE
pl_exec(VALUE val, VALUE args, VALUE self)
{
    rb_yield(args);
    return Qnil;
}

static VALUE
pl_catch(VALUE obj)
{
    VALUE res;
    struct pl_throw *plt;

    pl_elog(NOTICE, "pl_catch");
    res = rb_catch("__plruby__transaction__", pl_exec, obj);
    if (TYPE(res) == T_DATA &&
        RDATA(res)->dmark == (RUBY_DATA_FUNC)pl_throw_mark) { 
        Data_Get_Struct(res, struct pl_throw, plt);
        if (plt->commit) {
            pl_intern_commit(obj);
        }
        else {
            pl_intern_abort(obj);
        }
        if (obj != plt->txn) {
            rb_throw("__plruby__transaction__", res);
        }
    }
    else {
        pl_intern_commit(obj);
    }
    return Qnil;
}
        

static VALUE
pl_transaction(VALUE obj)
{
    struct pl_trans *trans;
    VALUE res;
    int state, rc, begin_sub;
    MemoryContext orig_context = 0;


    pl_elog(NOTICE, "==> pl_transaction");
    if (!rb_block_given_p()) {
        rb_raise(rb_eArgError, "no block given");
    }
    res = Data_Make_Struct(pl_cTrans, struct pl_trans, pl_trans_mark, 0, trans);
    trans->name = Qnil;
    PLRUBY_BEGIN_PROTECT(1);
    if (IsSubTransaction()) {
        char name[1024];

        pl_elog(NOTICE, "DefineSavepoint");
        sprintf(name, "__plruby__%d__", pl_in_transaction);
        DefineSavepoint(name);
        CommitTransactionCommand();
        StartTransactionCommand();
        pl_in_transaction++;
        begin_sub = Qfalse;
        trans->name = rb_str_new2(name);
    }
    else {
        pl_in_transaction = 0;
        pl_elog(NOTICE, "BeginTransactionBlock");
        orig_context = CurrentMemoryContext;
        SPI_push();
        BeginInternalSubTransaction(NULL);
        MemoryContextSwitchTo(orig_context);
        if ((rc = SPI_connect()) != SPI_OK_CONNECT) {
            elog(ERROR, "SPI_connect in transaction failed : %s", 
                 SPI_result_code_string(rc));
        }
        begin_sub = Qtrue;
    }
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "==> rb_protect");
    rb_protect(pl_catch, res, &state);
    pl_elog(NOTICE, "<== rb_protect");
    if (state) {
        VALUE error = rb_gv_get("$!");
        if (begin_sub && CLASS_OF(error) == pl_eCatch) {
            if (!trans->commit) {
                rb_protect(pl_intern_error, res, 0);
            }
        }
        else {
            if (!trans->commit) {
                rb_protect(pl_intern_abort, res, 0);
            }
            if (begin_sub) {
                MemoryContextSwitchTo(orig_context);
                SPI_pop();
            }
        }
        rb_jump_tag(state);
    }
    Data_Get_Struct(res, struct pl_trans, trans);
    if (begin_sub) {
        pl_elog(NOTICE, "commit");
        if (!trans->commit) {
            rb_protect(pl_intern_commit, res, 0);
        }
        MemoryContextSwitchTo(orig_context);
        SPI_pop();
    }
    pl_elog(NOTICE, "<== pl_transaction");
    return Qnil;
}
        
#endif

#if PG_PL_VERSION >= 81

static VALUE
pl_savepoint(VALUE obj, VALUE a)
{
    if (!IsTransactionBlock() || !IsSubTransaction()) {
	rb_raise(pl_ePLruby, "savepoint called outside a transaction");
    }
    a = plruby_to_s(a);
    pl_elog(NOTICE, "====> definesavepoint");
    PLRUBY_BEGIN_PROTECT(1);
    DefineSavepoint(RSTRING_PTR(a));
    CommitTransactionCommand();
    StartTransactionCommand();
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "<==== definesavepoint");
    return Qnil;
}

static VALUE
pl_release(VALUE obj, VALUE a)
{
    if (!IsTransactionBlock() || !IsSubTransaction()) {
	rb_raise(pl_ePLruby, "release called outside a transaction");
    }
    a = plruby_to_s(a);
    pl_elog(NOTICE, "====> releasesavepoint");
    PLRUBY_BEGIN_PROTECT(1);
    ReleaseSavepoint(list_make1(make_defelem("savepoint_name", a)));
    CommitTransactionCommand();
    StartTransactionCommand();
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "<==== releasesavepoint");
    return Qnil;
}

static VALUE
pl_rollback(VALUE obj, VALUE a)
{
    if (!IsTransactionBlock() || !IsSubTransaction()) {
	rb_raise(pl_ePLruby, "rollback called outside a transaction");
    }
    a = plruby_to_s(a);
    pl_elog(NOTICE, "====> rollbacksavepoint");
    PLRUBY_BEGIN_PROTECT(1);
    RollbackToSavepoint(list_make1(make_defelem("savepoint_name", a)));
    CommitTransactionCommand();
    RollbackAndReleaseCurrentSubTransaction();
    PLRUBY_END_PROTECT;
    pl_elog(NOTICE, "<==== rollbacksavepoint");
    return Qnil;
}

#endif
    
void
Init_plruby_trans()
{
#if PG_PL_VERSION >= 75
    VALUE pl_mPL;

    pl_mPL = rb_const_get(rb_cObject, rb_intern("PL"));
    pl_ePLruby = rb_const_get(pl_mPL, rb_intern("Error"));
    pl_eCatch = rb_const_get(pl_mPL, rb_intern("Catch"));

    rb_define_global_const("READ_UNCOMMITED", INT2FIX(PG_PL_READ_UNCOMMITTED));
    rb_define_global_const("READ_COMMITED", INT2FIX(PG_PL_READ_COMMITTED));
    rb_define_global_const("REPETABLE_READ", INT2FIX(PG_PL_REPETABLE_READ));
    rb_define_global_const("SERIALIZABLE", INT2FIX(PG_PL_SERIALIZABLE));
    rb_define_global_function("transaction", pl_transaction, 0);
#if PG_PL_VERSION >= 81
    rb_define_global_function("savepoint", pl_savepoint, 1);
    rb_define_global_function("release_savepoint", pl_release, 1);
    rb_define_global_function("rollback_to_savepoint", pl_rollback, 1);
#endif
    pl_cTrans = rb_define_class_under(pl_mPL, "Transaction", rb_cObject);
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_undef_alloc_func(pl_cTrans);
#else
    rb_undef_method(CLASS_OF(pl_cTrans), "allocate");
#endif
    rb_undef_method(CLASS_OF(pl_cTrans), "new");
    rb_define_method(pl_cTrans, "commit", pl_commit, 0);
    rb_define_method(pl_cTrans, "abort", pl_abort, 0);
    rb_define_method(pl_cTrans, "rollback", pl_abort, 0);
#endif
}
