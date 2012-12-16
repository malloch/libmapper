
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

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    ;
}

int setup_device()
{
    device = mdev_new("testsync", 0, 0);
    if (!device)
        goto error;

    mdev_add_input(device, "/insig", 1, 'f', 0,
                   0, 0, insig_handler, 0);
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
        mdev_timetag_now(device, &device_time);
        if (device_time.sec != last_update) {
            last_update = device_time.sec;
            if (ready) {
                printf("\n%i  ||", counter++);
                // print device clock offset
                printf("  %f  |  %f  |  %f  |  %f  |  %f  |  %f  |",
                       device->admin->clock.offset,
                       device->admin->clock.latency,
                       device->admin->clock.jitter,
                       device->admin->clock.confidence,
                       device->admin->clock.remote_diff,
                       device->admin->clock.remote_jitter);
            }
            else {
                if (mdev_ready(device)) {
                    printf("   || %s | ", mdev_name(device));
                    // Give device clock a random starting offset
                    device->admin->clock.offset = (rand() % 100) - 50;
                    printf(" latency   |  jitter   |  confidence");
                    ready = 1;
                }
            }
        }
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
        printf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    loop();

  done:
    cleanup_device();
    return result;
}
