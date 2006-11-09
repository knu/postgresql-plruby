#include "plruby.h"

static VALUE pl_cPLPlan, pl_cPLCursor, pl_ePLruby;
static VALUE pl_eCatch;

static void
query_free(qdesc)
    pl_query_desc *qdesc;
{
    if (qdesc->argtypes) free(qdesc->argtypes);
    if (qdesc->arginfuncs) free(qdesc->arginfuncs);
    if (qdesc->argtypelems) free(qdesc->argtypelems);
    if (qdesc->arglen) free(qdesc->arglen);
    if (qdesc->arg_is_array) free(qdesc->arg_is_array);
    if (qdesc->arg_val) free(qdesc->arg_val);
    if (qdesc->arg_align) free(qdesc->arg_align);
    free(qdesc);
}

static void
query_mark(qdesc)
    pl_query_desc *qdesc;
{
    rb_gc_mark(qdesc->po.argsv);
}

static VALUE
pl_plan_s_alloc(VALUE obj)
{
    pl_query_desc *qdesc;
    return Data_Make_Struct(obj, pl_query_desc, query_mark, 
                            query_free, qdesc);
}

static VALUE
pl_plan_save(VALUE obj)
{
    pl_query_desc *qdesc;
    void *tmp;

    GetPlan(obj, qdesc);

    PLRUBY_BEGIN_PROTECT(1);
    tmp = qdesc->plan;
    qdesc->plan = SPI_saveplan(tmp);
    SPI_freeplan(tmp);
    PLRUBY_END_PROTECT;

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
        rb_iterate(rb_each, argv[argc - 1], plruby_i_each, (VALUE)&(qdesc->po));
        argc--;
    }
    switch (rb_scan_args(argc, argv, "13", &a, &b, &c, &d)) {
    case 4:
        plruby_exec_output(d, RET_ARRAY, &(qdesc->po.output));
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
        qdesc->nargs = RARRAY_LEN(qdesc->po.argsv);
    }
    qdesc->argtypes = NULL;
    if (qdesc->nargs) {
        qdesc->argtypes = ALLOC_N(Oid, qdesc->nargs);
        MEMZERO(qdesc->argtypes, Oid, qdesc->nargs);
        qdesc->arginfuncs = ALLOC_N(FmgrInfo, qdesc->nargs);
        MEMZERO(qdesc->arginfuncs, FmgrInfo, qdesc->nargs);
        qdesc->argtypelems = ALLOC_N(Oid, qdesc->nargs);
        MEMZERO(qdesc->argtypelems, Oid, qdesc->nargs);
        qdesc->arglen = ALLOC_N(int, qdesc->nargs);
        MEMZERO(qdesc->arglen, int, qdesc->nargs);
        qdesc->arg_is_array = ALLOC_N(bool, qdesc->nargs);
        MEMZERO(qdesc->arg_is_array, bool, qdesc->nargs);
        qdesc->arg_val = ALLOC_N(bool, qdesc->nargs);
        MEMZERO(qdesc->arg_val, bool, qdesc->nargs);
        qdesc->arg_align = ALLOC_N(char, qdesc->nargs);
        MEMZERO(qdesc->arg_align, char, qdesc->nargs);
        for (i = 0; i < qdesc->nargs; i++) {
            char *argcopy;
            List *names = NIL;
#if PG_PL_VERSION >= 75
            ListCell *lp;
#else
            List  *lp;
#endif
            TypeName *typename;
            Form_pg_type fpgt;
            int arg_is_array = 0;
            VALUE args = plruby_to_s(RARRAY_PTR(qdesc->po.argsv)[i]);

            PLRUBY_BEGIN_PROTECT(1);
            argcopy  = pstrdup(RSTRING_PTR(args));
            SplitIdentifierString(argcopy, '.', &names);
            typename = makeNode(TypeName);
            foreach (lp, names)
                typename->names = lappend(typename->names, makeString(lfirst(lp)));
#if PG_PL_VERSION >= 82
            typeTup = typenameType(NULL, typename);
#else
            typeTup = typenameType(typename);
#endif
            qdesc->argtypes[i] = HeapTupleGetOid(typeTup);
            fpgt = (Form_pg_type) GETSTRUCT(typeTup);
            arg_is_array = qdesc->arg_is_array[i] = NameStr(fpgt->typname)[0] == '_';
            if (qdesc->arg_is_array[i]) {
                Oid elemtyp;
                HeapTuple typeTuple;

#if PG_PL_VERSION >= 75
                elemtyp = getTypeIOParam(typeTup);
#else
                elemtyp = fpgt->typelem;
#endif
                typeTuple = SearchSysCache(TYPEOID, OidGD(elemtyp), 0, 0, 0);

                if (!HeapTupleIsValid(typeTuple)) {
                    elog(ERROR, "cache lookup failed for type %u", elemtyp);
                }
                fpgt = (Form_pg_type) GETSTRUCT(typeTuple);
                fmgr_info(fpgt->typinput, &(qdesc->arginfuncs[i]));
                qdesc->arglen[i] = fpgt->typlen;
                qdesc->arg_val[i] = fpgt->typbyval;
                qdesc->arg_align[i] = fpgt->typalign;
                ReleaseSysCache(typeTuple);
            }
#if PG_PL_VERSION >= 75
            qdesc->argtypelems[i] = getTypeIOParam(typeTup);
#else
            qdesc->argtypelems[i] = ((Form_pg_type) GETSTRUCT(typeTup))->typelem;
#endif
            if (!arg_is_array) {
                fmgr_info(((Form_pg_type) GETSTRUCT(typeTup))->typinput,
                          &(qdesc->arginfuncs[i]));
                qdesc->arglen[i] = (int) (((Form_pg_type) GETSTRUCT(typeTup))->typlen);
            }
            ReleaseSysCache(typeTup);
#if PG_PL_VERSION >= 75
#define freeList(a_) list_free(a_)
#endif
            freeList(typename->names);
            pfree(typename);
            freeList(names);
            pfree(argcopy);
            PLRUBY_END_PROTECT;

        }
    }

    {
#ifdef PG_PL_TRYCATCH
        PG_TRY();
        {
            plan = SPI_prepare(RSTRING_PTR(a), qdesc->nargs, qdesc->argtypes);
        }
        PG_CATCH();
        {
            plan = NULL;
        }
        PG_END_TRY();
#else
        sigjmp_buf save_restart;
        extern bool InError;

        memcpy(&save_restart, &Warn_restart, sizeof(save_restart));
        if (sigsetjmp(Warn_restart, 1) == 0) {
            PLRUBY_BEGIN_PROTECT(1);
            plan = SPI_prepare(RSTRING_PTR(a), qdesc->nargs, qdesc->argtypes);
            memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));
            PLRUBY_END_PROTECT;
        }
        else {
            memcpy(&Warn_restart, &save_restart, sizeof(Warn_restart));
            InError = 0;
            plan = NULL;
        }
#endif
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
                 reason, RSTRING_PTR(a));
    }
    qdesc->plan = plan;
    if (qdesc->po.save) {
        pl_plan_save(obj);
    }
    return obj;
}

static VALUE
pl_plan_prepare(int argc, VALUE *argv, VALUE obj)
{
    if (!argc || TYPE(argv[argc - 1]) != T_HASH) {
        argv[argc] = rb_hash_new();
        ++argc;
    }
    rb_hash_aset(argv[argc - 1], rb_str_new2("save"), Qtrue);
    return plruby_s_new(argc, argv, pl_cPLPlan);
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
        if (RARRAY_LEN(argsv) != qdesc->nargs) {
            rb_raise(pl_ePLruby, "length of arguments doesn't match # of arguments");
        }
        callnargs = RARRAY_LEN(argsv);
        portal->nargs = callnargs;
        portal->nulls = ALLOC_N(char, callnargs + 1);
        MEMZERO(portal->nulls, char, callnargs + 1);
        portal->argvalues = ALLOC_N(Datum, callnargs);
        MEMZERO(portal->argvalues, Datum, callnargs);
        portal->arglen = ALLOC_N(int, callnargs);
        MEMZERO(portal->arglen, int, callnargs);
        for (j = 0; j < callnargs; j++) {
            if (NIL_P(RARRAY_PTR(argsv)[j])) {
                portal->nulls[j] = 'n';
                portal->argvalues[j] = (Datum)NULL;
            }
            else {
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
                        plruby_return_array(RARRAY_PTR(argsv)[j], &prodesc);
                } 
                else {
                    VALUE args = RARRAY_PTR(argsv)[j];
                    portal->nulls[j] = ' ';
                    portal->arglen[j] = qdesc->arglen[j];
                    portal->argvalues[j] =
                        plruby_to_datum(args, &qdesc->arginfuncs[j],
                                        qdesc->argtypes[j], 
                                        qdesc->argtypelems[j],
                                        -1);
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

VALUE
plruby_i_each(VALUE obj, struct portal_options *po)
{
    VALUE key, value;
    char *options;

    key = rb_ary_entry(obj, 0);
    value = rb_ary_entry(obj, 1);
    key = plruby_to_s(key);
    options = RSTRING_PTR(key);
    if (strcmp(options, "values") == 0 ||
        strcmp(options, "types") == 0) {
        po->argsv = value;
    }
    else if (strcmp(options, "count") == 0) {
        po->count = NUM2INT(value);
    }
    else if (strcmp(options, "output") == 0) {
        plruby_exec_output(value, RET_ARRAY, &(po->output));
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
        rb_iterate(rb_each, argv[argc - 1], plruby_i_each, (VALUE)&portal->po);
        argc--;
    }
    switch (rb_scan_args(argc, argv, "03", &argsv, &countv, &c)) {
    case 3:
        plruby_exec_output(c, RET_ARRAY, &(portal->po.output));
        /* ... */
    case 2:
        if (!NIL_P(countv)) {
            portal->po.count = NUM2INT(countv);
        }
        /* ... */
    case 1:
        portal->po.argsv = argsv;
    }
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
    PLRUBY_BEGIN_PROTECT(1);
    spi_rc = SPI_execp(qdesc->plan, portal->argvalues,
                       portal->nulls, portal->po.count);
    Data_Get_Struct(vortal, struct PLportal, portal);
    free_args(portal);
    PLRUBY_END_PROTECT;
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
            plruby_build_tuple(tuples[0], tupdesc, form);
        }
        else {
            for (i = 0; i < ntuples; i++) {
                rb_yield(plruby_build_tuple(tuples[i], tupdesc, typout));
            }
        }
        result = Qtrue;
    }
    else {
        if (count == 1) {
            result = plruby_build_tuple(tuples[0], tupdesc, typout);
        }
        else {
            result = rb_ary_new2(ntuples);
            for (i = 0; i < ntuples; i++) {
                rb_ary_push(result, 
                            plruby_build_tuple(tuples[i], tupdesc, typout));
            }
        }
    }
    SPI_freetuptable(SPI_tuptable);
    return result;
}

#if PG_PL_VERSION == 74
#define PORTAL_ACTIVE(port) ((port)->portalActive)
#elif PG_PL_VERSION > 74
#define  PORTAL_ACTIVE(port) ((port)->status == PORTAL_ACTIVE)
#else
#define PORTAL_ACTIVE(port) 0
#endif

static VALUE
pl_close(VALUE vortal)
{
    struct PLportal *portal;

    GetPortal(vortal, portal);
    PLRUBY_BEGIN_PROTECT(1);
    if (!PORTAL_ACTIVE(portal->portal)) {
        SPI_cursor_close(portal->portal);
    }
    portal->portal = 0;
    PLRUBY_END_PROTECT;
    return Qnil;
}

static VALUE
pl_portal_name(VALUE vortal)
{
    struct PLportal *portal;

    GetPortal(vortal, portal);
    return rb_tainted_str_new2(portal->portal->name);
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
        PLRUBY_BEGIN_PROTECT(1);
        SPI_cursor_fetch(portal->portal, true, block);
        PLRUBY_END_PROTECT;
        if (SPI_processed <= 0) {
            return Qnil;
        }
        proces = SPI_processed;
        tuptab = SPI_tuptable;
        tuples = tuptab->vals;
        tupdesc = tuptab->tupdesc;
        for (i = 0; i < proces && count != pcount; ++i, ++count) {
            rb_yield(plruby_build_tuple(tuples[i], tupdesc, portal->po.output));
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
    PLRUBY_BEGIN_PROTECT(1);
#if PG_PL_VERSION >= 80
    pgportal = SPI_cursor_open(NULL, qdesc->plan, portal->argvalues,
			       portal->nulls, false);
#else
    pgportal = SPI_cursor_open(NULL, qdesc->plan, 
                               portal->argvalues, portal->nulls);
#endif
    Data_Get_Struct(vortal, struct PLportal, portal);
    free_args(portal);
    PLRUBY_END_PROTECT;
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
            name = RSTRING_PTR(argv[0]);
        }
        --argc; ++argv;
    }
    vortal = create_vortal(argc, argv, obj);
    Data_Get_Struct(vortal, struct PLportal, portal);
    PLRUBY_BEGIN_PROTECT(1);
#if PG_PL_VERSION >= 80
    pgportal = SPI_cursor_open(name, qdesc->plan, portal->argvalues,
			       portal->nulls, false);
#else
    pgportal = SPI_cursor_open(name, qdesc->plan, 
                               portal->argvalues, portal->nulls);
#endif
    PLRUBY_END_PROTECT;
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
    PLRUBY_BEGIN_PROTECT(1);
    spi_rc = SPI_freeplan(qdesc->plan);
    qdesc->plan = 0;
    PLRUBY_END_PROTECT;
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
        PLRUBY_BEGIN_PROTECT(1);
        SPI_cursor_move(portal->portal, forward, count);
        PLRUBY_END_PROTECT;
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
    PLRUBY_BEGIN_PROTECT(1);
    SPI_cursor_fetch(portal->portal, forward, count);
    PLRUBY_END_PROTECT;
    proces = SPI_processed;
    tup = SPI_tuptable;
    if (proces <= 0) {
        return Qnil;
    }
    if (proces == 1) {
        res = plruby_build_tuple(tup->vals[0], tup->tupdesc, portal->po.output);
    }
    else {
        res = rb_ary_new2(proces);
        for (i = 0; i < proces; ++i) {
            rb_ary_push(res, plruby_build_tuple(tup->vals[i], tup->tupdesc, 
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
        PLRUBY_BEGIN_PROTECT(1);
        SPI_cursor_move(portal->portal, 0, 12);
        PLRUBY_END_PROTECT;
        proces = SPI_processed;
    }
    return obj;
}

void Init_plruby_plan()
{
    VALUE pl_mPL;

    pl_mPL = rb_const_get(rb_cObject, rb_intern("PL"));
    pl_ePLruby = rb_const_get(pl_mPL, rb_intern("Error"));
    pl_eCatch = rb_const_get(pl_mPL, rb_intern("Catch"));
    /* deprecated */
    rb_define_module_function(pl_mPL, "spi_prepare", pl_plan_prepare, -1);
    rb_define_module_function(pl_mPL, "prepare", pl_plan_prepare, -1);
    /* ... */
    pl_cPLPlan = rb_define_class_under(pl_mPL, "Plan", rb_cObject);
    rb_include_module(pl_cPLPlan, rb_mEnumerable);
    rb_const_set(rb_cObject, rb_intern("PLrubyPlan"), pl_cPLPlan);
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cPLPlan, pl_plan_s_alloc);
#else
    rb_define_singleton_method(pl_cPLPlan, "allocate", pl_plan_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cPLPlan, "new", plruby_s_new, -1);
    rb_define_private_method(pl_cPLPlan, "initialize", pl_plan_init, -1);
    rb_define_method(pl_cPLPlan, "save", pl_plan_save, 0);
    rb_define_method(pl_cPLPlan, "spi_execp", pl_plan_execp, -1);
    rb_define_method(pl_cPLPlan, "execp", pl_plan_execp, -1);
    rb_define_method(pl_cPLPlan, "exec", pl_plan_execp, -1);
    rb_define_method(pl_cPLPlan, "spi_fetch", pl_plan_each, -1);
    rb_define_method(pl_cPLPlan, "each", pl_plan_each, -1);
    rb_define_method(pl_cPLPlan, "fetch", pl_plan_each, -1);
    rb_define_method(pl_cPLPlan, "cursor", pl_plan_cursor, -1);
    rb_define_method(pl_cPLPlan, "release", pl_plan_release, 0);
    pl_cPLCursor = rb_define_class_under(pl_mPL, "Cursor", rb_cObject);
    rb_undef_method(CLASS_OF(pl_cPLCursor), "allocate");
    rb_undef_method(CLASS_OF(pl_cPLCursor), "new");
    rb_include_module(pl_cPLCursor, rb_mEnumerable);
    rb_define_method(pl_cPLCursor, "each", pl_cursor_each, 0);
    rb_define_method(pl_cPLCursor, "reverse_each", pl_cursor_rev_each, 0);
    rb_define_method(pl_cPLCursor, "close", pl_close, 0);
    rb_define_method(pl_cPLCursor, "portal_name", pl_portal_name, 0);
    rb_define_method(pl_cPLCursor, "fetch", pl_cursor_fetch, -1);
    rb_define_method(pl_cPLCursor, "row", pl_cursor_fetch, -1);
    rb_define_method(pl_cPLCursor, "move", pl_cursor_move, 1);
    rb_define_method(pl_cPLCursor, "rewind", pl_cursor_rewind, 0);
}
