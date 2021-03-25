#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id id, int len, mpr_type type,
			 const void *val, mpr_time t)
{
	if (val)
	{
		// printf("Num Inst: %d\n", mpr_obj_get_prop_as_int32(sig, MPR_PROP_NUM_INST, 0));
		// printf("Inst ID: %d\tName: %s\n", (int)id, mpr_sig_get_inst_name(sig, id));

		printf("Got value: %d\n\n", *((int *)val));
	}
}

int main(int argc, char **argv)
{
	int result = 0;
	printf("Begin Test\n\n");
	goto done; // TODO: Make this a properly formed test.

	/* Create a source and a destination for signals to be added to */
	mpr_dev src = mpr_dev_new("src", 0);
	mpr_dev dst = mpr_dev_new("dst", 0);

	int ni = 5; // Number of instances to reserve on the destination.

	mpr_sig sig_out = mpr_sig_new(src, MPR_DIR_OUT, "out-sig", 1, MPR_INT32, "none", 0, 0, 0, 0, 0);
	mpr_sig sig_in = mpr_sig_new(dst, MPR_DIR_IN, "in-sig", 1, MPR_INT32, "none", 0, 0, 0, handler, MPR_SIG_UPDATE);

	char *names[] = {"Thumb", "Index", "Middle", "Ring", "Pinky"}; // Five elements
	int num_inst = sizeof(names) / sizeof(names[0]);			   // 5

	char *names2[] = {"Matthew", "Stuart", "Peachey"};	// 3 elements
	int num_inst2 = sizeof(names2) / sizeof(names2[0]); // 5

	/* Reserve Named Instances on both ends of the map */
	mpr_sig_reserve_named_inst(sig_out, names, num_inst, 0);
	mpr_sig_reserve_named_inst(sig_in, names2, num_inst2, 0);

	while (!mpr_dev_get_is_ready(src) && !mpr_dev_get_is_ready(dst))
	{
		mpr_dev_poll(src, 0);
		mpr_dev_poll(dst, 0);
	}

	mpr_map map = mpr_map_new(1, &sig_out, 1, &sig_in);
	mpr_obj_push(map);

	while (!mpr_map_get_is_ready(map))
	{
		mpr_dev_poll(src, 10);
		mpr_dev_poll(dst, 10);
	}

	printf("Names of Instances (sig_out):\n");
	for (int i = 0; i < ni; i++)
	{
		printf("Inst %d: %s\n", i, mpr_sig_get_inst_name(sig_out, i));
	}

	printf("\n----- GO! -----\n\n");

	int next_id = 0;
	int val = 50;
	while (1)
	{
		mpr_dev_poll(src, 500);
		mpr_dev_poll(dst, 500);

		// mpr_sig_set_value(sig_out, &next_id, 1, MPR_INT32, &next_id);
		mpr_sig_set_named_inst_value(sig_out, names[next_id], 1, MPR_INT32, &val);

		next_id++;
		if (next_id > 4)
		{
			next_id = 0;
		}
	}

	printf("\nEnd Test\n");

	return 0;

done:
	printf("...................Test %s\x1B[0m.\n",
		   result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
	return result;
}
