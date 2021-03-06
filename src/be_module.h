#ifndef BE_MODULE_H
#define BE_MODULE_H

#include "be_object.h"

typedef struct bmodule {
    bcommon_header;
    bmap *table;
    union {
        bntvmodule *native;
        const char *name;
    } info;
    bgcobject *gray; /* for gc gray list */
} bmodule;

bmodule *be_module_new(bvm *vm);
void be_module_delete(bvm *vm, bmodule *module);
int be_module_load(bvm *vm, bstring *path);
bvalue* be_module_attr(bmodule *module, bstring *attr);
bvalue* be_module_bind(bvm *vm, bmodule *module, bstring *attr);
const char* be_module_name(bmodule *module);

#endif
