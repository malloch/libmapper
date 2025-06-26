#include <stdlib.h>

#include "id_map.h"
#include "util/mpr_debug.h"

typedef struct _mpr_id_mapper {
    struct _mpr_id_map *active;     /*!< The list of active instance id maps. */
    struct _mpr_id_map *reserve;    /*!< The list of reserve instance id maps. */
} mpr_id_mapper_t;

mpr_id_mapper mpr_id_mapper_new()
{
    mpr_id_mapper im = (mpr_id_mapper) calloc(1, sizeof(mpr_id_mapper_t));
    return im;
}

void mpr_id_mapper_free(mpr_id_mapper im)
{
    /* Release active id maps */
    while (im->active) {
        mpr_id_map id_map = im->active;
        im->active = id_map->next;
        free(id_map);
    }
    /* Release reserve id maps */
    while (im->reserve) {
        mpr_id_map id_map = im->reserve;
        im->reserve = id_map->next;
        free(id_map);
    }
    free(im);
}

void mpr_id_mapper_reserve_id_map(mpr_id_mapper im)
{
    mpr_id_map id_map = (mpr_id_map) calloc(1, sizeof(mpr_id_map_t));
    id_map->next = im->reserve;
    im->reserve = id_map;
}

int mpr_id_mapper_get_num_id_maps(mpr_id_mapper im, int active)
{
    int count = 0;
    mpr_id_map id_map = active ? im->active : im->reserve;
    while (id_map) {
        ++count;
        id_map = id_map->next;
    }
    return count;
}

#ifdef DEBUG
void mpr_id_mapper_print(mpr_id_mapper im)
{
    printf("ACTIVE ID MAPS:\n");
    mpr_id_map id_map = im->active;
    while (id_map) {
        printf("  %p: %"PR_MPR_ID" (%d) -> %"PR_MPR_ID"%s (%d)\n", id_map, id_map->LID,
               id_map->LID_refcount, id_map->GID, id_map->indirect ? "*" : "", id_map->GID_refcount);
        id_map = id_map->next;
    }
}
#endif

mpr_id_map mpr_id_mapper_add_id_map(mpr_id_mapper im, mpr_id LID, mpr_id GID, int indirect)
{
    mpr_id_map id_map;
    if (!im->reserve)
        mpr_id_mapper_reserve_id_map(im);
    id_map = im->reserve;
    id_map->LID = LID;
    id_map->GID = GID;
#ifdef DEBUG
    trace("mpr_id_mapper_add_id_map() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", LID, id_map->GID);
#endif
    id_map->LID_refcount = 1;
    id_map->GID_refcount = 0;
    id_map->indirect = indirect;

    /* remove from reserve list */
    im->reserve = id_map->next;

    /* add to active list */
    id_map->next = im->active;
    im->active = id_map;

    return id_map;
}

void mpr_id_mapper_remove_id_map(mpr_id_mapper im, mpr_id_map rem)
{
    mpr_id_map *map = &im->active;
#ifdef DEBUG
    trace("mpr_id_mapper_remove_id_map() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", rem->LID, rem->GID);
#endif
    while (*map) {
        if ((*map) == rem) {
            *map = (*map)->next;
            rem->next = im->reserve;
            im->reserve = rem;
            break;
        }
        map = &(*map)->next;
    }
}

int mpr_id_mapper_LID_decref(mpr_id_mapper im, mpr_id_map id_map)
{
    --id_map->LID_refcount;
#ifdef DEBUG
    trace("mpr_id_mapper_LID_decref() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", id_map->LID, id_map->GID);
    trace("  refcounts: {LID:%d, GID:%d}\n", id_map->LID_refcount, id_map->GID_refcount);
#endif
    if (id_map->LID_refcount <= 0) {
        id_map->LID_refcount = 0;
        if (id_map->GID_refcount <= 0) {
            mpr_id_mapper_remove_id_map(im, id_map);
            return 1;
        }
    }
    return 0;
}

int mpr_id_mapper_GID_decref(mpr_id_mapper im, mpr_id_map id_map)
{
    --id_map->GID_refcount;
#ifdef DEBUG
    trace("mpr_id_mapper_GID_decref() %"PR_MPR_ID" -> %"PR_MPR_ID"\n", id_map->LID, id_map->GID);
    trace("  refcounts: {LID:%d, GID:%d}\n", id_map->LID_refcount, id_map->GID_refcount);
#endif
    if (id_map->GID_refcount <= 0) {
        id_map->GID_refcount = 0;
        if (id_map->LID_refcount <= id_map->indirect) {
            mpr_id_mapper_remove_id_map(im, id_map);
            return 1;
        }
    }
    return 0;
}

mpr_id_map mpr_id_mapper_get_id_map_by_LID(mpr_id_mapper im, mpr_id LID)
{
    mpr_id_map id_map = im->active;
    while (id_map) {
        if (id_map->LID == LID && id_map->LID_refcount > 0) {
            return id_map;
        }
        id_map = id_map->next;
    }
    return 0;
}

mpr_id_map mpr_id_mapper_get_id_map_by_GID(mpr_id_mapper im, mpr_id GID)
{
    mpr_id_map id_map = im->active;
    while (id_map) {
        if (id_map->GID == GID)
            return id_map;
        id_map = id_map->next;
    }
    return 0;
}

/* TODO: rename this function */
mpr_id_map mpr_id_mapper_get_id_map_GID_free(mpr_id_mapper im, mpr_id last_GID)
{
    int searching = last_GID != 0;
    mpr_id_map id_map = im->active;
    while (id_map && searching) {
        if (id_map->GID == last_GID)
            searching = 0;
        id_map = id_map->next;
    }
    while (id_map) {
        if (id_map->GID_refcount <= 0)
            return id_map;
        id_map = id_map->next;
    }
    return 0;
}
