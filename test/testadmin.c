
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

mapper_admin my_admin = NULL;
mapper_device my_device = NULL;

int test_admin()
{
    int error = 0, wait;

    my_admin = mapper_admin_new(0, 0, 0);
    if (!my_admin) {
        printf("Error creating admin structure.\n");
        return 1;
    }

    printf("Admin structure initialized.\n");

    my_device = mdev_new("tester", 0, my_admin);
    if (!my_device) {
        printf("Error creating device structure.\n");
        return 1;
    }

    printf("Device structure initialized.\n");

    mapper_interface iface = my_admin->interfaces;
    while (iface) {
        printf("Found interface %s with IP %s and status %d\n", iface->name,
               inet_ntoa(iface->ip), iface->status);
        iface = iface->next;
    }

    while (!my_device->registered) {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    printf("Using port %d.\n", my_device->props.port);
    printf("Allocated ordinal %d.\n", my_device->ordinal.value);

    printf("Delaying for 5 seconds..\n");
    wait = 500;
    while (wait-- >= 0) {
        usleep(10000);
        mapper_admin_poll(my_admin);
    }

    mdev_free(my_device);
    printf("Device structure freed.\n");
    mapper_admin_free(my_admin);
    printf("Admin structure freed.\n");

    return error;
}

int main()
{
    int result = test_admin();
    if (result) {
        printf("Test FAILED.\n");
        return 1;
    }

    printf("Test PASSED.\n");
    return 0;
}
