
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

mapper_device device = 0;
mapper_timetag_t device_time;
uint32_t last_update;
int ready = 0;
int done = 0;
int counter = 0;
int current_bar = -1;

void handler(mapper_metronome m, unsigned int bar,
             unsigned int beat, void *user_data)
{
    if (bar != current_bar) {
        printf("\n%i --> %i", bar, beat);
        fflush(stdout);
        current_bar = bar;
    }
    else {
        printf(" %i", beat);
        fflush(stdout);
    }
    switch (bar) {
        case 20:
            mdev_set_metronome_bpm(device, m, 240., 1);
            break;
        default:
            break;
    }
}

int setup_device()
{
    device = mdev_new("testmetronome", 0, 0);
    if (!device)
        goto error;

    device->admin->clock.offset = (rand() % 20) - 10;

    mapper_timetag_t tt;
    mdev_timetag_now(device, &tt);
    mapper_timetag_add_seconds(&tt, 5.);
    mdev_add_metronome(device, "/foo", tt, 120, 4, handler, 0);

    return 0;

  error:
    return 1;
}

void cleanup_device()
{
    if (device) {
        printf("\nFreeing device... ");
        fflush(stdout);
        mdev_free(device);
        printf("ok");
    }
}

void loop()
{
    int i = 0;
    printf("Loading device...\n");

    while (i >= 0 && !done) {
        mdev_poll(device, 10);
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;

    signal(SIGINT, ctrlc);

    if (setup_device()) {
        printf("Error initializing device.\n");
        result = 1;
        goto done;
    }

    loop();

  done:
    cleanup_device();
    return result;
}
