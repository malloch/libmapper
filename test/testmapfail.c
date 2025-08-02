#include "../src/graph.h"
#include "../src/link.h"
#include <mapper/mapper.h>
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

mpr_dev src = 0;
mpr_dev dst = 0;
mpr_graph srcgraph = 0;
mpr_graph dstgraph = 0;
mpr_sig sendsig = 0;
mpr_sig recvsig = 0;

int src_linked = 0;
int dst_linked = 0;

int sent = 0;
int received = 0;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int setup_src(const char *iface)
{
    int mn=0, mx=1;
    mpr_list l;

    src = mpr_dev_new("testmapfail-send", 0);
    if (!src)
        goto error;
    srcgraph = mpr_obj_get_graph((mpr_obj)src);
    if (iface)
        mpr_graph_set_interface(srcgraph, iface);
    eprintf("source created using interface %s.\n", mpr_graph_get_interface(srcgraph));

    sendsig = mpr_sig_new((mpr_obj)src, MPR_DIR_OUT, "outsig", 1, MPR_INT32, NULL, &mn, &mx, NULL);

    eprintf("Output signal 'outsig' registered.\n");
    l = mpr_dev_get_sigs(src, MPR_DIR_OUT);
    eprintf("Number of outputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

    return 0;

  error:
    return 1;
}

void cleanup_src(void)
{
    if (src) {
        eprintf("Freeing source.. ");
        fflush(stdout);
        mpr_dev_free(src);
        eprintf("ok\n");
    }
}

void handler(mpr_obj obj, mpr_status evt, mpr_id instance, const void *data)
{
    float *value = (float*) mpr_sig_get_value((mpr_sig)obj, instance, NULL);
    if (value) {
        eprintf("handler: Got %f\n", (*value));
    }
    received++;
}

int setup_dst(const char *iface)
{
    float mn=0, mx=1;
    mpr_list l;

    dst = mpr_dev_new("testmapfail-recv", 0);
    if (!dst)
        goto error;
    dstgraph = mpr_obj_get_graph((mpr_obj)dst);
    if (iface)
        mpr_graph_set_interface(dstgraph, iface);
    eprintf("destination created using interface %s.\n", mpr_graph_get_interface(dstgraph));

    recvsig = mpr_sig_new((mpr_obj)dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, &mn, &mx, NULL);
    mpr_obj_add_cb((mpr_obj)recvsig, handler, MPR_STATUS_UPDATE_REM, NULL, 0);

    eprintf("Input signal 'insig' registered.\n");
    l = mpr_dev_get_sigs(dst, MPR_DIR_IN);
    eprintf("Number of inputs: %d\n", mpr_list_get_size(l));
    mpr_list_free(l);

    return 0;

  error:
    return 1;
}

void cleanup_dst(void)
{
    if (dst) {
        eprintf("Freeing destination.. ");
        fflush(stdout);
        mpr_dev_free(dst);
        eprintf("ok\n");
    }
}

int setup_maps(void)
{
    int i = 10;
    /* Here we will deliberately create a map with a faulty expression */
    mpr_map map = mpr_map_new(1, &sendsig, 1, &recvsig);
    const char *e = "a=10";
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, e, 1);
    mpr_obj_push((mpr_obj)map);

    /* Wait until mapping has been established */
    while (!done && i-- > 0) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    /* release the map */
    mpr_map_release(map);

    return 0;
}

int wait_ready(void)
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
    return done;
}

void loop(void)
{
    int i = 0;
    mpr_list links = 0;
    const char *name = mpr_obj_get_prop_as_str((mpr_obj)sendsig, MPR_PROP_NAME, NULL);
    eprintf("Polling device..\n");
    while ((   !terminate
            || (links = mpr_graph_get_list(srcgraph, MPR_LINK))
            || (links = mpr_graph_get_list(dstgraph, MPR_LINK)))
           && !done) {
        mpr_list_free(links);
        eprintf("Updating signal %s to %d\n", name, i);
        mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &i);
        sent++;
        mpr_dev_poll(src, 0);
        mpr_dev_poll(dst, 100);
        i++;

        if (!verbose) {
            printf("\r  Sent: %4i, Received: %4i   ", sent, received);
            fflush(stdout);
        }
    }
}

void ctrlc(int signal)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0;
    char *iface = 0;
    mpr_list links;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testmapfail.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-h help, "
                               "--iface network interface\n");
                        return 1;
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

    signal(SIGINT, ctrlc);

    if (setup_dst(iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    if (autoconnect && setup_maps()) {
        eprintf("Error initializing maps.\n");
        result = 1;
        goto done;
    }

    loop();

    if (   (links = mpr_graph_get_list(srcgraph, MPR_LINK))
        || (links = mpr_graph_get_list(dstgraph, MPR_LINK))) {
        mpr_list_free(links);
        eprintf("Link cleanup failed.\n");
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    printf("...................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
