#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "device.h"
#include "list.h"
#include "map.h"
#include "network.h"
#include "mpr_signal.h"
#include "property.h"
#include "slot.h"
#include <mapper/mapper.h>

void mpr_obj_init(mpr_obj o, mpr_graph g, const char *name, mpr_type type, int is_local)
{
    trace("initializing %s object '%s'\n", is_local ? "local" : "remote", name);
    o->graph = g;
    if (name)
        o->name = strdup(name);
    if (MPR_DEV == type)
        o->root = (MPR_DEV == type) ? (mpr_dev)o : o->root;
    o->type = type;
    o->is_local = is_local;
    o->status = MPR_STATUS_NEW;
    mpr_obj_set_status((mpr_obj)o->graph, MPR_STATUS_NEW, 0);
}

static mpr_obj mpr_obj_build_tree(mpr_obj parent, const char *name, mpr_type type, int is_local)
{
    mpr_obj o;
    RETURN_ARG_UNLESS(name, NULL);
    if ('/' == name[0])
        ++name;
    trace("adding object '%s' to '%s'\n", name, parent ? parent->name : "NULL");

    if (parent) {
        /* check if there is already a child object with this name */
        o = parent->child;
        while (o) {
            if (0 == strcmp(o->name, name))
                return o;
            o = o->next;
        }
    }

    /* allocate a new object */
    o = (mpr_obj) calloc(1, sizeof(mpr_obj_t));
    o->graph = parent->graph;
    o->name = strdup(name);
    o->type = type;
    o->status = MPR_STATUS_NEW;

    if (parent) {
        o->parent = parent;

        if (parent->root) {
            o->root = parent->root;
            o->id = mpr_graph_generate_unique_id(o->graph);
            mpr_obj_set_status((mpr_obj)o->graph, MPR_STATUS_NEW, 0);
        }
        else {

        }

        /* add child to parent */
        if (parent->child) {
            o->next = parent->child;
        }
        parent->child = o;
    }
    return o;
}

void mpr_obj_build_tree_temp(mpr_obj parent, mpr_obj child)
{
    trace("building tree from '%s' to '%s'\n", parent->name, child->name);

    mpr_obj o = parent;
    if (strchr(child->name, '/')) {
        char *name = child->name, *slash;
        while ((slash = strchr(name, '/'))) {
            slash[0] = '\0';
            o = mpr_obj_build_tree(o, name, MPR_OBJ, child->is_local);
            name = slash + 1;
        }
        name = strdup(name);
        free(child->name);
        child->name = name;
    }

    if (o->child) {
        child->next = o->child;
    }
    o->child = child;
    child->parent = o;
}

mpr_obj mpr_obj_new_internal(mpr_obj parent, const char *name, int num_inst, int ephemeral,
                             mpr_type type, int is_local)
{
    mpr_obj o = parent;

    // for now:
    assert(MPR_OBJ == type || MPR_SIG == type);

    RETURN_ARG_UNLESS(name && *name && ('/' != name[strlen(name)-1]), NULL);
    // no parents for maps? or links?
    RETURN_ARG_UNLESS(!parent || parent->type != MPR_MAP, NULL);

    if ('/' == name[0])
        ++name;

    /* we are adding a device, signal, or generic object */
    if (!parent) {
        /* add a graph structure */
        trace("Adding new parent graph\n");
        o = (mpr_obj)mpr_graph_new(0);
        mpr_graph_set_owned((mpr_graph)o, 0);
    }

    if (strchr(name, '/')) {
        char *slash;
        while ((slash = strchr(name, '/'))) {
            slash[0] = '\0';
            o = mpr_obj_build_tree(o, name, MPR_OBJ, is_local);
            name = slash + 1;
        }
    }
    o = mpr_obj_build_tree(o, name, type, is_local);

#ifdef DEBUG
    name = mpr_obj_get_full_name(o, 0);
    printf("added object '%s'\n", name);
    free((char*)name);
#endif

//    if (!parent) {
//        /* add a new graph */
//        mpr_graph g = mpr_graph_new();
//        mpr_obj o2 = o;
//        while (1) {
//            o2->graph = g;
//            if (o2->parent)
//                o2 = o2->parent;
//            else {
//                /* promote root object to a device */
//                mpr_obj o3 = mpr_graph_add_obj(g, MPR_DEV)
//                break;
//            }
//        }
//    }
//    else if (MPR_GRAPH == parent->type) {
//        /* adding a new device */
//    }

    o->is_local = is_local;
    o->num_inst = num_inst;
    o->status = MPR_STATUS_NEW;
//    if (ephemeral)
//        o->flags |= MPR_INST_EPHEMERAL;

    if (MPR_GRAPH == parent->type) {
        /* make room for device struct */
        mpr_obj old_ptr = o;
        o = realloc(o, mpr_dev_get_struct_size(is_local));

        trace("reallocating device struct...\n");
        o->type = MPR_DEV;

        // rewrite parent (graph) ptr to this object
        if (parent->child == old_ptr)
            parent->child = o;
        else if (parent->child) {
            mpr_obj o2 = parent->child;
            while (o2->next) {
                if (o2->next == old_ptr) {
                    o->next = o2->next->next;
                    o2->next = o;
                    break;
                }
                o2 = o2->next;
            }
        }

        // no children yet

        /* set root to self */
        o->root = (mpr_dev)o;
    }
    else
        o->root = parent->root;

    /* inform graph */
//    mpr_graph_add_obj((mpr_graph)(((mpr_obj)o->root)->parent), o);

    // TODO: update parent status flags?

    return o;
}

//not used for maps, links
mpr_obj mpr_obj_new(mpr_obj parent, const char *name)
{
    RETURN_ARG_UNLESS(name && *name && parent, NULL);
    return mpr_obj_new_internal(parent, name, 1, 0, MPR_OBJ, 1);
}

void mpr_obj_free(mpr_obj o)
{
    // free children recursively
    while (o->child) {
        mpr_obj child = o->child;
        o->child = o->child->next;
        mpr_obj_free(child);
    }
    trace("freeing obj %s%s\n", o->name, o->is_local ? "*" : "");
    // remove from parent
    if (o->parent) {
        mpr_obj *child = &o->parent->child, temp; // address of parent->child
        while (*child) {
            if (*child == o) {
                temp = *child;
                *child = temp->next;
                break;
            }
            child = &(*child)->next;
        }
    }

    // TODO: free implicitly-added objects that are now childless

//    switch(o->type) {
//        case MPR_DEV:
//            mpr_dev_free_internal((mpr_dev)o);
//        case MPR_SIG:
//            mpr_sig_free_internal((mpr_sig)o);
//        case MPR_MAP:
//            mpr_map_free_internal((mpr_map)o);
//        case MPR_GRAPH:
//            mpr_graph_free_internal((mpr_graph)o);
//        default:
//            break;
//    }

    FUNC_IF(free, o->name);
    FUNC_IF(mpr_tbl_free, o->props.staged);
    FUNC_IF(mpr_tbl_free, o->props.synced);
//    FUNC_IF(mpr_id_mapper_free, o->id_mapper);
}

const char *mpr_obj_get_full_name(mpr_obj o, int offset)
{
    int depth = 0, length = 0;
    char *full_name;
    mpr_obj o2 = o;

    RETURN_ARG_UNLESS(o->name, NULL);

    while ((o2 = o2->parent))
        ++depth;

    RETURN_ARG_UNLESS(depth >= offset, NULL);
    depth -= offset;

    o2 = o;
    do {
        length += (strlen(o2->name) + 1);
        --depth;
    } while (depth >= 0 && (o2 = o2->parent) && o2->name);
    full_name = malloc(length + 1);
    full_name[length] = '\0';

    o2 = o;
    do {
        int str_len = strlen(o2->name);
        length -= str_len;
        memcpy(full_name + length, o2->name, str_len);
        full_name[--length] = '/';
    } while ((o2 = o2->parent) && length > 0);

    return full_name;
}

void mpr_obj_print_full_name(mpr_obj o)
{
    if (o->parent && o->parent->name)
        mpr_obj_print_full_name(o->parent);
    printf("/%s", o->name);
}

mpr_graph mpr_obj_get_graph(mpr_obj o)
{
    return o ? o->graph : 0;
//    return (o && o->root) ? o->root->parent : 0;
}

mpr_type mpr_obj_get_type(mpr_obj o)
{
    return o ? o->type : 0;
}

mpr_obj mpr_obj_get_child_by_name(mpr_obj o, const char *name)
{
    RETURN_ARG_UNLESS(o && name, NULL);

    /* skip leading slash */
    if ('/' == name[0])
        ++name;

    o = o->child;
    while (o && name) {
        char *slash = strchr(name, '/');
        int len  = slash ? slash - name : strlen(name);
        while (o) {
            if (strlen(o->name) == len && !strncmp(o->name, name, len)) {
                /* found matching object */
                if (!slash) {
                    /* no more string to match */
                    return o;
                }
                /* advance to next section and check this object's children */
                name = slash + 1;
                o = o->child;
                break;
            }
            o = o->next;
        }
    }
    return NULL;
}

//mpr_obj mpr_obj_get_child_by_id(mpr_obj o, mpr_id id)
//{
//    RETURN_ARG_UNLESS(id && o && (o = o->child), NULL);
//
//    while (o) {
//        if (id == o->id)
//            break;
//        o = o->next;
//    };
//    return o;
//}

int mpr_obj_incr_version(mpr_obj o)
{
    int version;
    RETURN_ARG_UNLESS(o, 0);
    if (o->is_local) {
        ++o->version;
//        o->version = version = ++o->root->version;
        mpr_tbl_set_is_dirty(o->props.synced, 1);
        if (o->type == MPR_SIG)
            ((mpr_obj)mpr_sig_get_dev((mpr_sig)o))->status |= MPR_DEV_SIG_CHANGED;
    }
    else if (o->props.staged) {
        mpr_tbl_set_is_dirty(o->props.staged, 1);
    }
    version = o->version;
//    while (o) {
//        o->status |= MPR_STATUS_MODIFIED;
//        o = o->parent;
//    }
    return version;
}

int mpr_obj_get_status(mpr_obj obj)
{
    return obj->status & 0xFFFF;
}

void mpr_obj_set_status(mpr_obj obj, int add, int remove)
{
    obj->status = (obj->status | add) & ~remove;
}

void mpr_obj_reset_status(mpr_obj obj)//, int recursive)
{
    obj->status &= (  MPR_STATUS_EXPIRED
                    | MPR_STATUS_STAGED
                    | MPR_STATUS_ACTIVE
                    | MPR_STATUS_HAS_VALUE
                    | 0xFFFF0000);
    if (MPR_GRAPH == obj->type)
        mpr_graph_reset_obj_statuses((mpr_graph)obj);
//    if (recursive) {
//        obj = obj->child;
//        while (obj) {
//            mpr_obj_reset_status(obj, 1);
//            obj = obj->next;
//        }
//    }
}

int mpr_obj_get_num_props(mpr_obj o, int staged)
{
    int len = 0;
    if (o) {
        if (o->props.synced)
            len += mpr_tbl_get_num_records(o->props.synced);
        if (staged && o->props.staged)
            len += mpr_tbl_get_num_records(o->props.staged);
    }
    return len;
}

mpr_prop mpr_obj_get_prop_by_key(mpr_obj o, const char *s, int *l, mpr_type *t,
                                 const void **v, int *p)
{
    RETURN_ARG_UNLESS(o && o->props.synced && s, 0);
    return mpr_tbl_get_record_by_key(o->props.synced, s, l, t, v, p);
}

mpr_prop mpr_obj_get_prop_by_idx(mpr_obj o, int p, const char **k, int *l,
                                 mpr_type *t, const void **v, int *pub)
{
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    return mpr_tbl_get_record_by_idx(o->props.synced, p, k, l, t, v, pub);
}

int mpr_obj_get_prop_as_int32(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return      *(int*)val;
        case MPR_INT64: return (int)*(int64_t*)val;
        case MPR_FLT:   return (int)*(float*)val;
        case MPR_DBL:   return (int)*(double*)val;
        case MPR_TYPE:  return (int)*(mpr_type*)val;
        case MPR_TIME:  return (int)(*(mpr_time*)val).sec;
        default:        return 0;
    }
}

int64_t mpr_obj_get_prop_as_int64(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return (int64_t)*(int*)val;
        case MPR_INT64: return          *(int64_t*)val;
        case MPR_FLT:   return (int64_t)*(float*)val;
        case MPR_DBL:   return (int64_t)*(double*)val;
        case MPR_TYPE:  return (int64_t)*(mpr_type*)val;
        case MPR_TIME:  return (int64_t)(*(mpr_time*)val).sec;
        default:        return 0;
    }
}

float mpr_obj_get_prop_as_flt(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return (float)*(int*)val;
        case MPR_INT64: return (float)*(int64_t*)val;
        case MPR_FLT:   return        *(float*)val;
        case MPR_DBL:   return (float)*(double*)val;
        case MPR_TYPE:  return (float)*(mpr_type*)val;
        case MPR_TIME:  return (float)mpr_time_as_dbl(*(mpr_time*)val);
        default:        return 0;
    }
}

double mpr_obj_get_prop_as_dbl(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val, 0);

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: return (double)*(int*)val;
        case MPR_INT64: return (double)*(int64_t*)val;
        case MPR_FLT:   return (double)*(float*)val;
        case MPR_DBL:   return         *(double*)val;
        case MPR_TYPE:  return (double)*(mpr_type*)val;
        case MPR_TIME:  return         mpr_time_as_dbl(*(mpr_time*)val);
        default:        return 0;
    }
}

const char *mpr_obj_get_prop_as_str(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_STR == type && 1 == len, 0);
    return (const char*)val;
}

const void *mpr_obj_get_prop_as_ptr(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_PTR == type && 1 == len, 0);
    return val;
}

mpr_obj mpr_obj_get_prop_as_obj(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_OBJ >= type && 1 == len, 0);
    return (mpr_obj)val;
}

mpr_list mpr_obj_get_prop_as_list(mpr_obj o, mpr_prop p, const char *s)
{
    const void *val;
    mpr_type type;
    int len;
    RETURN_ARG_UNLESS(o && o->props.synced, 0);
    if (s)
        p = mpr_tbl_get_record_by_key(o->props.synced, s, &len, &type, &val, NULL);
    else
        p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, &len, &type, &val, NULL);
    RETURN_ARG_UNLESS(val && MPR_LIST == type && 1 == len, 0);
    return (mpr_list)val;
}

mpr_prop mpr_obj_set_prop(mpr_obj o, mpr_prop p, const char *s, int len,
                          mpr_type type, const void *val, int publish)
{
    int flags, updated;
    mpr_tbl tbl;
    RETURN_ARG_UNLESS(o, 0);
    if (MPR_PROP_UNKNOWN == p || MPR_PROP_EXTRA == p || !MASK_PROP_BITFLAGS(p)) {
        if (!s || '@' == s[0])
            return MPR_PROP_UNKNOWN;
        p = mpr_prop_from_str(s);
    }

    /* MPR_PROP_DATA and MPR_PROP_SYNCED always refer to a local proporty even if obj is remote */
    if (!o->props.staged || !publish || p == MPR_PROP_DATA || p == MPR_PROP_SYNCED) {
        tbl = o->props.synced;
        flags = MOD_LOCAL;
    }
    else {
        /* Check if the synced property is writable before trying to edit staged table */
        if (!mpr_tbl_get_record_is_writable(o->props.synced, p)) {
            trace("Cannot set read-only property [%d] '%s'\n", p, s ? s : mpr_prop_as_str(p, 1));
            return MPR_PROP_UNKNOWN;
        }
        tbl = o->props.staged;
        flags = MOD_REMOTE;
    }
    if (!publish)
        flags |= LOCAL_ACCESS;
    updated = mpr_tbl_add_record(tbl, p, s, len, type, val, flags);
    if (updated && o->is_local)
        mpr_obj_incr_version(o);
    return updated ? p : MPR_PROP_UNKNOWN;
}

int mpr_obj_remove_prop(mpr_obj o, mpr_prop p, const char *s)
{
    int updated = 0, public = 0;

    if (MPR_PROP_DATA == p || o->is_local)
        updated = mpr_tbl_remove_record(o->props.synced, p, s, MOD_LOCAL);
    else if (MPR_PROP_EXTRA == p) {
        if (s)
            p = mpr_tbl_get_record_by_key(o->props.synced, s, NULL, NULL, NULL, &public);
        else
            p = mpr_tbl_get_record_by_idx(o->props.synced, p, NULL, NULL, NULL, NULL, &public);
        if (MPR_PROP_UNKNOWN == p)
            return 0;
        if (public)
            updated = mpr_tbl_add_record(o->props.staged, p | PROP_REMOVE, s, 0, 0, 0, MOD_REMOTE);
        else
            updated = mpr_tbl_remove_record(o->props.synced, p, s, MOD_LOCAL);
    }
    else
        trace("Cannot remove static property [%d] '%s'\n", p, s ? s : mpr_prop_as_str(p, 1));
    if (updated && o->is_local)
        mpr_obj_incr_version(o);
    return updated ? 1 : 0;
}

void mpr_obj_push(mpr_obj o)
{
    mpr_net n;
    RETURN_UNLESS(o);
    n = mpr_graph_get_net(mpr_obj_get_graph(o));
    ++o->version;

    if (MPR_DEV == o->type) {
        mpr_dev d = (mpr_dev)o;
        if (o->is_local) {
            RETURN_UNLESS(mpr_dev_get_is_registered(d));
            mpr_net_use_subscribers(n, (mpr_local_dev)d, o->type);
            mpr_dev_send_state(d, MSG_DEV);
        }
        else {
            mpr_net_use_bus(n);
            mpr_dev_send_state(d, MSG_DEV_MOD);
        }
    }
    else if (o->type & MPR_SIG) {
        mpr_sig s = (mpr_sig)o;
        if (o->is_local) {
            mpr_type type = ((MPR_DIR_OUT == mpr_sig_get_dir(s)) ? MPR_SIG_OUT : MPR_SIG_IN);
            mpr_dev d = mpr_sig_get_dev(s);
            RETURN_UNLESS(mpr_dev_get_is_registered(d));
            mpr_net_use_subscribers(n, (mpr_local_dev)d, type);
            mpr_sig_send_state(s, MSG_SIG);
        }
        else {
            mpr_net_use_bus(n);
            mpr_sig_send_state(s, MSG_SIG_MOD);
        }
    }
    else if (o->type & MPR_MAP) {
        int status = o->status;
        mpr_map m = (mpr_map)o;
        mpr_net_use_bus(n);
        if ((status & (MPR_STATUS_ACTIVE | MPR_STATUS_REMOVED)) == MPR_STATUS_ACTIVE) {
            mpr_map_send_state(m, -1, MSG_MAP_MOD, 0);
        }
        else if (   !o->is_local
                 || (MPR_SLOT_DEV_KNOWN & mpr_local_map_update_status((mpr_local_map)m))) {
            mpr_map_send_state(m, -1, MSG_MAP, 0);
        }
        else {
            trace("didn't send /map message\n");
            --o->version;
        }
    }
    else {
        trace("mpr_obj_push(): unknown object type %d\n", o->type);
        return;
    }

    /* clear the staged properties */
    FUNC_IF(mpr_tbl_clear, o->props.staged);
}

void mpr_obj_print(mpr_obj o, int include_props)
{
    int i = 0, len, num_props;
    mpr_prop p;
    const char *key;
    mpr_type type;
    const void *val;

    RETURN_UNLESS(o);

    switch (o->type) {
        case MPR_GRAPH:
            mpr_graph_print((mpr_graph)o);
            break;
        case MPR_OBJ:
            printf("OBJ: ");
            mpr_prop_print(1, MPR_OBJ, o);
            break;
        case MPR_DEV:
            printf("DEVICE: ");
            mpr_prop_print(1, MPR_DEV, o);
            break;
        case MPR_SIG:
            printf("SIGNAL: ");
            mpr_prop_print(1, MPR_SIG, o);
            break;
        case MPR_MAP:
            printf("MAP: ");
            mpr_prop_print(1, MPR_MAP, o);
            break;
        default:
            trace("mpr_obj_print(): unknown object type %d.", o->type);
            return;
    }

    RETURN_UNLESS(include_props && o->props.synced);
    num_props = mpr_obj_get_num_props(o, 0);
    for (i = 0; i < num_props; i++) {
        p = mpr_tbl_get_record_by_idx(o->props.synced, i, &key, &len, &type, &val, 0);
        die_unless(val != 0 || MPR_LIST == type, "returned zero value\n");

        /* already printed this */
        if (MPR_PROP_NAME == p)
            continue;
        /* don't print device signals as metadata */
        if (MPR_DEV == o->type && MPR_PROP_SIG == p) {
            if (val && MPR_LIST == type)
                mpr_list_free((mpr_list)val);
            continue;
        }

        printf(", %s=", key);

        /* handle pretty-printing a few enum values */
        if (1 == len && MPR_INT32 == type) {
            switch(p) {
                case MPR_PROP_DIR:
                    printf("%s", MPR_DIR_OUT == *(int*)val ? "output" : "input");
                    break;
                case MPR_PROP_PROCESS_LOC:
                    printf("%s", mpr_loc_as_str(*(int*)val));
                    break;
                case MPR_PROP_PROTOCOL:
                    printf("%s", mpr_proto_as_str(*(int*)val));
                    break;
                default:
                    mpr_prop_print(len, type, val);
            }
        }
        else
            mpr_prop_print(len, type, val);

        if (!o->props.staged)
            continue;

        /* check if staged values exist */
        if (MPR_PROP_EXTRA == p)
            p = mpr_tbl_get_record_by_key(o->props.staged, key, &len, &type, &val, 0);
        else
            p = mpr_tbl_get_record_by_idx(o->props.staged, p, NULL, &len, &type, &val, 0);
        if (MPR_PROP_UNKNOWN != p) {
            printf(" (staged: ");
            mpr_prop_print(len, type, val);
            printf(")");
        }
    }
    if (MPR_MAP == o->type) {
        /* also print slot props */
        mpr_map map = (mpr_map)o;
        for (i = 0; i < mpr_map_get_num_src(map); i++)
            mpr_slot_print(mpr_map_get_src_slot(map, i), 0);
        mpr_slot_print(mpr_map_get_dst_slot(map), 1);
    }
    printf("\n");
}

void mpr_obj_print_tree(mpr_obj o, int indent)
{
    int i;
    for (i = 0; i < indent; i++)
        printf(" ");
    printf("%s (%p)\n", o->name, o->name);

    o = o->child;
    while (o) {
        mpr_obj_print_tree(o, indent + 2);
        o = o->next;
    }
}
