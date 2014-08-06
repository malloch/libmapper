
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#define eprintf(format, ...) do {               \
    if (verbose)                                \
        fprintf(stdout, format, ##__VA_ARGS__); \
} while(0)

int verbose = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

mapper_monitor mon = 0;

int sent = 0;
int received = 0;

int setup_source()
{
    source = mdev_new("testsend", 0, 0);
    if (!source)
        goto error;
    eprintf("source created.\n");

    float mn=0, mx=1;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, &mn, &mx);

    eprintf("Output signal /outsig registered.\n");
    eprintf("Number of outputs: %d\n", mdev_num_outputs(source));
    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        eprintf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        eprintf("handler: Got %f", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    eprintf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f', 0,
                             &mn, &mx, insig_handler, 0);

    eprintf("Input signal /insig registered.\n");
    eprintf("Number of inputs: %d\n", mdev_num_inputs(destination));
    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}

void set_connection_protocol(int protocol)
{
    char src_name[1024], dest_name[1024];
    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);

    mapper_db_connection_t props;
    props.protocol = protocol;

    mapper_monitor_connection_modify(mon, src_name, dest_name, &props,
                                     CONNECTION_PROTOCOL);

    // wait until change has taken effect
    while (source->routers->signals->connections->props.protocol != protocol) {
        mdev_poll(source, 1);
        mdev_poll(destination, 1);
    }
}

int setup_connection()
{
    mon = mapper_monitor_new(source->admin, 0);

    char src_name[1024], dest_name[1024];
    mapper_monitor_link(mon, mdev_name(source),
                        mdev_name(destination), 0, 0);

    while (!source->routers) {
        mdev_poll(source, 1);
        mdev_poll(destination, 1);
    }

    msig_full_name(sendsig, src_name, 1024);
    msig_full_name(recvsig, dest_name, 1024);

    mapper_monitor_connect(mon, src_name, dest_name, 0, 0);

    // poll devices for a bit to allow time for connection
    while (!source->routers->n_connections) {
        mdev_poll(destination, 1);
        mdev_poll(source, 1);
    }

    return 0;
}

void wait_ready()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 100);
        mdev_poll(destination, 100);
    }
}

void loop()
{
    int i;
    for (i = 0; i < 50; i++) {
        mdev_poll(source, 0);
        eprintf("\nUpdating signal %s to %f\n",
                sendsig->props.name, (i * 1.0f));
        if (!verbose) {
            printf(".");
            fflush(stdout);
        }
        msig_update_float(sendsig, (i * 1.0f));
        sent++;
        mdev_poll(destination, 100);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    // process flags for -v verbose, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        eprintf("testconnectionproto.c: possible arguments "
                                "-q quiet (suppress output), "
                                "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (setup_destination()) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (setup_connection()) {
        eprintf("Error initializing router.\n");
        result = 1;
        goto done;
    }

    printf("SENDING UDP");
    loop();

    set_connection_protocol(CONNECTION_PROTO_TCP);
    printf("SENDING TCP");
    loop();

    set_connection_protocol(CONNECTION_PROTO_UDP);
    printf("SENDING UDP");
    loop();

    if (sent != received) {
        eprintf("Not all sent messages were received.\n");
        eprintf("Updated value %d time%s, but received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    mapper_monitor_free(mon);
    cleanup_destination();
    cleanup_source();
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
