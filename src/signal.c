#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <assert.h>

#include "bitflags.h"
#include "device.h"
#include "graph.h"
#include "mpr_signal.h"
#include "object.h"
#include "path.h"
#include "table.h"
#include "util/mpr_set_coerced.h"

#include <mapper/mapper.h>

#ifdef _MSC_VER
#include <malloc.h>
#endif

#define MAX_INST 128
#define BUFFSIZE 512

/* Signals and signal instances
 * signal instances have ids, id_pairs (when active), and an idx
 * */

/* Function prototypes */
static int mpr_sig_get_inst(mpr_local_sig lsig, mpr_id *local_id, mpr_id *global_id,
                            mpr_time time, int flags, uint8_t activate, uint8_t call_handler);
static void mpr_sig_release_inst_internal(mpr_local_sig lsig, int id_map_idx);

static int activate_instance(mpr_local_sig lsig, mpr_id *local_id, mpr_id *global_id);
static int initialize_instance(mpr_local_sig lsig, mpr_obj_inst in, mpr_id_pair ids, int activate);

#define MPR_SIG_STRUCT_ITEMS                                                            \
    mpr_obj_t obj;              /* always first */                                      \
    const char *path;           /*! OSC path.  Must start with '/'. */                  \
    char *unit;                 /*!< The unit of this signal, or NULL for N/A. */       \
    int dir;                    /*!< `DIR_OUTGOING` / `DIR_INCOMING` / `DIR_BOTH` */    \
    int len;                    /*!< Length of the signal vector, or 1 for scalars. */  \
    int num_maps_in;            /* TODO: use dynamic query instead? */                  \
    int num_maps_out;           /* TODO: use dynamic query instead? */                  \
    mpr_steal_type steal_mode;  /*!< Type of voice stealing to perform. */              \
    mpr_type type;              /*!< The type of this signal. */

/*! A record that describes properties of a signal. */
typedef struct _mpr_sig
{
    MPR_SIG_STRUCT_ITEMS
    mpr_dev dev;
} mpr_sig_t;

/*! A signal is defined as a vector of values, along with some metadata. */

typedef struct _mpr_local_sig
{
    MPR_SIG_STRUCT_ITEMS
    mpr_local_dev dev;

    mpr_obj_id_pair id_map;         /*!< ID maps and active instances. */
    mpr_value value;
    unsigned int id_map_size;
    mpr_obj_inst *inst;             /*!< Array of pointers to the signal insts. */
    mpr_bitflags updated_inst;      /*!< Bitflags to indicate updated instances. */

    /*! An optional function to be called when the signal value changes or when
     *  signal instance management events occur.. */
    void *handler;
    int event_flags;                /*! Flags for deciding when to call the
                                     *  instance event handler. */

    mpr_local_slot *slots_in;
    mpr_local_slot *slots_out;

    uint8_t locked;
    uint8_t updated;                /* TODO: fold into updated_inst bitflags. */
} mpr_local_sig_t;

size_t mpr_sig_get_struct_size(int is_local)
{
    return is_local ? sizeof(mpr_local_sig_t) : sizeof(mpr_sig_t);
}

static int _compare_inst_ids(const void *l, const void *r)
{
    return (*(mpr_obj_inst*)l)->id - (*(mpr_obj_inst*)r)->id;
}

static mpr_obj_inst _find_inst_by_id(mpr_local_sig lsig, mpr_id id)
{
    mpr_obj_inst_t in, *inp, **inpp;
    RETURN_ARG_UNLESS(lsig->obj.num_inst, 0);
    RETURN_ARG_UNLESS(mpr_obj_get_use_inst((mpr_obj)lsig), lsig->inst[0]);
    inp = &in;
    in.id = id;
    inpp = bsearch(&inp, lsig->inst, lsig->obj.num_inst, sizeof(mpr_obj_inst), _compare_inst_ids);
    return (inpp && *inpp) ? *inpp : 0;
}

MPR_INLINE static mpr_obj_inst _get_inst_by_id_map_idx(mpr_local_sig sig, int id_map_idx)
{
    return sig->id_map[id_map_idx].inst;
}

/*! Helper to check if a type character is valid. */
MPR_INLINE static int check_sig_length(int length)
{
    return (length < 1 || length > MPR_MAX_VECTOR_LEN);
}

MPR_INLINE static int check_types(const mpr_type *types, int len, mpr_type sig_type, int sig_len)
{
    int i, vals = 0;
    RETURN_ARG_UNLESS(len >= sig_len, -1);
    for (i = 0; i < sig_len; i++) {
        if (types[i] == sig_type)
            ++vals;
        else if (types[i] != MPR_NULL)
            return -1;
    }
    return vals;
}

static void process_maps(mpr_local_sig sig, int id_map_idx)
{
    mpr_id_pair ids = sig->id_map[id_map_idx].ids;
    mpr_obj_inst in;
    mpr_local_map map;
    int i, j, inst_idx;
    uint8_t *locked = &sig->locked;
    mpr_time time;

    /* abort if signal is already being processed - might be a local loop */
    if (*locked) {
        trace("Mapping loop detected on signal %s! (1)\n", sig->path);
        return;
    }

    in = _get_inst_by_id_map_idx(sig, id_map_idx);
    inst_idx = in->idx;
    time = mpr_dev_get_time((mpr_dev)sig->dev);

    /* TODO: remove duplicate flag set */
    mpr_local_dev_set_sending(sig->dev); /* mark as updated */

    /* TODO: use an incrementing counter for slot ids; remove item and squash array when map is
     * removed; use bsearch for slot lookup since ids will be monotonic though not sequential */

    if (!mpr_value_get_num_samps(sig->value, inst_idx)) {
        RETURN_UNLESS(mpr_obj_get_use_inst((mpr_obj)sig));
        *locked = 1;
        for (i = 0; i < sig->num_maps_in; i++) {
            mpr_local_slot dst_slot = sig->slots_in[i];
            map = (mpr_local_map)mpr_slot_get_map((mpr_slot)dst_slot);
            if ((mpr_obj_get_status((mpr_obj)map) & (MPR_STATUS_ACTIVE | MPR_STATUS_REMOVED)) != MPR_STATUS_ACTIVE)
                continue;

            mpr_id_pair tmp_ids = mpr_local_map_get_ids(map);
            if (tmp_ids->global == ids->global) {
                tmp_ids->local = tmp_ids->global = 0;
                mpr_dev_ids_decref_global(sig->dev, ids);
            }

            /* reset associated output memory */
            mpr_slot_set_value(dst_slot, inst_idx, NULL, time);

            for (j = 0; j < mpr_map_get_num_src((mpr_map)map); j++) {
                mpr_local_slot src_slot = (mpr_local_slot)mpr_map_get_src_slot((mpr_map)map, j);

                /* reset associated input memory */
                mpr_slot_set_value(src_slot, inst_idx, NULL, time);

                if (!mpr_local_map_get_has_scope(map, ids->global))
                    continue;
                if (sig->id_map[id_map_idx].status & RELEASED_REMOTELY)
                    continue;

                /* send release to upstream */
                mpr_slot_build_msg(src_slot, 0, 0, ids);
                /* TODO: consider calling this later for batch releases */
                mpr_local_slot_send_msg(src_slot, NULL, time, mpr_map_get_protocol((mpr_map)map));
            }
        }
        for (i = 0; i < sig->num_maps_out; i++) {
            mpr_local_slot src_slot = sig->slots_out[i], dst_slot;
            map = (mpr_local_map)mpr_slot_get_map((mpr_slot)src_slot);
            if ((mpr_obj_get_status((mpr_obj)map) & (MPR_STATUS_ACTIVE | MPR_STATUS_REMOVED)) != MPR_STATUS_ACTIVE)
                continue;

            /* reset associated output memory */
            dst_slot = (mpr_local_slot)mpr_map_get_dst_slot((mpr_map)map);
            mpr_slot_set_value(dst_slot, inst_idx, NULL, time);

            /* reset associated input memory */
            mpr_slot_set_value(src_slot, inst_idx, NULL, time);

            if (mpr_obj_get_use_inst((mpr_obj)map)) {
                /* send release to downstream */
                if (MPR_LOC_SRC == mpr_map_get_process_loc((mpr_map)map)) {
                    /* need to build msg immediately since the ids won't be available later */
                    /* TODO: use updated bitflags (or released before/after if necessary) to mark release,
                     * don't send immediately */
                    mpr_slot_build_msg(dst_slot, 0, 0, ids);
                    mpr_local_map_set_updated(map, inst_idx);
                }
                else if (mpr_local_map_get_has_scope(map, ids->global)) {
                    /* need to build msg immediately since the ids won't be available later */
                    mpr_slot_build_msg(src_slot, 0, 0, ids);
                }
            }
        }
        *locked = 0;
        return;
    }
    *locked = 1;
    for (i = 0; i < sig->num_maps_out; i++) {
        mpr_local_slot src_slot;
        int all;

        src_slot = sig->slots_out[i];

        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)src_slot);
        if ((mpr_obj_get_status((mpr_obj)map) & (MPR_STATUS_ACTIVE | MPR_STATUS_REMOVED)) != MPR_STATUS_ACTIVE)
            continue;

        /* TODO: should we continue for out-of-scope local destination updates? */
        if (mpr_obj_get_use_inst((mpr_obj)map) && !(mpr_local_map_get_has_scope(map, ids->global)))
            continue;

        /* If this signal is non-instanced but the map has other instanced
         * sources we will need to update all of the active map instances. */
        all = (   mpr_map_get_num_src((mpr_map)map) > 1
               && mpr_obj_get_num_inst_internal((mpr_obj)map) > sig->obj.num_inst);

        if (MPR_LOC_DST == mpr_map_get_process_loc((mpr_map)map)) {
            /* bypass map processing and bundle value without type coercion */
            mpr_slot_build_msg(src_slot, sig->value, inst_idx,
                               (   mpr_obj_get_use_inst((mpr_obj)map)
                                && mpr_obj_get_use_inst((mpr_obj)sig)) ? ids : 0);
            continue;
        }

        if (!mpr_local_map_get_expr(map)) {
            trace("error: missing expression!\n");
            continue;
        }

        /* copy input value */
        mpr_slot_set_value(src_slot, inst_idx, mpr_value_get_value(sig->value, inst_idx, 0), time);

        if (!mpr_slot_get_causes_update((mpr_slot)src_slot))
            continue;

        if (all) {
            /* find a source signal with more instances */
            for (j = 0; j < mpr_map_get_num_src((mpr_map)map); j++) {
                mpr_slot src_slot2 = mpr_map_get_src_slot((mpr_map)map, j);
                mpr_sig src_sig = mpr_slot_get_sig(src_slot2);
                if (   src_sig->obj.is_local
                    && mpr_slot_get_num_inst(src_slot2) > mpr_slot_get_num_inst((mpr_slot)src_slot))
                    sig = (mpr_local_sig)src_sig;
            }
            id_map_idx = 0;
        }

        for (; id_map_idx < sig->id_map_size; id_map_idx++) {
            /* check if map instance is active */
            mpr_obj_inst in = _get_inst_by_id_map_idx(sig, id_map_idx);
            if (!in && (all || mpr_obj_get_use_inst((mpr_obj)sig)))
                continue;
            inst_idx = in->idx;
            mpr_local_map_set_updated(map, inst_idx);
            if (!all)
                break;
        }
    }
    *locked = 0;
}

/* Notes:
 * - Incoming signal values may be scalars or vectors, but much match the length of the target
 *   signal or mapping slot.
 * - Vectors are of homogeneous type (MPR_INT32, MPR_FLT or MPR_DBL) however individual elements
 *   may have no value (type MPR_NULL) if the map is processed at the source device.
 * - A vector consisting completely of nulls indicates a signal instance release
 *   TODO: use more specific message for release?
 * - Updates to a specific signal instance are indicated using the label "@in" followed by a 64bit
 *   integer which uniquely identifies this instance within the distributed graph
 * - Updates to specific "slots" of a convergent (i.e. multi-source) mapping are indicated using
 *   the label "@sl" followed by a single integer slot #
 * - Instance creation and release may also be triggered by expression evaluation. Refer to the
 *   document "Understanding Instanced Signals and Maps" for more information.
 * - Multiple instance can be updated using a single message, each preceded by the instance id
 *   as decribed above.
 * - example: "/mypath" ,sishffhffN "sl" 0 "in" 1234 1.0 2.0 3.0 "in" 5678 4.0 5.0
 */
/* Current solution for persistent (non-ephemeral) signal instances:
 * - once a signal instance is active it continues using the same ids
 * - this means it always has the same GUID to downstream peers.
 * - flexible input (mapping something new to the persistent instances) is handled
 *   by using dynamic proxy id_pairs */

int mpr_sig_osc_handler(const char *path, const char *types, lo_arg **argv, int argc,
                        lo_message msg, void *data)
{
    mpr_local_sig sig = (mpr_local_sig)data;
    mpr_local_dev dev;
    mpr_obj_inst in;
    mpr_net net = mpr_graph_get_net(sig->obj.graph);
    int i, offset = 0, val_len = 0, vals;
    int id_map_idx, inst_idx, slot_id = -1, map_manages_inst = 0;
    mpr_id global_id = 0;
    mpr_id_pair ids, remote_ids = 0;
    mpr_local_map map = 0;
    mpr_local_slot slot = 0;
    mpr_sig slot_sig = 0;
    mpr_time time;

    assert(sig);
    dev = sig->dev;

#ifdef DEBUG
    printf("'%s:%s' received update: ", mpr_dev_get_name((mpr_dev)dev), sig->path);
    lo_message_pp(msg);
#endif

    TRACE_RETURN_UNLESS(sig->obj.num_inst, 0, "signal '%s' has no instances.\n", sig->path);
    RETURN_ARG_UNLESS(argc, 0);

    time = mpr_net_get_bundle_time(net);

    /* We need to consider that there may be properties prepended to the msg
     * check length and find properties if any */
    if (types[0] == MPR_STR) {
        if ((strcmp(&argv[0]->s, "@sl") == 0) && argc >= 2) {
            TRACE_RETURN_UNLESS(types[1] == MPR_INT32, 0,
                                "error in mpr_sig_osc_handler: bad arguments for 'slot' prop.\n")
            slot_id = argv[1]->i32;
            trace("retrieved slot id %d\n", slot_id);
            offset += 2;
        }
    }
again:
    if (types[offset] == MPR_STR) {
        if ((strcmp(&argv[offset]->s, "@in") == 0) && argc >= offset + 2) {
            TRACE_RETURN_UNLESS(types[offset + 1] == MPR_INT64, 0,
                                "error in mpr_sig_osc_handler: bad arguments for 'instance' prop.\n")
            global_id = argv[offset + 1]->i64;
            trace("retrieved global id %"PR_MPR_ID" from message index %d\n", global_id, offset + 1);
            offset += 2;
        }
        else {
            trace("error in mpr_sig_osc_handler: unknown property name '%s'.\n", &argv[offset]->s);
            return 0;
        }
    }
    val_len = offset;
    while (val_len < argc && types[val_len] != MPR_STR)
        ++val_len;
    val_len -= offset;

    if (slot_id >= 0) {
        mpr_expr expr;
        /* retrieve mapping associated with this slot */
        for (i = 0; i < sig->num_maps_in; i++) {
            map = (mpr_local_map)mpr_slot_get_map((mpr_slot)sig->slots_in[i]);
            if ((slot = (mpr_local_slot)mpr_map_get_src_slot_by_id((mpr_map)map, slot_id)))
                break;
        }
        TRACE_RETURN_UNLESS(slot, 0, "error in mpr_sig_osc_handler: slot %d not found.\n", slot_id);
        slot_sig = mpr_slot_get_sig((mpr_slot)slot);
        TRACE_RETURN_UNLESS((mpr_obj_get_status((mpr_obj)map) & (MPR_STATUS_ACTIVE | MPR_STATUS_REMOVED)) == MPR_STATUS_ACTIVE,
                            0, "error in mpr_sig_osc_handler: map not yet ready.\n");
        if ((expr = mpr_local_map_get_expr(map)) && MPR_LOC_BOTH != mpr_map_get_locality((mpr_map)map)) {
            vals = check_types(types + offset, val_len, slot_sig->type, slot_sig->len);
            val_len = slot_sig->len;
            map_manages_inst = mpr_expr_get_manages_inst(expr);
        }
        else {
            /* value has already been processed at source device */
            map = 0;
            vals = check_types(types + offset, val_len, sig->type, sig->len);
            val_len = sig->len;
        }
    }
    else {
        vals = check_types(types + offset, val_len, sig->type, sig->len);
        val_len = sig->len;
    }
    RETURN_ARG_UNLESS(vals >= 0, 0);

    /* TODO: optionally discard out-of-order messages
     * requires timebase sync for many-to-one mappings or local updates
     *    if (sig->discard_out_of_order && out_of_order(mpr_value_get_time(sig->value, in->idx, 0), t))
     *        return 0;
     */

    /* TODO: should _get_use_inst() call be part of map_manages_inst? */
    if (map_manages_inst && slot_sig->obj.use_inst && mpr_obj_get_use_inst((mpr_obj)map)) {
        mpr_id_pair ids = mpr_local_map_get_ids(map);
        if (!ids->global) {
            if (!vals) {
                trace("no map-managed instances available for GUID %"PR_MPR_ID"\n", global_id);
                goto done;
            }
            /* id_pair is currently empty - claim it now */
            ids->local = global_id;
            ids->global = mpr_dev_generate_unique_id((mpr_dev)dev);
        }
        else if (ids->local != global_id) {
            trace("no map-managed instances available for GUID %"PR_MPR_ID"\n", global_id);
            goto done;
        }
        trace("remapping instance GUID %"PR_MPR_ID" -> %"PR_MPR_ID"\n", global_id, ids->global);
        global_id = ids->global;
        if (!vals) {
            trace("releasing map-managed instance id_pair\n");
            ids->local = ids->global = 0;
        }
    }

    /* TODO: if dst-processing and the expression is reducing we should enable caching up to slot->num_inst values */

    if (global_id) {
        /* don't activate an instance just to release it again */
        remote_ids = mpr_dev_get_ids_global(dev, global_id);

        if (remote_ids && remote_ids->indirect) {
            trace("remapping instance GUID %"PR_MPR_ID" -> %"PR_MPR_ID"\n", global_id, remote_ids->local);
            global_id = remote_ids->local;
        }
        else
            remote_ids = 0;

        id_map_idx = mpr_sig_get_inst(sig, NULL, &global_id, time, RELEASED_LOCALLY,
                                      (vals && sig->dir == MPR_DIR_IN), 1);

        if (id_map_idx < 0) {
            trace("no instances available for GUID %"PR_MPR_ID"\n", global_id);
            goto done;
        }

        if (sig->id_map[id_map_idx].status & RELEASED_LOCALLY) {
            /* instance was already released locally, we are only interested in release messages */
            if (0 == vals) {
                /* we can clear signal's reference to map */
                mpr_dev_ids_decref_global(dev, sig->id_map[id_map_idx].ids);
                sig->id_map[id_map_idx].ids = 0;
                if (remote_ids) {
                    mpr_dev_ids_decref_global(dev, remote_ids);
                }
            }
            trace("instance already released locally\n");
            goto done;
        }
        if (!sig->id_map[id_map_idx].inst) {
            trace("error in mpr_sig_osc_handler: missing instance!\n");
            goto done;
        }
    }
    else {
        /* use the first available instance */
        for (i = 0; i < sig->obj.num_inst; i++) {
            if (sig->inst[i]->status & MPR_STATUS_ACTIVE)
                break;
        }
        if (i >= sig->obj.num_inst)
            i = 0;
        id_map_idx = mpr_sig_get_inst(sig, &sig->inst[i]->id, NULL, time, RELEASED_REMOTELY, 1, 1);
        if (id_map_idx < 0) {
            trace("no instances available\n");
            goto done;
        }
    }
    in = _get_inst_by_id_map_idx(sig, id_map_idx);
    inst_idx = in->idx;
    ids = sig->id_map[id_map_idx].ids;

    if (vals == 0) {
        if (global_id) {
            if (sig->obj.ephemeral)
                sig->id_map[id_map_idx].status |= RELEASED_REMOTELY;
            if (sig->dir == MPR_DIR_IN)
                sig->id_map[id_map_idx].inst->status |= MPR_STATUS_REL_UPSTRM;
            else
                sig->id_map[id_map_idx].inst->status |= MPR_STATUS_REL_DNSTRM;
            sig->obj.status |= sig->id_map[id_map_idx].inst->status;
            mpr_dev_ids_decref_global(dev, ids);
            if (remote_ids) {
                mpr_dev_ids_decref_global(dev, remote_ids);
            }
        }
        /* if user-code has registered callback for release events we will proceed even if the
         * signal is non-ephemeral. Conceptually this matches setting the "released" bitflag. */
        if (map && !mpr_obj_get_use_inst((mpr_obj)map)) {
            goto done;
        }

        /* Try to release instance, but do not call process_maps() here, since we don't
         * know if the local signal instance will actually be released. */
        if (sig->dir == MPR_DIR_IN)
            mpr_sig_call_handler(sig, MPR_STATUS_REL_UPSTRM, ids->local, inst_idx);
        else
            mpr_sig_call_handler(sig, MPR_STATUS_REL_DNSTRM, ids->local, inst_idx);

        if (map && MPR_LOC_DST == mpr_map_get_process_loc((mpr_map)map) && sig->dir == MPR_DIR_IN) {
            /* Reset memory for corresponding source slot. */
            mpr_slot_set_value(slot, inst_idx, NULL, time);
        }
        goto done;
    }
    else if (sig->dir == MPR_DIR_OUT) {
        trace("can't set value of outgoing signal\n");
        goto done;
    }

    if (map) {
        /* Partial vector updates are not allowed in convergent maps since the slot value mirrors
         * the remote signal value. */
        if (vals != slot_sig->len) {
#ifdef DEBUG
            trace_dev(dev, "error in mpr_sig_osc_handler: partial vector update "
                      "applied to convergent mapping slot.");
#endif
            return 0;
        }
        /* Setting to local timestamp here */
        time = mpr_dev_get_time((mpr_dev)dev);
        /* check if map instance is active */

        /* TODO: why are we checking if instance is already active? */

        if ((in = _get_inst_by_id_map_idx(sig, id_map_idx)) && (in->status & MPR_STATUS_ACTIVE)) {
            inst_idx = in->idx;
            trace("setting value of instance idx %d\n", inst_idx)
            /* TODO: jitter mitigation etc. */
            if (mpr_slot_set_value(slot, inst_idx, argv[offset], time)) {
                mpr_local_map_set_updated(map, inst_idx);
                mpr_local_dev_set_receiving(dev);
            }
        }
        goto done;
    }
    /* If no instance id was included in the message we will apply this update to all instances */
    if (!global_id) {
        id_map_idx = 0;
    }

    for (; id_map_idx < sig->id_map_size; id_map_idx++) {
        /* check if instance is active */
        if (   (ids = sig->id_map[id_map_idx].ids)
            && (in = _get_inst_by_id_map_idx(sig, id_map_idx))
            && (in->status & MPR_STATUS_ACTIVE)) {
            uint16_t status = 0;
            trace("setting value of instance idx %d\n", in->idx)
            if (!(in->status & MPR_STATUS_HAS_VALUE)) {
                status = MPR_STATUS_NEW_VALUE;
                mpr_value_incr_idx(sig->value, in->idx, time);
            }
            else {
                mpr_value_cpy_next(sig->value, in->idx, time);
            }
            /* we can't use mpr_value_set() here since some vector elements may be missing */
            for (i = offset; i < offset + sig->len; i++) {
                if (types[i] == MPR_NULL)
                    continue;
                if (mpr_value_set_element(sig->value, in->idx, i, argv[i]))
                    status = MPR_STATUS_NEW_VALUE;
            }
            if (mpr_value_get_has_value(sig->value, in->idx)) {
                status |= (MPR_STATUS_UPDATE_REM | MPR_STATUS_HAS_VALUE);
                in->status |= status;
                sig->obj.status |= in->status;
                mpr_bitflags_unset(sig->updated_inst, in->idx);
                mpr_sig_call_handler(sig, status, ids->local, in->idx);
                /* Pass this update downstream if signal is an input and was not updated in handler. */
                if (!(sig->dir & MPR_DIR_OUT) && !mpr_bitflags_get(sig->updated_inst, in->idx)) {
                    process_maps(sig, id_map_idx);
                    /* TODO: ensure update is propagated within this poll cycle */
                }
            }
        }
        if (global_id)
            break;
    }
done:
    offset += val_len;
    if (offset < argc)
        goto again;
    return 0;
}

/* Add a signal to a parent object. */
mpr_sig mpr_sig_new(mpr_obj parent, mpr_dir dir, const char *name, int len,
                    mpr_type type, const char *unit, const void *min,
                    const void *max, int *num_inst, mpr_sig_handler *h,
                    int events)
{
    mpr_graph g;
    mpr_local_sig lsig;

    /* For now we only allow adding signals to devices. */
    RETURN_ARG_UNLESS(parent && parent->type != MPR_MAP && parent->is_local, 0);
    RETURN_ARG_UNLESS(name && !check_sig_length(len) && mpr_type_get_is_num(type), 0);
    TRACE_RETURN_UNLESS(name[strlen(name)-1] != '/', 0,
                        "trailing slash detected in signal name.\n");
    TRACE_RETURN_UNLESS(dir == MPR_DIR_IN || dir == MPR_DIR_OUT, 0,
                        "signal direction must be either input or output.\n")

    if ((lsig = (mpr_local_sig) mpr_obj_get_child_by_name(parent, name)))
        return (mpr_sig) lsig;

    g = mpr_obj_get_graph(parent);

    lsig = (mpr_local_sig) mpr_graph_add_obj(g, name, MPR_SIG, 1);
    mpr_obj_build_tree_temp(parent, (mpr_obj)lsig);
    lsig->obj.id = mpr_dev_generate_unique_id(parent->root);
    lsig->handler = (void*)h;
    lsig->event_flags = events;
    mpr_sig_init((mpr_sig)lsig, parent->root, dir, len, type, unit, min, max, num_inst);
    mpr_local_dev_add_sig((mpr_local_dev) parent->root, lsig, dir);
    return (mpr_sig)lsig;
}

void mpr_local_sig_add_to_net(mpr_local_sig sig, mpr_net net)
{
    mpr_net_add_dev_server_method(net, sig->dev, sig->path, mpr_sig_osc_handler, sig);
}

void mpr_sig_init(mpr_sig sig, mpr_dev dev, mpr_dir dir, int len, mpr_type type,
                  const char *unit, const void *min, const void *max, int *num_inst)
{
    int mod = sig->obj.is_local ? MOD_ANY : MOD_NONE;
    mpr_tbl tbl;

    sig->dev = dev;

    sig->path = mpr_obj_get_full_name((mpr_obj)sig, 1);

    sig->len = len;
    sig->type = type;
    sig->dir = dir ? dir : MPR_DIR_OUT;
    sig->unit = unit ? strdup(unit) : strdup("unknown");
    sig->obj.ephemeral = 0;
    sig->steal_mode = MPR_STEAL_NONE;

    // TODO: move to obj
    sig->obj.type = MPR_SIG;
    sig->obj.props.synced = mpr_tbl_new();

    if (sig->obj.is_local) {
        mpr_local_sig lsig = (mpr_local_sig)sig;
        sig->obj.num_inst = 0;
        lsig->updated_inst = 0;
        if (num_inst) {
            mpr_sig_reserve_inst((mpr_sig)lsig, *num_inst, 0, 0);
            /* default to ephemeral */
            lsig->obj.ephemeral = 1;
        }
        else {
            mpr_sig_reserve_inst((mpr_sig)lsig, 1, 0, 0);
            lsig->obj.use_inst = 0;
        }

        /* Reserve one instance id map */
        lsig->id_map_size = 1;
        lsig->id_map = calloc(1, sizeof(struct _mpr_obj_id_pair));
    }
    else {
        sig->obj.num_inst = 1;
        sig->obj.use_inst = 0;
        sig->obj.props.staged = mpr_tbl_new();
        sig->obj.status = MPR_STATUS_NEW;
    }

    tbl = sig->obj.props.synced;

    /* these properties need to be added in alphabetical order */
#define link(PROP, TYPE, DATA, FLAGS) \
    mpr_tbl_link_value(tbl, MPR_PROP_##PROP, 1, TYPE, DATA, FLAGS | PROP_SET);
    link(DATA,         MPR_PTR,   &sig->obj.data,     MOD_LOCAL | INDIRECT | LOCAL_ACCESS);
    link(DEV,          MPR_DEV,   &sig->dev,          MOD_NONE | INDIRECT | LOCAL_ACCESS);
    link(DIR,          MPR_INT32, &sig->dir,          MOD_ANY);
    link(EPHEM,        MPR_BOOL,  &sig->obj.ephemeral,mod);
    link(ID,           MPR_INT64, &sig->obj.id,       MOD_NONE);
    link(LEN,          MPR_INT32, &sig->len,          MOD_NONE);
    link(NAME,         MPR_STR,   &sig->obj.name, 	  MOD_NONE | INDIRECT);
    link(NUM_INST,     MPR_INT32, &sig->obj.num_inst, MOD_NONE);
    link(NUM_MAPS_IN,  MPR_INT32, &sig->num_maps_in,  MOD_NONE);
    link(NUM_MAPS_OUT, MPR_INT32, &sig->num_maps_out, MOD_NONE);
    link(STATUS,       MPR_INT32, &sig->obj.status,   MOD_NONE | LOCAL_ACCESS);
    link(STEAL_MODE,   MPR_INT32, &sig->steal_mode,   MOD_ANY);
    link(TYPE,         MPR_TYPE,  &sig->type,         MOD_NONE);
    link(UNIT,         MPR_STR,   &sig->unit,         mod | INDIRECT);
    link(USE_INST,     MPR_BOOL,  &sig->obj.use_inst, MOD_NONE);
    link(VERSION,      MPR_INT32, &sig->obj.version,  MOD_NONE);
#undef link

    if (min)
        mpr_tbl_add_record(tbl, MPR_PROP_MIN, NULL, len, type, min, MOD_LOCAL);
    if (max)
        mpr_tbl_add_record(tbl, MPR_PROP_MAX, NULL, len, type, max, MOD_LOCAL);

    mpr_tbl_add_record(tbl, MPR_PROP_IS_LOCAL, NULL, 1, MPR_BOOL, &sig->obj.is_local,
                       LOCAL_ACCESS | MOD_NONE);
}

void mpr_sig_free(mpr_sig sig)
{
    int i;
    mpr_local_dev ldev;
    mpr_net net;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->obj.is_local);
    ldev = (mpr_local_dev)sig->dev;
    net = mpr_graph_get_net(sig->obj.graph);

    /* release associated OSC methods */
    mpr_net_remove_dev_server_method(net, ldev, lsig->path);
    net = mpr_graph_get_net(sig->obj.graph);

    /* release active instances */
    for (i = 0; i < lsig->id_map_size; i++) {
        if (lsig->id_map[i].inst)
            mpr_sig_release_inst_internal(lsig, i);
    }

    if (mpr_dev_get_is_registered((mpr_dev)ldev)) {
        /* Notify subscribers */
        int dir = (sig->dir == MPR_DIR_IN) ? MPR_SIG_IN : MPR_SIG_OUT;
        const char *sig_name;
        NEW_LO_MSG(msg, return);
        RETURN_UNLESS(sig_name = mpr_obj_get_full_name((mpr_obj)lsig, 1));
        mpr_net_use_subscribers(net, ldev, dir);
        lo_message_add_string(msg, sig_name);
        mpr_net_add_msg(mpr_graph_get_net(lsig->obj.graph), 0, MSG_SIG_REM, msg);
        free((char*)sig_name);
    }

    sig->obj.status |= MPR_STATUS_REMOVED;
}

void mpr_sig_free_internal(mpr_sig sig)
{
    int i;
    RETURN_UNLESS(sig);
    mpr_dev_remove_sig(sig->dev, sig);
    if (sig->obj.is_local) {
        mpr_local_sig lsig = (mpr_local_sig)sig;
        free(lsig->id_map);
        for (i = 0; i < lsig->obj.num_inst; i++) {
            free(lsig->inst[i]);
        }
        free(lsig->inst);
        mpr_bitflags_free(lsig->updated_inst);
        mpr_value_free(lsig->value);

        FUNC_IF(free, lsig->slots_in);
        FUNC_IF(free, lsig->slots_out);
    }

    FUNC_IF(free, (char*)sig->path);
    FUNC_IF(free, sig->unit);
    mpr_obj_free(&sig->obj);
}

/* TODO: consider using inst status to trigger callbacks here (if registered)
 * - would need to reset each ephemeral status bitflag after callback
 * - documentation should clarify that ephemeral callbacks will be reset by `mpr_obj_get_status()`
 *   _or_ callback
 * - cache inst status in this function before calling callbacks in case user-code calls
 *   `mpr_obj_get_status()`
 * - bonus: would trivially support registering a callback for MPR_STATUS_NEW_VALUE
 */
void mpr_sig_call_handler(mpr_local_sig lsig, int evt, mpr_id id, unsigned int inst_idx)
{
    mpr_sig_handler *h;
    void *value = NULL;
    mpr_time time;

    /* abort if signal is already being processed - might be a local loop */
    if (lsig->locked) {
        trace_dev(lsig->dev, "Mapping loop detected on signal %s! (2)\n", lsig->obj.name);
        return;
    }

    value = mpr_value_get_value(lsig->value, inst_idx, 0);

    /* Non-ephemeral signals cannot have a null value */
    RETURN_UNLESS(value || lsig->obj.ephemeral);

    evt &= lsig->event_flags;
    RETURN_UNLESS(evt && (h = (mpr_sig_handler*)lsig->handler));
    time = mpr_value_get_time(lsig->value, inst_idx, 0);

    h((mpr_sig)lsig, evt, lsig->obj.use_inst ? id : 0, value ? lsig->len : 0, lsig->type, value, time);
}

/**** Instances ****/

/* If the `id` argument is NULL, we have an added requirement to avoid local ids that are already
 * in use in the device id_pair (i.e. that have a local refcount > 0 */
static int activate_instance(mpr_local_sig lsig, mpr_id *local_id, mpr_id *global_id)
{
    int i;
    mpr_obj_inst in;
    mpr_id_pair ids = 0, remote_ids = 0;

#ifdef DEBUG
    printf("-- activating instance with ids [");
    if (local_id)
        printf("%"PR_MPR_ID", ", *local_id);
    else
        printf("NULL, ");
    if (global_id)
        printf("%"PR_MPR_ID"]\n", *global_id);
    else
        printf("NULL]\n");
#endif

    if (local_id) {
        /* check if the device already has an id_pair for this local id */
        ids = mpr_dev_get_ids_local(lsig->dev, *local_id);
        /* try to find existing instance with this id */
        if ((in = _find_inst_by_id(lsig, *local_id))) {
            trace("found existing match...\n");
            goto done;
        }
    }
    else if (global_id) {
        /* check if the device already has an id_pair for this global id */
        ids = mpr_dev_get_ids_global(lsig->dev, *global_id);
        if (ids) {
            trace("found existing id_pair for global id\n");
            remote_ids = mpr_dev_get_ids_local(lsig->dev, *global_id);
            if ((in = _find_inst_by_id(lsig, ids->local))) {
                trace("found existing match...\n");
                goto done;
            }
        }
    }

    /* First try to find an instance without an id_pair */
    /* 'Released' non-ephemeral instances will still have an id_pair but with refcount.global==0 */
    trace("  checking released instances...\n");
    for (i = 0; i < lsig->id_map_size; i++) {
        if (!lsig->id_map[i].ids && (in = lsig->id_map[i].inst)) {
            if (local_id) {
                if (in->id != *local_id)
                    continue;
            }
            else {
                ids = mpr_dev_get_ids_local(lsig->dev, in->id);
                if (ids && (lsig->obj.ephemeral || ids->refcount.global > 0))
                    continue;
            }
            trace("    found released instance at id_map[%d]\n", i);
            goto done;
        }
    }

    trace("  checking inactive instances...\n");
    /* Next we will try to find an inactive instance */
    for (i = 0; i < lsig->obj.num_inst; i++) {
        in = lsig->inst[i];
        if (   (!lsig->obj.ephemeral || !(in->status & MPR_STATUS_ACTIVE))
            && (local_id || !mpr_dev_get_ids_local(lsig->dev, in->id))) {
            trace("    found inactive instance at inst[%d] idx %d\n", i, in->idx);
            goto done;
        }
    }

    /* Otherwise if the signal is not ephemeral we will choose instances without an upstream src */
    RETURN_ARG_UNLESS(lsig->dir == MPR_DIR_IN && !lsig->obj.ephemeral, -1);

    trace("  checking instances with no upstream...\n");
    for (i = 0; i < lsig->id_map_size; i++) {
        ids = lsig->id_map[i].ids;
        if (ids && (ids->refcount.global > 0)) {
            continue;
        }
        if ((in = lsig->id_map[i].inst)) {
            trace("  found instance at id_map[%d]\n", i);
            if (ids && global_id) {
                /* set up indirect id_pair to refer to old id_pair */
                trace("  setting up id_pair indirection: %"PR_MPR_ID" -> %"PR_MPR_ID" : %"PR_MPR_ID"\n",
                      *global_id, ids->global, ids->local);
                mpr_id_pair indirect = mpr_dev_add_ids(lsig->dev, ids->global, *global_id, 1);

                /* increment global id refcounts for both id_pairs */
                mpr_ids_incref_global(indirect);
                mpr_ids_incref_global(ids);

                /* return id_pair index for local id_pair */
                return i;
            }
            goto done;
        }
    }

    {
        mpr_id last = 0;
        while ((ids = mpr_dev_get_ids_global_free(lsig->dev, last))) {
            trace("  found freed id_pair %"PR_MPR_ID" (%d) : %"PR_MPR_ID" (%d)\n", ids->local,
                  ids->refcount.local, ids->global, ids->refcount.global);
            if ((in = _find_inst_by_id(lsig, ids->local))) {
                /* set up indirect id_pair to refer to old id_pair */
                trace("  setting up id_pair indirection: %"PR_MPR_ID" -> %"PR_MPR_ID" : %"PR_MPR_ID"\n",
                      *global_id, ids->global, ids->local);
                mpr_id_pair indirect = mpr_dev_add_ids(lsig->dev, ids->global, *global_id, 1);

                /* increment global id refcounts for new id_pair */
                mpr_ids_incref_global(indirect);

                /* code below will add the id_pair to this signal and increment local and global
                 * refcounts for the old id_pair */
                goto done;
            }
            last = ids->global;
        }
    }

    return -1;
done:
    if (local_id) {
        in->id = *local_id;
        /* TODO: we don't really need to use qsort here since the list is already sorted
         * just need to insert new inst at correct position */
        /* also we're calling qsort in return */
        qsort(lsig->inst, lsig->obj.num_inst, sizeof(mpr_obj_inst), _compare_inst_ids);
    }
    if (!ids) {
        /* Claim id map locally */
        ids = mpr_dev_add_ids(lsig->dev, in->id,
                              global_id ? *global_id : mpr_dev_generate_unique_id((mpr_dev)lsig->dev), 0);
    }
    else
        mpr_ids_incref_local(ids);
    if (global_id)
        mpr_ids_incref_global(ids);
    if (remote_ids)
        mpr_ids_incref_global(remote_ids);
    /* store pointer to device map in a new signal map */
    return initialize_instance(lsig, in, ids, 1);
}

// TODO: in the case of non-ephemeral instances we could still steal oldest proxy id_pair
// currently this is trivial since linked list is not sorted
static int _oldest_inst(mpr_local_sig lsig)
{
    int i, oldest;
    mpr_obj_inst in;
    for (i = 0; i < lsig->id_map_size; i++) {
        if (lsig->id_map[i].inst)
            break;
    }
    if (i == lsig->id_map_size) {
        /* no active instances to steal! */
        return -1;
    }
    oldest = i;
    for (i = oldest+1; i < lsig->id_map_size; i++) {
        if (!(in = _get_inst_by_id_map_idx(lsig, i)))
            continue;
        if ((in->created.sec < lsig->id_map[oldest].inst->created.sec) ||
            (in->created.sec == lsig->id_map[oldest].inst->created.sec &&
             in->created.frac < lsig->id_map[oldest].inst->created.frac))
            oldest = i;
    }
    return oldest;
}

mpr_id mpr_sig_get_oldest_inst_id(mpr_sig sig)
{
    int idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);
    /* for non-ephemeral signals all instances are the same age */
    RETURN_ARG_UNLESS(sig->obj.ephemeral, lsig->id_map[0].ids->local);
    idx = _oldest_inst((mpr_local_sig)sig);
    return (idx >= 0) ? lsig->id_map[idx].ids->local : 0;
}

int _newest_inst(mpr_local_sig lsig)
{
    int i, newest;
    mpr_obj_inst in;
    for (i = 0; i < lsig->id_map_size; i++) {
        if (lsig->id_map[i].inst)
            break;
    }
    if (i == lsig->id_map_size) {
        /* no active instances to steal! */
        return -1;
    }
    newest = i;
    for (i = newest + 1; i < lsig->id_map_size; i++) {
        if (!(in = _get_inst_by_id_map_idx(lsig, i)))
            continue;
        if ((in->created.sec > lsig->id_map[newest].inst->created.sec) ||
            (in->created.sec == lsig->id_map[newest].inst->created.sec &&
             in->created.frac > lsig->id_map[newest].inst->created.frac))
            newest = i;
    }
    return newest;
}

mpr_id mpr_sig_get_newest_inst_id(mpr_sig sig)
{
    int idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);
    /* for non-ephemeral signals all instances are the same age */
    RETURN_ARG_UNLESS(sig->obj.ephemeral, lsig->id_map[0].ids->local);
    idx = _newest_inst((mpr_local_sig)sig);
    return (idx >= 0) ? lsig->id_map[idx].ids->local : 0;
}

/*! Fetch a reserved (preallocated) signal instance using local or global instance ids,
 *  activating it if necessary.
 *  \param sig          The signal owning the desired instance.
 *  \param local_id     Local id of this instance, or NULL if not known.
 *  \param global_id    Globally unique id of this instance, or NULL if not known.
 *  \param flags        Bitflags indicating if search should include released instances.
 *  \param time         Time associated with this action.
 *  \param activate     Set to 1 to activate a reserved instance if necessary.
 *  \param call_hander  Set to 1 to call handler if a new instance is activated
 *  \return             The index of the retrieved instance id map, or -1 if no free instances were
 *                      available and allocation of a new instance was unsuccessful according to
 *                      the selected allocation strategy. */
static int mpr_sig_get_inst(mpr_local_sig lsig, mpr_id *local_id, mpr_id *global_id, mpr_time time, int flags, uint8_t activate, uint8_t call_handler)
{
    mpr_sig_handler *h = (mpr_sig_handler*)lsig->handler;
    mpr_obj_inst in;
    int i;

    for (i = 0; i < lsig->id_map_size; i++) {
        mpr_obj_id_pair sip = &lsig->id_map[i];
        if (!sip->ids)
            continue;
        if (local_id && (!sip->ids->refcount.local || sip->ids->local != *local_id))
            continue;
        if (global_id && (!sip->ids->refcount.global || sip->ids->global != *global_id))
            continue;
        return (sip->status & ~flags) ? -1 : i;
    }
    RETURN_ARG_UNLESS(activate, -1);

    /* No instance with that id exists - need to try to activate instance and
     * create new id map if necessary. */
    i = activate_instance(lsig, local_id, global_id);
    if (i >= 0 && lsig->id_map[i].inst) {
        if (call_handler && h && lsig->obj.ephemeral && (lsig->event_flags & MPR_STATUS_NEW))
            h((mpr_sig)lsig, MPR_STATUS_NEW, lsig->id_map[i].inst->id, 0, lsig->type, NULL, time);
        return i;
    }

    /* try releasing instance in use */
    if (h && lsig->event_flags & MPR_STATUS_OVERFLOW) {
        /* call instance event handler */
        h((mpr_sig)lsig, MPR_STATUS_OVERFLOW, 0, 0, lsig->type, NULL, time);
    }
    else if (lsig->steal_mode == MPR_STEAL_OLDEST) {
        i = _oldest_inst(lsig);
        if (i < 0)
            return -1;
        if (h)
            h((mpr_sig)lsig, MPR_STATUS_REL_UPSTRM & lsig->event_flags ? MPR_STATUS_REL_UPSTRM : MPR_STATUS_UPDATE_REM,
              lsig->id_map[i].ids->local, 0, lsig->type, 0, time);
    }
    else if (lsig->steal_mode == MPR_STEAL_NEWEST) {
        i = _newest_inst(lsig);
        if (i < 0)
            return -1;
        if (h)
            h((mpr_sig)lsig, MPR_STATUS_REL_UPSTRM & lsig->event_flags ? MPR_STATUS_REL_UPSTRM : MPR_STATUS_UPDATE_REM,
              lsig->id_map[i].ids->local, 0, lsig->type, 0, time);
    }
    else {
        lsig->obj.status |= MPR_STATUS_OVERFLOW;
        return -1;
    }

    /* try again */
    i = activate_instance(lsig, local_id, global_id);
    if (i >= 0) {
        in = lsig->id_map[i].inst;
        RETURN_ARG_UNLESS(in, -1);
        if (call_handler && h && lsig->obj.ephemeral && (lsig->event_flags & MPR_STATUS_NEW))
            h((mpr_sig)lsig, MPR_STATUS_NEW, in->id, 0, lsig->type, NULL, time);
    }
    return i;
}

/* finding id_pairs here will be a bit inefficient for now */
/* TODO: optimize storage and lookup */
static int _get_id_map_idx_by_inst_idx(mpr_local_sig sig, unsigned int inst_idx)
{
    int i;
    for (i = 0; i < sig->id_map_size; i++) {
        mpr_obj_id_pair obj_id_pair = &sig->id_map[i];
        if (obj_id_pair->inst && obj_id_pair->inst->idx == inst_idx) {
            return i;
        }
    }
    return -1;
}

mpr_id_pair mpr_local_sig_get_ids_by_inst_idx(mpr_local_sig sig, unsigned int inst_idx)
{
    int id_map_idx = _get_id_map_idx_by_inst_idx(sig, inst_idx);
    return (id_map_idx >= 0) ? sig->id_map[id_map_idx].ids : 0;
}

static int _reserve_inst(mpr_local_sig lsig, mpr_id *id, void *data)
{
    int i, cont;
    mpr_obj_inst in;
    RETURN_ARG_UNLESS(lsig->obj.num_inst < MAX_INST, -1);

    /* check if instance with this id already exists! If so, stop here. */
    if (id && _find_inst_by_id(lsig, *id))
        return -1;

    /* reallocate array of instances */
    lsig->inst = realloc(lsig->inst, sizeof(mpr_obj_inst) * (lsig->obj.num_inst + 1));
    lsig->inst[lsig->obj.num_inst] = (mpr_obj_inst) calloc(1, sizeof(struct _mpr_obj_inst));
    in = lsig->inst[lsig->obj.num_inst];
    in->status = MPR_STATUS_STAGED;

    if (id)
        in->id = *id;
    else {
        /* find lowest unused id */
        mpr_id lowest_id = 0;
        cont = 1;
        while (cont) {
            cont = 0;
            for (i = 0; i < lsig->obj.num_inst; i++) {
                if (lsig->inst[i]->id == lowest_id) {
                    cont = 1;
                    break;
                }
            }
            lowest_id += cont;
        }
        in->id = lowest_id;
    }
    in->idx = lsig->obj.num_inst;
    in->data = data;

    ++lsig->obj.num_inst;
    /* TODO: move this qsort out to call with multiple ids */
    qsort(lsig->inst, lsig->obj.num_inst, sizeof(mpr_obj_inst), _compare_inst_ids);
    return lsig->obj.num_inst - 1;
}

static void realloc_maps(mpr_local_sig sig, int size)
{
    int i;
    for (i = 0; i < sig->num_maps_out; i++) {
        mpr_local_slot slot = sig->slots_out[i];
        mpr_local_map map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);
        mpr_map_alloc_values(map, 0);
    }
    for (i = 0; i < sig->num_maps_in; i++) {
        mpr_local_slot slot = sig->slots_in[i];
        mpr_local_map map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);
        mpr_map_alloc_values(map, 0);
    }
}

int mpr_sig_reserve_inst(mpr_sig sig, int num, mpr_id *ids, void **data)
{
    int i = 0, count = 0, highest = -1, result, old_num = sig->obj.num_inst;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && num, 0);

    if (!sig->obj.use_inst && lsig->obj.num_inst == 1 && !lsig->inst[0]->id && !lsig->inst[0]->data) {
        /* we will overwrite the default instance first */
        if (ids)
            lsig->inst[0]->id = ids[0];
        if (data)
            lsig->inst[0]->data = data[0];
        ++i;
        ++count;
    }
    sig->obj.use_inst = 1;
    for (; i < num; i++) {
        result = _reserve_inst(lsig, ids ? &ids[i] : 0, data ? data[i] : 0);
        if (result == -1)
            continue;
        highest = result;
        ++count;
    }
    if (highest != -1)
        realloc_maps(lsig, highest + 1);

    if (!lsig->value)
        lsig->value = mpr_value_new(lsig->len, lsig->type, 1, lsig->obj.num_inst);
    else
        mpr_value_realloc(lsig->value, lsig->len, lsig->type, 1, lsig->obj.num_inst, 0);

    mpr_obj_incr_version((mpr_obj)lsig);

    if (old_num > 0 && (lsig->obj.num_inst / 8) == (old_num / 8))
        return count;

    /* reallocate instance update bitflags */
    if (!lsig->updated_inst)
        lsig->updated_inst = mpr_bitflags_new(lsig->obj.num_inst);
    else {
        lsig->updated_inst = mpr_bitflags_realloc(lsig->updated_inst, lsig->obj.num_inst);
    }
    return count;
}

static void mpr_local_sig_set_updated(mpr_local_sig sig, int inst_idx)
{
    mpr_bitflags_set(sig->updated_inst, inst_idx);
    mpr_local_dev_set_sending(sig->dev);
    sig->updated = 1;
}

static void _mpr_remote_sig_set_value(mpr_sig sig, int len, mpr_type type, const void *val)
{
    mpr_dev dev;
    int port, i;
    const char *host;
    char port_str[10];
    lo_message msg = NULL;
    lo_address addr = NULL;
    void *coerced = 0;

    /* find destination IP and port */
    dev = sig->dev;
    host = mpr_obj_get_prop_as_str((mpr_obj)dev, MPR_PROP_HOST, NULL);
    port = mpr_obj_get_prop_as_int32((mpr_obj)dev, MPR_PROP_PORT, NULL);
    RETURN_UNLESS(host && port);

    if (!(msg = lo_message_new()))
        return;

    if (type != sig->type || len < sig->len) {
        coerced = calloc(1, mpr_type_get_size(sig->type) * sig->len);
        mpr_set_coerced(len, type, val, sig->len, sig->type, (void*)coerced);
        val = coerced;
    }
    switch (sig->type) {
        case MPR_INT32:
            for (i = 0; i < sig->len; i++)
                lo_message_add_int32(msg, ((int*)val)[i]);
            break;
        case MPR_FLT:
            for (i = 0; i < sig->len; i++)
                lo_message_add_float(msg, ((float*)val)[i]);
            break;
        case MPR_DBL:
            for (i = 0; i < sig->len; i++)
                lo_message_add_double(msg, ((double*)val)[i]);
            break;
    }
    FUNC_IF(free, coerced);

    snprintf(port_str, 10, "%d", port);
    if (!(addr = lo_address_new(host, port_str)))
        goto done;
    lo_send_message(addr, sig->path, msg);

done:
    FUNC_IF(lo_message_free, msg);
    FUNC_IF(lo_address_free, addr);
}

void mpr_sig_set_value(mpr_sig sig, mpr_id id, int len, mpr_type type, const void *val)
{
    mpr_time time;
    int id_map_idx = 0, status = MPR_STATUS_HAS_VALUE | MPR_STATUS_UPDATE_LOC;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    mpr_obj_inst in;
    RETURN_UNLESS(sig);
    if (!sig->obj.is_local) {
        _mpr_remote_sig_set_value(sig, len, type, val);
        return;
    }
    if (!len || !val) {
        mpr_sig_release_inst(sig, id);
        return;
    }
    if (!mpr_type_get_is_num(type)) {
#ifdef DEBUG
        trace("called update on signal '%s' with non-number type '%c'\n", lsig->obj.name, type);
#endif
        return;
    }
    if (type != MPR_INT32) {
        /* check for NaN */
        int i;
        if (type == MPR_FLT) {
            for (i = 0; i < len; i++)
                RETURN_UNLESS(((float*)val)[i] == ((float*)val)[i]);
        }
        else if (type == MPR_DBL) {
            for (i = 0; i < len; i++)
                RETURN_UNLESS(((double*)val)[i] == ((double*)val)[i]);
        }
    }
    time = mpr_dev_get_time(sig->dev);
    id_map_idx = mpr_sig_get_inst(lsig, &id, NULL, time, 0, 1, 0);
    RETURN_UNLESS(id_map_idx >= 0);
    in = _get_inst_by_id_map_idx(lsig, id_map_idx);

    /* update value */
    if (type != lsig->type || len < lsig->len) {
        if (!mpr_value_set_next_coerced(lsig->value, in->idx, lsig->len, type, val, time))
            status |= MPR_STATUS_NEW_VALUE;
    }
    else if (mpr_value_set_next(lsig->value, in->idx, val, time)) {
        in->status |= MPR_STATUS_NEW_VALUE;
    }
    in->status |= status;
    sig->obj.status |= status;

    /* mark instance as updated */
    mpr_local_sig_set_updated(lsig, in->idx);

    process_maps(lsig, id_map_idx);
}

void mpr_sig_release_inst(mpr_sig sig, mpr_id id)
{
    mpr_obj_inst in;
    RETURN_UNLESS(sig && sig->obj.is_local && sig->obj.ephemeral);
    in = _find_inst_by_id((mpr_local_sig)sig, id);
    if (in) {
        int id_map_idx = _get_id_map_idx_by_inst_idx((mpr_local_sig)sig, in->idx);
        if (id_map_idx >= 0)
            mpr_sig_release_inst_internal((mpr_local_sig)sig, id_map_idx);
    }
}

static void mpr_sig_release_inst_internal(mpr_local_sig lsig, int id_map_idx)
{
    mpr_time time;
    mpr_obj_id_pair smap = &lsig->id_map[id_map_idx];
    RETURN_UNLESS(smap->inst);

    mpr_dev_get_time((mpr_dev)lsig->dev);

    /* mark instance as updated */
    mpr_local_sig_set_updated(lsig, smap->inst->idx);

    time = mpr_dev_get_time((mpr_dev)lsig->dev);
    mpr_value_reset_inst(lsig->value, smap->inst->idx, time);
    process_maps(lsig, id_map_idx);
    if (smap->ids && mpr_dev_ids_decref_local((mpr_local_dev)lsig->dev, smap->ids)) {
        smap->ids = 0;
    }
    else if ((lsig->dir & MPR_DIR_OUT) || smap->status & RELEASED_REMOTELY) {
        /* TODO: consider multiple upstream source instances? */
        smap->ids = 0;
    }
    else {
        /* mark map as locally-released but do not remove it */
        smap->status |= RELEASED_LOCALLY;
    }

    /* Put instance back in reserve list */
    smap->inst->status = MPR_STATUS_STAGED;
    smap->inst = 0;
}

/* this function is called for local destination signals when a map is released or when a map
 * scope is removed */
void mpr_local_sig_release_inst_by_origin(mpr_local_sig lsig, mpr_dev origin)
{
    int i;
    mpr_id id;
    mpr_time time;

    RETURN_UNLESS(lsig->obj.ephemeral);

    mpr_time_set(&time, MPR_NOW);
    id = mpr_obj_get_id((mpr_obj)origin);
    for (i = 0; i < lsig->id_map_size; i++) {
        mpr_obj_inst in = lsig->id_map[i].inst;
        mpr_id_pair ids = lsig->id_map[i].ids;
        if (   in     && in->status & MPR_STATUS_ACTIVE
            && ids && (ids->global & 0xFFFFFFFF00000000) == id) {
            /* decrement the id_pair's global refcount */
            mpr_dev_ids_decref_global(lsig->dev, ids);

            mpr_sig_call_handler(lsig, MPR_STATUS_REL_UPSTRM, in->id, in->idx);
        }
    }
}

void mpr_sig_remove_inst(mpr_sig sig, mpr_id id)
{
    int i, remove_idx;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->obj.is_local && sig->obj.use_inst);
    for (i = 0; i < lsig->obj.num_inst; i++) {
        if (lsig->inst[i]->id == id)
            break;
    }
    RETURN_UNLESS(i < lsig->obj.num_inst);

    if (lsig->inst[i]->status & MPR_STATUS_ACTIVE) {
       /* First release instance */
       mpr_sig_release_inst_internal(lsig, i);
    }

    remove_idx = lsig->inst[i]->idx;

    /* Free value and timetag memory held by instance */
    mpr_value_remove_inst(lsig->value, i);
    free(lsig->inst[i]);

    for (++i; i < lsig->obj.num_inst; i++)
    lsig->inst[i-1] = lsig->inst[i];
    --lsig->obj.num_inst;
    lsig->inst = realloc(lsig->inst, sizeof(mpr_obj_inst) * lsig->obj.num_inst);

    /* Remove instance memory held by map slots */
    for (i = 0; i < lsig->num_maps_out; i++)
        mpr_slot_remove_inst(lsig->slots_out[i], remove_idx);
    for (i = 0; i < lsig->num_maps_in; i++)
        mpr_slot_remove_inst(lsig->slots_in[i], remove_idx);

    /* Renumber instance indexes */
    for (i = 0; i < lsig->obj.num_inst; i++) {
        if (lsig->inst[i]->idx > remove_idx)
            --lsig->inst[i]->idx;
    }
    mpr_obj_incr_version((mpr_obj)sig);
}

const void *mpr_sig_get_value(mpr_sig sig, mpr_id id, mpr_time *time)
{
    mpr_local_sig lsig = (mpr_local_sig)sig;
    mpr_obj_inst in;
    mpr_time now;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);

    if (!lsig->obj.use_inst)
        in = _get_inst_by_id_map_idx(lsig, 0);
    else {
        int id_map_idx = mpr_sig_get_inst(lsig, &id, NULL, MPR_NOW, RELEASED_REMOTELY, 0, 0);
        RETURN_ARG_UNLESS(id_map_idx >= 0, 0);
        in = _get_inst_by_id_map_idx(lsig, id_map_idx);
    }
    RETURN_ARG_UNLESS(in, 0);
    if (time)
        mpr_time_set(time, mpr_value_get_time(lsig->value, in->idx, 0));
    RETURN_ARG_UNLESS(in->status & MPR_STATUS_HAS_VALUE, 0);
    /* clear all flags except for HAS_VALUE, STAGED/ACTIVE, and REL_UPSTRM */
    in->status &= (MPR_STATUS_HAS_VALUE | MPR_STATUS_ACTIVE | MPR_STATUS_STAGED | MPR_STATUS_REL_UPSTRM);
    mpr_time_set(&now, MPR_NOW);
    return mpr_value_get_value(lsig->value, in->idx, 0);
}

int mpr_sig_get_num_inst(mpr_sig sig, mpr_status status)
{
    int i, j;
    RETURN_ARG_UNLESS(sig, 0);
    RETURN_ARG_UNLESS(sig->obj.is_local, sig->obj.num_inst);
    if (status == MPR_STATUS_ANY)
        return sig->obj.num_inst;
    for (i = 0, j = 0; i < sig->obj.num_inst; i++) {
        if (((mpr_local_sig)sig)->inst[i]->status & status)
            ++j;
    }
    return j;
}

mpr_status mpr_sig_get_inst_id(mpr_sig sig, int idx, mpr_status status, mpr_id *instance)
{
    int i, j;
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && idx >= 0 && idx < sig->obj.num_inst,
                      MPR_STATUS_UNDEFINED);
    if (status != MPR_STATUS_ANY) {
        for (i = 0, j = -1; i < lsig->obj.num_inst; i++) {
            if (!(lsig->inst[i]->status & status))
                continue;
            if (++j == idx) {
                idx = i;
                break;
            }
        }
        if (i == lsig->obj.num_inst)
            return MPR_STATUS_UNDEFINED;
    }
    if (lsig->inst[idx]->status & status) {
        if (instance)
            *instance = lsig->inst[idx]->id;
        return lsig->inst[idx]->status;
    }
    return MPR_STATUS_UNDEFINED;
}

int mpr_sig_activate_inst(mpr_sig sig, mpr_id id)
{
    int id_map_idx;
    mpr_time time;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && sig->obj.use_inst, 0);
    time = mpr_dev_get_time(sig->dev);
    id_map_idx = mpr_sig_get_inst((mpr_local_sig)sig, &id, NULL, time, 0, 1, 0);
    return id_map_idx >= 0;
}

void mpr_sig_set_inst_data(mpr_sig sig, mpr_id id, const void *data)
{
    mpr_obj_inst in;
    RETURN_UNLESS(sig && sig->obj.is_local && sig->obj.use_inst);
    in = _find_inst_by_id((mpr_local_sig)sig, id);
    if (in)
        in->data = (void*)data;
}

void *mpr_sig_get_inst_data(mpr_sig sig, mpr_id id)
{
    mpr_obj_inst in;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local && sig->obj.use_inst, 0);
    in = _find_inst_by_id((mpr_local_sig)sig, id);
    return in ? in->data : 0;
}

int mpr_sig_get_inst_status(mpr_sig sig, mpr_id id)
{
    int status = 0;
    mpr_obj_inst in;
    RETURN_ARG_UNLESS(sig && sig->obj.is_local, 0);
    in = _find_inst_by_id((mpr_local_sig)sig, id);
    if (in) {
        if ((status = in->status)) {
            /* clear all flags except for HAS_VALUE, STAGED/ACTIVE, and REL_UPSTRM */
            in->status &= (  MPR_STATUS_HAS_VALUE | MPR_STATUS_ACTIVE
                           | MPR_STATUS_STAGED | MPR_STATUS_REL_UPSTRM);
        }
        else
            status = MPR_STATUS_STAGED;
    }
    return status;
}

/**** Queries ****/

void mpr_sig_set_cb(mpr_sig sig, mpr_sig_handler *h, int events)
{
    mpr_local_sig lsig = (mpr_local_sig)sig;
    RETURN_UNLESS(sig && sig->obj.is_local);
    lsig->handler = (void*)h;
    lsig->event_flags = events;
}

/**** Signal Properties ****/

mpr_dev mpr_sig_get_dev(mpr_sig sig)
{
    return sig ? sig->dev : NULL;
}

static int cmp_qry_sig_maps(const void *context_data, mpr_map map)
{
    mpr_sig sig = *(mpr_sig*)context_data;
    int dir = *(int*)((char*)context_data + sizeof(mpr_sig*));
    return mpr_map_get_has_sig(map, sig, dir);
}

mpr_list mpr_sig_get_maps(mpr_sig sig, mpr_dir dir)
{
    RETURN_ARG_UNLESS(sig, 0);
    return mpr_graph_new_query(sig->obj.graph, 1, MPR_MAP, (void*)cmp_qry_sig_maps, "vi", &sig, dir);
}

static int initialize_instance(mpr_local_sig lsig, mpr_obj_inst in, mpr_id_pair ids, int activate)
{
    int i;
    if (activate && !(in->status & MPR_STATUS_ACTIVE)) {
        in->status &= ~MPR_STATUS_STAGED;
        in->status |= (MPR_STATUS_NEW | MPR_STATUS_ACTIVE);
        mpr_time_set(&in->created, MPR_NOW);
    }

    /* find unused signal map */
    for (i = 0; i < lsig->id_map_size; i++) {
        if (!lsig->id_map[i].ids)
            break;
    }
    if (i == lsig->id_map_size) {
        /* need more memory */
        if (lsig->id_map_size >= MAX_INST) {
            /* Arbitrary limit to number of tracked id_pairs */
            /* TODO: add checks for this return value */
            trace("warning: reached maximum number of instances for signal %s.\n", lsig->obj.name);
            return -1;
        }
        lsig->id_map_size = lsig->id_map_size ? lsig->id_map_size * 2 : 1;
        lsig->id_map = realloc(lsig->id_map, (lsig->id_map_size * sizeof(struct _mpr_obj_id_pair)));
        memset(lsig->id_map + i, 0, ((lsig->id_map_size - i) * sizeof(struct _mpr_obj_id_pair)));
    }
    lsig->id_map[i].ids = ids;
    lsig->id_map[i].inst = in;
    lsig->id_map[i].status = 0;

    in->id = ids->local;
    /* same comment wrt qsort here */
    qsort(lsig->inst, lsig->obj.num_inst, sizeof(mpr_obj_inst), _compare_inst_ids);

    /* return id_pair index */
    return i;
}

void mpr_sig_send_state(mpr_sig sig, net_msg_t cmd)
{
    lo_message msg;
    mpr_net net;
    RETURN_UNLESS(sig);
    msg = lo_message_new();
    RETURN_UNLESS(msg);
    net = mpr_graph_get_net(sig->obj.graph);

    if (cmd == MSG_SIG_MOD) {
        char str[BUFFSIZE];
        lo_message_add_string(msg, sig->path + 1);

        /* properties */
        mpr_obj_add_props_to_msg((mpr_obj)sig, msg);

        snprintf(str, BUFFSIZE, "/%s/signal/modify", mpr_dev_get_name(sig->dev));
        mpr_net_add_msg(net, str, 0, msg);
        /* send immediately since path string is not cached */
        mpr_net_send(net);
    }
    else {
        const char *sig_name = mpr_obj_get_full_name((mpr_obj)sig, 0);
        lo_message_add_string(msg, sig_name);
        free((char*)sig_name);

        /* properties */
        mpr_obj_add_props_to_msg((mpr_obj)sig, msg);
        mpr_net_add_msg(net, 0, cmd, msg);
    }
}

/*! Update information about a signal record based on message properties. */
int mpr_sig_set_from_msg(mpr_sig sig, mpr_msg msg)
{
    mpr_msg_atom a;
    int i, updated = 0, prop;
    mpr_tbl tbl = sig->obj.props.synced;
    const mpr_type *types;
    lo_arg **vals;
    RETURN_ARG_UNLESS(msg, 0);

    for (i = 0; i < mpr_msg_get_num_atoms(msg); i++) {
        a = mpr_msg_get_atom(msg, i);
        prop = mpr_msg_atom_get_prop(a);
        if (sig->obj.is_local && (MASK_PROP_BITFLAGS(prop) != MPR_PROP_EXTRA))
            continue;
        types = mpr_msg_atom_get_types(a);
        vals = mpr_msg_atom_get_values(a);
        switch (prop) {
            case MPR_PROP_DIR: {
                int dir = 0;
                if (!mpr_type_get_is_str(types[0]))
                    break;
                if (strcmp(&(*vals)->s, "output")==0)
                    dir = MPR_DIR_OUT;
                else if (strcmp(&(*vals)->s, "input")==0)
                    dir = MPR_DIR_IN;
                else
                    break;
                updated += mpr_tbl_add_record(tbl, MPR_PROP_DIR, NULL, 1, MPR_INT32, &dir, MOD_REMOTE);
                break;
            }
            case MPR_PROP_ID:
                if (types[0] == 'h') {
                    if (sig->obj.id != (vals[0])->i64) {
                        sig->obj.id = (vals[0])->i64;
                        ++updated;
                    }
                }
                break;
            case MPR_PROP_STEAL_MODE: {
                int stl;
                if (!mpr_type_get_is_str(types[0]))
                    break;
                if (strcmp(&(*vals)->s, "none")==0)
                    stl = MPR_STEAL_NONE;
                else if (strcmp(&(*vals)->s, "oldest")==0)
                    stl = MPR_STEAL_OLDEST;
                else if (strcmp(&(*vals)->s, "newest")==0)
                    stl = MPR_STEAL_NEWEST;
                else
                    break;
                updated += mpr_tbl_add_record(tbl, MPR_PROP_STEAL_MODE, NULL, 1,
                                              MPR_INT32, &stl, MOD_REMOTE);
                break;
            }
            default:
                updated += mpr_tbl_add_record_from_msg_atom(tbl, a, MOD_REMOTE);
                break;
        }
    }
    if (updated)
        mpr_obj_incr_version((mpr_obj)sig);
    return updated;
}

void mpr_local_sig_set_dev_id(mpr_local_sig sig, mpr_id id)
{
    int i;
    for (i = 0; i < sig->id_map_size; i++) {
        mpr_id_pair ids = sig->id_map[i].ids;
        if (ids && !(ids->global >> 32))
            ids->global |= id;
    }
    sig->obj.id |= id;
}

mpr_dir mpr_sig_get_dir(mpr_sig sig)
{
    return sig->dir;
}

int mpr_sig_get_len(mpr_sig sig)
{
    return sig->len;
}

const char *mpr_sig_get_path(mpr_sig sig)
{
    return sig->path;
}

mpr_type mpr_sig_get_type(mpr_sig sig)
{
    return sig->type;
}

int mpr_sig_compare_names(mpr_sig l, mpr_sig r)
{
    int res = strcmp(mpr_dev_get_name(l->dev), mpr_dev_get_name(r->dev));
    if (0 == res)
        res = strcmp(l->path, r->path);
    return res;
}

void mpr_sig_copy_props(mpr_sig to, mpr_sig from)
{
    mpr_dev dev = to->dev;
    if (!to->obj.id) {
        to->obj.id = from->obj.id;
        to->dir = from->dir;
        to->len = from->len;
        to->type = from->type;
    }

    if (!mpr_obj_get_id((mpr_obj)dev))
        mpr_obj_set_id((mpr_obj)dev, mpr_obj_get_id((mpr_obj)from->dev));
}

void mpr_local_sig_set_inst_value(mpr_local_sig sig, const void *value, int inst_idx,
                                  mpr_id_pair ids, int eval_status, int map_manages_inst,
                                  mpr_time time)
{
    mpr_obj_inst in;
    int id_map_idx = 0, all = 0;

    if (inst_idx < 0) {
        all = 1;
        inst_idx = 0;
    }
    if (!sig->obj.use_inst)
        inst_idx = 0;

    for (; inst_idx < sig->obj.num_inst; inst_idx++) {
        id_map_idx = _get_id_map_idx_by_inst_idx(sig, inst_idx);
        if (id_map_idx == -1) {
            if (eval_status & EXPR_UPDATE)
                trace("error: couldn't find id_pair for signal instance idx %d\n", inst_idx);
            continue;
        }
        in = sig->id_map[id_map_idx].inst;
        if (all && !(in->status & MPR_STATUS_ACTIVE))
            continue;

        ids = sig->id_map[id_map_idx].ids;

        /* TODO: would it be better to release all instance first, then update, etc? */

        if (eval_status & EXPR_RELEASE_BEFORE_UPDATE) {
            /* Try to release instance, but do not call process_maps() here, since we don't
             * know if the local signal instance will actually be released. */
            in->status |= MPR_STATUS_REL_UPSTRM;
            sig->obj.status |= MPR_STATUS_REL_UPSTRM;
            mpr_sig_call_handler(sig, MPR_STATUS_REL_UPSTRM, ids ? ids->local : 0, in->idx);
        }
        if (eval_status & EXPR_UPDATE) {
            /* copy to signal value and call handler */
            if (in->status == MPR_STATUS_STAGED) {
                /* instance was released in previous handler call */
                assert(map_manages_inst);
                /* try to re-activate with a new global id */
                ids->global = mpr_dev_generate_unique_id((mpr_dev)sig->dev);
                id_map_idx = mpr_sig_get_inst(sig, NULL, &ids->global, time, RELEASED_LOCALLY, 1, 1);
                if (id_map_idx < 0) {
                    trace("error: couldn't find id_pair for signal instance idx %d\n", id_map_idx);
                    continue;
                }
                in = sig->id_map[id_map_idx].inst;
                ids = sig->id_map[id_map_idx].ids;
            }
            in->status |= (MPR_STATUS_HAS_VALUE | MPR_STATUS_UPDATE_REM);
            if (mpr_value_cmp(sig->value, in->idx, 0, value))
                in->status |= MPR_STATUS_NEW_VALUE;
            mpr_value_set_next(sig->value, in->idx, value, time);
            sig->obj.status |= in->status;

            mpr_sig_call_handler(sig, MPR_STATUS_UPDATE_REM, in->id, in->idx);

            /* Pass this update downstream if signal is an input and was not updated in handler. */
            if (!(sig->dir & MPR_DIR_OUT) && !mpr_bitflags_get(sig->updated_inst, in->idx)) {
                /* mark instance as updated */
                mpr_local_sig_set_updated(sig, in->idx);
                process_maps(sig, id_map_idx);
            }
        }

        if (eval_status & EXPR_RELEASE_AFTER_UPDATE) {
            /* Try to release instance, but do not call process_maps() here, since we don't
             * know if the local signal instance will actually be released. */
            if (in->status == MPR_STATUS_STAGED) {
                /* instance was released in previous handler call */
                continue;
            }
            in->status |= MPR_STATUS_REL_UPSTRM;
            sig->obj.status |= in->status;
            mpr_sig_call_handler(sig, MPR_STATUS_REL_UPSTRM, ids ? ids->local : 0, in->idx);
        }
        if (!all)
            break;
    }
}

/* Check if there is already a map from a local signal to any of a list of remote signals. */
int mpr_local_sig_check_outgoing(mpr_local_sig sig, int num_dst_sigs, const char **dst_sig_names)
{
    int i, j;
    for (i = 0; i < sig->num_maps_out; i++) {
        mpr_local_slot slot = sig->slots_out[i];
        mpr_local_map map;
        if (!slot || MPR_DIR_IN == mpr_slot_get_dir((mpr_slot)slot))
            continue;
        map = (mpr_local_map)mpr_slot_get_map((mpr_slot)slot);

        /* check destination */
        for (j = 0; j < num_dst_sigs; j++) {
            if (!mpr_slot_match_full_name(mpr_map_get_dst_slot((mpr_map)map), dst_sig_names[j]))
                return 1;
        }
    }
    return 0;
}

/* TODO: track array size separately to reduce reallocations */
void mpr_local_sig_add_slot(mpr_local_sig sig, mpr_local_slot slot, mpr_dir dir)
{
    int i;
    if (MPR_DIR_IN == dir) {
        for (i = 0; i < sig->num_maps_in; i++) {
            if (sig->slots_in[i] == slot)
                return;
        }
        ++sig->num_maps_in;
        sig->slots_in = realloc(sig->slots_in, sizeof(mpr_local_slot) * sig->num_maps_in);
        sig->slots_in[sig->num_maps_in - 1] = slot;
    }
    else if (MPR_DIR_OUT == dir) {
        for (i = 0; i < sig->num_maps_out; i++) {
            if (sig->slots_out[i] == slot)
                return;
        }
        ++sig->num_maps_out;
        sig->slots_out = realloc(sig->slots_out, sizeof(mpr_local_slot) * sig->num_maps_out);
        sig->slots_out[sig->num_maps_out - 1] = slot;
    }
}

/* TODO: track array size separately to reduce reallocations */
void mpr_local_sig_remove_slot(mpr_local_sig sig, mpr_local_slot slot, mpr_dir dir)
{
    int i, found = 0;
    if (MPR_DIR_IN == dir) {
        for (i = 0; i < sig->num_maps_in; i++) {
            if (found)
                sig->slots_in[i-1] = sig->slots_in[i];
            else if (sig->slots_in[i] == slot)
                found = 1;
        }
        if (found) {
            --sig->num_maps_in;
            sig->slots_in = realloc(sig->slots_in, sizeof(mpr_local_slot) * sig->num_maps_in);
        }
    }
    else if (MPR_DIR_OUT == dir) {
        for (i = 0; i < sig->num_maps_out; i++) {
            if (found)
                sig->slots_out[i-1] = sig->slots_out[i];
            else if (sig->slots_out[i] == slot)
                found = 1;
        }
        if (found) {
            --sig->num_maps_out;
            sig->slots_out = realloc(sig->slots_out, sizeof(mpr_local_slot) * sig->num_maps_out);
        }
    }
}

/* Functions below are only used by testinstance.c for printing instance indices */
unsigned int mpr_local_sig_get_id_map_size(mpr_local_sig sig)
{
    return sig->id_map_size;
}
