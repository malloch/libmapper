#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int verbose = 1;

static void eprintf(const char *format, ...)
{
    va_list args;
    if (!verbose)
        return;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id id, int len, mpr_type type,
			 const void *val, mpr_time t)
{
    eprintf("Inst Id: %d, Name: %s ", (int)id, mpr_sig_get_inst_name(sig, id));
	if (val)
		eprintf("Got value: %d\n", *((int *)val));
    else
        eprintf("Released\n");
}

int main(int argc, char **argv)
{
	int result = 0;

	/* Create a source and a destination for signals to be added to */
	mpr_dev src = mpr_dev_new("testnamedinstance-send", 0);
	mpr_dev dst = mpr_dev_new("testnamedinstance-recv", 0);

	int ni = 5; /* Number of instances to reserve on the destination. */

	mpr_sig finger_sig = mpr_sig_new((mpr_obj)src, MPR_DIR_OUT, "finger",
                                     1, MPR_INT32, "none", 0, 0, 0, 0, 0);
	mpr_sig appliance_sig = mpr_sig_new((mpr_obj)dst, MPR_DIR_IN, "appliance", 1, MPR_INT32,
                                        "none", 0, 0, 0, handler, MPR_SIG_UPDATE);

    /* Reserve named instances */
	const char *finger_names[] = {"thumb", "index", "middle", "ring"};
    mpr_sig_reserve_inst(finger_sig, 4, 0, finger_names, 0);

    /* Try reserving another named instance */
    finger_names[0] = "pinky";
    mpr_sig_reserve_inst(finger_sig, 1, 0, finger_names, 0);

	const char *appliance_names[] = {"Matthew", "Stuart", "Peachey"};
	mpr_sig_reserve_inst(appliance_sig, 3, 0, appliance_names, 0);

	while (!mpr_dev_get_is_ready(src) && !mpr_dev_get_is_ready(dst)) {
		mpr_dev_poll(src, 0);
		mpr_dev_poll(dst, 0);
	}

    /* Anonymous instance mapping */
	mpr_map map = mpr_map_new(1, &finger_sig, 1, &appliance_sig);
	mpr_obj_push((mpr_obj)map);

	while (!mpr_map_get_is_ready(map)) {
		mpr_dev_poll(src, 10);
		mpr_dev_poll(dst, 10);
	}

	eprintf("Names of Instances (finger):\n");
	for (int i = 0; i < ni; i++) {
		eprintf("Inst %d: %s\n", i, mpr_sig_get_inst_name(finger_sig, i));
	}

	printf("\n----- GO! -----\n\n");

	int next_id = 0;
	int val = 50;
	while (1) {
		mpr_dev_poll(src, 500);
		mpr_dev_poll(dst, 500);

		// mpr_sig_set_value(sig_out, &next_id, 1, MPR_INT32, &next_id);
		mpr_sig_set_named_inst_value(finger_sig, finger_names[next_id], 1, MPR_INT32, &val);

		next_id++;
		if (next_id > 4) {
			next_id = 0;
		}
	}

	return 0;

	printf("...................Test %s\x1B[0m.\n",
		   result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
	return result;
}
