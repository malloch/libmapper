#include <stdlib.h>

#include "id_map.h"
#include "util/mpr_debug.h"

typedef struct _mpr_id_map {
    struct _mpr_id_pair *active;    /*!< The list of active instance id pairs. */
    struct _mpr_id_pair *reserve;   /*!< The list of reserve instance id pairs. */
} mpr_id_map_t;

mpr_id_map mpr_id_map_new()
{
    mpr_id_map map = (mpr_id_map) calloc(1, sizeof(mpr_id_map_t));
    return map;
}

void mpr_id_map_free(mpr_id_map map)
{
    /* Release active id maps */
    while (map->active) {
        mpr_id_pair ids = map->active;
        map->active = ids->next;
        free(ids);
    }
    /* Release reserve id maps */
    while (map->reserve) {
        mpr_id_pair ids = map->reserve;
        map->reserve = ids->next;
        free(ids);
    }
    free(map);
}

void mpr_id_map_reserve(mpr_id_map map)
{
    mpr_id_pair ids = (mpr_id_pair) calloc(1, sizeof(mpr_id_pair_t));
    ids->next = map->reserve;
    map->reserve = ids;
}

int mpr_id_map_get_size(mpr_id_map map, int active)
{
    int count = 0;
    mpr_id_pair ids = active ? map->active : map->reserve;
    while (ids) {
        ++count;
        ids = ids->next;
    }
    return count;
}

#ifdef DEBUG
void mpr_id_map_print(mpr_id_map map)
{
    printf("ACTIVE ID PAIRS:\n");
    mpr_id_pair ids = map->active;
    printf("  address\t\t  local\tglobal\n");
    while (ids) {
        printf("  %p: %"PR_MPR_ID" (%d) -> %"PR_MPR_ID"%s (%d)\n", ids, ids->local,
               ids->refcount.local, ids->global, ids->indirect ? "*" : "", ids->refcount.global);
        ids = ids->next;
    }
}
#endif

mpr_id_pair mpr_id_map_add(mpr_id_map map, mpr_id local, mpr_id global, int indirect)
{
    mpr_id_pair ids;
    if (!map->reserve)
        mpr_id_map_reserve(map);
    ids = map->reserve;
    ids->local = local;
    ids->global = global;
#ifdef DEBUG
    trace("mpr_id_map_add() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", local, global);
#endif
    ids->refcount.local = 1;
    ids->refcount.global = 0;
    ids->indirect = indirect;

    /* remove from reserve list */
    map->reserve = ids->next;

    /* add to active list */
    ids->next = map->active;
    map->active = ids;

    return ids;
}

void mpr_id_map_remove(mpr_id_map map, mpr_id_pair rem)
{
    mpr_id_pair *ids = &map->active;
#ifdef DEBUG
    trace("mpr_id_map_remove() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", rem->local, rem->global);
#endif
    while (*ids) {
        if ((*ids) == rem) {
            *ids = (*ids)->next;
            rem->next = map->reserve;
            map->reserve = rem;
            break;
        }
        ids = &(*ids)->next;
    }
}

int mpr_id_map_decref_local(mpr_id_map map, mpr_id_pair ids)
{
    --ids->refcount.local;
#ifdef DEBUG
    trace("mpr_id_map_decref_local() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", ids->local, ids->global);
    trace("  refcounts: {local:%d, global:%d}\n", ids->refcount.local, ids->refcount.global);
#endif
    if (ids->refcount.local <= 0) {
        ids->refcount.local = 0;
        if (ids->refcount.global <= 0) {
            mpr_id_map_remove(map, ids);
            return 1;
        }
    }
    return 0;
}

int mpr_id_map_decref_global(mpr_id_map map, mpr_id_pair ids)
{
    --ids->refcount.global;
#ifdef DEBUG
    trace("mpr_id_map_decref_global() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", ids->local, ids->global);
    trace("  refcounts: {local:%d, global:%d}\n", ids->refcount.local, ids->refcount.global);
#endif
    if (ids->refcount.global <= 0) {
        ids->refcount.global = 0;
        if (ids->refcount.local <= ids->indirect) {
            mpr_id_map_remove(map, ids);
            return 1;
        }
    }
    return 0;
}

mpr_id_pair mpr_id_map_get_first(mpr_id_map map)
{
    return map->active;
}

mpr_id_pair mpr_id_map_get_local(mpr_id_map map, mpr_id id)
{
    mpr_id_pair ids = map->active;
    while (ids) {
        if (ids->local == id && ids->refcount.local > 0) {
            return ids;
        }
        ids = ids->next;
    }
    return 0;
}

mpr_id_pair mpr_id_map_get_global(mpr_id_map map, mpr_id id)
{
    mpr_id_pair ids = map->active;
    while (ids) {
        if (ids->global == id)
            return ids;
        ids = ids->next;
    }
    return 0;
}

/* TODO: rename this function */
mpr_id_pair mpr_id_map_get_global_free(mpr_id_map map, mpr_id last_id)
{
    int searching = last_id != 0;
    mpr_id_pair ids = map->active;
    while (ids && searching) {
        if (ids->global == last_id)
            searching = 0;
        ids = ids->next;
    }
    while (ids) {
        if (ids->refcount.global <= 0)
            return ids;
        ids = ids->next;
    }
    return 0;
}
