
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

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
    printf("source created.\n");

    float mn=0, mx=1;

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, &mn, &mx);

    printf("Output signal /outsig registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(source));
    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        printf("handler: Got %f\n", (*(float*)value));
    }
    received++;
}

int setup_destination()
{
    destination = mdev_new("testrecv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    recvsig = mdev_add_input(destination, "/insig", 1, 'f', 0,
                             &mn, &mx, insig_handler, 0);

    printf("Input signal /insig registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(destination));
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
    props.src_name = src_name;
    props.dest_name = dest_name;

    mapper_monitor_connection_modify(mon, &props, CONNECTION_PROTOCOL);

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
        mdev_poll(source, 0);
        mdev_poll(destination, 0);
        usleep(500 * 1000);
    }
}

void loop()
{
    int i;
    for (i = 0; i < 10; i++) {
        mdev_poll(source, 0);
        printf("Updating signal %s to %f\n",
               sendsig->props.name, (i * 1.0f));
        msig_update_float(sendsig, (i * 1.0f));
        sent++;
        mdev_poll(destination, 100);
    }
}

int main()
{
    int result = 0;

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_ready();

    if (setup_connection()) {
        printf("Error initializing router.\n");
        result = 1;
        goto done;
    }

    printf("SENDING UDP\n");
    loop();

    set_connection_protocol(CONNECTION_PROTO_TCP);
    printf("SENDING TCP\n");
    loop();

    set_connection_protocol(CONNECTION_PROTO_UDP);
    printf("SENDING UDP AGAIN\n");
    loop();

    if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
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
