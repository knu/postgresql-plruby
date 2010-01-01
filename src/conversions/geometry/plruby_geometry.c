#include "convcommon.h"

#include <math.h>
#include <limits.h>
#include <float.h>
#include <ctype.h>

#include "libpq/pqformat.h"
#include "utils/geo_decls.h"

static VALUE pl_cPoint;

static VALUE
pl_convert(VALUE obj, ID id, void (*func)())
{
    if (TYPE(obj) == T_DATA &&
        RDATA(obj)->dmark == (RUBY_DATA_FUNC)func) {
        return obj;
    }
    obj = rb_funcall(obj, id, 0, 0);
    if (TYPE(obj) != T_DATA ||
        RDATA(obj)->dmark != (RUBY_DATA_FUNC)func) {
        rb_raise(rb_eArgError, "invalid conversion");
    }
    return obj;
}

static void pl_point_mark(Point *p) {}
static void pl_circle_mark(CIRCLE *l) {}
static void pl_poly_mark(POLYGON *l) {}

#define INIT_COPY(init_copy, type_struct, mark_func)                    \
static VALUE                                                            \
init_copy(VALUE copy, VALUE orig)                                       \
{                                                                       \
    type_struct *t0, *t1;                                               \
                                                                        \
    if (copy == orig) return copy;                                      \
    if (TYPE(orig) != T_DATA ||                                         \
        RDATA(orig)->dmark != (RUBY_DATA_FUNC)mark_func) {              \
        rb_raise(rb_eTypeError, "wrong argument type to clone");        \
    }                                                                   \
    Data_Get_Struct(orig, type_struct, t0);                             \
    Data_Get_Struct(copy, type_struct, t1);                             \
    MEMCPY(t1, t0, type_struct, 1);                                     \
    return copy;                                                        \
}

static VALUE
pl_point_s_alloc(VALUE obj)
{
    Point *p;
    return Data_Make_Struct(obj, Point, pl_point_mark, free, p);
}

INIT_COPY(pl_point_init_copy, Point, pl_point_mark);

static VALUE
pl_point_s_datum(VALUE obj, VALUE a)
{
    Point *p0, *p1;
    Oid typoid;
    VALUE res;

    p0 = (Point *)plruby_datum_get(a, &typoid);
    if (typoid != POINTOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    p1 = ALLOC_N(Point, 1);
    memcpy(p1, p0, sizeof(Point));
    res = Data_Wrap_Struct(obj, pl_point_mark, free, p1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_point_to_datum(VALUE obj, VALUE a)
{
    Point *p0, *p1;
    int typoid;

    typoid = plruby_datum_oid(a, 0);
    if (typoid != POINTOID) {
        return Qnil;
    }
    Data_Get_Struct(obj, Point, p0);
    p1 = (Point *)palloc(sizeof(Point));
    memcpy(p1, p0, sizeof(Point));
    return plruby_datum_set(a, (Datum)p1);
}

PL_MLOAD(pl_point_mload, point_recv, Point);
PL_MDUMP(pl_point_mdump, point_send);

static VALUE
pl_point_s_str(VALUE obj, VALUE a)
{
    Point *p;
    VALUE res;

    a = plruby_to_s(a);
    res = Data_Make_Struct(obj, Point, pl_point_mark, free, p);
    CPY_FREE(p, PLRUBY_DFC1(point_in,  RSTRING_PTR(a)), sizeof(Point));
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_point_init(VALUE obj, VALUE a, VALUE b)
{
    Point *point;

    Data_Get_Struct(obj, Point, point);
    a = rb_Float(a);
    b = rb_Float(b);
    point->x = RFLOAT_VALUE(a);
    point->y = RFLOAT_VALUE(b);
    return obj;
}

static VALUE
pl_point_x(VALUE obj)
{
    Point *point;
    VALUE res;

    Data_Get_Struct(obj, Point, point);
    res = rb_float_new(point->x);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_point_setx(VALUE obj, VALUE a)
{
    Point *point;

    Data_Get_Struct(obj, Point, point);
    a = rb_Float(a);
    point->x = RFLOAT_VALUE(a);
    return a;
}

static VALUE
pl_point_y(VALUE obj)
{
    Point *point;
    VALUE res;

    Data_Get_Struct(obj, Point, point);
    res = rb_float_new(point->y);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_point_sety(VALUE obj, VALUE a)
{
    Point *point;

    Data_Get_Struct(obj, Point, point);
    a = rb_Float(a);
    point->y = RFLOAT_VALUE(a);
    return a;
}

static VALUE
pl_point_aref(VALUE obj, VALUE a)
{
    Point *point;
    int i;
    VALUE res;

    Data_Get_Struct(obj, Point, point);
    i = NUM2INT(rb_Integer(a));
    if (i < 0) i = -i;
    switch (i) {
    case 0:
        res = rb_float_new(point->x);
        break;

    case 1:
        res = rb_float_new(point->y);
        break;

    default:
        res = Qnil;
    }
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_point_aset(VALUE obj, VALUE a, VALUE b)
{
    Point *point;
    int i;

    Data_Get_Struct(obj, Point, point);
    i = NUM2INT(rb_Integer(a));
    b = rb_Float(b);
    if (i < 0) i = -i;
    switch (i) {
    case 0:
        point->x = RFLOAT_VALUE(b);
        break;
    case 1:
        point->y = RFLOAT_VALUE(b);
        break;
    default:
        rb_raise(rb_eArgError, "[]= invalid indice");
    }
    return b;
}

#define TO_STRING(NAME_,FUNCTION_)              \
static VALUE                                    \
NAME_(VALUE obj)                                \
{                                               \
    Point *p;                                   \
    char *str;                                  \
                                                \
    Data_Get_Struct(obj, Point, p);             \
    str = (char *)PLRUBY_DFC1(FUNCTION_, p);    \
    if (OBJ_TAINTED(obj)) {                     \
        return rb_tainted_str_new2(str);        \
    }                                           \
    return rb_str_new2(str);                    \
}

TO_STRING(pl_point_to_s,point_out);

#define CHECK_CLASS(obj, a)                                     \
    if (!rb_obj_is_kind_of(a, rb_obj_class(obj))) {             \
        rb_raise(rb_eArgError, "invalid classes (%s, %s)",      \
                 rb_class2name(rb_obj_class(obj)),              \
                 rb_class2name(rb_obj_class(a)));               \
    }

#define POINT_CALL(NAME_,FUNCTION_)                             \
static VALUE                                                    \
NAME_(VALUE obj, VALUE a)                                       \
{                                                               \
    Point *p0, *p1, *pr;                                        \
    VALUE res;                                                  \
                                                                \
    if (TYPE(a) == T_DATA &&                                    \
        RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {     \
        Data_Get_Struct(obj, Point, p0);                        \
        Data_Get_Struct(a, Point, p1);                          \
        res = Data_Make_Struct(rb_obj_class(obj), Point,        \
                               pl_point_mark, free, pr);        \
        CPY_FREE(pr, PLRUBY_DFC2(FUNCTION_, p0, p1), sizeof(Point));   \
        if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res); \
        return res;                                             \
    }                                                           \
    return rb_funcall(a, rb_frame_last_func(), 1, obj);         \
}

POINT_CALL(pl_point_add,point_add);
POINT_CALL(pl_point_sub,point_sub);
POINT_CALL(pl_point_mul,point_mul);
POINT_CALL(pl_point_div,point_div);

#define POINT_CALL_BOOL(NAME_,FUNCTION_)        \
static VALUE                                    \
NAME_(VALUE obj, VALUE a)                       \
{                                               \
    Point *p0, *p1;                     \
                                                \
    CHECK_CLASS(obj, a);                        \
    Data_Get_Struct(obj, Point, p0);            \
    Data_Get_Struct(a, Point, p1);              \
    if (PLRUBY_DFC2(FUNCTION_, p0, p1))         \
        return Qtrue;                           \
    return Qfalse;                              \
}

POINT_CALL_BOOL(pl_point_left,point_left);
POINT_CALL_BOOL(pl_point_right,point_right);
POINT_CALL_BOOL(pl_point_above,point_above);
POINT_CALL_BOOL(pl_point_below,point_below);
POINT_CALL_BOOL(pl_point_vert,point_vert);
POINT_CALL_BOOL(pl_point_horiz,point_horiz);
POINT_CALL_BOOL(pl_point_eq,point_eq);

#ifdef USE_FLOAT8_BYVAL
#define RETURN_FLOAT(obj_, function_) do {      \
    float8 f_;                                  \
    VALUE res_;                                 \
    f_ = DatumGetFloat8(function_);             \
    res_ = rb_float_new(f_);                    \
    if (OBJ_TAINTED(obj_)) OBJ_TAINT(res_);     \
    return res_;                                \
} while (0)

#define RETURN_FLOAT2(obj_, a_, function_) do { \
    float8 f_;                                  \
    VALUE res_;                                 \
    f_ = DatumGetFloat8(function_);             \
    res_ = rb_float_new(f_);                    \
    if (OBJ_TAINTED(obj_) ||                    \
        OBJ_TAINTED(a_)) OBJ_TAINT(res_);       \
    return res_;                                \
} while (0)
#else
#define RETURN_FLOAT(obj_, function_) do {      \
    float8 *f_;                                 \
    VALUE res_;                                 \
    f_ = (float8 *)function_;                   \
    if (!f_) res_ = rb_float_new(0.0);          \
    else {                                      \
        res_ = rb_float_new(*f_);               \
        pfree(f_);                              \
    }                                           \
    if (OBJ_TAINTED(obj_)) OBJ_TAINT(res_);     \
    return res_;                                \
} while (0)

#define RETURN_FLOAT2(obj_, a_, function_) do { \
    float8 *f_;                                 \
    VALUE res_;                                 \
    f_ = (float8 *)function_;                   \
    if (!f_) res_ = rb_float_new(0.0);          \
    else {                                      \
        res_ = rb_float_new(*f_);               \
        pfree(f_);                              \
    }                                           \
    if (OBJ_TAINTED(obj_) ||                    \
        OBJ_TAINTED(a_)) OBJ_TAINT(res_);       \
    return res_;                                \
} while (0)
#endif


static VALUE
pl_point_slope(VALUE obj, VALUE a)
{
    Point *p0, *p1;                                
                                                        
    CHECK_CLASS(obj, a);
    Data_Get_Struct(obj, Point, p0);
    Data_Get_Struct(a, Point, p1);
    RETURN_FLOAT2(obj, a, PLRUBY_DFC2(point_slope, p0, p1));
}

static void pl_lseg_mark(LSEG *l) {}

static VALUE
pl_lseg_s_alloc(VALUE obj)
{
    LSEG *lseg;
    return Data_Make_Struct(obj, LSEG, pl_lseg_mark, free, lseg);
}

INIT_COPY(pl_lseg_init_copy, LSEG, pl_lseg_mark);

static VALUE
pl_lseg_s_datum(VALUE obj, VALUE a)
{
    LSEG *p0, *p1;
    Oid typoid;
    VALUE res;

    p0 = (LSEG *)plruby_datum_get(a, &typoid);
    if (typoid != LSEGOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    p1 = ALLOC_N(LSEG, 1);
    memcpy(p1, p0, sizeof(LSEG));
    res = Data_Wrap_Struct(obj, pl_lseg_mark, free, p1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_lseg_to_datum(VALUE obj, VALUE a)
{
    LSEG *p0, *p1;
    int typoid;

    typoid = plruby_datum_oid(a, 0);
    if (typoid != LSEGOID) {
        return Qnil;
    }
    Data_Get_Struct(obj, LSEG, p0);
    p1 = (LSEG *)palloc(sizeof(LSEG));
    memcpy(p1, p0, sizeof(LSEG));
    return plruby_datum_set(a, (Datum)p1);
}

PL_MLOAD(pl_lseg_mload, lseg_recv, LSEG);
PL_MDUMP(pl_lseg_mdump, lseg_send);

static VALUE
pl_lseg_s_str(VALUE obj, VALUE a)
{
    LSEG *l;
    VALUE res;

    a = plruby_to_s(a);
    res = Data_Make_Struct(obj, LSEG, pl_lseg_mark, free, l);
    CPY_FREE(l, PLRUBY_DFC1(lseg_in, RSTRING_PTR(a)), sizeof(LSEG));
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_lseg_init(VALUE obj, VALUE a, VALUE b)
{
    LSEG *l;
    Point *p;

    a = pl_convert(a, rb_intern("to_point"), pl_point_mark);
    b = pl_convert(b, rb_intern("to_point"), pl_point_mark);
    Data_Get_Struct(obj, LSEG, l);
    Data_Get_Struct(a, Point, p);
    l->p[0].x = p->x;
    l->p[0].y = p->y;
    Data_Get_Struct(b, Point, p);
    l->p[1].x = p->x;
    l->p[1].y = p->y;
    return obj;
}

TO_STRING(pl_lseg_to_s,lseg_out);

static VALUE
pl_lseg_aref(VALUE obj, VALUE a)
{
    LSEG *lseg;
    Point *point;
    int i;
    VALUE res;

    Data_Get_Struct(obj, LSEG, lseg);
    i = NUM2INT(rb_Integer(a));
    if (i < 0) i = -i;
    switch (i) {
    case 0:
        res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, point);
        point->x = lseg->p[0].x;
        point->y = lseg->p[0].y;
        break;

    case 1:
        res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, point);
        point->x = lseg->p[1].x;
        point->y = lseg->p[1].y;
        break;

    default:
        res = Qnil;
    }
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_lseg_aset(VALUE obj, VALUE a, VALUE b)
{
    LSEG *lseg;
    Point *point;
    int i;

    Data_Get_Struct(obj, LSEG, lseg);
    i = NUM2INT(rb_Integer(a));
    b = pl_convert(b, rb_intern("to_point"), pl_point_mark);
    Data_Get_Struct(b, Point, point);
    if (i < 0) i = -i;
    switch (i) {
    case 0:
        lseg->p[0].x = point->x;
        lseg->p[0].y = point->y;
        break;
    case 1:
        lseg->p[1].x = point->x;
        lseg->p[1].y = point->y;
        break;
    default:
        rb_raise(rb_eArgError, "[]= invalid indice");
    }
    return b;
}


static VALUE
pl_lseg_length(VALUE obj)
{
    LSEG *l;

    Data_Get_Struct(obj, LSEG, l);
    RETURN_FLOAT(obj, PLRUBY_DFC1(lseg_length, l));
}

static VALUE
pl_lseg_parallel(VALUE obj, VALUE a)
{
    LSEG *l0, *l1;

    CHECK_CLASS(obj, a);
    Data_Get_Struct(obj, LSEG, l0);
    Data_Get_Struct(a, LSEG, l1);
    if (PLRUBY_DFC2(lseg_parallel, l0, l1)) return Qtrue;
    return Qfalse;
}

static VALUE
pl_lseg_perp(VALUE obj, VALUE a)
{
    LSEG *l0, *l1;

    CHECK_CLASS(obj, a);
    Data_Get_Struct(obj, LSEG, l0);
    Data_Get_Struct(a, LSEG, l1);
    if (PLRUBY_DFC2(lseg_perp, l0, l1)) return Qtrue;
    return Qfalse;
}

#define LSEG_BOOL(NAME_,FUNCTION_)              \
static VALUE                                    \
NAME_(VALUE obj)                                \
{                                               \
    LSEG *l;                                    \
                                                \
    Data_Get_Struct(obj, LSEG, l);              \
    if (PLRUBY_DFC1(FUNCTION_, l)) return Qtrue;\
    return Qfalse;                              \
}

LSEG_BOOL(pl_lseg_horizontal,lseg_horizontal);
LSEG_BOOL(pl_lseg_vertical,lseg_vertical);

static VALUE
pl_lseg_cmp(VALUE obj, VALUE a)
{
    LSEG *l0, *l1;

    if (!rb_obj_is_kind_of(a, rb_obj_class(obj))) {
        return Qnil;
    }
    Data_Get_Struct(obj, LSEG, l0);
    Data_Get_Struct(a, LSEG, l1);
    if (PLRUBY_DFC2(lseg_eq, l0, l1)) return INT2NUM(0);
    if (PLRUBY_DFC2(lseg_lt, l0, l1)) return INT2NUM(-1);
    return INT2NUM(1);
}

static VALUE
pl_lseg_center(VALUE obj)
{
    LSEG *l;
    Point *p;
    VALUE res;

    Data_Get_Struct(obj, LSEG, l);
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p);
    CPY_FREE(p, PLRUBY_DFC1(lseg_center, l), sizeof(Point));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_lseg_closest(VALUE obj, VALUE a)
{
    LSEG *l0, *l1;
    Point *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, LSEG, l0);
    if (TYPE(a) == T_DATA) {
        if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *p;

            Data_Get_Struct(a, Point, p);
            p0 = (Point *)PLRUBY_DFC2(close_ps, p, l0);
            if (!p0) return Qnil;
            res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
            CPY_FREE(p1, p0, sizeof(Point));
            if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
            return res;
        }
        if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
            Data_Get_Struct(a, LSEG, l1);
            p0 = (Point *)PLRUBY_DFC2(close_lseg, l0, l1);
            if (!p0) return Qnil;
            res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
            CPY_FREE(p1, p0, sizeof(Point));
            if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
            return res;
        }
    }
    return rb_funcall(a, rb_frame_last_func(), 1, obj);
}

static VALUE
pl_lseg_intersect(VALUE obj, VALUE a)
{
    LSEG *l0, *l1;

    Data_Get_Struct(obj, LSEG, l0);
    if (TYPE(a) == T_DATA &&
        RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
        Data_Get_Struct(a, LSEG, l1);
        if (PLRUBY_DFC2(lseg_intersect, l0, l1)) return Qtrue;
        return Qfalse;
    }
    return rb_funcall(a, rb_frame_last_func(), 1, obj);
}

static VALUE
pl_lseg_intersection(VALUE obj, VALUE a)
{
    LSEG *l0, *l1;
    Point *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, LSEG, l0);
    if (!rb_obj_is_kind_of(a, rb_obj_class(obj))) {
        rb_raise(rb_eArgError, "intersection : expected a Segment");
    }
    Data_Get_Struct(a, LSEG, l1);
    p0 = (Point *)PLRUBY_DFC2(lseg_interpt, l0, l1);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(Point));
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static void pl_box_mark(BOX *l) {}

static VALUE
pl_box_s_alloc(VALUE obj)
{
    BOX *box;
    return Data_Make_Struct(obj, BOX, pl_box_mark, free, box);
}

INIT_COPY(pl_box_init_copy, BOX, pl_box_mark);

static VALUE
pl_box_s_datum(VALUE obj, VALUE a)
{
    BOX *p0, *p1;
    Oid typoid;
    VALUE res;

    p0 = (BOX *)plruby_datum_get(a, &typoid);
    if (typoid != BOXOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    p1 = ALLOC_N(BOX, 1);
    memcpy(p1, p0, sizeof(BOX));
    res = Data_Wrap_Struct(obj, pl_box_mark, free, p1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_box_to_datum(VALUE obj, VALUE a)
{
    BOX *p0, *p1;
    int typoid;

    typoid = plruby_datum_oid(a, 0);
    switch (typoid) {
    case BOXOID:
        break;

    case CIRCLEOID:
        obj = pl_convert(obj, rb_intern("to_circle"), pl_circle_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case POLYGONOID:
        obj = pl_convert(obj, rb_intern("to_poly"), pl_poly_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case POINTOID:
        obj = pl_convert(obj, rb_intern("to_point"), pl_point_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    default:
        return Qnil;
    }
    Data_Get_Struct(obj, BOX, p0);
    p1 = (BOX *)palloc(sizeof(BOX));
    memcpy(p1, p0, sizeof(BOX));
    return plruby_datum_set(a, (Datum)p1);
}

PL_MLOAD(pl_box_mload, box_recv, BOX);
PL_MDUMP(pl_box_mdump, box_send);

static VALUE
pl_box_s_str(VALUE obj, VALUE a)
{
    BOX *l;
    VALUE res;

    a = plruby_to_s(a);
    res = Data_Make_Struct(obj, BOX, pl_box_mark, free, l);
    CPY_FREE(l, PLRUBY_DFC1(box_in, RSTRING_PTR(a)), sizeof(BOX));
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static void
pl_box_adjust(BOX *bx)
{
    if (bx->high.x < bx->low.x) {
        double x = bx->high.x;
        bx->high.x = bx->low.x;
        bx->low.x = x;
    }
    if (bx->high.y < bx->low.y) {
        double y = bx->high.y;
        bx->high.y = bx->low.y;
        bx->low.y = y;
    }
}

static VALUE
pl_box_init(int argc, VALUE *argv, VALUE obj)
{
    BOX *bx;

    if (argc != 2 && argc != 4) {
        rb_raise(rb_eArgError, "initialize : expected 2 Points");
    }
    Data_Get_Struct(obj, BOX, bx);
    if (argc == 2) {
        Point *p0, *p1;
        VALUE a, b;

        a = argv[0];
        b = argv[1];
        if (TYPE(a) != T_DATA ||
            RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_point_mark) {
            a = pl_convert(a, rb_intern("to_point"), pl_point_mark);
        }
        if (TYPE(b) != T_DATA ||
            RDATA(b)->dmark != (RUBY_DATA_FUNC)pl_point_mark) {
            b = pl_convert(b, rb_intern("to_point"), pl_point_mark);
        }
        Data_Get_Struct(a, Point, p0);
        Data_Get_Struct(b, Point, p1);
        bx->low.x = p0->x;
        bx->low.y = p0->y;
        bx->high.x = p1->x;
        bx->high.y = p1->y;
    }
    else {
        bx->low.x = RFLOAT_VALUE(rb_Float(argv[0]));
        bx->low.y = RFLOAT_VALUE(rb_Float(argv[1]));
        bx->high.x = RFLOAT_VALUE(rb_Float(argv[2]));
        bx->high.y = RFLOAT_VALUE(rb_Float(argv[3]));
    }
    pl_box_adjust(bx);
    return obj;
}

TO_STRING(pl_box_to_s,box_out);

static VALUE
pl_box_aref(VALUE obj, VALUE a)
{
    BOX *box;
    Point *point;
    int i;
    VALUE res;

    Data_Get_Struct(obj, BOX, box);
    i = NUM2INT(rb_Integer(a));
    if (i < 0) i = -i;
    switch (i) {
    case 0:
        res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, point);
        point->x = box->low.x;
        point->y = box->low.y;
        break;

    case 1:
        res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, point);
        point->x = box->high.x;
        point->y = box->high.y;
        break;

    default:
        res = Qnil;
    }
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_box_low(VALUE obj)
{
    BOX *box;
    Point *point;
    VALUE res;

    Data_Get_Struct(obj, BOX, box);
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, point);
    point->x = box->low.x;
    point->y = box->low.y;
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_box_high(VALUE obj)
{
    BOX *box;
    Point *point;
    VALUE res;

    Data_Get_Struct(obj, BOX, box);
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, point);
    point->x = box->high.x;
    point->y = box->high.y;
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_box_aset(VALUE obj, VALUE a, VALUE b)
{
    BOX *box;
    Point *point;
    int i;

    Data_Get_Struct(obj, BOX, box);
    i = NUM2INT(rb_Integer(a));
    b = pl_convert(b, rb_intern("to_point"), pl_point_mark);
    Data_Get_Struct(b, Point, point);
    if (i < 0) i = -i;
    switch (i) {
    case 0:
        box->low.x = point->x;
        box->low.y = point->y;
        break;
    case 1:
        box->high.x = point->x;
        box->high.y = point->y;
        break;
    default:
        rb_raise(rb_eArgError, "[]= invalid indice");
    }
    pl_box_adjust(box);
    return b;
}

static VALUE
pl_box_lowset(VALUE obj, VALUE a)
{
    BOX *box;
    Point *point;

    Data_Get_Struct(obj, BOX, box);
    a = pl_convert(a, rb_intern("to_point"), pl_point_mark);
    Data_Get_Struct(a, Point, point);
    box->low.x = point->x;
    box->low.y = point->y;
    pl_box_adjust(box);
    return a;
}

static VALUE
pl_box_highset(VALUE obj, VALUE a)
{
    BOX *box;
    Point *point;

    Data_Get_Struct(obj, BOX, box);
    a = pl_convert(a, rb_intern("to_point"), pl_point_mark);
    Data_Get_Struct(a, Point, point);
    box->high.x = point->x;
    box->high.y = point->y;
    pl_box_adjust(box);
    return a;
}

static VALUE
pl_box_cmp(VALUE obj, VALUE a)
{
    BOX *l0, *l1;

    if (!rb_obj_is_kind_of(a, rb_obj_class(obj))) {
        return Qnil;
    }
    Data_Get_Struct(obj, BOX, l0);
    Data_Get_Struct(a, BOX, l1);
    if (PLRUBY_DFC2(box_eq, l0, l1)) return INT2NUM(0);
    if (PLRUBY_DFC2(box_lt, l0, l1)) return INT2NUM(-1);
    return INT2NUM(1);
}

#define BOX_BOOL(NAME_,FUNCTION_)			\
static VALUE						\
NAME_(VALUE obj, VALUE a)				\
{							\
    BOX *p0, *p1;					\
							\
    CHECK_CLASS(obj, a);				\
    Data_Get_Struct(obj, BOX, p0);			\
    Data_Get_Struct(a, BOX, p1);			\
    if (PLRUBY_DFC2(FUNCTION_, p0, p1)) return Qtrue;	\
    return Qfalse;					\
}

BOX_BOOL(pl_box_same,box_same);
BOX_BOOL(pl_box_overlap,box_overlap);
BOX_BOOL(pl_box_overleft,box_overleft);
BOX_BOOL(pl_box_left,box_left);
BOX_BOOL(pl_box_right,box_right);
BOX_BOOL(pl_box_overright,box_overright);
BOX_BOOL(pl_box_contained,box_contained);
BOX_BOOL(pl_box_contain,box_contain);
BOX_BOOL(pl_box_below,box_below);
BOX_BOOL(pl_box_above,box_above);

#define BOX_CALL(NAME_,FUNCTION_)                               \
static VALUE                                                    \
NAME_(VALUE obj, VALUE a)                                       \
{                                                               \
    BOX *p0, *pr;                                               \
    VALUE res;                                                  \
    Point *pt;                                                  \
                                                                \
    Data_Get_Struct(obj, BOX, p0);                              \
    a = pl_convert(a, rb_intern("to_point"), pl_point_mark);    \
    Data_Get_Struct(a, Point, pt);                              \
    res = Data_Make_Struct(rb_obj_class(obj), BOX,              \
                           pl_box_mark, free, pr);              \
    CPY_FREE(pr, PLRUBY_DFC2(FUNCTION_, p0, pt), sizeof(BOX));  \
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);     \
    return res;                                                 \
}

BOX_CALL(pl_box_add,box_add);
BOX_CALL(pl_box_sub,box_sub);
BOX_CALL(pl_box_mul,box_mul);
BOX_CALL(pl_box_div,box_div);

#define BOX_FLOAT(NAME_,FUNCTION_)              \
static VALUE                                    \
NAME_(VALUE obj)                                \
{                                               \
    BOX *l;                                     \
                                                \
    Data_Get_Struct(obj, BOX, l);               \
    RETURN_FLOAT(obj, PLRUBY_DFC1(FUNCTION_, l));\
}

BOX_FLOAT(pl_box_area,box_area);
BOX_FLOAT(pl_box_height,box_height);
BOX_FLOAT(pl_box_width,box_width);

static VALUE
pl_box_center(VALUE obj)
{
    BOX *b;
    Point *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, BOX, b);
    p0 = (Point *)PLRUBY_DFC1(box_center, b);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(Point));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_box_closest(VALUE obj, VALUE a)
{
    BOX *l0;
    Point *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, BOX, l0);
    if (TYPE(a) == T_DATA) {
        if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *p;

            Data_Get_Struct(a, Point, p);
            p0 = (Point *)PLRUBY_DFC2(close_pb, p, l0);
            if (!p0) return Qnil;
            res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
            CPY_FREE(p1, p0, sizeof(Point));
            if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
            return res;
        }
        if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
            LSEG *p;

            Data_Get_Struct(a, LSEG, p);
            p0 = (Point *)PLRUBY_DFC2(close_sb, p, l0);
            if (!p0) return Qnil;
            res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
            CPY_FREE(p1, p0, sizeof(Point));
            if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
            return res;
        }
    }
    rb_raise(rb_eArgError, "closest : invalid argument");
    return Qnil;
}

static VALUE
pl_box_intersect(VALUE obj, VALUE a)
{
    BOX *l0;

    Data_Get_Struct(obj, BOX, l0);
    if (TYPE(a) == T_DATA) {
        if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
            LSEG *p;

            Data_Get_Struct(a, LSEG, p);
            if (PLRUBY_DFC2(inter_sb, p, l0)) return Qtrue;
            return Qfalse;
        }
    }
    rb_raise(rb_eArgError, "intersect : invalid argument");
    return Qnil;
}

static VALUE
pl_box_intersection(VALUE obj, VALUE a)
{
    BOX *l0, *l1, *l2;
    VALUE res;

    Data_Get_Struct(obj, BOX, l0);
    a = pl_convert(a, rb_intern("to_box"), pl_box_mark);
    Data_Get_Struct(a, BOX, l1);
    l1 = (BOX *)PLRUBY_DFC2(box_intersect, l0, l1);
    if (!l1) return Qnil;
    res = Data_Make_Struct(rb_obj_class(obj), BOX, pl_box_mark, free, l2);
    CPY_FREE(l2, l1, sizeof(BOX));
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_box_diagonal(VALUE obj)
{
    BOX *b;
    LSEG *l;
    VALUE res;

    Data_Get_Struct(obj, BOX, b);
    res = Data_Make_Struct(obj, LSEG, pl_lseg_mark, free, l);
    CPY_FREE(l, PLRUBY_DFC1(box_diagonal, b), sizeof(LSEG));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static void pl_path_mark(PATH *l) {}

#ifndef SET_VARSIZE
#define SET_VARSIZE(p__, s__) (p__)->size = s__
#define GET_VARSIZE(t__, p__) (p__)->size
#else
#define GET_VARSIZE(t__, p__) (offsetof(t__, p[0]) + sizeof(p__->p[0]) * (p__)->npts)
#endif

static VALUE
pl_path_s_alloc(VALUE obj)
{
    PATH *path;
    VALUE res;
    int sz;

    res = Data_Make_Struct(obj, PATH, pl_path_mark, free, path);
    sz = GET_VARSIZE(PATH, path);
    SET_VARSIZE(path, sz);
    return res;
}

static VALUE
pl_path_init_copy(VALUE copy, VALUE orig)
{
    PATH *p0, *p1;
    int sz0, sz1;

    if (copy == orig) return copy;
    if (TYPE(orig) != T_DATA ||
        RDATA(orig)->dmark != (RUBY_DATA_FUNC)pl_path_mark) {
        rb_raise(rb_eTypeError, "wrong argument type to clone");
    }
    Data_Get_Struct(orig, PATH, p0);
    Data_Get_Struct(copy, PATH, p1);
    sz0 = GET_VARSIZE(PATH, p0);
    sz1 = GET_VARSIZE(PATH, p1);
    if (sz0 != sz1) {
        free(p1);
        RDATA(copy)->data = 0;
        p1 = (PATH *)ALLOC_N(char, sz0);
        SET_VARSIZE(p1, sz0);
        RDATA(copy)->data = p1;
    }
    memcpy(p1, p0, sz0);
    return copy;
}

static VALUE
pl_path_s_datum(VALUE obj, VALUE a)
{
    PATH *p0, *p1;
    Oid typoid;
    VALUE res;
    int sz0;

    p0 = (PATH *)plruby_datum_get(a, &typoid);
    if (typoid != PATHOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    sz0 = GET_VARSIZE(PATH, p0);
    p1 = (PATH *)ALLOC_N(char, sz0);
    memcpy(p1, p0, sz0);
    res = Data_Wrap_Struct(obj, pl_path_mark, free, p1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_path_to_datum(VALUE obj, VALUE a)
{
    PATH *p0, *p1;
    int typoid;
    int sz0;

    typoid = plruby_datum_oid(a, 0);
    switch (typoid) {
    case PATHOID:
        break;

    case POLYGONOID:
        obj = pl_convert(obj, rb_intern("to_poly"), pl_poly_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case POINTOID:
        obj = pl_convert(obj, rb_intern("to_point"), pl_point_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    default:
        return Qnil;
    }
    Data_Get_Struct(obj, PATH, p0);
    sz0 = GET_VARSIZE(PATH, p0);
    p1 = (PATH *)palloc(sz0);
    memcpy(p1, p0, sz0);
    return plruby_datum_set(a, (Datum)p1);
}

#define PATHSIZE(p_) GET_VARSIZE(PATH, p_)
PL_MLOADVAR(pl_path_mload, path_recv, PATH, PATHSIZE);
PL_MDUMP(pl_path_mdump, path_send);

static VALUE
pl_path_s_str(VALUE obj, VALUE a)
{
    PATH *p, *m;
    VALUE res;
    int sz0;

    a = plruby_to_s(a);
    m = (PATH *)PLRUBY_DFC1(path_in, RSTRING_PTR(a));
    sz0 = GET_VARSIZE(PATH, m);
    p = (PATH *)ALLOC_N(char, sz0);
    CPY_FREE(p, m, sz0);
    res = Data_Wrap_Struct(obj, pl_path_mark, free, p);
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_path_init(int argc, VALUE *argv, VALUE obj)
{
    PATH *p;
    VALUE a;
    int i, size, closed = Qfalse;

    if (argc < 1 || argc > 2) {
        rb_raise(rb_eArgError, "expected Array of Points");
    }
    if (argc == 2) {
        closed = RTEST(argv[1]);
    }
    a = rb_Array(argv[0]);
    Data_Get_Struct(obj, PATH, p);
    free(p);
    size = offsetof(PATH, p[0]) + sizeof(p->p[0]) * RARRAY_LEN(a);
    p = (PATH *)ALLOC_N(char, size);
    MEMZERO(p, char, size);
    p->closed = closed;
    DATA_PTR(obj) = p;
    for (i = 0; i < RARRAY_LEN(a); ++i) {
        VALUE b = RARRAY_PTR(a)[i];
        if (TYPE(b) == T_DATA &&
            RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *po;

            Data_Get_Struct(b, Point, po);
            p->p[i].x = po->x;
            p->p[i].y = po->y;
        }
        else {
            VALUE tmp;

            b = rb_Array(b);
            if (RARRAY_LEN(b) != 2) {
                rb_raise(rb_eArgError, "initialize : expected Array [x, y]");
            }
            tmp = rb_Float(RARRAY_PTR(b)[0]);
            p->p[i].x = RFLOAT_VALUE(tmp);
            tmp = rb_Float(RARRAY_PTR(b)[1]);
            p->p[i].y = RFLOAT_VALUE(tmp);
        }
    }
    p->npts = RARRAY_LEN(a);
    return obj;
}

TO_STRING(pl_path_to_s, path_out);

static VALUE
pl_path_cmp(VALUE obj, VALUE a)
{
    PATH *l0, *l1;

    if (!rb_obj_is_kind_of(a, rb_obj_class(obj))) {
        return Qnil;
    }
    Data_Get_Struct(obj, PATH, l0);
    Data_Get_Struct(a, PATH, l1);
    if (PLRUBY_DFC2(path_n_eq, l0, l1)) return INT2NUM(0);
    if (PLRUBY_DFC2(path_n_lt, l0, l1)) return INT2NUM(-1);
    return INT2NUM(1);
}

static VALUE
pl_path_npoints(VALUE obj)
{
    PATH *p;
    Data_Get_Struct(obj, PATH, p);
    return INT2NUM(p->npts);
}

static VALUE
pl_path_close(VALUE obj)
{
    PATH *p;
    Data_Get_Struct(obj, PATH, p);
    p->closed = Qtrue;
    return obj;
}

static VALUE
pl_path_open(VALUE obj)
{
    PATH *p;
    Data_Get_Struct(obj, PATH, p);
    p->closed = Qfalse;
    return obj;
}

static VALUE
pl_path_closed(VALUE obj)
{
    PATH *p;
    Data_Get_Struct(obj, PATH, p);
    return (p->closed?Qtrue:Qfalse);
}

static VALUE
pl_path_length(VALUE obj)
{
    PATH *p;

    Data_Get_Struct(obj, PATH, p);
    RETURN_FLOAT(obj, PLRUBY_DFC1(path_length, p));
}

#define PATH_CALL(NAME_, FUNCTION_)                                     \
static VALUE                                                            \
NAME_(VALUE obj, VALUE a)                                               \
{                                                                       \
    PATH *p0, *p1, *p2;                                                 \
    int size;                                                           \
    Point *p;                                                           \
    VALUE res;                                                          \
                                                                        \
    Data_Get_Struct(obj, PATH, p0);                                     \
    if (TYPE(a) != T_DATA ||                                            \
        RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_point_mark) {             \
        a = pl_convert(a, rb_intern("to_point"), pl_point_mark);        \
    }                                                                   \
    Data_Get_Struct(a, Point, p);                                       \
    p1 = (PATH *)PLRUBY_DFC2(FUNCTION_, p0, p);                         \
    size = GET_VARSIZE(PATH, p1);					\
    p2 = (PATH *)ALLOC_N(char, size);                                   \
    CPY_FREE(p2, p1, size);                                             \
    res = Data_Wrap_Struct(rb_obj_class(obj), pl_path_mark, free, p2);  \
    if (OBJ_TAINTED(obj)|| OBJ_TAINTED(a)) OBJ_TAINT(res);              \
    return res;                                                         \
}

PATH_CALL(pl_path_add, path_add_pt);
PATH_CALL(pl_path_sub, path_sub_pt);
PATH_CALL(pl_path_mul, path_mul_pt);
PATH_CALL(pl_path_div, path_div_pt);

static VALUE
pl_path_concat(VALUE obj, VALUE a)
{
    PATH *p0, *p1;
    Point *p;
    int sz1;

    Data_Get_Struct(obj, PATH, p0);
    a = pl_convert(a, rb_intern("to_path"), pl_path_mark);
    Data_Get_Struct(a, Point, p);
    p1 = (PATH *)PLRUBY_DFC2(path_add_pt, p0, p);
    free(p0);
    sz1 = GET_VARSIZE(PATH, p1);
    p0 = (PATH *)ALLOC_N(char, sz1);
    CPY_FREE(p0, p1, sz1);
    RDATA(obj)->data = p0;
    return obj;
}

#if PG_PL_VERSION >= 75

static VALUE
pl_path_area(VALUE obj)
{
    PATH *p0;

    Data_Get_Struct(obj, PATH, p0);
    RETURN_FLOAT(obj, PLRUBY_DFC1(path_area, p0));
}

#endif


/* Extracted from geo_utils.c */

/*------------------------------------------------------------------
 * The following routines define a data type and operator class for
 * POLYGONS .... Part of which (the polygon's bounding box) is built on
 * top of the BOX data type.
 *
 * make_bound_box - create the bounding box for the input polygon
 *------------------------------------------------------------------*/

/*---------------------------------------------------------------------
 * Make the smallest bounding box for the given polygon.
 *---------------------------------------------------------------------*/
static void
make_bound_box(POLYGON *poly)
{
    int i;
    double x1, y1, x2, y2;

    if (poly->npts > 0) {
        x2 = x1 = poly->p[0].x;
        y2 = y1 = poly->p[0].y;
        for (i = 1; i < poly->npts; i++) {
            if (poly->p[i].x < x1)
                x1 = poly->p[i].x;
            if (poly->p[i].x > x2)
                x2 = poly->p[i].x;
            if (poly->p[i].y < y1)
                y1 = poly->p[i].y;
            if (poly->p[i].y > y2)
                y2 = poly->p[i].y;
        }
        poly->boundbox.low.x = x1;
        poly->boundbox.low.y = y1;
        poly->boundbox.high.x = x2;
        poly->boundbox.high.y = y2;
        pl_box_adjust(&(poly->boundbox));
    }
    else {
        rb_raise(rb_eArgError, "can't create bounding box for empty polygon");
    }
}

static VALUE
pl_poly_s_alloc(VALUE obj)
{
    POLYGON *poly;
    VALUE res;
    int sz;

    res = Data_Make_Struct(obj, POLYGON, pl_poly_mark, free, poly);
    sz = GET_VARSIZE(POLYGON, poly);
    SET_VARSIZE(poly, sz);
    return res;
}

static VALUE
pl_poly_init_copy(VALUE copy, VALUE orig)
{
    POLYGON *p0, *p1;
    int sz0, sz1;

    if (copy == orig) return copy;
    if (TYPE(orig) != T_DATA ||
        RDATA(orig)->dmark != (RUBY_DATA_FUNC)pl_poly_mark) {
        rb_raise(rb_eTypeError, "wrong argument type to clone");
    }
    Data_Get_Struct(orig, POLYGON, p0);
    Data_Get_Struct(copy, POLYGON, p1);
    sz0 = GET_VARSIZE(POLYGON, p0);
    sz1 = GET_VARSIZE(POLYGON, p1);
    if (sz0 != sz1) {
        free(p1);
        RDATA(copy)->data = 0;
        p1 = (POLYGON *)ALLOC_N(char, sz0);
	SET_VARSIZE(p1, sz0);
        RDATA(copy)->data = p1;
    }
    memcpy(p1, p0, sz0);
    return copy;
}

static VALUE
pl_poly_s_datum(VALUE obj, VALUE a)
{
    POLYGON *p0, *p1;
    Oid typoid;
    VALUE res;
    int sz0;

    p0 = (POLYGON *)plruby_datum_get(a, &typoid);
    if (typoid != POLYGONOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    sz0 = GET_VARSIZE(POLYGON, p0);
    p1 = (POLYGON *)ALLOC_N(char, sz0);
    memcpy(p1, p0, sz0);
    res = Data_Wrap_Struct(obj, pl_poly_mark, free, p1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_poly_to_datum(VALUE obj, VALUE a)
{
    POLYGON *p0, *p1;
    int typoid;
    int sz0;

    typoid = plruby_datum_oid(a, 0);
    switch (typoid) {
    case POLYGONOID:
        break;

    case CIRCLEOID:
        obj = pl_convert(obj, rb_intern("to_circle"), pl_circle_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case PATHOID:
        obj = pl_convert(obj, rb_intern("to_path"), pl_path_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case BOXOID:
        obj = pl_convert(obj, rb_intern("to_box"), pl_box_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case POINTOID:
        obj = pl_convert(obj, rb_intern("to_point"), pl_point_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    default:
        return Qnil;
    }
    Data_Get_Struct(obj, POLYGON, p0);
    sz0 = GET_VARSIZE(POLYGON, p0);
    p1 = (POLYGON *)palloc(sz0);
    memcpy(p1, p0, sz0);
    return plruby_datum_set(a, (Datum)p1);
}

#define POLYSIZE(p_) GET_VARSIZE(POLYGON, p_)
PL_MLOADVAR(pl_poly_mload, poly_recv, POLYGON, POLYSIZE);
PL_MDUMP(pl_poly_mdump, poly_send);

static VALUE
pl_poly_s_str(VALUE obj, VALUE a)
{
    POLYGON *p, *m;
    VALUE res;
    int sz0;

    a = plruby_to_s(a);
    m = (POLYGON *)PLRUBY_DFC1(poly_in, RSTRING_PTR(a));
    sz0 = GET_VARSIZE(POLYGON, m);
    p = (POLYGON *)ALLOC_N(char, sz0);
    CPY_FREE(p, m, sz0);
    res = Data_Wrap_Struct(obj, pl_poly_mark, free, p);
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_poly_init(int argc, VALUE *argv, VALUE obj)
{
    POLYGON *p;
    VALUE a;
    int i, size, closed = Qfalse;

    if (argc < 1 || argc > 2) {
        rb_raise(rb_eArgError, "initialize : expected Array of Points");
    }
    if (argc == 2) {
        closed = RTEST(argv[1]);
    }
    a = rb_Array(argv[0]);
    Data_Get_Struct(obj, POLYGON, p);
    free(p);
    size = offsetof(POLYGON, p[0]) + sizeof(p->p[0]) * RARRAY_LEN(a);
    p = (POLYGON *)ALLOC_N(char, size);
    MEMZERO(p, char, size);
    DATA_PTR(obj) = p;
    p->npts = RARRAY_LEN(a);
    for (i = 0; i < p->npts; ++i) {
        VALUE b = RARRAY_PTR(a)[i];
        if (TYPE(b) == T_DATA &&
            RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *po;

            Data_Get_Struct(b, Point, po);
            p->p[i].x = po->x;
            p->p[i].y = po->y;
        }
        else {
            VALUE tmp;

            b = rb_Array(b);
            if (RARRAY_LEN(b) != 2) {
                rb_raise(rb_eArgError, "initialize : expected Array [x, y]");
            }
            tmp = rb_Float(RARRAY_PTR(b)[0]);
            p->p[i].x = RFLOAT_VALUE(tmp);
            tmp = rb_Float(RARRAY_PTR(b)[1]);
            p->p[i].y = RFLOAT_VALUE(tmp);
        }
    }
    make_bound_box(p);
    return obj;
}

TO_STRING(pl_poly_to_s, poly_out);

#define POLY_BOOL(NAME_,FUNCTION_)              \
static VALUE                                    \
NAME_(VALUE obj, VALUE a)                       \
{                                               \
    POLYGON *p0, *p1;                           \
                                                \
    CHECK_CLASS(obj, a);                        \
    Data_Get_Struct(obj, POLYGON, p0);          \
    Data_Get_Struct(a, POLYGON, p1);            \
    if (PLRUBY_DFC2(FUNCTION_, p0, p1)) return Qtrue;\
    return Qfalse;                              \
}

POLY_BOOL(pl_poly_same,poly_same);
POLY_BOOL(pl_poly_overlap,poly_overlap);
POLY_BOOL(pl_poly_overleft,poly_overleft);
POLY_BOOL(pl_poly_left,poly_left);
POLY_BOOL(pl_poly_right,poly_right);
POLY_BOOL(pl_poly_overright,poly_overright);
POLY_BOOL(pl_poly_contained,poly_contained);

static VALUE
pl_poly_contain(VALUE obj, VALUE a)
{
    POLYGON *p0;

    Data_Get_Struct(obj, POLYGON, p0);
    if (TYPE(a) != T_DATA) {
        rb_raise(rb_eArgError, "contain : expected a geometry object");
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
        Point *p1;

        Data_Get_Struct(a, Point, p1);
        if (PLRUBY_DFC2(poly_contain_pt, p0, p1)) return Qtrue;
        return Qfalse;
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_poly_mark) {
        POLYGON *p1;

        Data_Get_Struct(a, POLYGON, p1);
        if (PLRUBY_DFC2(poly_contain, p0, p1)) return Qtrue;
        return Qfalse;
    }
    rb_raise(rb_eArgError, "invalid geometry object");
    return Qnil;
}

static VALUE
pl_poly_npoints(VALUE obj)
{
    POLYGON *p;
    Data_Get_Struct(obj, POLYGON, p);
    return INT2NUM(p->npts);
}

static VALUE
pl_poly_center(VALUE obj)
{
    POLYGON *p;
    Point *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, POLYGON, p);
    p0 = (Point *)PLRUBY_DFC1(poly_center, p);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(Point));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE pl_cPoly, pl_cBox, pl_cPath;

static VALUE
pl_box_to_poly(VALUE obj)
{
    BOX *b;
    POLYGON *p0, *p1;
    VALUE res;
    int sz0;

    Data_Get_Struct(obj, BOX, b);
    p0 = (POLYGON *)PLRUBY_DFC1(box_poly, b);
    if (!p0) return Qnil;
    sz0 = GET_VARSIZE(POLYGON, p0);
    p1 = (POLYGON *)ALLOC_N(char, sz0);
    CPY_FREE(p1, p0, sz0);
    res = Data_Wrap_Struct(pl_cPoly, pl_poly_mark, free, p1);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_path_to_poly(VALUE obj)
{
    PATH *b;
    POLYGON *p0, *p1;
    VALUE res;
    int sz0;

    Data_Get_Struct(obj, PATH, b);
    p0 = (POLYGON *)PLRUBY_DFC1(path_poly, b);
    if (!p0) return Qnil;
    sz0 = GET_VARSIZE(POLYGON, p0);
    p1 = (POLYGON *)ALLOC_N(char, sz0);
    CPY_FREE(p1, p0, sz0);
    res = Data_Wrap_Struct(pl_cPoly, pl_poly_mark, free, p1);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_poly_to_path(VALUE obj)
{
    POLYGON *b;
    PATH *p0, *p1;
    VALUE res;
    int sz0;

    Data_Get_Struct(obj, POLYGON, b);
    p0 = (PATH *)PLRUBY_DFC1(poly_path, b);
    if (!p0) return Qnil;
    sz0 = GET_VARSIZE(PATH, p0);
    p1 = (PATH *)ALLOC_N(char, sz0);
    CPY_FREE(p1, p0, sz0);
    res = Data_Wrap_Struct(pl_cPath, pl_path_mark, free, p1);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_poly_to_box(VALUE obj)
{
    POLYGON *b;
    BOX *p0, *p1;
    VALUE res;
    
    Data_Get_Struct(obj, POLYGON, b);
    p0 = (BOX *)PLRUBY_DFC1(poly_box, b);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cBox, BOX, pl_box_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(BOX));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_circle_s_alloc(VALUE obj)
{
    CIRCLE *circle;
    return Data_Make_Struct(obj, CIRCLE, pl_circle_mark, free, circle);
}

INIT_COPY(pl_circle_init_copy, CIRCLE, pl_circle_mark);

static VALUE
pl_circle_s_datum(VALUE obj, VALUE a)
{
    CIRCLE *p0, *p1;
    Oid typoid;
    VALUE res;

    p0 = (CIRCLE *)plruby_datum_get(a, &typoid);
    if (typoid != CIRCLEOID) {
	rb_raise(rb_eArgError, "unknown OID type %d", typoid);
    }
    p1 = ALLOC_N(CIRCLE, 1);
    memcpy(p1, p0, sizeof(CIRCLE));
    res = Data_Wrap_Struct(obj, pl_circle_mark, free, p1);
    OBJ_TAINT(res);
    return res;
}

static VALUE
pl_circle_to_datum(VALUE obj, VALUE a)
{
    CIRCLE *p0, *p1;
    int typoid;

    typoid = plruby_datum_oid(a, 0);
    switch (typoid) {
    case CIRCLEOID:
        break;

    case BOXOID:
        obj = pl_convert(obj, rb_intern("to_box"), pl_box_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);
        
    case POLYGONOID:
        obj = pl_convert(obj, rb_intern("to_poly"), pl_poly_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    case POINTOID:
        obj = pl_convert(obj, rb_intern("to_point"), pl_point_mark);
        return rb_funcall(obj, rb_frame_last_func(), 1, a);

    default:
        return Qnil;
    }
    Data_Get_Struct(obj, CIRCLE, p0);
    p1 = (CIRCLE *)palloc(sizeof(CIRCLE));
    memcpy(p1, p0, sizeof(CIRCLE));
    return plruby_datum_set(a, (Datum)p1);
}

PL_MLOAD(pl_circle_mload, circle_recv, CIRCLE);
PL_MDUMP(pl_circle_mdump, circle_send);

static VALUE
pl_circle_s_str(VALUE obj, VALUE a)
{
    CIRCLE *p, *m;
    VALUE res;

    a = plruby_to_s(a);
    m = (CIRCLE *)PLRUBY_DFC1(circle_in, RSTRING_PTR(a));
    res = Data_Make_Struct(obj, CIRCLE, pl_circle_mark, free, p);
    CPY_FREE(p, m, sizeof(CIRCLE));
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_circle_init(VALUE obj, VALUE a, VALUE b)
{
    CIRCLE *p;

    Data_Get_Struct(obj, CIRCLE, p);
    if (TYPE(a) == T_DATA &&
        RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
        Point *po;

        Data_Get_Struct(a, Point, po);
        p->center.x = po->x;
        p->center.y = po->y;
    }
    else {
        VALUE tmp;

        a = rb_Array(a);
        if (RARRAY_LEN(a) != 2) {
            rb_raise(rb_eArgError, "initialize : expected Array [x, y]");
        }
        tmp = rb_Float(RARRAY_PTR(a)[0]);
        p->center.x = RFLOAT_VALUE(tmp);
        tmp = rb_Float(RARRAY_PTR(a)[1]);
        p->center.y = RFLOAT_VALUE(tmp);
    }
    p->radius = RFLOAT_VALUE(rb_Float(b));
    return obj;
}

TO_STRING(pl_circle_to_s, circle_out);

#define CIRCLE_BOOL(NAME_,FUNCTION_)            \
static VALUE                                    \
NAME_(VALUE obj, VALUE a)                       \
{                                               \
    CIRCLE *p0, *p1;                            \
                                                \
    CHECK_CLASS(obj, a);                        \
    Data_Get_Struct(obj, CIRCLE, p0);           \
    Data_Get_Struct(a, CIRCLE, p1);             \
    if (PLRUBY_DFC2(FUNCTION_, p0, p1)) return Qtrue;\
    return Qfalse;                              \
}

CIRCLE_BOOL(pl_circle_same,circle_same);
CIRCLE_BOOL(pl_circle_overlap,circle_overlap);
CIRCLE_BOOL(pl_circle_overleft,circle_overleft);
CIRCLE_BOOL(pl_circle_left,circle_left);
CIRCLE_BOOL(pl_circle_right,circle_right);
CIRCLE_BOOL(pl_circle_overright,circle_overright);
CIRCLE_BOOL(pl_circle_contained,circle_contained);
CIRCLE_BOOL(pl_circle_below,circle_below);
CIRCLE_BOOL(pl_circle_above,circle_above);

static VALUE
pl_circle_contain(VALUE obj, VALUE a)
{
    CIRCLE *p0;

    Data_Get_Struct(obj, CIRCLE, p0);
    if (TYPE(a) != T_DATA) {
        rb_raise(rb_eArgError, "contain : expected a geometry object");
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
        Point *p1;

        Data_Get_Struct(a, Point, p1);
        if (PLRUBY_DFC2(circle_contain_pt, p0, p1)) return Qtrue;
        return Qfalse;
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_circle_mark) {
        CIRCLE *p1;

        Data_Get_Struct(a, CIRCLE, p1);
        if (PLRUBY_DFC2(circle_contain, p0, p1)) return Qtrue;
        return Qfalse;
    }
    rb_raise(rb_eArgError, "contain : invalid geometry object");
    return Qnil;
}

#define CIRCLE_CALL(NAME_,FUNCTION_)                                    \
static VALUE                                                            \
NAME_(VALUE obj, VALUE a)                                               \
{                                                                       \
    CIRCLE *p0, *pr;                                                    \
    Point *pt;                                                          \
    VALUE res;                                                          \
                                                                        \
    Data_Get_Struct(obj, CIRCLE, p0);                                   \
    if (TYPE(a) != T_DATA ||                                            \
        RDATA(a)->dmark != (RUBY_DATA_FUNC)pl_point_mark) {             \
        a = pl_convert(a, rb_intern("to_point"), pl_point_mark);        \
    }                                                                   \
    Data_Get_Struct(a, Point, pt);                                      \
    res = Data_Make_Struct(rb_obj_class(obj), CIRCLE,                   \
                           pl_circle_mark, free, pr);                   \
    CPY_FREE(pr, PLRUBY_DFC2(FUNCTION_, p0, pt), sizeof(CIRCLE));       \
    if (OBJ_TAINTED(obj) || OBJ_TAINTED(a)) OBJ_TAINT(res);             \
    return res;                                                         \
}

CIRCLE_CALL(pl_circle_add,circle_add_pt);
CIRCLE_CALL(pl_circle_sub,circle_sub_pt);
CIRCLE_CALL(pl_circle_mul,circle_mul_pt);
CIRCLE_CALL(pl_circle_div,circle_div_pt);

#define CIRCLE_FLOAT(NAME_,FUNCTION_)           \
static VALUE                                    \
NAME_(VALUE obj)                                \
{                                               \
    CIRCLE *l;                                  \
                                                \
    Data_Get_Struct(obj, CIRCLE, l);            \
    RETURN_FLOAT(obj, PLRUBY_DFC1(FUNCTION_, l));\
}

CIRCLE_FLOAT(pl_circle_area,circle_area);
CIRCLE_FLOAT(pl_circle_radius,circle_radius);
CIRCLE_FLOAT(pl_circle_diameter,circle_diameter);

static VALUE
pl_circle_cmp(VALUE obj, VALUE a)
{
    CIRCLE *l0, *l1;

    if (!rb_obj_is_kind_of(a, rb_obj_class(obj))) {
        return Qnil;
    }
    Data_Get_Struct(obj, CIRCLE, l0);
    Data_Get_Struct(a, CIRCLE, l1);
    if (PLRUBY_DFC2(circle_eq, l0, l1)) return INT2NUM(0);
    if (PLRUBY_DFC2(circle_lt, l0, l1)) return INT2NUM(-1);
    return INT2NUM(1);
}

static VALUE
pl_circle_center(VALUE obj)
{
    CIRCLE *l0;
    Point *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, CIRCLE, l0);
    p0 = (Point *)PLRUBY_DFC1(circle_center, l0);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cPoint, Point, pl_point_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(Point));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE pl_cCircle;

static VALUE
pl_box_to_circle(VALUE obj)
{
    BOX *b;
    CIRCLE *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, BOX, b);
    p0 = (CIRCLE *)PLRUBY_DFC1(box_circle, b);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cCircle, CIRCLE, pl_circle_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(CIRCLE));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_poly_to_circle(VALUE obj)
{
    POLYGON *b;
    CIRCLE *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, POLYGON, b);
    p0 = (CIRCLE *)PLRUBY_DFC1(poly_circle, b);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cCircle, CIRCLE, pl_circle_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(CIRCLE));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_circle_to_poly(VALUE obj, VALUE a)
{
    CIRCLE *b;
    POLYGON *p0, *p1;
    VALUE res;
    int sz0;

    Data_Get_Struct(obj, CIRCLE, b);
    p0 = (POLYGON *)PLRUBY_DFC2(circle_poly, Int32GetDatum(NUM2INT(a)), b);
    if (!p0) return Qnil;
    sz0 = GET_VARSIZE(POLYGON, p0);
    p1 = (POLYGON *)ALLOC_N(char, sz0);
    CPY_FREE(p1, p0, sz0);
    res = Data_Wrap_Struct(pl_cPoly, pl_poly_mark, free, p1);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_circle_to_box(VALUE obj)
{
    CIRCLE *b;
    BOX *p0, *p1;
    VALUE res;

    Data_Get_Struct(obj, CIRCLE, b);
    p0 = (BOX *)PLRUBY_DFC1(poly_box, b);
    if (!p0) return Qnil;
    res = Data_Make_Struct(pl_cBox, BOX, pl_box_mark, free, p1);
    CPY_FREE(p1, p0, sizeof(BOX));
    if (OBJ_TAINTED(obj)) OBJ_TAINT(res);
    return res;
}

static VALUE
pl_point_on(VALUE obj, VALUE a)
{
    Point *p0;

    Data_Get_Struct(obj, Point, p0);
    if (TYPE(a) != T_DATA) {
        rb_raise(rb_eArgError, "on : expected a geometry object");
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
        return pl_point_eq(obj, a);
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
        LSEG *l1;

        Data_Get_Struct(a, LSEG, l1);
        if (PLRUBY_DFC2(on_ps, p0, l1)) return Qtrue;
        return Qfalse;
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_box_mark) {
        BOX *l1;

        Data_Get_Struct(a, BOX, l1);
        if (PLRUBY_DFC2(on_pb, p0, l1)) return Qtrue;
        return Qfalse;
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_path_mark) {
        PATH *l1;

        Data_Get_Struct(a, PATH, l1);
        if (PLRUBY_DFC2(on_ppath, p0, l1)) return Qtrue;
        return Qfalse;
    }
    rb_raise(rb_eArgError, "on : invalid geometry object");
    return Qnil;
}

static VALUE
pl_point_contained(VALUE obj, VALUE a)
{
    Point *p0;

    Data_Get_Struct(obj, Point, p0);
    if (TYPE(a) != T_DATA) {
        rb_raise(rb_eArgError, "contained : expected a geometry object");
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
        return pl_point_eq(obj, a);
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_poly_mark) {
        POLYGON *l1;

        Data_Get_Struct(a, POLYGON, l1);
        if (PLRUBY_DFC2(pt_contained_poly, p0, l1)) return Qtrue;
        return Qfalse;
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_circle_mark) {
        CIRCLE *l1;

        Data_Get_Struct(a, CIRCLE, l1);
        if (PLRUBY_DFC2(pt_contained_circle, p0, l1)) return Qtrue;
        return Qfalse;
    }
    rb_raise(rb_eArgError, "contained : invalid geometry object");
    return Qnil;
}

static VALUE
pl_lseg_on(VALUE obj, VALUE a)
{
    LSEG *l0;

    Data_Get_Struct(obj, LSEG, l0);
    if (TYPE(a) != T_DATA) {
        rb_raise(rb_eArgError, "on : expected a geometry object");
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
        a = pl_lseg_cmp(obj, a);
        if (NUM2INT(a) == 0) return Qtrue;
        return Qfalse;
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_box_mark) {
        BOX *l1;
        
        Data_Get_Struct(a, BOX, l1);
        if (PLRUBY_DFC2(on_sb, l0, l1)) return Qtrue;
        return Qfalse;
    }
    rb_raise(rb_eArgError, "on : invalid geometry object");
    return Qnil;
}

static VALUE
pl_geo_distance(VALUE obj, VALUE a, VALUE b)
{

    if (TYPE(a) != T_DATA || TYPE(b) != T_DATA) {
        rb_raise(rb_eArgError, "distance : expected 2 geometry object");
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
        Point *p0;

        Data_Get_Struct(a, Point, p0);
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *p1;

            Data_Get_Struct(b, Point, p1);
            RETURN_FLOAT2(a, b, PLRUBY_DFC2(point_distance, p0, p1));
        }
        return pl_geo_distance(obj, b, a);
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
        LSEG *l0;

        Data_Get_Struct(a, LSEG, l0);
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *p;

            Data_Get_Struct(b, Point, p);
            RETURN_FLOAT2(a, b, PLRUBY_DFC2(dist_ps, p, l0));
        }
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
            LSEG *l1;

            Data_Get_Struct(a, LSEG, l1);
            RETURN_FLOAT2(a, b, PLRUBY_DFC2(lseg_distance, l0, l1));
        }
        return pl_geo_distance(obj, b, a);
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_box_mark) {
        BOX *l0;

        Data_Get_Struct(a, BOX, l0);
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *p;

            Data_Get_Struct(b, Point, p);
            RETURN_FLOAT2(a, b, PLRUBY_DFC2(dist_pb, p, l0));
        }
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_lseg_mark) {
            LSEG *l1;

            Data_Get_Struct(a, LSEG, l1);
            RETURN_FLOAT2(a, b, PLRUBY_DFC2(dist_sb, l1, l0));
        }
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_box_mark) {
            BOX *l1;

            Data_Get_Struct(b, BOX, l1);
            RETURN_FLOAT2(a, a, PLRUBY_DFC2(box_distance, l0, l1));
        }
        return pl_geo_distance(obj, b, a);
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_path_mark) {
        PATH *l0;

        Data_Get_Struct(a, PATH, l0);
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_point_mark) {
            Point *p;

            Data_Get_Struct(b, Point, p);
            RETURN_FLOAT2(a, b, PLRUBY_DFC2(dist_ppath, p, l0));
        }
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_path_mark) {
            PATH *l1;

            Data_Get_Struct(b, PATH, l1);
            RETURN_FLOAT2(a, a, PLRUBY_DFC2(path_distance, l0, l1));
        }
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_poly_mark) {
        POLYGON *l0;

        Data_Get_Struct(a, POLYGON, l0);
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_poly_mark) {
            POLYGON *l1;

            Data_Get_Struct(b, POLYGON, l1);
            RETURN_FLOAT2(a, a, PLRUBY_DFC2(poly_distance, l0, l1));
        }
    }
    if (RDATA(a)->dmark == (RUBY_DATA_FUNC)pl_circle_mark) {
        CIRCLE *l0;

        Data_Get_Struct(a, CIRCLE, l0);
        if (RDATA(b)->dmark == (RUBY_DATA_FUNC)pl_circle_mark) {
            CIRCLE *l1;

            Data_Get_Struct(b, CIRCLE, l1);
            RETURN_FLOAT2(a, a, PLRUBY_DFC2(circle_distance, l0, l1));
        }
    }
    rb_raise(rb_eArgError, "distance : invalid geometry objects (%s, %s)",
             rb_class2name(rb_obj_class(a)), rb_class2name(rb_obj_class(b)));
    return Qnil;
}

void Init_plruby_geometry()
{
    VALUE pl_cLseg, pl_mGeo;

    pl_mGeo = rb_define_module("Geometry");
    rb_define_module_function(pl_mGeo, "distance", pl_geo_distance, 2);
    pl_cPoint = rb_define_class("Point", rb_cObject);
    rb_undef_method(CLASS_OF(pl_cPoint), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cPoint, pl_point_s_alloc);
#else
    rb_define_singleton_method(pl_cPoint, "allocate", pl_point_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cPoint, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cPoint, "from_string", pl_point_s_str, 1);
    rb_define_singleton_method(pl_cPoint, "from_datum", pl_point_s_datum, 1);
    rb_define_method(pl_cPoint, "to_datum", pl_point_to_datum, 1);
    rb_define_method(pl_cPoint, "initialize", pl_point_init, 2);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cPoint, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cPoint, "initialize_copy", pl_point_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cPoint, "marshal_load", pl_point_mload, 1);
    rb_define_method(pl_cPoint, "marshal_dump", pl_point_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cPoint, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cPoint, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cPoint, "x", pl_point_x, 0);
    rb_define_method(pl_cPoint, "x=", pl_point_setx, 1);
    rb_define_method(pl_cPoint, "y", pl_point_y, 0);
    rb_define_method(pl_cPoint, "y=", pl_point_sety, 1);
    rb_define_method(pl_cPoint, "[]", pl_point_aref, 1);
    rb_define_method(pl_cPoint, "[]=", pl_point_aset, 2);
    rb_define_method(pl_cPoint, "to_s", pl_point_to_s, 0);
    rb_define_method(pl_cPoint, "+", pl_point_add, 1);
    rb_define_method(pl_cPoint, "-", pl_point_sub, 1);
    rb_define_method(pl_cPoint, "*", pl_point_mul, 1);
    rb_define_method(pl_cPoint, "/", pl_point_div, 1);
    rb_define_method(pl_cPoint, "left?", pl_point_left, 1);
    rb_define_method(pl_cPoint, "right?", pl_point_right, 1);
    rb_define_method(pl_cPoint, "above?", pl_point_above, 1);
    rb_define_method(pl_cPoint, "below?", pl_point_below, 1);
    rb_define_method(pl_cPoint, "vertical?", pl_point_vert, 1);
    rb_define_method(pl_cPoint, "horizontal?", pl_point_horiz, 1);
    rb_define_method(pl_cPoint, "==", pl_point_eq, 1);
    rb_define_method(pl_cPoint, "slope", pl_point_slope, 1);
    rb_define_method(pl_cPoint, "on?", pl_point_on, 1);
    rb_define_method(pl_cPoint, "in?", pl_point_contained, 1);
    rb_define_method(pl_cPoint, "contained?", pl_point_contained, 1);
    pl_cLseg = rb_define_class("Segment", rb_cObject);
    rb_include_module(pl_cLseg, rb_mComparable);
    rb_undef_method(CLASS_OF(pl_cLseg), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cLseg, pl_lseg_s_alloc);
#else
    rb_define_singleton_method(pl_cLseg, "allocate", pl_lseg_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cLseg, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cLseg, "from_string", pl_lseg_s_str, 1);
    rb_define_singleton_method(pl_cLseg, "from_datum", pl_lseg_s_datum, 1);
    rb_define_method(pl_cLseg, "to_datum", pl_lseg_to_datum, 1);
    rb_define_method(pl_cLseg, "initialize", pl_lseg_init, 2);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cLseg, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cLseg, "initialize_copy", pl_lseg_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cLseg, "marshal_load", pl_lseg_mload, 1);
    rb_define_method(pl_cLseg, "marshal_dump", pl_lseg_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cLseg, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cLseg, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cLseg, "[]", pl_lseg_aref, 1);
    rb_define_method(pl_cLseg, "[]=", pl_lseg_aset, 2);
    rb_define_method(pl_cLseg, "to_s", pl_lseg_to_s, 0);
    rb_define_method(pl_cLseg, "length", pl_lseg_length, 0);
    rb_define_method(pl_cLseg, "parallel?", pl_lseg_parallel, 1);
    rb_define_method(pl_cLseg, "perpendicular?", pl_lseg_perp, 1);
    rb_define_method(pl_cLseg, "vertical?", pl_lseg_vertical, 0);
    rb_define_method(pl_cLseg, "horizontal?", pl_lseg_horizontal, 0);
    rb_define_method(pl_cLseg, "<=>", pl_lseg_cmp, 1);
    rb_define_method(pl_cLseg, "center", pl_lseg_center, 0);
    rb_define_method(pl_cLseg, "closest", pl_lseg_closest, 1);
    rb_define_method(pl_cLseg, "intersect?", pl_lseg_intersect, 1);
    rb_define_method(pl_cLseg, "intersection", pl_lseg_intersection, 1);
    rb_define_method(pl_cLseg, "on?", pl_lseg_on, 1);
    rb_define_method(pl_cLseg, "to_point", pl_lseg_center, 0);
    pl_cBox = rb_define_class("Box", rb_cObject);
    rb_include_module(pl_cBox, rb_mComparable);
    rb_undef_method(CLASS_OF(pl_cBox), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cBox, pl_box_s_alloc);
#else
    rb_define_singleton_method(pl_cBox, "allocate", pl_box_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cBox, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cBox, "from_string", pl_box_s_str, 1);
    rb_define_singleton_method(pl_cBox, "from_datum", pl_box_s_datum, 1);
    rb_define_method(pl_cBox, "to_datum", pl_box_to_datum, 1);
    rb_define_method(pl_cBox, "initialize", pl_box_init, -1);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cBox, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cBox, "initialize_copy", pl_box_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cBox, "marshal_load", pl_box_mload, 1);
    rb_define_method(pl_cBox, "marshal_dump", pl_box_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cBox, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cBox, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cBox, "low", pl_box_low, 0);
    rb_define_method(pl_cBox, "high", pl_box_high, 0);
    rb_define_method(pl_cBox, "low=", pl_box_lowset, 1);
    rb_define_method(pl_cBox, "high=", pl_box_highset, 1);
    rb_define_method(pl_cBox, "[]", pl_box_aref, 1);
    rb_define_method(pl_cBox, "[]=", pl_box_aset, 2);
    rb_define_method(pl_cBox, "to_s", pl_box_to_s, 0);
    rb_define_method(pl_cBox, "<=>", pl_box_cmp, 1);
    rb_define_method(pl_cBox, "+", pl_box_add, 1);
    rb_define_method(pl_cBox, "-", pl_box_sub, 1);
    rb_define_method(pl_cBox, "*", pl_box_mul, 1);
    rb_define_method(pl_cBox, "/", pl_box_div, 1);
    rb_define_method(pl_cBox, "same?", pl_box_same, 1);
    rb_define_method(pl_cBox, "===", pl_box_same, 1);
    rb_define_method(pl_cBox, "overlap?", pl_box_overlap, 1);
    rb_define_method(pl_cBox, "overleft?", pl_box_overleft, 1);
    rb_define_method(pl_cBox, "left?", pl_box_left, 1);
    rb_define_method(pl_cBox, "right?", pl_box_right, 1);
    rb_define_method(pl_cBox, "overright?", pl_box_overright, 1);
    rb_define_method(pl_cBox, "contained?", pl_box_contained, 1);
    rb_define_method(pl_cBox, "in?", pl_box_contained, 1);
    rb_define_method(pl_cBox, "contain?", pl_box_contain, 1);
    rb_define_method(pl_cBox, "below?", pl_box_below, 1);
    rb_define_method(pl_cBox, "above?", pl_box_above, 1);
    rb_define_method(pl_cBox, "area", pl_box_area, 0);
    rb_define_method(pl_cBox, "width", pl_box_width, 0);
    rb_define_method(pl_cBox, "height", pl_box_height, 0);
    rb_define_method(pl_cBox, "center", pl_box_center, 0);
    rb_define_method(pl_cBox, "closest", pl_box_closest, 1);
    rb_define_method(pl_cBox, "intersect?", pl_box_intersect, 1);
    rb_define_method(pl_cBox, "intersection", pl_box_intersection, 1);
    rb_define_method(pl_cBox, "diagonal", pl_box_diagonal, 0);
    rb_define_method(pl_cBox, "to_point", pl_box_center, 0);
    rb_define_method(pl_cBox, "to_segment", pl_box_diagonal, 0);
    rb_define_method(pl_cBox, "to_lseg", pl_box_diagonal, 0);
    rb_define_method(pl_cBox, "to_poly", pl_box_to_poly, 0);
    rb_define_method(pl_cBox, "to_polygon", pl_box_to_poly, 0);
    rb_define_method(pl_cBox, "to_circle", pl_box_to_circle, 0);
    pl_cPath = rb_define_class("Path", rb_cObject);
    rb_include_module(pl_cPath, rb_mComparable);
    rb_undef_method(CLASS_OF(pl_cPath), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cPath, pl_path_s_alloc);
#else
    rb_define_singleton_method(pl_cPath, "allocate", pl_path_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cPath, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cPath, "from_string", pl_path_s_str, 1);
    rb_define_singleton_method(pl_cPath, "from_datum", pl_path_s_datum, 1);
    rb_define_method(pl_cPath, "to_datum", pl_path_to_datum, 1);
    rb_define_method(pl_cPath, "initialize", pl_path_init, -1);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cPath, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cPath, "initialize_copy", pl_path_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cPath, "marshal_load", pl_path_mload, 1);
    rb_define_method(pl_cPath, "marshal_dump", pl_path_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cPath, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cPath, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cPath, "to_s", pl_path_to_s, 0);
    rb_define_method(pl_cPath, "<=>", pl_path_cmp, 1);
    rb_define_method(pl_cPath, "npoints", pl_path_npoints, 0);
    rb_define_method(pl_cPath, "close", pl_path_close, 0);
    rb_define_method(pl_cPath, "open", pl_path_open, 0);
    rb_define_method(pl_cPath, "closed?", pl_path_closed, 0);
    rb_define_method(pl_cPath, "length", pl_path_length, 0);
    rb_define_method(pl_cPath, "<<", pl_path_concat, 1);
    rb_define_method(pl_cPath, "concat", pl_path_concat, 1);
    rb_define_method(pl_cPath, "+", pl_path_add, 1);
    rb_define_method(pl_cPath, "-", pl_path_sub, 1);
    rb_define_method(pl_cPath, "*", pl_path_mul, 1);
    rb_define_method(pl_cPath, "/", pl_path_div, 1);
    rb_define_method(pl_cPath, "to_poly", pl_path_to_poly, 0);
    rb_define_method(pl_cPath, "to_polygon", pl_path_to_poly, 0);
#if PG_PL_VERSION >= 75
    rb_define_method(pl_cPath, "area", pl_path_area, 0);
#endif
    pl_cPoly = rb_define_class("Polygon", rb_cObject);
    rb_undef_method(CLASS_OF(pl_cPoly), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cPoly, pl_poly_s_alloc);
#else
    rb_define_singleton_method(pl_cPoly, "allocate", pl_poly_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cPoly, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cPoly, "from_string", pl_poly_s_str, 1);
    rb_define_singleton_method(pl_cPoly, "from_datum", pl_poly_s_datum, 1);
    rb_define_method(pl_cPoly, "to_datum", pl_poly_to_datum, 1);
    rb_define_method(pl_cPoly, "initialize", pl_poly_init, -1);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cPoly, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cPoly, "initialize_copy", pl_poly_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cPoly, "marshal_load", pl_poly_mload, 1);
    rb_define_method(pl_cPoly, "marshal_dump", pl_poly_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cPoly, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cPoly, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cPoly, "to_s", pl_poly_to_s, 0);
    rb_define_method(pl_cPoly, "left?", pl_poly_left, 1);
    rb_define_method(pl_cPoly, "overleft?", pl_poly_overleft, 1);
    rb_define_method(pl_cPoly, "right?", pl_poly_right, 1);
    rb_define_method(pl_cPoly, "overright?", pl_poly_overright, 1);
    rb_define_method(pl_cPoly, "same?", pl_poly_same, 1);
    rb_define_method(pl_cPoly, "==", pl_poly_same, 1);
    rb_define_method(pl_cPoly, "contain?", pl_poly_contain, 1);
    rb_define_method(pl_cPoly, "contained?", pl_poly_contained, 1);
    rb_define_method(pl_cPoly, "in?", pl_poly_contained, 1);
    rb_define_method(pl_cPoly, "overlap?", pl_poly_overlap, 1);
    rb_define_method(pl_cPoly, "npoints", pl_poly_npoints, 0);
    rb_define_method(pl_cPoly, "center", pl_poly_center, 0);
    rb_define_method(pl_cPoly, "to_point", pl_poly_center, 0);
    rb_define_method(pl_cPoly, "to_path", pl_poly_to_path, 0);
    rb_define_method(pl_cPoly, "to_box", pl_poly_to_box, 0);
    rb_define_method(pl_cPoly, "to_circle", pl_poly_to_circle, 0);
    pl_cCircle = rb_define_class("Circle", rb_cObject);
    rb_include_module(pl_cCircle, rb_mComparable);
    rb_undef_method(CLASS_OF(pl_cCircle), "method_missing");
#if HAVE_RB_DEFINE_ALLOC_FUNC
    rb_define_alloc_func(pl_cCircle, pl_circle_s_alloc);
#else
    rb_define_singleton_method(pl_cCircle, "allocate", pl_circle_s_alloc, 0);
#endif
    rb_define_singleton_method(pl_cCircle, "new", plruby_s_new, -1);
    rb_define_singleton_method(pl_cCircle, "from_string", pl_circle_s_str, 1);
    rb_define_singleton_method(pl_cCircle, "from_datum", pl_circle_s_datum, 1);
    rb_define_method(pl_cCircle, "to_datum", pl_circle_to_datum, 1);
    rb_define_method(pl_cCircle, "initialize", pl_circle_init, 2);
#ifndef HAVE_RB_INITIALIZE_COPY
    rb_define_method(pl_cCircle, "clone", plruby_clone, 0);
#endif
    rb_define_method(pl_cCircle, "initialize_copy", pl_circle_init_copy, 1);
#if PG_PL_VERSION >= 74
    rb_define_method(pl_cCircle, "marshal_load", pl_circle_mload, 1);
    rb_define_method(pl_cCircle, "marshal_dump", pl_circle_mdump, -1);
#ifndef RUBY_CAN_USE_MARSHAL_LOAD
    rb_define_singleton_method(pl_cCircle, "_load", plruby_s_load, 1);
    rb_define_alias(pl_cCircle, "_dump", "marshal_dump");
#endif
#endif
    rb_define_method(pl_cCircle, "to_s", pl_circle_to_s, 0);
    rb_define_method(pl_cCircle, "left?", pl_circle_left, 1);
    rb_define_method(pl_cCircle, "overleft?", pl_circle_overleft, 1);
    rb_define_method(pl_cCircle, "right?", pl_circle_right, 1);
    rb_define_method(pl_cCircle, "overright?", pl_circle_overright, 1);
    rb_define_method(pl_cCircle, "same?", pl_circle_same, 1);
    rb_define_method(pl_cCircle, "===", pl_circle_same, 1);
    rb_define_method(pl_cCircle, "contain?", pl_circle_contain, 1);
    rb_define_method(pl_cCircle, "contained?", pl_circle_contained, 1);
    rb_define_method(pl_cCircle, "in?", pl_circle_contained, 1);
    rb_define_method(pl_cCircle, "overlap?", pl_circle_overlap, 1);
    rb_define_method(pl_cCircle, "below?", pl_circle_below, 1);
    rb_define_method(pl_cCircle, "above?", pl_circle_above, 1);
    rb_define_method(pl_cCircle, "<=>", pl_circle_cmp, 1);
    rb_define_method(pl_cCircle, "+", pl_circle_add, 1);
    rb_define_method(pl_cCircle, "-", pl_circle_sub, 1);
    rb_define_method(pl_cCircle, "*", pl_circle_mul, 1);
    rb_define_method(pl_cCircle, "/", pl_circle_div, 1);
    rb_define_method(pl_cCircle, "area", pl_circle_area, 0);
    rb_define_method(pl_cCircle, "radius", pl_circle_radius, 0);
    rb_define_method(pl_cCircle, "diameter", pl_circle_diameter, 0);
    rb_define_method(pl_cCircle, "center", pl_circle_center, 0);
    rb_define_method(pl_cCircle, "to_point", pl_circle_center, 0);
    rb_define_method(pl_cCircle, "to_poly", pl_circle_to_poly, 1);
    rb_define_method(pl_cCircle, "to_polygon", pl_circle_to_poly, 1);
    rb_define_method(pl_cCircle, "to_box", pl_circle_to_box, 0);
}
