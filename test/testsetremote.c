#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int done = 0;
int period = 100;
int signal_detected = 0;

mpr_dev dev = 0;
mpr_graph graph = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int sent = 0;
int received = 0;

float M, B, expected;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void on_sig(mpr_obj o, mpr_status e, mpr_id instance, const void *user)
{
    /* Check if this signal matches recvsig */
    mpr_id id = mpr_obj_get_prop_as_int64(o, MPR_PROP_ID, NULL);
    mpr_id recvsig_id = mpr_obj_get_prop_as_int64((mpr_obj)recvsig, MPR_PROP_ID, NULL);

    if (MPR_SIG != mpr_obj_get_type(o) || id != recvsig_id)
        return;

    switch (e) {
        case MPR_OBJ_NEW:
        case MPR_OBJ_MOD:
            signal_detected = 1;
            break;
        case MPR_OBJ_REM:
        case MPR_OBJ_EXP:
            signal_detected = 0;
            break;
        default:
            break;
    }
}

int setup_graph(const char *iface)
{
    graph = mpr_graph_new(MPR_OBJ);
    if (!graph)
        goto error;
    if (iface)
        mpr_graph_set_interface(graph, iface);

    mpr_obj_add_cb((mpr_obj)graph, on_sig, MPR_STATUS_ANY, NULL, 0);

    eprintf("graph created using interface %s.\n", mpr_graph_get_interface(graph));
    return 0;

  error:
    return 1;
}

void cleanup_graph(void)
{
    if (graph) {
        eprintf("Freeing graph.. ");
        fflush(stdout);
        mpr_graph_free(graph);
        eprintf("ok\n");
    }
}

void handler(mpr_obj obj, mpr_status event, mpr_id instance, const void *data)
{
    float *value = (float*) mpr_sig_get_value((mpr_sig)obj, instance, NULL);
    if (value) {
        eprintf("handler: Got %f\n", (*value));
        if (fabs(*(float*)value - expected) < 0.0001)
            received++;
        else
            eprintf(" expected %f\n", expected);
    }
}

int setup_device(const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    dev = mpr_dev_new("testsetremote.recv", NULL);
    if (!dev)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dev), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dev)));

    recvsig = mpr_sig_new((mpr_obj)dev, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, &mn, &mx, NULL);
    mpr_obj_add_cb((mpr_obj)recvsig, handler, MPR_STATUS_UPDATE_REM, NULL, 0);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dev, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);
    return 0;

  error:
    return 1;
}

void cleanup_dev(void)
{
    if (dev) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dev);
        eprintf("ok\n");
    }
}

int wait_ready(void)
{
    while (!done && (!mpr_dev_get_is_ready(dev) || !signal_detected)) {
        mpr_dev_poll(dev, 25);
        mpr_graph_poll(graph, 25);
    }
    return done;
}

void loop(void)
{
    int i = 0;
    mpr_list sigs;
    mpr_id sigid = mpr_obj_get_prop_as_int64((mpr_obj)recvsig, MPR_PROP_ID, NULL);

    while ((!terminate || i < 50) && !done && signal_detected) {
        sigs = mpr_graph_get_list(graph, MPR_SIG);
        sigs = mpr_list_filter(sigs, MPR_PROP_ID, NULL, 1, MPR_INT64, &sigid, MPR_OP_EQ);
        if (sigs && *sigs) {
            eprintf("Updating remote signal %s to %d\n",
                    mpr_obj_get_prop_as_str(*sigs, MPR_PROP_NAME, NULL), i);
            mpr_sig_set_value(*sigs, 0, 1, MPR_INT32, &i);
            mpr_list_free(sigs);
        }
        expected = i;
        sent++;
        mpr_graph_poll(graph, 0);
        mpr_dev_poll(dev, period);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testsetremote.c: possible arguments "
                               "-f fast (execute quickly), "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            i++;
                            iface = argv[i];
                            j = len;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);

    if (setup_device(iface)) {
        eprintf("Error initializing device.\n");
        result = 1;
        goto done;
    }

    if (setup_graph(iface)) {
        eprintf("Error initializing graph.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    loop();

    if (!received || sent != received) {
        eprintf("Not all sent updates were received.\n");
        eprintf("Updated value %d time%s and received %d of them.\n",
                sent, sent == 1 ? "" : "s", received);
        result = 1;
    }

  done:
    cleanup_dev();
    cleanup_graph();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
