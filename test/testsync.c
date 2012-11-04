
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

mapper_device devices[5] = {0, 0, 0, 0, 0};
lo_timetag system_time;
mapper_timetag_t device_times[5];
uint32_t last_update;
int ready = 0;
int done = 0;
int counter = 0;
int numDevices = 5;

/*! Creation of devices. */
int setup_devices()
{
    int i;
    for (i=0; i<numDevices; i++) {
        devices[i] = mdev_new("testsync", 0, 0);
        if (!devices[i])
            goto error;
    }
    return 0;

  error:
    return 1;
}

void cleanup_devices()
{
    int i;
    for (i=0; i<numDevices; i++) {
        if (devices[i]) {
            printf("\nFreeing device %i... ", i);
            fflush(stdout);
            mdev_free(devices[i]);
            printf("ok");
        }
    }
}

void loop()
{
    int i = 0;
    printf("Loading devices...\n");

    while (i >= 0 && !done) {
        for (i=0; i<numDevices; i++)
            mdev_poll(devices[i], 1);
        lo_timetag_now(&system_time);
        if (system_time.sec != last_update) {
            last_update = system_time.sec;
            if (ready) {
                // calculate standard deviation
                double mean = 0.0, min = 0.0, max = 0.0;
                for (i=0; i<numDevices; i++) {
                    double offset = devices[i]->admin->clock.offset;
                    mean += offset;
                    if (min == 0.0 || offset < min)
                        min = offset;
                    if (max == 0.0 || offset > max)
                        max = offset;
                }
                mean /= numDevices;
                double difference_aggregate = 0;
                for (i=0; i<numDevices; i++) {
                    difference_aggregate += powf(devices[i]->admin->clock.offset - mean, 2);
                }
                printf("\n%i  ||", counter++);
                // print device clock offsets
                for (i=0; i<numDevices; i++) {
                    printf("  %f  |", devices[i]->admin->clock.offset);
                }
                printf("|  %f  |  ", max - min);
                printf("%f", sqrtf(difference_aggregate / numDevices));
            }
            else {
                int count = 0;
                for (i=0; i<numDevices; i++) {
                    count += mdev_ready(devices[i]);
                }
                if (count >= numDevices) {
                    printf("   || ");
                    for (i=0; i<numDevices; i++) {
                        printf("%s | ", mdev_name(devices[i]));
                        // Give each device clock a random starting offset
                        devices[i]->admin->clock.offset = (rand() % 100) - 50;
                    }
                    printf("  RANGE    |  STD DEV");
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

    if (setup_devices()) {
        printf("Error initializing devices.\n");
        result = 1;
        goto done;
    }

    loop();

  done:
    cleanup_devices();
    return result;
}
