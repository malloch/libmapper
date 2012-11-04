
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

void mdev_clock_init(mapper_device dev)
{
    mapper_clock_t *clock = &dev->admin->clock;
    clock->rate = 1.0;
    clock->offset = 0.0;
    clock->latency = 0.0;
    clock->jitter = 0.0;
    clock->confidence = 0.1;
    clock->local_index = 0;
    int i;
    for (i=0; i<10; i++) {
        clock->local[i].device_id = 0;
    }
    clock->remote.device_id = 0;
    mdev_timetag_now(dev, &clock->now);
    clock->next_ping = clock->now.sec + 10;
}

void mdev_clock_adjust(mapper_device dev,
                       double difference,
                       double confidence,
                       int is_latency_adjusted)
{
    mapper_clock_t *clock = &dev->admin->clock;
    // set confidence to 1 for now since it is not being updated
    confidence = 0.2;

    // calculate relative confidences
    //double sum = confidence + clock->confidence;
    //if (sum <= 0)
    //    confidence = 0.1;
    //else
    //    confidence /= sum;

    double new_offset;
    // use diff to influence rate & offset

    if (!is_latency_adjusted) {
        if (difference < 0.0 && difference > -1.0) {
            return;
        }
        confidence *= 0.5;
    }

    new_offset = clock->offset * (1 - confidence) +
        (clock->offset + difference) * confidence;

    // try inserting pull from system clock
    //new_offset *= 0.9999;

    double adjustment = new_offset - clock->offset;

    // adjust stored timetags
    int i;
    for (i=0; i<10; i++) {
        if (clock->local[i].timetag.sec == 0)
            continue;
        mapper_timetag_add_seconds(&clock->local[i].timetag, adjustment);
    }
    mapper_timetag_add_seconds(&clock->remote.timetag, adjustment);
    clock->confidence *= adjustment < 0.001 ? 1.1 : 0.9;
    clock->offset = new_offset;
}

void mdev_timetag_now(mapper_device dev,
                      mapper_timetag_t *timetag)
{
    if (!dev)
        return;
    // first get current time from system clock
    // adjust using rate and offset from mapping network sync
    lo_timetag_now((lo_timetag*)timetag);
    mapper_timetag_add_seconds(timetag, dev->admin->clock.offset);
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
