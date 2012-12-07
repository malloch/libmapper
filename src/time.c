
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
    clock->confidence = 0.001;

    mdev_timetag_now(dev, &clock->ping);
    clock->next_ping = clock->now.sec + 20;
}

void mdev_clock_adjust(mapper_device dev,
                       double seconds)
{
    printf("mdev_clock_adjust: %f\n", seconds);
    mapper_clock_t *clock = &dev->admin->clock;

    double weight = 1.0 - clock->confidence;
    double new_offset = clock->offset + seconds * weight;

    // try inserting pull from system clock
    //new_offset *= 0.9999;

    double adjustment = new_offset - clock->offset;

    // adjust stored timetag
    clock->remote_diff += adjustment;
    clock->confidence *= adjustment < 0.001 ? 1.1 : 0.99;
    if (clock->confidence > 0.9) {
        clock->confidence = 0.9;
    }
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
