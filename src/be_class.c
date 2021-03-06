#include "be_class.h"
#include "be_string.h"
#include "be_vector.h"
#include "be_map.h"
#include "be_exec.h"
#include "be_gc.h"
#include "be_vm.h"
#include "be_func.h"
#include "be_var.h"

#define check_members(vm, c)            \
    if (!(c)->members) {                \
        (c)->members = be_map_new(vm);  \
    }

bclass* be_newclass(bvm *vm, bstring *name, bclass *super)
{
    bgcobject *gco = be_gcnew(vm, BE_CLASS, bclass);
    bclass *obj = cast_class(gco);
    bvalue *buf = be_incrtop(vm); /* protect new objects from GC */
    var_setclass(buf, obj);
    if (obj) {
        obj->super = super;
        obj->members = NULL; /* gc protection */
        obj->nvar = 0;
        obj->name = name;
    }
    be_stackpop(vm, 1);
    return obj;
}

void be_class_compress(bvm *vm, bclass *c)
{
    if (!gc_isconst(c) && c->members) {
        be_map_release(vm, c->members); /* clear space */
    }
}

int be_class_attribute(bclass *c, bstring *attr)
{
    for (; c; c = c->super) {
        if (c->members) {
            bvalue *v = be_map_findstr(c->members, attr);
            if (v) {
                return var_type(v);
            }
        }
    }
    return BE_NIL;
}

void be_member_bind(bvm *vm, bclass *c, bstring *name)
{
    bvalue *attr;
    check_members(vm, c);
    attr = be_map_insertstr(vm, c->members, name, NULL);
    attr->v.i = c->nvar++;
    attr->type = MT_VARIABLE;
}

void be_method_bind(bvm *vm, bclass *c, bstring *name, bproto *p)
{
    bvalue *attr;
    check_members(vm, c);
    attr = be_map_insertstr(vm, c->members, name, NULL);
    /* closure with upvalues need to be created when instantiating */
    if (p->nupvals > 0) {
        var_setproto(attr, p);
        attr->type = BE_PROTO;
        /* Store the index of the method in the instance
         * data field into the extra field of the prototype. */
        p->extra = c->nvar++;
    } else { /* create closures without upvalues directly */
        bclosure *cl = be_newclosure(vm, 0);
        cl->proto = p;
        var_setclosure(attr, cl);
    }
}

void be_prim_method_bind(bvm *vm, bclass *c, bstring *name, bntvfunc f)
{
    bvalue *attr;
    check_members(vm, c);
    attr = be_map_insertstr(vm, c->members, name, NULL);
    attr->v.nf = f;
    attr->type = MT_PRIMMETHOD;
}

/* get the closure method count that need upvalues */
int be_class_closure_count(bclass *c)
{
    int count = 0;
    if (c->members) {
        bmapnode *node;
        bmap *members = c->members;
        bmapiter iter = be_map_iter();
        while ((node = be_map_next(members, &iter)) != NULL) {
            if (var_isproto(&node->value)) {
                ++count;
            }
        }
    }
    return count;
}

static binstance* instance_member(binstance *obj, bstring *name, bvalue *dst)
{
    for (; obj; obj = obj->super) {
        if (obj->class->members) {
            bvalue *v = be_map_findstr(obj->class->members, name);
            if (v) {
                *dst = *v;
                return obj;
            }
        }
    }
    var_setnil(dst);
    return NULL;
}

static void create_closures(bvm *vm, binstance *obj)
{
    bmapnode *node;
    bmapiter iter = be_map_iter();
    bmap *members = obj->class->members;
    bvalue *varbuf = obj->members;
    bvalue *top = be_incrtop(vm); /* protect new objects from GC */
    var_setinstance(top, obj);
    while ((node = be_map_next(members, &iter)) != NULL) {
        if (var_isproto(&node->value)) {
            /* The prototype in the node means that the method closure
             * has upvalues, we store them in the instance data field.
             * Each method closure is created when instantiated. */
            bproto *proto = var_toobj(&node->value);
            bclosure *cl = be_newclosure(vm, proto->nupvals);
            bvalue *var = varbuf + proto->extra;
            var_setclosure(var, cl);
            cl->proto = proto;
            be_initupvals(vm, cl); /* initialize the closure's upvalues */
        }
    }
    be_stackpop(vm, 1);
}

static binstance* newobjself(bvm *vm, bclass *c)
{
    size_t size = sizeof(binstance) + sizeof(bvalue) * (c->nvar - 1);
    bgcobject *gco = be_newgcobj(vm, BE_INSTANCE, size);
    binstance *obj = cast_instance(gco);
    be_assert(obj != NULL);
    if (obj) { /* initialize members */
        bvalue *v = obj->members, *end = v + c->nvar;
        while (v < end) { var_setnil(v); ++v; }
        obj->class = c;
        obj->super = NULL;
        if (c->members) {
            create_closures(vm, obj);
        }
    }
    return obj;
}

static binstance* newobject(bvm *vm, bclass *c)
{
    binstance *obj, *prev;
    be_assert(c != NULL);
    obj = prev = newobjself(vm, c);
    var_setinstance(vm->top, obj);
    be_incrtop(vm); /* protect new objects from GC */
    for (c = c->super; c; c = c->super) {
        prev->super = newobjself(vm, c);
        prev = prev->super;
    }
    be_stackpop(vm, 1);
    return obj;
}

int be_class_newobj(bvm *vm, bclass *c, bvalue *argv, int argc)
{
    bvalue init;
    binstance *obj = newobject(vm, c);
    var_setinstance(argv, obj);
    /* find constructor */
    obj = instance_member(obj, be_newstr(vm, "init"), &init);
    if (obj && var_type(&init) != MT_VARIABLE) {
        /* user constructor */
        bvalue *reg = argv + 1;
        /* copy argv */
        for (; argc > 0; --argc) {
            reg[argc] = argv[argc - 1];
        }
        if (var_type(&init) == BE_PROTO) {
            /* find the closure of the constructor in the data field */
            bproto *proto = var_toobj(&init);
            *reg = obj->members[proto->extra];
        } else {
            *reg = init; /* set constructor */
        }
        return 1;
    }
    return 0;
}

int be_instance_member(binstance *obj, bstring *name, bvalue *dst)
{
    int type;
    be_assert(name != NULL);
    obj = instance_member(obj, name, dst);
    type = var_type(dst);
    if (obj) {
        if (type == MT_VARIABLE) {
            *dst = obj->members[dst->v.i];
        } else if (type == BE_PROTO) {
            /* find the closure of the method in the data field */
            bproto *proto = var_toobj(dst);
            *dst = obj->members[proto->extra];
            return MT_METHOD;
        }
    }
    return type;
}

int be_instance_setmember(binstance *obj, bstring *name, bvalue *src)
{
    bvalue v;
    be_assert(name != NULL);
    obj = instance_member(obj, name, &v);
    if (obj && var_istype(&v, MT_VARIABLE)) {
        obj->members[var_toint(&v)] = *src;
        return 1;
    }
    return 0;
}
