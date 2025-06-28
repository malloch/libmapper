#include <stdlib.h>

#include "id_map.h"
#include "util/mpr_debug.h"

typedef struct _mpr_id_map {
    struct _mpr_id_pair *active;    /*!< The list of active instance id maps. */
    struct _mpr_id_pair *reserve;   /*!< The list of reserve instance id maps. */
} mpr_id_map_t;

mpr_id_map mpr_id_map_new()
{
    mpr_id_map m = (mpr_id_map) calloc(1, sizeof(mpr_id_map_t));
    return m;
}

void mpr_id_map_free(mpr_id_map map)
{
    /* Release active id maps */
    while (map->active) {
        mpr_id_pair id_pair = map->active;
        map->active = id_pair->next;
        free(id_pair);
    }
    /* Release reserve id maps */
    while (map->reserve) {
        mpr_id_pair id_pair = map->reserve;
        map->reserve = id_pair->next;
        free(id_pair);
    }
    free(map);
}

void mpr_id_map_reserve(mpr_id_map map)
{
    mpr_id_pair id_pair = (mpr_id_pair) calloc(1, sizeof(mpr_id_pair_t));
    id_pair->next = map->reserve;
    map->reserve = id_pair;
}

int mpr_id_map_get_size(mpr_id_map map, int active)
{
    int count = 0;
    mpr_id_pair id_pair = active ? map->active : map->reserve;
    while (id_pair) {
        ++count;
        id_pair = id_pair->next;
    }
    return count;
}

#ifdef DEBUG
void mpr_id_map_print(mpr_id_map map)
{
    printf("ACTIVE ID PAIRS:\n");
    mpr_id_pair id_pair = map->active;
    while (id_pair) {
        printf("  %p: %"PR_MPR_ID" (%d) -> %"PR_MPR_ID"%s (%d)\n", id_pair, id_pair->LID,
               id_pair->LID_refcount, id_pair->GID, id_pair->indirect ? "*" : "", id_pair->GID_refcount);
        id_pair = id_pair->next;
    }
}
#endif

mpr_id_pair mpr_id_map_add(mpr_id_map map, mpr_id LID, mpr_id GID, int indirect)
{
    mpr_id_pair id_pair;
    if (!map->reserve)
        mpr_id_map_reserve(map);
    id_pair = map->reserve;
    id_pair->LID = LID;
    id_pair->GID = GID;
#ifdef DEBUG
    trace("mpr_id_map_add() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", LID, id_pair->GID);
#endif
    id_pair->LID_refcount = 1;
    id_pair->GID_refcount = 0;
    id_pair->indirect = indirect;

    /* remove from reserve list */
    map->reserve = id_pair->next;

    /* add to active list */
    id_pair->next = map->active;
    map->active = id_pair;

    return id_pair;
}

void mpr_id_map_remove(mpr_id_map map, mpr_id_pair rem)
{
    mpr_id_pair *pair = &map->active;
#ifdef DEBUG
    trace("mpr_id_map_remove() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", rem->LID, rem->GID);
#endif
    while (*pair) {
        if ((*pair) == rem) {
            *pair = (*pair)->next;
            rem->next = map->reserve;
            map->reserve = rem;
            break;
        }
        pair = &(*pair)->next;
    }
}

int mpr_id_map_LID_decref(mpr_id_map map, mpr_id_pair id_pair)
{
    --id_pair->LID_refcount;
#ifdef DEBUG
    trace("mpr_id_map_LID_decref() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", id_pair->LID, id_pair->GID);
    trace("  refcounts: {LID:%d, GID:%d}\n", id_pair->LID_refcount, id_pair->GID_refcount);
#endif
    if (id_pair->LID_refcount <= 0) {
        id_pair->LID_refcount = 0;
        if (id_pair->GID_refcount <= 0) {
            mpr_id_map_remove(map, id_pair);
            return 1;
        }
    }
    return 0;
}

int mpr_id_map_GID_decref(mpr_id_map map, mpr_id_pair id_pair)
{
    --id_pair->GID_refcount;
#ifdef DEBUG
    trace("mpr_id_map_GID_decref() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", id_pair->LID, id_pair->GID);
    trace("  refcounts: {LID:%d, GID:%d}\n", id_pair->LID_refcount, id_pair->GID_refcount);
#endif
    if (id_pair->GID_refcount <= 0) {
        id_pair->GID_refcount = 0;
        if (id_pair->LID_refcount <= id_pair->indirect) {
            mpr_id_map_remove(map, id_pair);
            return 1;
        }
    }
    return 0;
}

mpr_id_pair mpr_id_map_get_by_LID(mpr_id_map map, mpr_id LID)
{
    mpr_id_pair id_pair = map->active;
    while (id_pair) {
        if (id_pair->LID == LID && id_pair->LID_refcount > 0) {
            return id_pair;
        }
        id_pair = id_pair->next;
    }
    return 0;
}

mpr_id_pair mpr_id_map_get_by_GID(mpr_id_map map, mpr_id GID)
{
    mpr_id_pair id_pair = map->active;
    while (id_pair) {
        if (id_pair->GID == GID)
            return id_pair;
        id_pair = id_pair->next;
    }
    return 0;
}

/* TODO: rename this function */
mpr_id_pair mpr_id_map_get_GID_free(mpr_id_map map, mpr_id last_GID)
{
    int searching = last_GID != 0;
    mpr_id_pair id_pair = map->active;
    while (id_pair && searching) {
        if (id_pair->GID == last_GID)
            searching = 0;
        id_pair = id_pair->next;
    }
    while (id_pair) {
        if (id_pair->GID_refcount <= 0)
            return id_pair;
        id_pair = id_pair->next;
    }
    return 0;
}
