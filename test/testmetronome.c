
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
        counter++;
        if (counter % 10 == 5) {
            double new_tempo = rand() % 150 + 50;
            printf("\nSetting tempo to %f BPM", new_tempo);
            mdev_set_metronome_bpm(device, m, new_tempo, 1);
        }
        else if (counter % 10 == 0) {
            unsigned int new_count = rand() % 9 + 1;
            printf("\nSetting count to %i", new_count);
            mdev_set_metronome_count(device, m, new_count, 1);
        }
        printf("\n%i --> %i", bar, beat);
        fflush(stdout);
        current_bar = bar;
    }
    else {
        printf(" %i", beat);
        fflush(stdout);
    }
}

int setup_device()
{
    device = mdev_new("testmetronome", 0, 0);
    if (!device)
        goto error;

    mapper_timetag_t tt = {10000000,0};
    mdev_timetag_now(device, &tt);
    mapper_timetag_add_seconds(&tt, 5.);
    mdev_add_metronome(device, "/foo", tt, 120., 4, handler, 0);

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
