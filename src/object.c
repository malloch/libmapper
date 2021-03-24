#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"

mpr_graph mpr_obj_get_graph(mpr_obj o)
{
    return o ? o->graph : 0;
}

mpr_type mpr_obj_get_type(mpr_obj o)
{
    return o ? o->type : 0;
}

void mpr_obj_increment_version(mpr_obj o)
{
    RETURN_UNLESS(o && o->props.staged);
    ++o->version;
    o->props.synced->dirty = 1;
}

int mpr_obj_get_num_props(mpr_obj o, int staged)
{
    int len = 0;
    if (o) {
        if (o->props.synced)
            len += mpr_tbl_get_size(o->props.synced);
        if (staged && o->props.staged)
            len += mpr_tbl_get_size(o->props.staged);
    }
    return len;
}

mpr_prop mpr_obj_get_prop_by_key(mpr_obj o, const char *s, int *l, mpr_type *t,
                                 const void **v, int *p)
{
    RETURN_UNLESS(o && s, 0);
    return mpr_tbl_get_prop_by_key(o->props.synced, s, l, t, v, p);
}

mpr_prop mpr_obj_get_prop_by_idx(mpr_obj o, mpr_prop p, const char **k, int *l,
                                 mpr_type *t, const void **v, int *pub)
{
    RETURN_UNLESS(o, 0);
    return mpr_tbl_get_prop_by_idx(o->props.synced, p | o->props.mask, k, l, t, v, pub);
}

int mpr_obj_get_prop_as_int32(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    mpr_tbl_record r = mpr_tbl_get(o->props.synced, p, s);
    RETURN_UNLESS(r && r->val && 1 == r->len, 0);
    void *v = (r->flags & INDIRECT) ? *r->val : r->val;
    switch(r->type) {
        case MPR_BOOL:
        case MPR_INT32: return *(int*)v;
        case MPR_INT64: return (int)(*(int64_t*)v);
        case MPR_FLT:   return (int)(*(float*)v);
        case MPR_DBL:   return (int)(*(double*)v);
        case MPR_TYPE:  return (int)(*(mpr_type*)v);
        default:        return 0;
    }
}

float mpr_obj_get_prop_as_flt(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    mpr_tbl_record r = mpr_tbl_get(o->props.synced, p, s);
    RETURN_UNLESS(r && r->val && 1 == r->len, 0);
    void *v = (r->flags & INDIRECT) ? *r->val : r->val;
    switch(r->type) {
        case MPR_BOOL:
        case MPR_INT32: return (float)(*(int*)v);
        case MPR_INT64: return (float)(*(int64_t*)v);
        case MPR_FLT:   return *(float*)v;
        case MPR_DBL:   return (float)(*(double*)v);
        default:        return 0;
    }
}

const char *mpr_obj_get_prop_as_str(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    mpr_tbl_record r = mpr_tbl_get(o->props.synced, p, s);
    RETURN_UNLESS(r && r->val && MPR_STR == r->type && 1 == r->len, 0);
    return r->flags & INDIRECT ? *r->val : r->val;
}

const void *mpr_obj_get_prop_as_ptr(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    mpr_tbl_record r = mpr_tbl_get(o->props.synced, p, s);
    RETURN_UNLESS(r && r->val && MPR_PTR == r->type && 1 == r->len, 0);
    return r->flags & INDIRECT ? *r->val : r->val;
}

mpr_obj mpr_obj_get_prop_as_obj(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    mpr_tbl_record r = mpr_tbl_get(o->props.synced, p, s);
    RETURN_UNLESS(r && r->val && MPR_OBJ >= r->type && 1 == r->len, 0);
    return r->flags & INDIRECT ? *r->val : r->val;
}

mpr_list mpr_obj_get_prop_as_list(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    mpr_tbl_record r = mpr_tbl_get(o->props.synced, p, s);
    RETURN_UNLESS(r && r->val && MPR_LIST == r->type && 1 == r->len, 0);
    mpr_list l = r->flags & INDIRECT ? *r->val : r->val;
    return l ? mpr_list_start(mpr_list_get_cpy(l)) : 0;
}

mpr_prop mpr_obj_set_prop(mpr_obj o, mpr_prop p, const char *s, int len,
                          mpr_type type, const void *val, int publish)
{
    RETURN_UNLESS(o, 0);
    // TODO: ensure ID property can't be changed by user code
    if (MPR_PROP_UNKNOWN == p || !MASK_PROP_BITFLAGS(p)) {
        if (!s || '@' == s[0])
            return MPR_PROP_UNKNOWN;
        p = mpr_prop_from_str(s);
    }

    // check if object represents local resource
    int local = o->props.staged ? 0 : 1;
    int flags = local ? LOCAL_MODIFY : REMOTE_MODIFY;
    if (!publish)
        flags |= LOCAL_ACCESS_ONLY;
    return mpr_tbl_set(local ? o->props.synced : o->props.staged,
                       p | o->props.mask, s, len, type, val, flags);
}

int mpr_obj_remove_prop(mpr_obj o, mpr_prop p, const char *s)
{
    RETURN_UNLESS(o, 0);
    // check if object represents local resource
    int local = o->props.staged ? 0 : 1;
    if (MPR_PROP_UNKNOWN == p)
        p = mpr_prop_from_str(s);
    if (MPR_PROP_DATA == p || local)
        return mpr_tbl_remove(o->props.synced, p, s, LOCAL_MODIFY);
    else if (MPR_PROP_EXTRA == p)
        return mpr_tbl_set(o->props.staged, p | PROP_REMOVE, s, 0, 0, 0, REMOTE_MODIFY);
    return 0;
}

void mpr_obj_push(mpr_obj o)
{
    RETURN_UNLESS(o);
    mpr_net n = &o->graph->net;

    if (MPR_DEV == o->type) {
        mpr_dev d = (mpr_dev)o;
        if (d->loc) {
            mpr_net_use_subscribers(n, d, o->type);
            mpr_dev_send_state(d, MSG_DEV);
        }
        else {
            mpr_net_use_bus(n);
            mpr_dev_send_state(d, MSG_DEV_MOD);
        }
    }
    else if (o->type & MPR_SIG) {
        mpr_sig s = (mpr_sig)o;
        if (s->loc) {
            mpr_type type = ((s->dir == MPR_DIR_OUT) ? MPR_SIG_OUT : MPR_SIG_IN);
            mpr_net_use_subscribers(n, s->dev, type);
            mpr_sig_send_state(s, MSG_SIG);
        }
        else {
            mpr_net_use_bus(n);
            mpr_sig_send_state(s, MSG_SIG_MOD);
        }
    }
    else if (o->type & MPR_MAP) {
        mpr_net_use_bus(n);
        mpr_map m = (mpr_map)o;
        if (m->status >= MPR_STATUS_ACTIVE){
            mpr_map_send_state(m, -1, MSG_MAP_MOD);
        }
        else{
            mpr_map_send_state(m, -1, MSG_MAP);
        }
    }
    else {
        trace("mpr_obj_push(): unknown object type %d\n", o->type);
        return;
    }

    // clear the staged properties
    FUNC_IF(mpr_tbl_clear, o->props.staged);
}

void mpr_obj_print(mpr_obj o, int staged)
{
    RETURN_UNLESS(o && o->props.synced);
    int i = 0, len;
    mpr_prop p;
    const char *key;
    mpr_type type;
    const void *val;

    switch (o->type) {
        case MPR_DEV:
            printf("DEVICE: ");
            mpr_prop_print(1, MPR_DEV, o);
            break;
        case MPR_SIG:
            printf("SIGNAL: ");
            mpr_prop_print(1, MPR_SIG, o);
            break;
        case MPR_MAP: {
            printf("MAP: ");
            mpr_map m = (mpr_map)o;
            if (m->num_src > 1)
                printf("[");
            mpr_list l = mpr_map_get_sigs(m, MPR_LOC_SRC);
            while (l) {
                mpr_prop_print(1, MPR_SIG, *l);
                l = mpr_list_get_next(l);
                printf("%s", l ? ", " : "");
            }
            if (m->num_src > 1)
                printf("]");
            printf(" -> ");
            l = mpr_map_get_sigs(m, MPR_LOC_DST);
            while (l) {
                mpr_prop_print(1, MPR_SIG, *l);
                l = mpr_list_get_next(l);
                printf("%s", l ? ", " : "");
            }
            break;
        }
        default:
            trace("mpr_obj_print(): unknown object type %d.", o->type);
            return;
    }

    int num_props = mpr_obj_get_num_props(o, 0);
    for (i = 0; i < num_props; i++) {
        p = mpr_obj_get_prop_by_idx(o, i, &key, &len, &type, &val, 0);
        die_unless(val != 0 || MPR_LIST == type, "returned zero value\n");

        // already printed this
        if (MPR_PROP_NAME == p)
            continue;
        // don't print device signals as metadata
        if (MPR_DEV == o->type && MPR_PROP_SIG == p)
            continue;

        printf(", %s=", key);

        // handle pretty-printing a few enum values
        if (1 == len && MPR_INT32 == type) {
            switch(p) {
                case MPR_PROP_DIR:
                    printf("%s", MPR_DIR_OUT == *(int*)val ? "output" : "input");
                    break;
                case MPR_PROP_PROCESS_LOC:
                    printf("%s", mpr_loc_as_str(*(int*)val));
                    break;
                case MPR_PROP_PROTOCOL:
                    printf("%s", mpr_protocol_as_str(*(int*)val));
                    break;
                default:
                    mpr_prop_print(len, type, val);
            }
        }
        else
            mpr_prop_print(len, type, val);

        if (!staged || !o->props.staged)
            continue;

        // check if staged values exist
        if (MPR_PROP_EXTRA == p)
            p = mpr_tbl_get_prop_by_key(o->props.staged, key, &len, &type, &val, 0);
        else
            p = mpr_tbl_get_prop_by_idx(o->props.staged, p, NULL, &len, &type, &val, 0);
        if (MPR_PROP_UNKNOWN != p) {
            printf(" (staged: ");
            mpr_prop_print(len, type, val);
            printf(")");
        }
    }
    printf("\n");
}

mpr_obj mpr_obj_add_child(mpr_obj parent, const char *name, mpr_graph g){
    
    // Add a new child object to the list of children associated with the parent object.
    mpr_obj child = (mpr_obj)mpr_list_add_item((void**)&parent->children, sizeof(mpr_obj_t));
    
    child->type = MPR_OBJ; // 31
    child->graph = g;
    child->parent = parent; // link the parent of this object
    child->name = name;

    //Todo: Add name when other branch is merged.

    return child;
}

mpr_obj mpr_obj_get_top_level_parent(mpr_obj obj){
    mpr_obj current = obj;

    // Loop through the tree of map_obj until the top level parent is found.
    while (current->parent)
        current = current->parent;

    return current;


}


/* Prepends t into s. Assumes s has enough space allocated
** for the combined string.
*/
void prepend(char* s, const char* t)
{
    size_t len = strlen(t);
    memmove(s + len, s, strlen(s) + 1);
    memcpy(s, t, len);
}

const char* mpr_obj_generate_full_path(mpr_obj obj, const char *name){
    mpr_obj current = obj;

    if(current->parent){
        char *full_path = malloc(100); // TODO: How to better size this dynamically?
        strcpy(full_path, current->name);
    
        // Loop through the tree of map_obj until the top level parent is found.
        while (current->parent){
            current = current->parent;
            if(current->name){
                prepend(full_path, "/");
                prepend(full_path, current->name);
            }
        }
        strcat(strcat(full_path,"/"), name);

        return full_path;
    }
    return name;
}


/* TODO: Move to proper spot when deemed complete. */
mpr_obj mpr_obj_new(const char *name, mpr_graph g){
    RETURN_UNLESS(name);
    TRACE_RETURN_UNLESS(name[strlen(name)-1] != '/', 0,
                        "trailing slash detected in object name.\n");

    if (!g) {
        g = mpr_graph_new(0);
        g->own = 0;
    }

    mpr_obj obj = (mpr_obj*)malloc(sizeof(mpr_obj_t)); // Is this correct?
    obj->type = MPR_OBJ;

    obj->name = (char*)malloc(strlen(name));
    strcpy(obj->name, name);
    obj->graph = g;

    return obj;
}

void mpr_obj_free(mpr_obj obj){
    RETURN_UNLESS(obj);

    // Free mpr_obj's name
    free(obj->name);

    //TODO: Ask joe, should I be freeing the graph of an object here?
    // mpr_graph_free(obj->graph); // TODO: This seems dangerous

    // Free this object's children objects
    mpr_list children = mpr_list_from_data(obj->children);

    while (children) {
        mpr_obj child = (mpr_obj)*children;
        children = mpr_list_get_next(children);

        //Free the child at this iteration of the loop.
        mpr_obj_free(child);
    }
}
