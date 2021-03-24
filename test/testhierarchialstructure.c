#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

/**\
 * 
 * 
 * NOTE: This test is a BAREBONES tests and does not yet cover full functionality.
 * 
 * TODO: Update this test to ensure full coverage.
 * 
 * 
*/

void handler(mpr_sig sig, mpr_sig_evt evt, mpr_id id, int len, mpr_type type,
             const void *val, mpr_time t)
{
    if (val)
    {
        const char *name = mpr_obj_get_prop_as_str(sig, MPR_PROP_NAME, NULL);
        printf("Name: %s\n", name);
    }
}

int main(int argc, char **argv)
{
    printf("Beginning Test...\n");

    mpr_obj parent = (mpr_obj)mpr_dev_new("test-parent", 0);

    mpr_obj child = mpr_obj_add_child(parent, "test-child", 0);
    mpr_obj child2 = mpr_obj_add_child(child, "test-grandchild", 0);

    float mnf[] = {0, 0, 0}, mxf[] = {1, 1, 1};
    
    mpr_sig s = mpr_sig_new(child2, MPR_DIR_IN, "insig", 1, MPR_FLT, NULL,
                            mnf, mxf, NULL, handler, MPR_SIG_UPDATE);

    mpr_sig s2 = mpr_sig_new(parent, MPR_DIR_OUT, "outsig", 1, MPR_FLT, NULL,
                            mnf, mxf, NULL, handler, MPR_SIG_UPDATE);

    while (1){
        mpr_dev_poll(parent, 0);
    }

    printf("Ending Test!\n");
}
