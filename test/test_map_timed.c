#include <mapper/mapper.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>

// TODO: test running multiple self-timed maps simultaneously

int verbose = 1;
int terminate = 0;
int autoconnect = 1;
int shared_graph = 0;
int done = 0;
int period = 100;

int num_inst = 10;
int num_iterations = 50;

mpr_dev src = NULL;
mpr_dev dst = NULL;
mpr_sig sendsig = NULL;
mpr_sig recvsig = NULL;

mpr_map map = NULL;

int current_config = -1;
int sent = 0;
int received = 0;
int matched = 0;

mpr_time t_last = {0, 0}, t_now;
double expected = 0;

// TODO: test with instanced and non-instanced maps
// - if map is instanced, map-produced instance updates should cause activation
// - if map is not instanced, only already-active instances should be updated

/* schedule next periodic event from current time */
#define NOW                 \
    "period = %g; "         \
    "y = a++; "             \
    "next = now + period;"

/* schedule next periodic event from current time with explicit phase
 * we "round up" when calculating num periods since start if closer than 0.999 */
#define NOW_W_START                                                         \
    "period = %g; "                                                         \
    "start{-1} = now; "                                                     \
    "y = 1; "                                                               \
    "next = (floor((now - start + 0.001) / period) + 1) * period + start;"

/* better to schedule by incrementing the `next` timestamp for drift-free timing */
/* schedule next periodic event with implicit phase */
/* also test assigning `t_next` instead of `next` (shouldn't make a difference) */
#define NEXT            \
    "period = %g; "     \
    "y = 1; "           \
    "t_next += period;"

/* schedule next periodic event with explicit phase
 * we "round up" if closer than 1ms when calculating num periods since start */
#define NEXT_W_START                                                \
    "period = %g; "                                                 \
    "start{-1} = now; "                                             \
    "y = 1; "                                                       \
    "next = (floor((next - start + 0.001) / period) + 1) * period + start;"

/* if we track num_periods it is much cheaper to calculate `(n++)*period+start`
 * also have access to beat number which could be useful
 * this version has no division at "runtime" */
/* the variable `i` is not instanced, so it gets incremented each time an instance is updated */
#define START_NO_DIV                            \
    "start{-1} = now; "                         \
    "period{-1} = %g; "                         \
    "i{-1} = floor((now - start) / period); "   \
    "y = i; next = (i++) * period + start;"

/* schedule next periodic event from a repeating pattern (implicit start time) */
#define PATTERN                 \
    "period = %g; "             \
    "p = [1,.5,.5] * period; "  \
    "y = 1; next += p[i++];"

/* schedule next periodic event using a ramp */
/* TODO: reduce jitter so period at receiver increases monotonically */
#define RAMP                \
    "period{-1} = %g; "     \
    "y = period; "          \
    "period *= 1.01; "      \
    "next += period;"

/* schedule next periodic event using a sinusoid */
#define SINE                                    \
    "start{-1} = now; "                         \
    "period = %g; "                             \
    "y = 1; "                                   \
    "next += (sin(now - start) + 1.1) * period;"

/* schedule next periodic event in the past */
/* setting the `t_next` timestamp to a past value will cause very fast repetition so we will limit
 * this behaviour to 50 iterations. */
#define PAST                        \
    "i{-1} = 0; "                   \
    "y = i; "                       \
    "next = now - 1 + (i > 50) * 2;"

/* 'start' time is in the future */
#define FUTURE                                      \
    "start{-1} = now; "                             \
    "period{-1} = %g; "                             \
    "i{-1} = floor((now - start) / period) + 50; "  \
    "y = 1; "                                       \
    "next = (i++) * period + start;"

/* upsampling envelope follower */
#define UPSAMPLE            \
    "period = %g; "         \
    "y = ema(_x, 0.1); "    \
    "next += period * 0.67;"

#define DOWNSAMPLE                  \
    "period = %g; "                 \
    "y = ema(_x, 0.5); "            \
    "next = next{-1} + period * 2;"

/* period assignment includes check that delta time is not zero */
/* We explicitly track an initialization state using the user variable `started`. In generaal this
 * could be replaced with `_t_x' > 0 ?: ...` however when running tests in series the previous map
 * may be recovered and reactivated in which case the first result `_t_x'` will not be zero. */
#define QUANTIZE                                    \
    "started{-1} = 0; "                             \
    "period = ema(started ? _t_x' : %g * 25, 0.2); "\
    "y = period; "                                  \
    "next += period; "                              \
    "started = 1;"

#define RANDOM                  \
    "p = %g; "                  \
    "r = uniform(p) + p * 0.5; "\
    "y = r;"                    \
    "next += r;"

/* TODO: need unmodified t_x to estimate timebase offset
 * or direct access to timebase offset estimation, e.g. `periodic(period, x - t0_x)` */

/* note: since the expression string is being processed by `snprintf()` below, the modulus operator
 * `%` needs to be escaped using `%%` */
#define SYNC                                                \
    "period = %g; "                                         \
    "y = 1; "                                               \
    "next = now + period - ((t_now - t_x - x) %% period);"

/* the periodic function (syntactic sugar) */
#define FN_PERIODIC                         \
    "period = %g; "                         \
    "start{-1} = now; "                     \
    "y = 1; "                               \
    "next = periodic(period, start);"

/* the periodic function with start time in the future */
#define FN_PERIODIC_FUTURE                  \
    "period{-1} = %g; "                     \
    "start{-1} = now + period * 50; "       \
    "y = 1; "                               \
    "next = periodic(period, start);"

/* the periodic function with a vector period argument */
/* this supports polyrhythms & phasing */
/* this is a boring example with period multiples for easy evaluation */
#define FN_PERIODIC_LIST                    \
    "period = %g; "                         \
    "p = [3,2,1] * period; "                \
    "start{-1} = now; "                     \
    "y = 1; "                               \
    "next = periodic(p, start);"

#define REINSTANCE_SHORT    \
    "p = %g; "              \
    "alive = 1; "           \
    "y = p; "               \
    "alive = 0; "           \
    "next += p;"

#define REINSTANCE_LONG     \
    "p = %g; "              \
    "alive = 0; "           \
    "alive = 1; "           \
    "y = p; "               \
    "next += p;"

// TODO:
// - decay with instance release
// - upsampling with linear envelope
// - check timing with difference number of instances
// - consider addding explicit control of map num_inst

typedef struct _test_config
{
    int test_id;
    const char *expr;
    mpr_loc process_loc;
    double time_mult_min;
    double time_mult_max;
} test_config;

test_config test_configs[] = {
    {  1, NOW,                  MPR_LOC_SRC, 0.95, 1.15 },
    {  2, NOW,                  MPR_LOC_DST, 0.95, 1.20 },
    {  3, NOW_W_START,          MPR_LOC_SRC, 0.95, 1.10 },
    {  4, NOW_W_START,          MPR_LOC_DST, 0.95, 1.10 },
    {  5, NEXT,                 MPR_LOC_SRC, 0.95, 1.10 },
    {  6, NEXT,                 MPR_LOC_DST, 0.95, 1.10 },
    {  7, NEXT_W_START,         MPR_LOC_SRC, 0.95, 1.10 },
    {  8, NEXT_W_START,         MPR_LOC_DST, 0.95, 1.10 },
    {  9, START_NO_DIV,         MPR_LOC_SRC, 0.95, 1.10 },
    { 10, START_NO_DIV,         MPR_LOC_DST, 0.95, 1.10 },
    { 11, PATTERN,              MPR_LOC_SRC, 0.61, 0.71 },
    { 12, PATTERN,              MPR_LOC_DST, 0.61, 0.71 },
    { 13, RAMP,                 MPR_LOC_SRC, 1.40, 1.60 },
    { 14, RAMP,                 MPR_LOC_DST, 1.40, 1.60 },
    { 15, SINE,                 MPR_LOC_SRC, 0.85, 2.00 },
    { 16, SINE,                 MPR_LOC_DST, 0.85, 2.00 },
    { 17, PAST,                 MPR_LOC_SRC, 0.00, 0.10 },
    { 18, PAST,                 MPR_LOC_DST, 0.00, 0.10 },
    { 19, FUTURE,               MPR_LOC_SRC, 1.95, 2.05 },
    { 20, FUTURE,               MPR_LOC_DST, 1.90, 2.05 },
    { 21, UPSAMPLE,             MPR_LOC_SRC, 0.60, 0.75 },
    { 22, UPSAMPLE,             MPR_LOC_DST, 0.60, 0.75 },
    { 23, DOWNSAMPLE,           MPR_LOC_SRC, 1.90, 2.10 },
    { 24, DOWNSAMPLE,           MPR_LOC_DST, 1.90, 2.10 },
    { 25, QUANTIZE,             MPR_LOC_SRC, 1.65, 2.00 },
    { 26, QUANTIZE,             MPR_LOC_DST, 1.65, 2.00 },
    { 27, RANDOM,               MPR_LOC_SRC, 0.50, 1.50 },
    { 28, RANDOM,               MPR_LOC_DST, 0.50, 1.50 },
    { 29, FN_PERIODIC,          MPR_LOC_SRC, 0.95, 1.15 },
    { 30, FN_PERIODIC,          MPR_LOC_DST, 0.95, 1.15 },
    { 31, FN_PERIODIC_FUTURE,   MPR_LOC_SRC, 1.95, 2.05 },
    { 32, FN_PERIODIC_FUTURE,   MPR_LOC_DST, 1.85, 2.05 },
    { 33, FN_PERIODIC_LIST,     MPR_LOC_SRC, 0.95, 1.10 },
    { 34, FN_PERIODIC_LIST,     MPR_LOC_DST, 0.95, 1.10 },
    { 35, REINSTANCE_SHORT,     MPR_LOC_SRC, 0.95, 1.05 },
    { 36, REINSTANCE_SHORT,     MPR_LOC_DST, 0.95, 1.05 },
    { 37, REINSTANCE_LONG,      MPR_LOC_SRC, 0.95, 1.05 },
    { 38, REINSTANCE_LONG,      MPR_LOC_DST, 0.95, 1.05 },
//    { 39, SYNC_LOCAL,           MPR_LOC_SRC, 2.00, 2.00 },
//    { 40, SYNC_LOCAL,           MPR_LOC_DST, 2.00, 2.00 },
//    { 41, SYNC_REMOTE,          MPR_LOC_SRC, 2.00, 2.00 },
//    { 42, SYNC_REMOTE,          MPR_LOC_DST, 2.00, 2.00 },

};
const int NUM_TESTS = sizeof(test_configs)/sizeof(test_configs[0]);

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int setup_src(mpr_graph g, const char *iface)
{
    mpr_list l;

    src = mpr_dev_new("test_map_timed-send", g);
    if (!src)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(src), iface);
    eprintf("source created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(src)));

    sendsig = mpr_sig_new(src, MPR_DIR_OUT, "outsig", 1, MPR_FLT,
                          NULL, NULL, NULL, &num_inst, NULL, 0);

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

void handler(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
             mpr_type type, const void *value, mpr_time t)
{
    mpr_time_set(&t_now, MPR_NOW);
    if (MPR_STATUS_UPDATE_REM == event) {
        ++received;
        eprintf("  received: %d (%gms)\n", received,
                (mpr_time_as_dbl(t_now) - mpr_time_as_dbl(t_last)) * 1000);
        if (terminate && received == num_iterations) {
            /* release map */
            mpr_list maps;
            if (current_config >= 0 && MPR_LOC_SRC == test_configs[current_config].process_loc)
                maps = mpr_dev_get_maps(src, MPR_DIR_ANY);
            else
                maps = mpr_dev_get_maps(dst, MPR_DIR_ANY);
            while (maps) {
                eprintf("releasing map\n");
                mpr_map map = (mpr_map)*maps;
                maps = mpr_list_get_next(maps);
                mpr_map_release(map);
            }
        }
    }
    else {
        eprintf("  received instance release\n");
        mpr_sig_release_inst(sig, instance);
    }
    t_last = t_now;

}

int setup_dst(mpr_graph g, const char *iface)
{
    mpr_list l;

    dst = mpr_dev_new("test_map_timed-recv", g);
    if (!dst)
        goto error;
    if (iface)
        mpr_graph_set_interface(mpr_obj_get_graph(dst), iface);
    eprintf("destination created using interface %s.\n",
            mpr_graph_get_interface(mpr_obj_get_graph(dst)));

    recvsig = mpr_sig_new(dst, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL, NULL, NULL, &num_inst,
                          handler, MPR_STATUS_UPDATE_REM | MPR_STATUS_REL_UPSTRM);

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

int wait_ready(void)
{
    while (!done && !(mpr_dev_get_is_ready(src) && mpr_dev_get_is_ready(dst))) {
        mpr_dev_poll(src, 25);
        mpr_dev_poll(dst, 25);
    }
    return done;
}

void loop()
{
    eprintf("Polling device..\n");
    received = 0;
    mpr_dev_start_polling(dst, 100);

    while ((!terminate || received < num_iterations) && !done) {

        ++sent;
        if (sent % 3)
            mpr_sig_set_value(sendsig, 0, 1, MPR_INT32, &sent);

        if (!shared_graph)
            mpr_dev_poll(src, period);

        if (!verbose) {
            printf("\r  Received: %4i", received);
            fflush(stdout);
        }
    }

    mpr_dev_stop_polling(dst);
}

int run_test(test_config *cfg)
{
    double period_sec = period * 0.001;
    mpr_time t_start;
    int result = 0;
    int use_inst = 1;
    char expr[256];

    /* process in-flight messages before running the next test configuration */
    mpr_dev_poll(src, period);
    mpr_dev_poll(dst, period);
    mpr_dev_poll(src, period);
    mpr_dev_poll(dst, period);
    mpr_dev_poll(src, period);
    mpr_dev_poll(dst, period);

    /* insert period value into expression string if specified */
    snprintf(expr, 256, cfg->expr, period_sec);

    printf("Configuration %d: ", cfg->test_id);
    printf("PROC: %s", cfg->process_loc == MPR_LOC_SRC ? "src" : "dst");
    printf("; EXPR: \"%s\"\n", expr);

    map = mpr_map_new(1, &sendsig, 1, &recvsig);
    if (!map)
        return 1;

    /* set process location */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_PROCESS_LOC, NULL, 1, MPR_INT32, &cfg->process_loc, 1);

    /* set expression */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);

    /* set use_inst property to False since the destination will claim an instance locally */
    /* TODO: test with multiple instances, instanced periods, etc. */
    /* also test setting boolean property with integer value */
    mpr_obj_set_prop((mpr_obj)map, MPR_PROP_USE_INST, NULL, 1, MPR_INT32, &use_inst, 1);

    mpr_obj_push(map);

    /* wait until mapping has been established */
    while (!done && !mpr_map_get_is_ready(map)) {
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
    }

    /* wait for changes to take effect */
    do {
        int ready = 1;
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);
        mpr_dev_poll(src, 10);
        mpr_dev_poll(dst, 10);

        eprintf("\r  checking process location...");
        if (   !shared_graph
            && mpr_obj_get_prop_as_int32((mpr_obj)map, MPR_PROP_PROCESS_LOC, 0) != cfg->process_loc)
            ready = 0;
        else {
            eprintf("\r  checking expression...      ");
            if (strcmp(mpr_obj_get_prop_as_str((mpr_obj)map, MPR_PROP_EXPR, 0), expr))
                ready = 0;
        }
        if (ready) {
            eprintf("\r  configuration ready         \n");
            break;
        }
    } while (!done);

    /* double-check that the map instancing is correct */
    if (mpr_obj_get_prop_as_int32((mpr_obj)map, MPR_PROP_USE_INST, NULL) != use_inst) {
        eprintf("error: map.use_inst=%d but should be %d\n", !use_inst, use_inst);
        return 1;
    }

    /* activate a destination instance */
//    mpr_sig_activate_inst(recvsig, 0);

    mpr_time_set(&t_start, MPR_NOW);
    t_last = t_start;

    loop();

    if (terminate) {
        /* check result timing */
        mpr_time t_end;
        double min_expected_sec;
        double max_expected_sec;
        double elapsed_sec;

        mpr_time_set(&t_end, MPR_NOW);

        elapsed_sec = mpr_time_as_dbl(t_end) - mpr_time_as_dbl(t_start);
        min_expected_sec = period_sec * (num_iterations * cfg->time_mult_min);
        max_expected_sec = period_sec * (num_iterations * cfg->time_mult_max);

        if (verbose) {
            /* print configuration again */
            printf("Configuration %d: ", cfg->test_id);
            printf("PROC: %s", cfg->process_loc == MPR_LOC_SRC ? "src" : "dst");
            printf("; EXPR: \"%s\"\n", expr);
        }
        printf(" in %.2fs (expected %.2fs–%.2fs)", elapsed_sec, min_expected_sec, max_expected_sec);

        result = elapsed_sec < min_expected_sec || elapsed_sec > (max_expected_sec);
    }

    printf(" ..... %s\x1B[0m.\n", result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}

void segv(int sig)
{
    printf("\x1B[31m(SEGV)\n\x1B[0m");
    exit(1);
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char **argv)
{
    int i, j, result = 0, config_start = 0, config_stop = NUM_TESTS;
    char *iface = 0;
    mpr_graph g;

    /* process flags for -v verbose, -t terminate, -h help */
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("test_map_timed.c: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-s shared (use one mpr_graph only), "
                               "-h help, "
                               "--iface network interface, "
                               "--config specify a configuration to run (1-%d)\n", NUM_TESTS);
                        return 1;
                        break;
                    case 'f':
                        period = 50;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    case 's':
                        shared_graph = 1;
                        break;
                    case '-':
                        if (strcmp(argv[i], "--iface")==0 && argc>i+1) {
                            ++i;
                            iface = argv[i];
                            j = len;
                        }
                        else if (strcmp(argv[i], "--config")==0 && argc>i+1) {
                            i++;
                            config_start = atoi(argv[i]);
                            if (config_start > 0 && config_start <= NUM_TESTS) {
                                config_stop = config_start;
                                --config_start;
                            }
                            else {
                                printf("config start argument must be between 1 and %d\n", NUM_TESTS);
                                return 1;
                            }
                            if (i + 1 < argc) {
                                if (strcmp(argv[i + 1], "...")==0) {
                                    config_stop = NUM_TESTS;
                                    ++i;
                                }
                                else if (isdigit(argv[i + 1][0])) {
                                    config_stop = atoi(argv[i + 1]);
                                    if (config_stop <= config_start || config_stop > NUM_TESTS) {
                                        printf("config stop argument must be between config start and %d\n", NUM_TESTS);
                                        return 1;
                                    }
                                    ++i;
                                }
                            }
                        }
                        j = len;
                        break;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGSEGV, segv);
    signal(SIGINT, ctrlc);

    g = shared_graph ? mpr_graph_new(0) : NULL;

    if (setup_dst(g, iface)) {
        eprintf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_src(g, iface)) {
        eprintf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    if (wait_ready()) {
        eprintf("Device registration aborted.\n");
        result = 1;
        goto done;
    }

    current_config = config_start;
    while (!done && current_config < config_stop) {
        test_config *config = &test_configs[current_config];
        if (run_test(config)) {
            result = 1;
            break;
        }
        ++current_config;
    }

    if (autoconnect && result) {
        result = 1;
    }

  done:
    cleanup_dst();
    cleanup_src();
    if (g) mpr_graph_free(g);
    printf("..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
