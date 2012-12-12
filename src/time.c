
#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static double multiplier = 1.0/((double)(1LL<<32));

void mapper_clock_init(mapper_clock clock)
{
    clock->rate = 1.0;
    clock->offset = 0.0;
    clock->latency = 0.0;
    clock->jitter = 0.0;
    clock->confidence = 0.001;

    mapper_clock_now(clock, &clock->now);
    clock->next_ping = clock->now.sec;

    clock->metronomes = 0;
}

void mapper_clock_adjust(mapper_clock clock,
                         double difference,
                         float confidence)
{
    double weight = 1.0 - clock->confidence;
    double new_offset = clock->offset + difference * weight;

    // try inserting pull from system clock
    //new_offset *= 0.9999;

    double adjustment = new_offset - clock->offset;

    // adjust stored timetag
    clock->remote_diff -= adjustment;
    clock->confidence *= adjustment < 0.001 ? 1.1 : 0.99;
    if (clock->confidence > 0.9) {
        clock->confidence = 0.9;
    }
    clock->offset = new_offset;
}

void mapper_clock_now(mapper_clock clock,
                      mapper_timetag_t *timetag)
{
    // first get current time from system clock
    // adjust using rate and offset from mapping network sync
    lo_timetag_now((lo_timetag*)timetag);
    mapper_timetag_add_seconds(timetag, clock->offset);
}

double mapper_timetag_difference(mapper_timetag_t a, mapper_timetag_t b)
{
    return (double)a.sec - (double)b.sec +
        ((double)a.frac - (double)b.frac) * multiplier;
}

void mapper_timetag_add_seconds(mapper_timetag_t *a, double b)
{
    if (!b)
        return;
    b += (double)a->frac * multiplier;
    a->sec += floor(b);
    b -= floor(b);
    if (b < 0.0) {
        a->sec--;
        b = 1.0 - b;
    }
    a->frac = (uint32_t) (((double)b) * (double)(1LL<<32));
}

double mapper_timetag_get_double(mapper_timetag_t timetag)
{
    return (double)timetag.sec + (double)timetag.frac * multiplier;
}

void mapper_timetag_set_from_int(mapper_timetag_t *tt, int value)
{
    tt->sec = value;
    tt->frac = 0;
}

void mapper_timetag_set_from_float(mapper_timetag_t *tt, float value)
{
    tt->sec = floor(value);
    tt->frac = (uint32_t) (((float)value) * (double)(1LL<<32));
}

void mapper_timetag_set_from_double(mapper_timetag_t *tt, double value)
{
    tt->sec = floor(value);
    tt->frac = (uint32_t) (((double)value) * (double)(1LL<<32));
}

void mapper_timetag_cpy(mapper_timetag_t *ttl, mapper_timetag_t ttr)
{
    ttl->sec = ttr.sec;
    ttl->frac = ttr.frac;
}

void mapper_clock_init_metronome(mapper_clock clock, mapper_metronome m)
{
    if (memcmp(&m->start, &MAPPER_TIMETAG_NOW, sizeof(mapper_timetag_t))==0) {
        mapper_timetag_cpy(&m->start, clock->now);
    }
    if (m->bpm > 0.) {
        m->spb = 60. / m->bpm;
        double elapsed = mapper_timetag_difference(clock->now, m->start);
        if (elapsed < 0.) {
            mapper_timetag_cpy(&m->next_beat, m->start);
        }
        else {
            mapper_timetag_cpy(&m->next_beat, clock->now);
            mapper_timetag_add_seconds(&m->next_beat,
                                       m->spb - fmod(elapsed, m->spb));
        }
    }
    else
        m->next_beat.sec = 0;
}

mapper_metronome mapper_clock_add_metronome(mapper_clock clock,
                                            const char *name,
                                            mapper_timetag_t start,
                                            float bpm,
                                            unsigned int count,
                                            mapper_metronome_handler h,
                                            void *user_data)
{
    // TODO: check if metronome already exists?
    if (!h)
        return 0;

    mapper_metronome m = (mapper_metronome) malloc(sizeof(mapper_metronome_t));
    m->name = strdup(name);
    mapper_timetag_cpy(&m->start, start);
    m->bpm = bpm;
    m->count = count;
    m->needs_init = 1;
    m->handler = h;
    m->user_data = user_data;
    m->next = clock->metronomes;
    clock->metronomes = m;

    mapper_clock_now(clock, &clock->now);
    return m;
}

float mapper_clock_check_metronomes(mapper_clock clock)
{
    float wait = -1.;
    mapper_clock_now(clock, &clock->now);
    mapper_metronome m = clock->metronomes;
    while (m) {
        if (m->needs_init) {
            mapper_clock_init_metronome(clock, m);
            m->needs_init = 0;
        }
        double diff = mapper_timetag_difference(m->next_beat, clock->now);
        if (m->bpm <= 0.) {
            m = m->next;
            continue;
        }
        if (diff <= 0) {
            // call handler
            double elapsed = mapper_timetag_difference(clock->now, m->start);
            long int beat_num = floor(elapsed / m->spb);

            if (m->handler)
                m->handler(m, beat_num / m->count,
                           beat_num % m->count, m->user_data);

            // handler may have caused changes to metronome settings, so recalulate elapsed time
            elapsed = mapper_timetag_difference(clock->now, m->start);
            mapper_timetag_cpy(&m->next_beat, clock->now);
            mapper_timetag_add_seconds(&m->next_beat, m->spb - fmod(elapsed, m->spb));
            if (wait < 0. || m->spb < wait)
                wait = m->spb;
        }
        else {
            if (diff < wait || wait < 0.)
                wait = diff;
        }
        m = m->next;
    }
    return wait * 1000;
}

const char *mapper_metronome_name(mapper_metronome m)
{
    return m->name;
}

void mapper_clock_remove_metronome(mapper_clock clock, mapper_metronome m)
{
    mapper_metronome *temp = &clock->metronomes;
    while (*temp) {
        if (*temp == m) {
            *temp = m->next;
            if (m->name)
                free(m->name);
            free(m);
            return;
        }
        temp = &(*temp)->next;
    }
}
