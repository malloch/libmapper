
#ifndef __MPR_OBJECT_H__
#define __MPR_OBJECT_H__
#define __MPR_TYPES_H__

typedef struct _mpr_obj *mpr_obj;

typedef struct _mpr_dict {
    struct _mpr_tbl *synced;
    struct _mpr_tbl *staged;
} mpr_dict_t, *mpr_dict;

#include "id.h"
#include "mpr_type.h"
#include "value.h"

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *ctx;
    struct _fptr_list *next;
    int events;
    int manage;
} *fptr_list;

typedef struct _mpr_obj
{
    struct _mpr_graph *graph;       /*!< Pointer back to the graph. */
    struct _mpr_dev *root;          /*!< Pointer back to the root of this tree. */
    struct _mpr_obj *parent;        /*!< Parent of this object – only one for now. */
    struct _mpr_obj *child;         /*!< Chidren of this object. */
    struct _mpr_obj *next;          /*!< Next sibling object. */

    mpr_id id;                      /*!< Unique id for this object. */
    char *name;                     /*!< The name of this object. */
    void *data;                     /*!< User context pointer. */
    struct _mpr_dict props;         /*!< Properties associated with this object. */
    int is_local;
    int version;                    /*!< Version number. */
    uint16_t status;
    mpr_type type;                  /*!< Object type. */

    int use_inst;                   /*!< 1 if object uses instances, 0 otherwise. */
    int num_inst;                   /*!< Number of instances. */
    int ephemeral;                  /*!< 1 if signal is ephemeral, 0 otherwise. */

    fptr_list callbacks;            /*!< List of object record callbacks. */
} mpr_obj_t;

#include "graph.h"
#include "util/mpr_inline.h"
#include "table.h"

// TODO: make static later
void mpr_obj_build_tree_temp(mpr_obj parent, mpr_obj child);

void mpr_obj_init(mpr_obj obj, mpr_graph graph, const char *name, mpr_type type, int is_local);

void mpr_obj_free(mpr_obj obj);

int mpr_obj_incr_version(mpr_obj obj);

const char *mpr_obj_get_full_name(mpr_obj o, int offset);

mpr_obj mpr_obj_get_child_by_name(mpr_obj o, const char *name);

void mpr_obj_print_tree(mpr_obj o, int indent);

void mpr_obj_print_full_name(mpr_obj o);

MPR_INLINE static mpr_id mpr_obj_get_id(mpr_obj obj)
    { return obj->id; }

MPR_INLINE static void mpr_obj_set_id(mpr_obj obj, mpr_id id)
    { obj->id = id; }

void mpr_obj_set_status(mpr_obj obj, int add, int remove);

MPR_INLINE static int mpr_obj_get_is_local(mpr_obj obj)
    { return obj->is_local; }

MPR_INLINE static void mpr_obj_set_is_local(mpr_obj obj, int val)
    { obj->is_local = val; }

MPR_INLINE static int mpr_obj_get_version(mpr_obj obj)
    { return obj->version; }

MPR_INLINE static void mpr_obj_set_version(mpr_obj obj, int ver)
    { obj->version = ver; }

MPR_INLINE static void mpr_obj_clear_empty_props(mpr_obj obj)
    { mpr_tbl_clear_empty_records(obj->props.synced); }

MPR_INLINE static mpr_tbl mpr_obj_get_prop_tbl(mpr_obj obj)
    { return obj->props.synced; }

MPR_INLINE static void mpr_obj_add_props_to_msg(mpr_obj obj, lo_message msg)
    { mpr_tbl_add_to_msg(obj->is_local ? obj->props.synced : 0, obj->props.staged, msg); }

/*! Call registered graph callbacks for a given object type.
 *  \param caller       The calling object.
 *  \param topic        The object to pass to the callbacks.
 *  \param event        Event to match before calling callback.
 *  \param evt          The event type. */
int mpr_obj_call_cbs(mpr_obj caller, mpr_obj topic, mpr_status event, mpr_id inst);

void mpr_obj_free_cbs(mpr_obj obj);

/**** Instances ****/

int mpr_obj_get_num_inst_internal(mpr_obj obj);

int mpr_obj_get_use_inst(mpr_obj obj);

#endif /* __MPR_OBJECT_H__ */
