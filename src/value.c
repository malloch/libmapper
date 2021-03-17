#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <assert.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static inline int _min(int a, int b) { return a < b ? a : b; }

void mpr_value_realloc(mpr_value v, int vlen, mpr_type type, int mlen, int num_inst, int is_input)
{
    RETURN_UNLESS(v && mlen && num_inst >= v->num_inst);
    int i, samp_size = vlen * mpr_type_get_size(type);

    if (!v->inst || num_inst > v->num_inst) {
        if (v->inst)
            v->inst = realloc(v->inst, sizeof(mpr_value_buffer_t) * num_inst);
        else {
            v->inst = malloc(sizeof(mpr_value_buffer_t) * num_inst);
            v->num_inst = 0;
        }
        // initialize new instances
        for (i = v->num_inst; i < num_inst; i++) {
            mpr_value_buffer b = &v->inst[i];
            b->samps = calloc(1, mlen * samp_size);
            b->times = calloc(1, mlen * sizeof(mpr_time));
            b->pos = -1;
            b->full = 0;
        }
    }

    if (!is_input || vlen != v->vlen || type != v->type) {
        // reallocate old instances (v->num_inst has not yet been updated)
        for (i = 0; i < v->num_inst; i++) {
            mpr_value_buffer b = &v->inst[i];
            b->samps = realloc(b->samps, mlen * samp_size);
            b->times = realloc(b->times, mlen * sizeof(mpr_time));
            // Initialize entire value to 0
            memset(b->samps, 0, mlen * samp_size);
            memset(b->times, 0, mlen * sizeof(mpr_time));
            b->pos = -1;
            b->full = 0;
        }
        goto done;
    }

    if (mlen == v->mlen)
        goto done;

    // only the memory size is different
    mpr_value_buffer_t tmp;

    for (i = 0; i < v->num_inst; i++) {
        mpr_value_buffer b = &v->inst[i];
        tmp.samps = malloc(samp_size * mlen);
        tmp.times = malloc(sizeof(mpr_time) * mlen);

        // TODO: don't bother copying memory if pos is -1
        if (mlen > v->mlen) {
            int npos = v->mlen - b->pos;
            // copy from [v->pos, v->mlen] to [0, v->mlen - v->pos]
            memcpy(tmp.samps, b->samps + b->pos * samp_size, npos * samp_size);
            memcpy(tmp.times, &b->times[b->pos], npos * sizeof(mpr_time));
            // copy from [0, v->pos] to [v->mlen - v->pos, v->mlen]
            memcpy(tmp.samps + npos * samp_size, b->samps, b->pos * samp_size);
            memcpy(&tmp.times[npos], b->times, b->pos * sizeof(mpr_time));
            // zero remainder
            memset(tmp.samps + v->mlen * samp_size, 0, (mlen - v->mlen) * samp_size);
            memset(&tmp.times[v->mlen], 0, (mlen - v->mlen) * sizeof(mpr_time));
            b->pos = v->mlen;
            b->full = 0;
        }
        else {
            int len = _min(v->mlen - b->pos, mlen);
            memcpy(tmp.samps, b->samps + b->pos * samp_size, len * samp_size);
            memcpy(tmp.times, &b->times[b->pos], len * sizeof(mpr_time));
            if (mlen > len) {
                memcpy(tmp.samps + len * samp_size, b->samps, (mlen - len) * samp_size);
                memcpy(&tmp.times[len], b->times, (mlen - len) * sizeof(mpr_time));
            }
            b->pos = len;
            b->full = (b->pos > mlen);
        }
        free(b->samps);
        free(b->times);
        b->samps = tmp.samps;
        b->times = tmp.times;
    }

done:
    v->vlen = vlen;
    v->type = type;
    v->mlen = mlen;
    v->num_inst = num_inst;
}

int mpr_value_remove_inst(mpr_value v, int idx)
{
    int i;
    RETURN_UNLESS(idx >= 0 && idx < v->num_inst, v->num_inst);
    free(v->inst[idx].samps);
    free(v->inst[idx].times);
    for (i = idx + 1; i < v->num_inst; i++) {
        // shift values down
        memcpy(&(v->inst[i-1]), &(v->inst[i]), sizeof(mpr_value_buffer_t));
    }
    --v->num_inst;
    assert(v->num_inst >= 0);
    v->inst = realloc(v->inst, sizeof(mpr_value_buffer_t) * v->num_inst);
    return v->num_inst;
}

void mpr_value_reset_inst(mpr_value v, int idx)
{
    RETURN_UNLESS(v->inst);
    mpr_value_buffer b = &v->inst[idx];
    memset(b->samps, 0, v->mlen * v->vlen * mpr_type_get_size(v->type));
    memset(b->times, 0, v->mlen * sizeof(mpr_time));
    b->pos = -1;
    b->full = 0;
}

void mpr_value_set_sample(mpr_value v, int idx, void *s, mpr_time t)
{
    mpr_value_buffer b = &v->inst[idx];
    b->pos += 1;
    if (b->pos >= v->mlen) {
        b->pos = 0;
        b->full = 1;
    }
    memcpy(mpr_value_get_samp(v, idx), s, v->vlen * mpr_type_get_size(v->type));
    memcpy(mpr_value_get_time(v, idx), &t, sizeof(mpr_time));
}

void mpr_value_free(mpr_value v) {
    int i;
    RETURN_UNLESS(v->inst);
    for (i = 0; i < v->num_inst; i++) {
        FUNC_IF(free, v->inst[i].samps);
        FUNC_IF(free, v->inst[i].times);
    }
    free(v->inst);
    v->inst = 0;
}

#ifdef DEBUG
static void _value_print(mpr_value v, int inst_idx, int hist_idx) {
    int i;
    if (v->inst[inst_idx].pos < 0) {
        printf("NULL\n");
        return;
    }
    void *s = mpr_value_get_samp_hist(v, inst_idx, hist_idx);
    mpr_time *t = mpr_value_get_time_hist(v, inst_idx, hist_idx);
    printf("%08x.%08x | ", (*t).sec, (*t).frac);
    if (v->vlen > 1)
        printf("[");

    switch (v->type) {
#define TYPED_CASE(MTYPE, TYPE, STR)        \
        case MTYPE:                         \
            for (i = 0; i < v->vlen; i++)   \
                printf(STR, ((TYPE*)s)[i]); \
            break;
        TYPED_CASE(MPR_INT32, int, "%d, ");
        TYPED_CASE(MPR_FLT, float, "%f, ");
        TYPED_CASE(MPR_DBL, double, "%f, ");
#undef TYPED_CASE
    }

    if (v->vlen > 1)
        printf("\b\b] @%p -> %p\n", v->inst[inst_idx].samps, s);
    else
        printf("\b\b @%p -> %p\n", v->inst[inst_idx].samps, s);
}

void mpr_value_print(mpr_value v, int inst_idx) {
    RETURN_UNLESS(inst_idx < v->num_inst && v->inst[inst_idx].pos >= 0);
    _value_print(v, inst_idx, v->inst[inst_idx].pos);
}

void mpr_value_print_hist(mpr_value v, int inst_idx) {
    RETURN_UNLESS(inst_idx < v->num_inst && v->inst[inst_idx].pos >= 0);

    // if history is full, print from pos+1 -> pos, else print from 0 -> pos
    int i, hidx = v->inst[inst_idx].pos * -1;
    for (i = 0; i < v->mlen; i++) {
        printf("%s {%3d} ", hidx ? "  " : "->", hidx);
        _value_print(v, inst_idx, hidx);
        ++hidx;
        if (hidx > 0)
            hidx -= v->mlen;
    };
}

#endif
