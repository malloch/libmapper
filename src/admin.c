
#include "config.h"

#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <zlib.h>

#ifdef HAVE_GETIFADDRS
 #include <ifaddrs.h>
 #include <net/if.h>
#endif

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#else
 #ifdef HAVE_WINSOCK2_H
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
 #endif
#endif

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

/*! Internal function to get the current time. */
static double get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

/* Note: any call to liblo where get_liblo_error will be called
 * afterwards must lock this mutex, otherwise there is a race
 * condition on receiving this information.  Could be fixed by the
 * liblo error handler having a user context pointer. */
static int liblo_error_num = 0;
static void liblo_error_handler(int num, const char *msg, const char *path)
{
    liblo_error_num = num;
    if (num == LO_NOPORT) {
        trace("liblo could not start a server because port unavailable\n");
    } else
        fprintf(stderr, "[libmapper] liblo server error %d in path %s: %s\n",
                num, path, msg);
}

const char* admin_msg_strings[] =
{
    "/connect",                 /* ADM_CONNECT */
    "/connectTo",               /* ADM_CONNECT_TO */
    "/connected",               /* ADM_CONNECTED */
    "/connection/modify",       /* ADM_CONNECTION_MODIFY */
    "/device",                  /* ADM_DEVICE */
    "/disconnect",              /* ADM_DISCONNECT */
    "/disconnected",            /* ADM_DISCONNECTED */
    "/link",                    /* ADM_LINK */
    "/link/modify",             /* ADM_LINK_MODIFY */
    "/linkTo",                  /* ADM_LINK_TO */
    "/linked",                  /* ADM_LINKED */
    "/logout",                  /* ADM_LOGOUT */
    "/signal",                  /* ADM_SIGNAL */
    "/input",                   /* ADM_INPUT */
    "/output",                  /* ADM_OUTPUT */
    "/input/removed",           /* ADM_INPUT_SIGNAL_REMOVED */
    "/output/removed",          /* ADM_OUTPUT_SIGNAL_REMOVED */
    "%s/subscribe",             /* ADM_SUBSCRIBE */
    "%s/unsubscribe",           /* ADM_UNSUBSCRIBE */
    "/sync",                    /* ADM_SYNC */
    "/unlink",                  /* ADM_UNLINK */
    "/unlinked",                /* ADM_UNLINKED */
    "/who",                     /* ADM_WHO */
};

/* Internal functions for sending admin messages. */
static void mapper_admin_send_device(mapper_admin admin, mapper_device device);
static void mapper_admin_send_linked(mapper_admin admin, mapper_link link,
                                     int is_outgoing);
static void mapper_admin_send_connected(mapper_admin admin, mapper_link link,
                                        mapper_connection c, int index,
                                        int is_outgoing);

/* Internal message handler prototypes. */
static int handler_who(const char *, const char *, lo_arg **, int,
                       lo_message, void *);
static int handler_device(const char *, const char *, lo_arg **,
                          int, lo_message, void *);
static int handler_logout(const char *, const char *, lo_arg **,
                          int, lo_message, void *);
static int handler_device_subcribe(const char *, const char *, lo_arg **,
                                   int, lo_message, void *);
static int handler_device_unsubcribe(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_signal_info(const char *, const char *, lo_arg **,
                               int, lo_message, void *);
static int handler_input_signal_removed(const char *, const char *, lo_arg **,
                                        int, lo_message, void *);
static int handler_output_signal_removed(const char *, const char *, lo_arg **,
                                         int, lo_message, void *);
static int handler_device_name_probe(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_device_name_registered(const char *, const char *, lo_arg **,
                                          int, lo_message, void *);
static int handler_device_link(const char *, const char *, lo_arg **, int,
                               lo_message, void *);
static int handler_device_linkTo(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_linked(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_link_modify(const char *, const char *, lo_arg **, int,
                                      lo_message, void *);
static int handler_device_unlink(const char *, const char *, lo_arg **,
                                 int, lo_message, void *);
static int handler_device_unlinked(const char *, const char *, lo_arg **,
                                   int, lo_message, void *);
static int handler_signal_connect(const char *, const char *, lo_arg **,
                                  int, lo_message, void *);
static int handler_signal_connectTo(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_signal_connected(const char *, const char *, lo_arg **,
                                    int, lo_message, void *);
static int handler_signal_connection_modify(const char *, const char *,
                                            lo_arg **, int, lo_message, void *);
static int handler_signal_disconnect(const char *, const char *, lo_arg **,
                                     int, lo_message, void *);
static int handler_signal_disconnected(const char *, const char *, lo_arg **,
                                       int, lo_message, void *);
static int handler_sync(const char *, const char *,
                        lo_arg **, int, lo_message, void *);

/* Handler <-> Message relationships */
struct handler_method_assoc {
    int str_index;
    char *types;
    lo_method_handler h;
};

static struct handler_method_assoc device_bus_handlers[] = {
    {ADM_WHO,                   NULL,       handler_who},
    {ADM_LOGOUT,                NULL,       handler_logout},
    {ADM_SYNC,                  "siifiid",  handler_sync},
    {ADM_LINK,                  NULL,       handler_device_link},
    {ADM_LINK_TO,               NULL,       handler_device_linkTo},
    {ADM_LINKED,                NULL,       handler_device_linked},
    {ADM_LINK_MODIFY,           NULL,       handler_device_link_modify},
    {ADM_UNLINK,                NULL,       handler_device_unlink},
    {ADM_UNLINKED,              NULL,       handler_device_unlinked},
    {ADM_SUBSCRIBE,             NULL,       handler_device_subcribe},
    {ADM_UNSUBSCRIBE,           NULL,       handler_device_unsubcribe},
    {ADM_CONNECT,               NULL,       handler_signal_connect},
    {ADM_CONNECT_TO,            NULL,       handler_signal_connectTo},
    {ADM_CONNECTED,             NULL,       handler_signal_connected},
    {ADM_CONNECTION_MODIFY,     NULL,       handler_signal_connection_modify},
    {ADM_DISCONNECT,            "ss",       handler_signal_disconnect},
    {ADM_DISCONNECTED,          "ss",       handler_signal_disconnected},
};
const int N_DEVICE_BUS_HANDLERS =
    sizeof(device_bus_handlers)/sizeof(device_bus_handlers[0]);

static struct handler_method_assoc device_mesh_handlers[] = {
    {ADM_LINKED,                 NULL,      handler_device_linked},
    {ADM_UNLINKED,               NULL,      handler_device_unlinked},
    {ADM_SUBSCRIBE,              NULL,      handler_device_subcribe},
    {ADM_UNSUBSCRIBE,            NULL,      handler_device_unsubcribe},
    {ADM_CONNECT_TO,             NULL,      handler_signal_connectTo},
    {ADM_CONNECTED,              NULL,      handler_signal_connected},
    {ADM_DISCONNECTED,           "ss",      handler_signal_disconnected},
};
const int N_DEVICE_MESH_HANDLERS =
    sizeof(device_mesh_handlers)/sizeof(device_mesh_handlers[0]);

static struct handler_method_assoc monitor_bus_handlers[] = {
    {ADM_DEVICE,                NULL,       handler_device},
    {ADM_LOGOUT,                NULL,       handler_logout},
    {ADM_SYNC,                  "siifiid",  handler_sync},
};
const int N_MONITOR_BUS_HANDLERS =
    sizeof(monitor_bus_handlers)/sizeof(monitor_bus_handlers[0]);

static struct handler_method_assoc monitor_mesh_handlers[] = {
    {ADM_DEVICE,                NULL,       handler_device},
    {ADM_SIGNAL,                NULL,       handler_signal_info},
    {ADM_INPUT_REMOVED,         "s",        handler_input_signal_removed},
    {ADM_OUTPUT_REMOVED,        "s",        handler_output_signal_removed},
    {ADM_LINKED,                NULL,       handler_device_linked},
    {ADM_UNLINKED,              NULL,       handler_device_unlinked},
    {ADM_CONNECTED,             NULL,       handler_signal_connected},
    {ADM_DISCONNECTED,          "ss",       handler_signal_disconnected},
};
const int N_MONITOR_MESH_HANDLERS =
    sizeof(monitor_mesh_handlers)/sizeof(monitor_mesh_handlers[0]);

/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    printf("[libmapper] liblo server error %d in path %s: %s\n",
           num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If
 * check_collisions() returns 1, the resource in question should be
 * probed on the admin bus. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource);

//static int check_interfaces(mapper_admin admin)
//{
//    // if admin->preferred_iface == 0 try all interfaces
//
//    // check if new interface has been added
//    // check if interface has been removed
//        // are we linked to any devices through this interface?
//        // remove link
//    // return current number of interfaces
//}

/* Local function to get the IP addresses of network interfaces. If a network
 * interface is provided as an argument we will only use that interface,
 * otherwise we will try to listen on all available network interfaces */
static int check_interfaces(mapper_admin admin)
{
    int found = 0;
    struct in_addr zero;
    struct sockaddr_in *sa;

    *(unsigned int *)&zero = inet_addr("0.0.0.0");

#ifdef HAVE_GETIFADDRS

    struct ifaddrs *ifaphead;
    struct ifaddrs *ifap;
    struct ifaddrs *iflo=0;

    if (getifaddrs(&ifaphead) != 0)
        return 1;

    ifap = ifaphead;
    while (ifap) {
        sa = (struct sockaddr_in *) ifap->ifa_addr;
        if (!sa) {
            ifap = ifap->ifa_next;
            continue;
        }

        // Note, we could also check for IFF_MULTICAST-- however this
        // is the data-sending port, not the admin bus port.

        if (sa->sin_family == AF_INET
            && memcmp(&sa->sin_addr, &zero, sizeof(struct in_addr))!=0)
        {
            if ((!admin->force_interface
                 || strcmp(ifap->ifa_name, admin->force_interface)==0)
#ifdef HAVE_LIBLO_SET_IFACE
#ifdef HAVE_LIBLO_SERVER_IFACE
                ) {
#else
                && ifap->ifa_flags & IFF_UP) {
#endif
#else
                && ifap->ifa_flags & IFF_UP) {
#endif
                // Check if iface already in list
                mapper_interface iface = admin->interfaces;
                while (iface) {
                    if (strcmp(iface->name, ifap->ifa_name)==0)
                        break;
                    iface = iface->next;
                }
                if (!iface) {
                    printf("adding interface %s to list\n", ifap->ifa_name);
                    // Allocate a new mapper_interface_t struct
                    iface = (mapper_interface) malloc(sizeof(struct _mapper_interface));
                    iface->name = strdup(ifap->ifa_name);
                    sa = (struct sockaddr_in *) ifap->ifa_addr;
                    iface->ip = sa->sin_addr;
                    if (ifap->ifa_flags & IFF_UP)
                        iface->status = 0;
                    else
                        iface->status = -1;
                    iface->next = admin->interfaces;
                    admin->interfaces = iface;
                    found++;
                    if (admin->force_interface) {
                        freeifaddrs(ifaphead);
                        return found;
                    }
                }
#ifndef HAVE_LIBLO_SET_IFACE
                return found;
#endif
#ifndef HAVE_LIBLO_SERVER_IFACE
                return found;
#endif
            }
            if (ifap->ifa_flags & IFF_LOOPBACK)
                iflo = ifap;
        }
        ifap = ifap->ifa_next;
    }

    // Include loopback address in case user is working locally?
    if (!admin->force_interface || strcmp(admin->force_interface, "lo0")==0) {
        mapper_interface iface = admin->interfaces;
        while (iface) {
            if (strcmp(iface->name, "lo0")==0)
                break;
            iface = iface->next;
        }
        if (!iface) {
            // Allocate a new mapper_interface_t struct
            mapper_interface iface = (mapper_interface) malloc(sizeof(struct _mapper_interface));
            iface->name = strdup(iflo->ifa_name);
            sa = (struct sockaddr_in *) iflo->ifa_addr;
            iface->ip = sa->sin_addr;
            iface->status = 0;
            iface->next = admin->interfaces;
            admin->interfaces = iface;
            found++;
        }
    }

    freeifaddrs(ifaphead);
    return found;

#else // !HAVE_GETIFADDRS

#ifdef HAVE_LIBIPHLPAPI
    // TODO consider "pref" as well

    /* Start with recommended 15k buffer for GetAdaptersAddresses. */
    ULONG size = 15*1024/2;
    int tries = 3;
    PIP_ADAPTER_ADDRESSES paa = malloc(size*2);
    DWORD rc = ERROR_SUCCESS-1;
    while (rc!=ERROR_SUCCESS && paa && tries-- > 0) {
        size *= 2;
        paa = realloc(paa, size);
        rc = GetAdaptersAddresses(AF_INET, 0, 0, paa, &size);
    }
    if (rc!=ERROR_SUCCESS)
        return 0;

    PIP_ADAPTER_ADDRESSES loaa=0, aa = paa;
    PIP_ADAPTER_UNICAST_ADDRESS lopua=0;
    while (aa && rc==ERROR_SUCCESS) {
        PIP_ADAPTER_UNICAST_ADDRESS pua = aa->FirstUnicastAddress;
        // Skip adapters that are not "Up".
        if (pua) {
            if (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                loaa = aa;
                lopua = pua;
            }
            else {
                // Skip addresses starting with 0.X.X.X or 169.X.X.X.
                sa = (struct sockaddr_in *) pua->Address.lpSockaddr;
                unsigned char prefix = sa->sin_addr.s_addr&0xFF;
                if (prefix!=0xA9 && prefix!=0) {
                    // Allocate a new mapper_interface_t struct
                    mapper_interface iface = (mapper_interface) malloc(sizeof(struct _mapper_interface));
                    iface->name = strdup(aa->AdapterName);
                    iface->ip = sa->sin_addr;
                    if (aa->OperStatus == IfOperStatusUp)
                        iface->status = 0;
                    else
                        iface->status = -1;
                    iface->next = admin->interfaces;
                    admin->interfaces = iface;
                    found++;
                }
            }
        }
        aa = aa->Next;
    }

    if (loaa && lopua) {
        // Allocate a new mapper_interface_t struct
        mapper_interface iface = (mapper_interface) malloc(sizeof(struct _mapper_interface));
        iface->name = strdup(loaa->AdapterName);
        sa = (struct sockaddr_in *) lopua->Address.lpSockaddr;
        iface->ip = sa->sin_addr;
        iface->status = 0;
        iface->next = admin->interfaces;
        admin->interfaces = iface;
        return ++found;
    }

    if (paa) free(paa);

#else

  #error No known method on this system to get the network interface address.

#endif // HAVE_LIBIPHLPAPI
#endif // !HAVE_GETIFADDRS

    return found;
}

/*! A helper function to seed the random number generator. */
static void seed_srand()
{
    unsigned int s;

#ifndef WIN32
    FILE *f = fopen("/dev/random", "rb");
    if (f) {
        if (fread(&s, 4, 1, f)==1) {
            srand(s);
            fclose(f);
            return;
        }
        fclose(f);
    }
#endif

    double d = get_current_time();
    s = (unsigned int)((d-(unsigned long)d)*100000);
    srand(s);
}

static void mapper_admin_add_device_methods(mapper_admin admin,
                                            mapper_device device)
{
    int i;
    char fullpath[256];
    mapper_interface iface;
    for (i=0; i < N_DEVICE_BUS_HANDLERS; i++)
    {
        snprintf(fullpath, 256,
                 admin_msg_strings[device_bus_handlers[i].str_index],
                 mdev_name(admin->device));
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                lo_server_add_method(iface->bus_server, fullpath,
                                     device_bus_handlers[i].types,
                                     device_bus_handlers[i].h,
                                     admin);
            iface = iface->next;
        }
    }
    for (i=0; i < N_DEVICE_MESH_HANDLERS; i++)
    {
        snprintf(fullpath, 256,
                 admin_msg_strings[device_mesh_handlers[i].str_index],
                 mdev_name(admin->device));
        lo_server_add_method(admin->mesh_server, fullpath,
                             device_mesh_handlers[i].types,
                             device_mesh_handlers[i].h,
                             admin);
        // add them for bus also
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                lo_server_add_method(iface->bus_server, fullpath,
                                     device_mesh_handlers[i].types,
                                     device_mesh_handlers[i].h,
                                     admin);
            iface = iface->next;
        }
    }
}

static void mapper_admin_add_monitor_methods(mapper_admin admin)
{
    int i;
    mapper_interface iface;
    for (i=0; i < N_MONITOR_BUS_HANDLERS; i++)
    {
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                lo_server_add_method(iface->bus_server,
                                     admin_msg_strings[monitor_bus_handlers[i].str_index],
                                     monitor_bus_handlers[i].types,
                                     monitor_bus_handlers[i].h,
                                     admin);
            iface = iface->next;
        }
    }
    for (i=0; i < N_MONITOR_MESH_HANDLERS; i++)
    {
        lo_server_add_method(admin->mesh_server,
                             admin_msg_strings[monitor_mesh_handlers[i].str_index],
                             monitor_mesh_handlers[i].types,
                             monitor_mesh_handlers[i].h,
                             admin);
        // add them for bus also
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                lo_server_add_method(iface->bus_server,
                                     admin_msg_strings[monitor_mesh_handlers[i].str_index],
                                     monitor_mesh_handlers[i].types,
                                     monitor_mesh_handlers[i].h,
                                     admin);
            iface = iface->next;
        }
    }
}

static void mapper_admin_remove_monitor_methods(mapper_admin admin)
{
    int i;
    mapper_interface iface;
    for (i=0; i < N_MONITOR_BUS_HANDLERS; i++)
    {
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                lo_server_del_method(iface->bus_server,
                                     admin_msg_strings[monitor_bus_handlers[i].str_index],
                                     monitor_bus_handlers[i].types);
            iface = iface->next;
        }
    }
    for (i=0; i < N_MONITOR_MESH_HANDLERS; i++)
    {
        lo_server_del_method(admin->mesh_server,
                             admin_msg_strings[monitor_mesh_handlers[i].str_index],
                             monitor_mesh_handlers[i].types);
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                lo_server_del_method(iface->bus_server,
                                     admin_msg_strings[monitor_mesh_handlers[i].str_index],
                                     monitor_mesh_handlers[i].types);
            iface = iface->next;
        }
    }
}

mapper_admin mapper_admin_new(const char *iface_name, const char *group, int port)
{
    mapper_admin admin = (mapper_admin)calloc(1, sizeof(mapper_admin_t));
    if (!admin)
        return NULL;

    /* Default standard ip and port is group 224.0.1.3, port 7570 */
    char port_str[10], *s_port = port_str;
    if (!group) group = "224.0.1.3";
    if (port==0)
        s_port = "7570";
    else
        snprintf(port_str, 10, "%d", port);

    /* Initialize interface information. */
    // TODO: occasionally rescan interfaces to check if they are up
    if (iface_name)
        admin->force_interface = strdup(iface_name);
    if (!check_interfaces(admin))
        trace("no network interfaces found!\n");

    // TODO: move to function check_interfaces()
    mapper_interface iface = admin->interfaces;
    while (iface) {
        /* Open address */
        iface->bus_addr = lo_address_new(group, s_port);
        if (!iface->bus_addr) {
            iface->status = 0;
        }
        else {
            /* Set TTL for packet to 1 -> local subnet */
            lo_address_set_ttl(iface->bus_addr, 1);

            /* Specify the interface to use for multicasting */
#ifdef HAVE_LIBLO_SET_IFACE
            lo_address_set_iface(iface->bus_addr,
                                 iface->name, 0);
#endif

            /* Open server for multicast group 224.0.1.3, port 7570 */
            iface->bus_server =
#ifdef HAVE_LIBLO_SERVER_IFACE
                lo_server_new_multicast_iface(group, s_port,
                                              iface->name, 0,
                                              handler_error);
#else
                lo_server_new_multicast(group, s_port, handler_error);
#endif
            if (iface->bus_server) {
                // Disable liblo message queueing.
                lo_server_enable_queue(iface->bus_server, 0, 1);
            }
            else {
                lo_address_free(iface->bus_addr);
                iface->bus_addr = 0;
                iface->status = 0;
            }
        }
        iface = iface->next;
    }

    // Also open address/server for mesh-style admin communications
    // TODO: use TCP instead?
    while (!(admin->mesh_server = lo_server_new(0, liblo_error_handler))) {}

    // Disable liblo message queueing.
    lo_server_enable_queue(admin->mesh_server, 0, 1);

    return admin;
}

void mapper_admin_send_bundle(mapper_admin admin)
{
    if (!admin->bundle)
        return;
    if (admin->bundle_dest == (void*)-1) {
        mapper_admin_subscriber *s = &admin->subscribers;
        if (*s) {
            mapper_clock_now(&admin->clock, &admin->clock.now);
        }
        while (*s) {
            if ((*s)->lease_expiration_sec < admin->clock.now.sec || !(*s)->flags) {
                // subscription expired, remove from subscriber list
                mapper_admin_subscriber temp = *s;
                *s = temp->next;
                if (temp->address)
                    lo_address_free(temp->address);
                free(temp);
                continue;
            }
            if ((*s)->flags & admin->message_type) {
                lo_send_bundle_from((*s)->address, admin->mesh_server, admin->bundle);
            }
            s = &(*s)->next;
        }
    }
    else if (admin->bundle_dest == 0) {
        // send on all active interfaces
        mapper_interface iface = admin->interfaces;
        while (iface) {
            if (iface->bus_addr)
                lo_send_bundle_from(iface->bus_addr, admin->mesh_server,
                                    admin->bundle);
            iface = iface->next;
        }
    }
    else {
        lo_send_bundle_from(admin->bundle_dest, admin->mesh_server, admin->bundle);
    }
    lo_bundle_free_messages(admin->bundle);
    admin->bundle = 0;
}

static int mapper_admin_init_bundle(mapper_admin admin)
{
    if (admin->bundle)
        mapper_admin_send_bundle(admin);

    mapper_clock_now(&admin->clock, &admin->clock.now);
    admin->bundle = lo_bundle_new(admin->clock.now);
    if (!admin->bundle) {
        trace("couldn't allocate lo_bundle\n");
        return 1;
    }

    return 0;
}

void mapper_admin_set_bundle_dest_bus(mapper_admin admin)
{
    if (admin->bundle && admin->bundle_dest != 0)
        mapper_admin_send_bundle(admin);
    admin->bundle_dest = 0;
    if (!admin->bundle)
        mapper_admin_init_bundle(admin);
}

void mapper_admin_set_bundle_dest_mesh(mapper_admin admin, lo_address address)
{
    if (admin->bundle && admin->bundle_dest != address)
        mapper_admin_send_bundle(admin);
    admin->bundle_dest = address;
    if (!admin->bundle)
        mapper_admin_init_bundle(admin);
}

void mapper_admin_set_bundle_dest_subscribers(mapper_admin admin, int type)
{
    if ((admin->bundle && admin->bundle_dest != (void*)-1) ||
        (admin->message_type != type))
        mapper_admin_send_bundle(admin);
    admin->bundle_dest = (void*)-1;
    admin->message_type = type;
    if (!admin->bundle)
        mapper_admin_init_bundle(admin);
}

/*! Free the memory allocated by a mapper admin structure.
 *  \param admin An admin structure handle.
 */
void mapper_admin_free(mapper_admin admin)
{
    if (!admin)
        return;

    // send out any cached messages
    mapper_admin_send_bundle(admin);

    mapper_interface iface = admin->interfaces;
    while (iface) {
        mapper_interface next = iface->next;
        if (iface->name)
            free(iface->name);
        if (iface->bus_server)
            lo_server_free(iface->bus_server);
        if (iface->bus_addr)
            lo_address_free(iface->bus_addr);
        free(iface);
        iface = next;
    }

    if (admin->force_interface)
        free(admin->force_interface);

    if (admin->mesh_server)
        lo_server_free(admin->mesh_server);

    mapper_admin_subscriber s;
    while (admin->subscribers) {
        s = admin->subscribers;
        if (s->address)
            lo_address_free(s->address);
        admin->subscribers = s->next;
        free(s);
    }

    free(admin);
}

/*! Add an uninitialized device to this admin. */
void mapper_admin_add_device(mapper_admin admin, mapper_device dev)
{
    /* Initialize data structures */
    if (dev)
    {
        admin->device = dev;

        // TODO: should we init clocks for monitors also?
        mapper_clock_init(&admin->clock);

        /* Seed the random number generator. */
        seed_srand();

        /* Choose a random ID for allocation speedup */
        admin->random_id = rand();

        /* Add methods for admin bus.  Only add methods needed for
         * allocation here. Further methods are added when the device is
         * registered. */
        mapper_interface iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server) {
                lo_server_add_method(iface->bus_server, "/name/probe", NULL,
                                     handler_device_name_probe, admin);
                lo_server_add_method(iface->bus_server, "/name/registered", NULL,
                                     handler_device_name_registered, admin);
            }
            iface = iface->next;
        }

        /* Probe potential name to admin bus. */
        mapper_admin_probe_device_name(admin, dev);
    }
}

/*! Add an uninitialized monitor to this admin. */
void mapper_admin_add_monitor(mapper_admin admin, mapper_monitor mon)
{
    /* Initialize monitor methods. */
    if (mon) {
        admin->monitor = mon;
        mapper_admin_add_monitor_methods(admin);
        mapper_admin_set_bundle_dest_bus(admin);
        mapper_admin_bundle_message(admin, ADM_WHO, 0, "");
    }
}

void mapper_admin_remove_monitor(mapper_admin admin, mapper_monitor mon)
{
    if (mon) {
        admin->monitor = 0;
        mapper_admin_remove_monitor_methods(admin);
    }
}

static void mapper_admin_maybe_send_ping(mapper_admin admin, int force)
{
    mapper_clock_t *clock = &admin->clock;
    mapper_clock_now(clock, &clock->now);
    if (force || (clock->now.sec >= clock->next_ping)) {
        lo_bundle b = lo_bundle_new(clock->now);
        lo_message m = lo_message_new();
        if (m) {
            lo_message_add_string(m, mdev_name(admin->device));
            lo_message_add_int32(m, admin->device->props.version);
            lo_message_add_int32(m, clock->message_id);
            lo_message_add_float(m, clock->confidence);
            lo_message_add_int32(m, clock->remote.device_id);
            lo_message_add_int32(m, clock->remote.message_id);
            lo_message_add_double(m, clock->remote.device_id ?
                                  mapper_timetag_difference(clock->now,
                                                            clock->remote.timetag) : 0);
            lo_bundle_add_message(b, "/sync", m);

            mapper_interface iface = admin->interfaces;
            while (iface) {
                // TODO: send different rates
                if (iface->bus_addr)
                    lo_send_bundle(iface->bus_addr, b);
                iface = iface->next;
            }

            clock->local[clock->local_index].message_id = clock->message_id;
            clock->local[clock->local_index].timetag.sec = clock->now.sec;
            clock->local[clock->local_index].timetag.frac = clock->now.frac;
            clock->local_index = (clock->local_index + 1) % 10;
            clock->message_id = (clock->message_id + 1) % 10;
            clock->next_ping = clock->now.sec + 5 + (rand() % 5);
        }
        lo_bundle_free_messages(b);
    }
}

/*! This is the main function to be called once in a while from a
 *  program so that the admin bus can be automatically managed.
 */
int mapper_admin_poll(mapper_admin admin)
{
    int count = 0, recvd = 1, status;
    mapper_device md = admin->device;

    // send out any cached messages
    mapper_admin_send_bundle(admin);

    if (md)
        md->flags &= ~FLAGS_SENT_ALL_DEVICE_MESSAGES;

//    while (count < 10 && (lo_server_recv_noblock(admin->bus_server, 0)
//           + lo_server_recv_noblock(admin->mesh_server, 0))) {
//        count++;
//    }
    // TODO: need to use select() here instead for efficiency
    mapper_interface iface;
    while (count < 10 && recvd) {
        recvd = 0;
        iface = admin->interfaces;
        while (iface) {
            if (iface->bus_server)
                recvd += lo_server_recv_noblock(iface->bus_server, 0);
            iface = iface->next;
        }
        recvd += lo_server_recv_noblock(admin->mesh_server, 0);
        count++;
    }

    if (!md)
        return count;

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!md->registered) {
        status = check_collisions(admin, &md->ordinal);
        if (status == 1) {
            /* If the ordinal has changed, re-probe the new name. */
            mapper_admin_probe_device_name(admin, md);
        }

        /* If we are ready to register the device, add the needed message
         * handlers. */
        if (md->ordinal.locked)
        {
            mdev_registered(md);

            /* Send registered msg. */
            iface = admin->interfaces;
            while (iface) {
                if (iface->bus_server)
                    lo_send(iface->bus_addr, "/name/registered",
                            "s", mdev_name(md));
                iface = iface->next;
            }

            mapper_admin_add_device_methods(admin, md);
            mapper_admin_maybe_send_ping(admin, 1);

            trace("</%s.?::%p> registered as <%s>\n",
                  md->props.identifier, admin, mdev_name(md));
            md->flags |= FLAGS_DEVICE_ATTRIBS_CHANGED;
        }
    }
    else {
        // Send out clock sync messages occasionally
        mapper_admin_maybe_send_ping(admin, 0);
    }
    return count;
}

/*! Probe the admin bus to see if a device's proposed name.ordinal is
 *  already taken.
 */
void mapper_admin_probe_device_name(mapper_admin admin, mapper_device device)
{
    device->ordinal.collision_count = 0;
    device->ordinal.count_time = get_current_time();

    /* Note: mdev_name() would refuse here since the
     * ordinal is not yet locked, so we have to build it manually at
     * this point. */
    char name[256];
    trace("</%s.?::%p> probing name\n", device->props.identifier, admin);
    snprintf(name, 256, "/%s.%d", device->props.identifier, device->ordinal.value);

    /* Calculate a hash from the name and store it in id.value */
    device->props.name_hash = crc32(0L, (const Bytef *)name, strlen(name));

    /* For the same reason, we can't use mapper_admin_send()
     * here. */
    mapper_interface iface = admin->interfaces;
    while (iface) {
        if (iface->bus_addr) {
            lo_send(iface->bus_addr, "/name/probe", "si",
                    name, admin->random_id);
            device->ordinal.collision_count--;
        }
        iface = iface->next;
    }
    printf(">> STARTING WITH COLLISION_COUNT %d\n", device->ordinal.collision_count);
}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource)
{
    double timediff;

    if (resource->locked)
        return 0;

    timediff = get_current_time() - resource->count_time;

    if (timediff >= 2.0 && resource->collision_count <= 1) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(admin->device, resource);
        return 2;
    }

    else if (timediff >= 0.5 && resource->collision_count > 0) {
        /* If resource collisions were found within 500 milliseconds of the
         * last probe, add a random number based on the number of
         * collisions. */
        resource->value += rand() % (resource->collision_count + 1);

        /* Prepare for causing new resource collisions. */
        resource->collision_count = -1;
        resource->count_time = get_current_time();

        /* Indicate that we need to re-probe the new value. */
        return 1;
    }

    return 0;
}

void _real_mapper_admin_bundle_message(mapper_admin admin,
                                       int msg_index,
                                       const char *path,
                                       const char *types, ...)
{
    char t[]=" ";

    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    va_list aq;
    va_start(aq, types);

    while (types && *types) {
        t[0] = types[0];
        switch (t[0]) {
        case 'i': lo_message_add(m, t, va_arg(aq, int)); break;
        case 's': lo_message_add(m, t, va_arg(aq, char*)); break;
        case 'f': lo_message_add(m, t, va_arg(aq, double)); break;
        default:
            die_unless(0, "message %s, unknown type '%c'\n",
                       path, t[0]);
        }
        types++;
    }

    mapper_msg_prepare_varargs(m, aq);

    va_end(aq);

    lo_bundle_add_message(admin->bundle, msg_index == -1 ?
                          path : admin_msg_strings[msg_index], m);

    /* Since liblo doesn't cache path strings, we must send the bundle
     * immediately if the path is non-standard. */
    if (msg_index == -1)
        mapper_admin_send_bundle(admin);
}

void _real_mapper_admin_bundle_message_with_params(mapper_admin admin,
                                                   mapper_message_t *params,
                                                   mapper_string_table_t *extra,
                                                   int msg_index,
                                                   const char *path,
                                                   const char *types, ...)
{
    char t[]=" ";

    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    va_list aq;
    va_start(aq, types);

    while (types && *types) {
        t[0] = types[0];
        switch (t[0]) {
            case 'i': lo_message_add(m, t, va_arg(aq, int)); break;
            case 's': lo_message_add(m, t, va_arg(aq, char*)); break;
            case 'f': lo_message_add(m, t, va_arg(aq, double)); break;
            default:
                die_unless(0, "message %s, unknown type '%c'\n",
                           path, t[0]);
        }
        types++;
    }

    mapper_msg_prepare_varargs(m, aq);

    va_end(aq);

    mapper_msg_prepare_params(m, params);
    if (extra)
        mapper_msg_add_value_table(m, extra);

    lo_bundle_add_message(admin->bundle, msg_index == -1 ?
                          path : admin_msg_strings[msg_index], m);

    /* Since liblo doesn't cache path strings, we must send the bundle
     * immediately if the path is non-standard. */
    if (msg_index == -1)
        mapper_admin_send_bundle(admin);
}

static void mapper_admin_send_device(mapper_admin admin,
                                     mapper_device device)
{
    if (!device)
        return;
    if (device->flags & FLAGS_SENT_DEVICE_INFO)
        return;

    mapper_admin_bundle_message(
        admin, ADM_DEVICE, 0, "s", mdev_name(device),
        AT_LIB_VERSION, PACKAGE_VERSION,
        AT_PORT, device->props.port,
        AT_NUM_INPUTS, mdev_num_inputs(device),
        AT_NUM_OUTPUTS, mdev_num_outputs(device),
        AT_NUM_LINKS_IN, mdev_num_links_in(device),
        AT_NUM_LINKS_OUT, mdev_num_links_out(device),
        AT_NUM_CONNECTIONS_IN, mdev_num_connections_in(device),
        AT_NUM_CONNECTIONS_OUT, mdev_num_connections_out(device),
        AT_REV, device->props.version,
        AT_EXTRA, device->props.extra);

    device->flags |= FLAGS_SENT_DEVICE_INFO;
}

void mapper_admin_send_signal(mapper_admin admin, mapper_device md,
                              mapper_signal sig)
{
    char sig_name[1024];
    msig_full_name(sig, sig_name, 1024);
    mapper_admin_bundle_message(
        admin, ADM_SIGNAL, 0, "s", sig_name,
        AT_DIRECTION, sig->props.is_output ? "output" : "input",
        AT_TYPE, sig->props.type,
        AT_LENGTH, sig->props.length,
        sig->props.unit ? AT_UNITS : -1, sig,
        sig->props.minimum ? AT_MIN : -1, sig,
        sig->props.maximum ? AT_MAX : -1, sig,
        sig->props.num_instances > 1 ? AT_INSTANCES : -1, sig,
        sig->props.rate ? AT_RATE : -1, sig,
        AT_EXTRA, sig->props.extra);
}

void mapper_admin_send_signal_removed(mapper_admin admin, mapper_device md,
                                      mapper_signal sig)
{
    char sig_name[1024];
    msig_full_name(sig, sig_name, 1024);
    mapper_admin_bundle_message(admin, sig->props.is_output ?
                                ADM_OUTPUT_REMOVED : ADM_INPUT_REMOVED,
                                0, "s", sig_name);
}

static void mapper_admin_send_inputs(mapper_admin admin, mapper_device md,
                                     int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > md->props.n_inputs)
        return;
    if (max < 0 || max > md->props.n_inputs)
        max = md->props.n_inputs-1;

    int i = min;
    for (; i <= max; i++)
        mapper_admin_send_signal(admin, md, md->inputs[i]);
}

static void mapper_admin_send_outputs(mapper_admin admin, mapper_device md,
                                      int min, int max)
{
    if (min < 0)
        min = 0;
    else if (min > md->props.n_outputs)
        return;
    if (max < 0 || max > md->props.n_outputs)
        max = md->props.n_outputs-1;

    int i = min;
    for (; i <= max; i++)
        mapper_admin_send_signal(admin, md, md->outputs[i]);
}

static void mapper_admin_send_linked(mapper_admin admin,
                                     mapper_link link,
                                     int is_outgoing)
{
    // Send /linked message
    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    if (is_outgoing) {
        lo_message_add_string(m, mdev_name(link->device));
        lo_message_add_string(m, link->props.dest_name);
        lo_message_add_string(m, "@srcPort");
        lo_message_add_int32(m, link->device->props.port);
        lo_message_add_string(m, "@destPort");
        lo_message_add_int32(m, link->props.dest_port);
    }
    else {
        lo_message_add_string(m, link->props.src_name);
        lo_message_add_string(m, mdev_name(link->device));
        lo_message_add_string(m, "@srcPort");
        lo_message_add_int32(m, link->props.src_port);
        lo_message_add_string(m, "@destPort");
        lo_message_add_int32(m, link->device->props.port);
    }

    mapper_link_prepare_osc_message(m, link);

    lo_bundle_add_message(admin->bundle, "/linked", m);
}

static void mapper_admin_send_links_in(mapper_admin admin, mapper_device md)
{
    mapper_receiver r = md->receivers;
    while (r) {
        mapper_admin_send_linked(admin, r, 0);
        r = r->next;
    }
}

static void mapper_admin_send_links_out(mapper_admin admin, mapper_device md)
{
    mapper_router r = md->routers;
    while (r) {
        mapper_admin_send_linked(admin, r, 1);
        r = r->next;
    }
}

static void mapper_admin_send_connected(mapper_admin admin,
                                        mapper_link link,
                                        mapper_connection c,
                                        int index,
                                        int is_outgoing)
{
    // Send /connected message
    lo_message m = lo_message_new();
    if (!m) {
        trace("couldn't allocate lo_message\n");
        return;
    }

    char src_name[1024], dest_name[1024];

    snprintf(src_name, 1024, "%s%s", is_outgoing ?
             mdev_name(link->device) : link->props.src_name,
             c->props.src_name);

    snprintf(dest_name, 1024, "%s%s", is_outgoing ?
             link->props.dest_name : mdev_name(link->device),
             c->props.dest_name);

    lo_message_add_string(m, src_name);
    lo_message_add_string(m, dest_name);

    if (index != -1) {
        lo_message_add_string(m, "@ID");
        lo_message_add_int32(m, index);
    }

    mapper_connection_prepare_osc_message(m, c);

    lo_bundle_add_message(admin->bundle, "/connected", m);
}

static void mapper_admin_send_connections_in(mapper_admin admin,
                                             mapper_device md,
                                             int min, int max)
{
    // TODO: allow requesting connections by parent link?
    int i = 0;
    mapper_receiver rc = md->receivers;
    while (rc) {
        mapper_receiver_signal rs = rc->signals;
        while (rs) {
			mapper_connection c = rs->connections;
            while (c) {
                if (max > 0 && i > max)
                    break;
                if (i >= min) {
                    mapper_admin_send_connected(admin, rc, c, i, 0);
                }
                c = c->next;
                i++;
            }
            rs = rs->next;
        }
        rc = rc->next;
    }
}

static void mapper_admin_send_connections_out(mapper_admin admin,
                                              mapper_device md,
                                              int min, int max)
{
    // TODO: allow requesting connections by parent link?
    int i = 0;
    mapper_router rt = md->routers;
    while (rt) {
        mapper_router_signal rs = rt->signals;
        while (rs) {
			mapper_connection c = rs->connections;
            while (c) {
                if (max > 0 && i > max)
                    break;
                if (i >= min) {
                    mapper_admin_send_connected(admin, rt, c, i, 1);
                }
                c = c->next;
                i++;
            }
            rs = rs->next;
        }
        rt = rt->next;
    }
}

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the basic device information. */
static int handler_who(const char *path, const char *types, lo_arg **argv,
                       int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_admin_maybe_send_ping(admin, 1);

    trace("%s received /who\n", mdev_name(admin->device));

    return 0;
}

/*! Register information about port and host for the device. */
static int handler_device(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    const char *name = &argv[0]->s;

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[1],
                            argc-1, &argv[1]);

    if (params.types[AT_IP]==0 || params.values[AT_IP]==0) {
        // Find the sender's hostname
        lo_address a = lo_message_get_source(msg);
        if (a) {
            const char *host = lo_address_get_hostname(a);
            if (host) {
                params.types[AT_IP] = types;  // 's'
                params.values[AT_IP] = (lo_arg**)&host;
                params.lengths[AT_IP] = 1;
            }
        }
    }

    mapper_db_add_or_update_device_params(db, name, &params);

    return 0;
}

/*! Respond to /logout by deleting record of device. */
static int handler_logout(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);
    int diff, ordinal;
    char *s;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    char *name = &argv[0]->s;

    trace("got /logout %s\n", name);

    if (mon) {
        mapper_db_remove_device_by_name(db, name);
    }

    // If device exists and is registered
    if (md && md->ordinal.locked) {
        /* Parse the ordinal from the complete name which is in the
         * format: /<name>.<n> */
        s = name;
        if (*s++ != '/')
            return 0;
        while (*s != '.' && *s++) {
        }
        ordinal = atoi(++s);

        // If device name matches
        strtok(name, ".");
        name++;
        if (strcmp(name, md->props.identifier) == 0) {
            // if registered ordinal is within my block, free it
            diff = ordinal - md->ordinal.value;
            if (diff > 0 && diff < 9) {
                md->ordinal.suggestion[diff-1] = 0;
            }
        }
    }

    return 0;
}

// Add/renew/remove a monitor subscription.
static void mapper_admin_manage_subscriber(mapper_admin admin, lo_address address,
                                           int flags, int timeout_seconds,
                                           int revision)
{
    mapper_admin_subscriber *s = &admin->subscribers;
    const char *ip = lo_address_get_hostname(address);
    const char *port = lo_address_get_port(address);
    if (!ip || !port)
        return;

    mapper_clock_t *clock = &admin->clock;
    mapper_clock_now(clock, &clock->now);

    while (*s) {
        if (strcmp(ip, lo_address_get_hostname((*s)->address))==0 &&
            strcmp(port, lo_address_get_port((*s)->address))==0) {
            // subscriber already exists
            if (!flags || !timeout_seconds) {
                // remove subscription
                mapper_admin_subscriber temp = *s;
                int prev_flags = temp->flags;
                *s = temp->next;
                if (temp->address)
                    lo_address_free(temp->address);
                free(temp);
                if (!flags || !(flags &= ~prev_flags))
                    return;
            }
            else {
                // reset timeout
                (*s)->lease_expiration_sec = clock->now.sec + timeout_seconds;
                if ((*s)->flags == flags)
                    return;
                int temp = flags;
                flags &= ~(*s)->flags;
                (*s)->flags = temp;
            }
            break;
        }
        s = &(*s)->next;
    }

    if (!flags)
        return;

    if (!(*s)) {
        // add new subscriber
        mapper_admin_subscriber sub = malloc(sizeof(struct _mapper_admin_subscriber));
        sub->address = lo_address_new(ip, port);
        sub->lease_expiration_sec = clock->now.sec + timeout_seconds;
        sub->flags = flags;
        sub->next = admin->subscribers;
        admin->subscribers = sub;
    }

    if (revision == admin->device->props.version)
        return;

    // bring new subscriber up to date
    mapper_admin_set_bundle_dest_mesh(admin, (*s)->address);
    if (flags & SUB_DEVICE)
        mapper_admin_send_device(admin, admin->device);
    if (flags & SUB_DEVICE_INPUTS)
        mapper_admin_send_inputs(admin, admin->device, -1, -1);
    if (flags & SUB_DEVICE_OUTPUTS)
        mapper_admin_send_outputs(admin, admin->device, -1, -1);
    if (flags & SUB_DEVICE_LINKS_IN)
        mapper_admin_send_links_in(admin, admin->device);
    if (flags & SUB_DEVICE_LINKS_OUT)
        mapper_admin_send_links_out(admin, admin->device);
    if (flags & SUB_DEVICE_CONNECTIONS_IN)
        mapper_admin_send_connections_in(admin, admin->device, -1, -1);
    if (flags & SUB_DEVICE_CONNECTIONS_OUT)
        mapper_admin_send_connections_out(admin, admin->device, -1, -1);
}

/*! Respond to /subscribe message by adding or renewing a subscription. */
static int handler_device_subcribe(const char *path, const char *types,
                                   lo_arg **argv, int argc, lo_message msg,
                                   void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    int version = -1;

    lo_address a  = lo_message_get_source(msg);
    if (!a || !argc) return 0;

    int timeout_seconds;
    if (types[0] == 'i')
        timeout_seconds = argv[0]->i;
    else if (types[0] == 'f')
        timeout_seconds = (int)argv[0]->f;
    else if (types[0] == 'd')
        timeout_seconds = (int)argv[0]->d;
    else {
        trace("<%s> error parsing message parameters in /subscribe.\n",
              mdev_name(md));
        return 0;
    }

    int i, flags = 0;
    if (argc == 1) {
        // default to subscribing to all items
        flags = SUB_DEVICE_ALL;
    }
    else {
        for (i = 1; i < argc; i++) {
            if (types[i] != 's' && types[i] != 'S')
                break;
            else if (strcmp(&argv[i]->s, "all")==0)
                flags = SUB_DEVICE_ALL;
            else if (strcmp(&argv[i]->s, "device")==0)
                flags |= SUB_DEVICE;
            else if (strcmp(&argv[i]->s, "inputs")==0)
                flags |= SUB_DEVICE_INPUTS;
            else if (strcmp(&argv[i]->s, "outputs")==0)
                flags |= SUB_DEVICE_OUTPUTS;
            else if (strcmp(&argv[i]->s, "links")==0)
                flags |= SUB_DEVICE_LINKS;
            else if (strcmp(&argv[i]->s, "links_in")==0)
                flags |= SUB_DEVICE_LINKS_IN;
            else if (strcmp(&argv[i]->s, "links_out")==0)
                flags |= SUB_DEVICE_LINKS_OUT;
            else if (strcmp(&argv[i]->s, "connections")==0)
                flags |= SUB_DEVICE_CONNECTIONS;
            else if (strcmp(&argv[i]->s, "connections_in")==0)
                flags |= SUB_DEVICE_CONNECTIONS_IN;
            else if (strcmp(&argv[i]->s, "connections_out")==0)
                flags |= SUB_DEVICE_CONNECTIONS_OUT;
            else if (strcmp(&argv[i]->s, "@version")==0) {
                // next argument is last device version recorded by subscriber
                ++i;
                if (i < argc && types[i] == 'i')
                    version = argv[i]->i;
            }
        }
    }

    // add or renew subscription
    mapper_admin_manage_subscriber(admin, a, flags, timeout_seconds, version);

    return 0;
}

/*! Respond to /unsubscribe message by removing a subscription. */
static int handler_device_unsubcribe(const char *path, const char *types,
                                     lo_arg **argv, int argc, lo_message msg,
                                     void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;

    lo_address a  = lo_message_get_source(msg);
    if (!a) return 0;

    // remove subscription
    mapper_admin_manage_subscriber(admin, a, 0, 0, 0);

    return 0;
}

/*! Register information about a signal. */
static int handler_signal_info(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message m,
                               void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 2)
        return 1;

    if (types[0] != 's' && types[0] != 'S')
        return 1;

    const char *full_sig_name = &argv[0]->s;
    const char *sig_name = strchr(full_sig_name+1, '/');
    if (!sig_name)
        return 1;

    int devnamelen = sig_name-full_sig_name;
    if (devnamelen >= 1024)
        return 0;

    char devname[1024];
    strncpy(devname, full_sig_name, devnamelen);
    devname[devnamelen]=0;

    mapper_message_t params;
    mapper_msg_parse_params(&params, path, &types[1],
                            argc-1, &argv[1]);

    mapper_db_add_or_update_signal_params( db, sig_name,
                                          devname,
                                          &params );

	return 0;
}

/*! Unregister information about a removed input signal. */
static int handler_input_signal_removed(const char *path, const char *types,
                                        lo_arg **argv, int argc, lo_message m,
                                        void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 1)
        return 1;

    if (types[0] != 's' && types[0] != 'S')
        return 1;

    const char *full_sig_name = &argv[0]->s;
    const char *sig_name = strchr(full_sig_name+1, '/');
    if (!sig_name)
        return 1;

    int dev_name_len = sig_name-full_sig_name;
    if (dev_name_len >= 1024)
        return 0;

    char dev_name[1024];
    strncpy(dev_name, full_sig_name, dev_name_len);
    dev_name[dev_name_len]=0;

    mapper_db_remove_input_by_name(db, dev_name, sig_name);

	return 0;
}

/*! Unregister information about a removed input signal. */
static int handler_output_signal_removed(const char *path, const char *types,
                                         lo_arg **argv, int argc, lo_message m,
                                         void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    if (argc < 1)
        return 1;

    if (types[0] != 's' && types[0] != 'S')
        return 1;

    const char *full_sig_name = &argv[0]->s;
    const char *sig_name = strchr(full_sig_name+1, '/');
    if (!sig_name)
        return 1;

    int dev_name_len = sig_name-full_sig_name;
    if (dev_name_len >= 1024)
        return 0;

    char dev_name[1024];
    strncpy(dev_name, full_sig_name, dev_name_len);
    dev_name[dev_name_len]=0;

    mapper_db_remove_output_by_name(db, dev_name, sig_name);

	return 0;
}

/*! Repond to name collisions during allocation, help suggest IDs once allocated. */
static int handler_device_name_registered(const char *path, const char *types,
                                          lo_arg **argv, int argc,
                                          lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char *name, *s;
    int hash, ordinal, diff;
    int temp_id = -1, suggestion = -1;

    if (argc < 1)
        return 0;

    if (types[0] != 's' && types[0] != 'S')
        return 0;

    name = &argv[0]->s;

    trace("</%s.?::%p> got /name/registered %s %i \n",
          md->props.identifier, admin, name, temp_id);

    if (md->ordinal.locked) {
        /* Parse the ordinal from the complete name which is in the
         * format: /<name>.<n> */
        s = name;
        if (*s != '/')
            return 0;
        s = strrchr(s, '.');
        if (!s)
            return 0;
        ordinal = atoi(s+1);
        *s = 0;

        // If device name matches
        if (strcmp(name+1, md->props.identifier) == 0) {
            // if id is locked and registered id is within my block, store it
            diff = ordinal - md->ordinal.value;
            if (diff > 0 && diff < 9) {
                md->ordinal.suggestion[diff-1] = -1;
            }
        }
    }
    else {
        hash = crc32(0L, (const Bytef *)name, strlen(name));
        if (hash == md->props.name_hash) {
            if (argc > 1) {
                if (types[1] == 'i')
                    temp_id = argv[1]->i;
                if (types[2] == 'i')
                    suggestion = argv[2]->i;
            }
            if (temp_id == admin->random_id &&
                suggestion != md->ordinal.value && suggestion > 0) {
                md->ordinal.value = suggestion;
                mapper_admin_probe_device_name(admin, md);
            }
            else {
                /* Count ordinal collisions. */
                md->ordinal.collision_count++;
                md->ordinal.count_time = get_current_time();
            }
        }
    }
    return 0;
}

/*! Repond to name probes during allocation, help suggest names once allocated. */
static int handler_device_name_probe(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    char *name;
    double current_time;
    int hash, temp_id = -1, i;

    if (types[0] == 's' || types[0] == 'S')
        name = &argv[0]->s;
    else
        return 0;

    if (argc > 0) {
        if (types[1] == 'i')
            temp_id = argv[1]->i;
        else if (types[1] == 'f')
            temp_id = (int) argv[1]->f;
    }

    trace("</%s.?::%p> got /name/probe %s %i \n",
          md->props.identifier, admin, name, temp_id);

    hash = crc32(0L, (const Bytef *)name, strlen(name));
    if (hash == md->props.name_hash) {
        if (md->ordinal.locked) {
            current_time = get_current_time();
            for (i=0; i<8; i++) {
                if (md->ordinal.suggestion[i] >= 0
                    && (current_time - md->ordinal.suggestion[i]) > 2.0) {
                    // reserve suggested ordinal
                    md->ordinal.suggestion[i] = get_current_time();
                    break;
                }
            }
            /* Name may not yet be registered, so we can't use
             * mapper_admin_send() here. */
            mapper_interface iface = admin->interfaces;
            while (iface) {
                if (iface->bus_addr)
                    lo_send(iface->bus_addr, "/name/registered",
                            "sii", name, temp_id,
                            (md->ordinal.value+i+1));
                iface = iface->next;
            }
        }
        else {
            md->ordinal.collision_count++;
            printf(">> COLLISION %d\n", md->ordinal.collision_count);
            md->ordinal.count_time = get_current_time();
        }
    }
    return 0;
}

/*! Link two devices. */
static int handler_device_link(const char *path, const char *types,
                               lo_arg **argv, int argc, lo_message msg,
                               void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    const char *src_name, *dest_name;

    if (argc < 2)
        return 0;

    // Need at least 2 devices to link
    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    if (strcmp(mdev_name(md), dest_name))
        return 0;

    trace("<%s> got /link %s %s\n", mdev_name(md),
          src_name, dest_name);

    mapper_message_t params;
    // add arguments from /link if any
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
    {
        trace("<%s> error parsing message parameters in /link.\n",
              mdev_name(md));
        return 0;
    }

    lo_arg *arg_port = (lo_arg*) &md->props.port;
    params.values[AT_DEST_PORT] = &arg_port;
    params.types[AT_DEST_PORT] = "i";
    params.lengths[AT_DEST_PORT] = 1;

    // TODO: check if src ip and port are available as metadata, send directly
    mapper_admin_set_bundle_dest_bus(admin);
    mapper_admin_bundle_message_with_params(
        admin, &params, 0, ADM_LINK_TO, 0, "ss", src_name, dest_name);

    return 0;
}

/*! Link two devices... continued. */
static int handler_device_linkTo(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    const char *src_name, *dest_name, *host=0, *admin_port;
    int data_port;
    mapper_message_t params;
    lo_address a = NULL;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    if (strcmp(src_name, mdev_name(md)))
    {
        trace("<%s> ignoring /linkTo %s %s\n",
              mdev_name(md), src_name, dest_name);
        return 0;
    }

    trace("<%s> got /linkTo %s %s\n", mdev_name(md),
          src_name, dest_name);

    // Parse the message.
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
    {
        trace("<%s> error parsing message parameters in /linkTo.\n",
              mdev_name(md));
        return 0;
    }

    // Discover whether the device is already linked.
    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);

    if (router) {
        // Already linked, forward to link/modify handler.
        handler_device_link_modify(path, types, argv, argc, msg, user_data);
        return 0;
    }

    // Find the sender's hostname
    a = lo_message_get_source(msg);
    host = lo_address_get_hostname(a);
    admin_port = lo_address_get_port(a);
    if (!host) {
        trace("can't perform /linkTo, host unknown\n");
        return 0;
    }

    // Retrieve the port
    if (mapper_msg_get_param_if_int(&params, AT_DEST_PORT, &data_port)) {
        trace("can't perform /linkTo, port unknown\n");
        return 0;
    }

    // Creation of a new router added to the source.
    router = mapper_router_new(md, host, atoi(admin_port), data_port, dest_name);
    if (!router) {
        trace("can't perform /linkTo, NULL router\n");
        return 0;
    }
    mdev_add_router(md, router);

    if (argc > 2)
        mapper_router_set_from_message(router, &params);

    // Call local link handler if it exists
    if (md->link_cb)
        md->link_cb(md, &router->props, MDEV_LOCAL_ESTABLISHED,
                    md->link_cb_userdata);

    // Announce the result to destination and subscribers.
    if (!a)
        mapper_admin_set_bundle_dest_bus(admin);
    else
        mapper_admin_set_bundle_dest_mesh(admin, a);

    mapper_admin_send_linked(admin, router, 1);
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS_OUT);
    mapper_admin_send_linked(admin, router, 1);

    trace("<%s> added new router to %s -> host: %s, port: %d\n",
          mdev_name(md), dest_name, host, data_port);

    return 0;
}

/*! Store record of linked devices. */
static int handler_device_linked(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_monitor mon = admin->monitor;
    mapper_db db = mapper_monitor_get_db(mon);

    const char *src_name, *dest_name, *host=0, *admin_port;
    int data_port = -1;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    trace("<monitor> got /linked %s %s\n",
          src_name, dest_name);

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, argv+2))
        return 0;
    if (mon)
        mapper_db_add_or_update_link_params(db, src_name, dest_name, &params);
    if (!md || strcmp(mdev_name(md), dest_name))
        return 0;

    // Add a receiver data structure
    mapper_receiver receiver =
        mapper_receiver_find_by_src_name(md->receivers, src_name);

    if (receiver) {
        // Already linked, add metadata.
        if (argc <= 2)
            return 0;
        if (mapper_receiver_set_from_message(receiver, &params)) {
            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS_IN);
            mapper_admin_send_linked(admin, receiver, 0);

            // Call local link handler if it exists
            if (md->link_cb)
                md->link_cb(md, &receiver->props, MDEV_LOCAL_MODIFIED,
                            md->link_cb_userdata);
        }
        return 0;
    }

    // Find the sender's hostname
    lo_address a = lo_message_get_source(msg);
    host = lo_address_get_hostname(a);
    admin_port = lo_address_get_port(a);
    if (!host || !admin_port) {
        trace("can't add receiver on /linked, host unknown\n");
        return 0;
    }

    // Retrieve the src device port if it is defined
    mapper_msg_get_param_if_int(&params, AT_SRC_PORT, &data_port);

    receiver = mapper_receiver_new(md, host, atoi(admin_port),
                                   data_port, src_name);
    if (!receiver) {
        trace("Error: NULL receiver\n");
        return 0;
    }
    mdev_add_receiver(md, receiver);
    if (argc > 2)
        mapper_receiver_set_from_message(receiver, &params);

    // Inform subscribers
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS_IN);
    mapper_admin_send_linked(admin, receiver, 0);

    // Call local link handler if it exists
    if (md->link_cb)
    md->link_cb(md, &receiver->props, MDEV_LOCAL_ESTABLISHED,
                md->link_cb_userdata);

    return 0;
}

/*! Modify the link properties : scope, etc. */
static int handler_device_link_modify(const char *path, const char *types,
                                      lo_arg **argv, int argc, lo_message msg,
                                      void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    const char *src_name, *dest_name;
    int updated;
    mapper_message_t params;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    if (strcmp(src_name, mdev_name(md))) {
        trace("<%s> ignoring /link/modify %s %s\n",
              mdev_name(md), src_name, dest_name);
        return 0;
    }

    trace("<%s> got /link/modify %s %s\n", mdev_name(md),
          src_name, dest_name);

    // Discover whether the device is already linked.
    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);

    if (!router)
        return 0;

    // Parse the message.
    if (mapper_msg_parse_params(&params, path, &types[2], argc-2, &argv[2])) {
        trace("<%s> error parsing message parameters in /link/modify.\n",
              mdev_name(md));
        return 0;
    }

    updated = mapper_router_set_from_message(router, &params);

    if (updated) {
        // increment device version
        md->version += updated;

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS_OUT);
        mapper_admin_send_linked(admin, router, 0);

        // Call local link handler if it exists
        if (md->link_cb)
            md->link_cb(md, &router->props, MDEV_LOCAL_MODIFIED,
                        md->link_cb_userdata);

        trace("<%s> modified link to %s\n", mdev_name(md), dest_name);
    }

    return 0;
}

/*! Unlink two devices. */
static int handler_device_unlink(const char *path, const char *types,
                                 lo_arg **argv, int argc, lo_message msg,
                                 void *user_data)
{
    const char *src_name, *dest_name;
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    trace("<%s> got /unlink %s %s + %i arguments\n", mdev_name(md),
          src_name, dest_name, argc-2);

    mapper_message_t params;
    // add arguments from /unlink if any
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
    {
        trace("<%s> error parsing message parameters in /unlink.\n",
              mdev_name(md));
        return 0;
    }

    if (strcmp(mdev_name(md), src_name))
        return 0;

    /* Remove the router for the destination. */
    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);
    if (router) {
        // Call the local link handler if it exists
        if (md->link_cb)
            md->link_cb(md, &router->props, MDEV_LOCAL_DESTROYED,
                        md->link_cb_userdata);

        mdev_remove_router(md, router);

        // Inform destination
        mapper_admin_set_bundle_dest_mesh(admin, router->admin_addr);
        mapper_admin_bundle_message_with_params(admin, &params, 0, ADM_UNLINKED,
                                                0, "ss", mdev_name(md), dest_name);

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS_OUT);
        mapper_admin_bundle_message_with_params(admin, &params, 0, ADM_UNLINKED,
                                                0, "ss", mdev_name(md), dest_name);
    }
    else {
        trace("<%s> no router for %s found in /unlink handler\n",
              mdev_name(md), dest_name);
    }
    return 0;
}

/*! Respond to /unlinked by removing link from database. */
static int handler_device_unlinked(const char *path, const char *types,
                                   lo_arg **argv, int argc, lo_message msg,
                                   void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_device md = admin->device;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    const char *src_name = &argv[0]->s;
    const char *dest_name = &argv[1]->s;

    mapper_message_t params;
    memset(&params, 0, sizeof(mapper_message_t));
    // add arguments from /unlink if any
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
    {
        trace("<%s> error parsing message parameters in /unlinked.\n",
              mdev_name(md));
        return 0;
    }

    if (mon) {
        trace("<monitor> got /unlinked %s %s + %i arguments\n",
              src_name, dest_name, argc-2);

        mapper_db db = mapper_monitor_get_db(mon);

        mapper_db_remove_connections_by_query(db,
            mapper_db_get_connections_by_src_dest_device_names(db, src_name,
                                                               dest_name));
        mapper_db_remove_link(db,
            mapper_db_get_link_by_src_dest_names(db, src_name, dest_name));
    }

    if (md) {
        trace("<%s> got /unlinked %s %s + %i arguments\n",
              mdev_name(md), src_name, dest_name, argc-2);

        if (strcmp(mdev_name(md), dest_name))
            return 0;

        mapper_message_t params;
        if (mapper_msg_parse_params(&params, path, &types[2],
                                    argc-2, &argv[2]))
        {
            trace("<%s> error parsing message parameters in /unlinked.\n",
                  mdev_name(md));
            return 0;
        }

        /* Remove the receiver for the source. */
        mapper_receiver receiver =
            mapper_receiver_find_by_src_name(md->receivers, src_name);
        if (receiver) {
            // Call the local link handler if it exists
            if (md->link_cb)
                md->link_cb(md, &receiver->props, MDEV_LOCAL_DESTROYED,
                            md->link_cb_userdata);

            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_LINKS_IN);
            mapper_admin_bundle_message(admin, ADM_UNLINKED, 0, "ss",
                                        mdev_name(md), dest_name);

            mdev_remove_receiver(md, receiver);
        }
        else {
            trace("<%s> no receiver for %s found in /unlinked handler\n",
                  mdev_name(md), src_name);
        }
    }
    return 0;
}

/* Helper function to check if the prefix matches.  Like strcmp(),
 * returns 0 if they match (up to the second '/'), non-0 otherwise.
 * Also optionally returns a pointer to the remainder of str1 after
 * the prefix. */
static int prefix_cmp(const char *str1, const char *str2,
                      const char **rest)
{
    if (str1[0]!='/') {
        trace("String '%s' does not start with '/'.\n", str1);
        return -1;
    }
    if (str2[0]!='/') {
        trace("String '%s' does not start with '/'.\n", str2);
        return -1;
    }

    // skip first slash
    const char *s1=str1+1, *s2=str2+1;

    while (*s1 && (*s1)!='/') s1++;
    while (*s2 && (*s2)!='/') s2++;

    int n1 = s1-str1, n2 = s2-str2;
    if (n1!=n2) return 1;

    if (rest)
        *rest = s1;

    return strncmp(str1, str2, n1);
}

/*! When the /connect message is received by the destination device,
 *  send a connectTo message to the source device. */
static int handler_signal_connect(const char *path, const char *types,
                                  lo_arg **argv, int argc, lo_message msg,
                                  void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal input;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    dest_name = &argv[1]->s;
    if (prefix_cmp(dest_name, mdev_name(md),
                       &dest_signal_name))
        return 0;

    src_name = &argv[0]->s;
    src_signal_name = strchr(src_name+1, '/');

    if (!src_signal_name) {
        trace("<%s> source '%s' has no parameter in /connect.\n",
              mdev_name(md), src_name);
        return 0;
    }

    trace("<%s> got /connect %s %s\n", mdev_name(md),
          src_name, dest_name);

    if (!(input=mdev_get_input_by_name(md, dest_signal_name, 0)))
    {
        trace("<%s> no input signal found for '%s' in /connect\n",
              mdev_name(md), dest_signal_name);
        return 0;
    }

    mapper_message_t params;
    // add arguments from /connect if any
    if (mapper_msg_parse_params(&params, path, &types[2],
                                argc-2, &argv[2]))
    {
        trace("<%s> error parsing message parameters in /connect.\n",
              mdev_name(md));
        return 0;
    }

    mapper_receiver receiver =
        mapper_receiver_find_by_src_name(md->receivers, src_name);

    /* If no link found, we simply stop here. The idea was floated
     * that we could automatically create links, but it was agreed
     * that this kind of logic would be best left up to client
     * applications. */
    if (!receiver) {
        trace("<%s> not linked from '%s' on /connect.\n",
              mdev_name(md), src_name);
        return 0;
    }

    // substitute some missing parameters with known properties
    lo_arg *arg_type = (lo_arg*) &input->props.type;
    params.values[AT_TYPE] = &arg_type;
    params.types[AT_TYPE] = "c";
    params.lengths[AT_TYPE] = 1;

    lo_arg *arg_length = (lo_arg*) &input->props.length;
    params.values[AT_LENGTH] = &arg_length;
    params.types[AT_LENGTH] = "i";
    params.lengths[AT_LENGTH] = 1;

    lo_arg *arg_num_instances = (lo_arg*) &input->props.num_instances;
    params.values[AT_INSTANCES] = &arg_num_instances;
    params.types[AT_INSTANCES] = "i";
    params.lengths[AT_INSTANCES] = 1;

    mapper_admin_set_bundle_dest_mesh(admin, receiver->admin_addr);
    mapper_admin_bundle_message_with_params(
        admin, &params, input->props.extra,
        ADM_CONNECT_TO, 0, "ss", src_name, dest_name,
        (!params.values[AT_MIN] && input->props.minimum) ? AT_MIN : -1, input,
        (!params.values[AT_MAX] && input->props.maximum) ? AT_MAX : -1, input);

    return 0;
}

/*! Connect two signals. */
static int handler_signal_connectTo(const char *path, const char *types,
                                    lo_arg **argv, int argc,
                                    lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal output;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S'))
        return 0;

    src_name = &argv[0]->s;
    if (prefix_cmp(src_name, mdev_name(md),
                   &src_signal_name))
        return 0;

    dest_name = &argv[1]->s;
    dest_signal_name = strchr(dest_name+1, '/');

    if (!dest_signal_name) {
        trace("<%s> destination '%s' has no parameter in /connectTo.\n",
              mdev_name(md), dest_name);
        return 0;
    }

    trace("<%s> got /connectTo %s %s + %d arguments\n",
          mdev_name(md), src_name, dest_name, argc-2);

    if (!(output=mdev_get_output_by_name(md, src_signal_name, 0)))
    {
        trace("<%s> no output signal found for '%s' in /connectTo\n",
              mdev_name(md), src_signal_name);
        return 0;
    }

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]))
    {
        trace("<%s> error parsing parameters in /connectTo, "
              "continuing anyway.\n", mdev_name(md));
    }

    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, dest_name);

    /* If no link found, we simply stop here. The idea was floated
     * that we could automatically create links, but it was agreed
     * that this kind of logic would be best left up to client
     * applications. */
    if (!router) {
        trace("<%s> not linked to '%s' on /connectTo.\n",
              mdev_name(md), dest_name);
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection_by_names(router, src_signal_name,
                                               dest_signal_name);
    /* If a connection connection already exists between these two signals,
     * forward the message to handler_signal_connection_modify() and stop. */
    if (c) {
        handler_signal_connection_modify(path, types, argv, argc,
                                         msg, user_data);
        return 0;
    }

    /* Creation of a connection requires the type and length info. */
    if (!params.values[AT_TYPE] || !params.values[AT_LENGTH])
        return 0;

    char dest_type = 0;
    if (*params.types[AT_TYPE] == 'c')
        dest_type = (*params.values[AT_TYPE])->c;
    else if (*params.types[AT_TYPE] == 's')
        dest_type = (*params.values[AT_TYPE])->s;
    else
        return 0;

    int dest_length = 0;
    if (*params.types[AT_LENGTH] == 'i')
        dest_length = (*params.values[AT_LENGTH])->i;
    else
        return 0;

    /* Add a flavourless connection */
    c = mapper_router_add_connection(router, output, dest_signal_name,
                                     dest_type, dest_length);
    if (!c) {
        trace("couldn't create mapper_connection "
              "in handler_signal_connectTo\n");
        return 0;
    }

    if (argc > 2) {
        /* If send_as_instance property is not set, make connection
         * default to passing updates as instances if either source
         * or destination signals have multiple instances. */
        if (!params.values[AT_SEND_AS_INSTANCE]) {
            int dest_instances = 0;
            mapper_msg_get_param_if_int(&params, AT_INSTANCES,
                                        &dest_instances);
            if (dest_instances > 1 || output->props.num_instances > 1) {
                c->props.send_as_instance = 1;
            }
            else
                c->props.send_as_instance = 0;
        }

        /* Set its properties. */
        mapper_connection_set_from_message(c, &params);
    }

    // Inform destination device
    mapper_admin_set_bundle_dest_mesh(admin, router->admin_addr);
    mapper_admin_send_connected(admin, router, c, -1, 1);

    // Inform subscribers
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_OUT);
    mapper_admin_send_connected(admin, router, c, -1, 1);

    // Call local connection handler if it exists
    if (md->connection_cb)
        md->connection_cb(md, &router->props, output,
                          &c->props, MDEV_LOCAL_ESTABLISHED,
                          md->connection_cb_userdata);

    return 0;
}

/*! Respond to /connected by storing connection in database. */
static int handler_signal_connected(const char *path, const char *types,
                                    lo_arg **argv, int argc, lo_message msg,
                                    void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_monitor mon = admin->monitor;
    mapper_signal input;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, argv+2)) {
        trace("<%s> error parsing parameters in /connected, "
              "continuing anyway.\n", mdev_name(md));
    }

    if (mon) {
        mapper_db db = mapper_monitor_get_db(mon);

        trace("<monitor> got /connected %s %s\n", src_name, dest_name);

        mapper_db_add_or_update_connection_params(db, src_name,
                                                  dest_name, &params);
    }

    if (!md || prefix_cmp(dest_name, mdev_name(md),
                          &dest_signal_name))
        return 0;

    trace("<%s> got /connected %s %s + %d arguments\n",
          mdev_name(md), src_name, dest_name, argc-2);

    src_signal_name = strchr(src_name+1, '/');
    if (!src_signal_name)
        return 0;

    if (!(input=mdev_get_input_by_name(md, dest_signal_name, 0)))
        return 0;

    mapper_receiver receiver =
        mapper_receiver_find_by_src_name(md->receivers, src_name);
    if (!receiver) {
        trace("<%s> not linked from '%s' on /connected.\n",
              mdev_name(md), src_name);
        return 0;
    }

    mapper_connection c =
        mapper_receiver_find_connection_by_names(receiver, src_signal_name,
                                                 dest_signal_name);
    if (c) {
        if (argc <= 2)
            return 0;
        // connection already exists, add metadata
        int updated = mapper_connection_set_from_message(c, &params);
        if (updated) {
            // Inform subscribers
            mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
            mapper_admin_send_connected(admin, receiver, c, -1, 0);

            // Call local connection handler if it exists
            if (md->connection_cb)
                md->connection_cb(md, &receiver->props, input,
                                  &c->props, MDEV_LOCAL_MODIFIED,
                                  md->connection_cb_userdata);
        }
        return 0;
    }
    else {
        /* Creation of a connection requires the type and length info. */
        if (!params.values[AT_SRC_TYPE] || !params.values[AT_SRC_LENGTH])
            return 0;

        char src_type = 0;
        if (*params.types[AT_SRC_TYPE] == 'c')
            src_type = (*params.values[AT_SRC_TYPE])->c;
        else if (*params.types[AT_SRC_TYPE] == 's')
            src_type = (*params.values[AT_SRC_TYPE])->s;
        else
            return 0;

        int src_length = 0;
        if (*params.types[AT_SRC_LENGTH] == 'i')
            src_length = (*params.values[AT_SRC_LENGTH])->i;
        else
            return 0;

        // Add a flavourless connection
        c = mapper_receiver_add_connection(receiver, input, src_signal_name,
                                           src_type, src_length);

        /* Set its properties. */
        mapper_connection_set_from_message(c, &params);

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
        mapper_admin_send_connected(admin, receiver, c, -1, 0);

        // Call local connection handler if it exists
        if (md->connection_cb)
            md->connection_cb(md, &receiver->props, input,
                              &c->props, MDEV_LOCAL_ESTABLISHED,
                              md->connection_cb_userdata);
    }

    return 0;
}

/*! Modify the connection properties : mode, range, expression,
 *  boundMin, boundMax. */
static int handler_signal_connection_modify(const char *path, const char *types,
                                            lo_arg **argv, int argc, lo_message msg,
                                            void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal output;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 4)
        return 0;

    if ((types[0] != 's' && types[0] != 'S')
        || (types[1] != 's' && types[1] != 'S') || (types[2] != 's'
                                                    && types[2] != 'S'))
        return 0;

    src_name = &argv[0]->s;
    if (prefix_cmp(src_name, mdev_name(md),
                   &src_signal_name))
        return 0;

    dest_name = &argv[1]->s;
    dest_signal_name = strchr(dest_name+1, '/');

    if (!dest_signal_name) {
        trace("<%s> destination '%s' has no parameter in /connection/modify.\n",
              mdev_name(md), dest_name);
        return 0;
    }

    if (!(output=mdev_get_output_by_name(md, src_signal_name, 0)))
    {
        trace("<%s> no output signal found for '%s' in /connection/modify\n",
              mdev_name(md), src_signal_name);
        return 0;
    }

    mapper_router router =
        mapper_router_find_by_dest_name(md->routers, &argv[1]->s);
    if (!router)
    {
        trace("<%s> no router found for '%s' in /connection/modify\n",
              mdev_name(md), &argv[1]->s);
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection_by_names(router, src_signal_name,
                                               dest_signal_name);
    if (!c)
        return 0;

    mapper_message_t params;
    if (mapper_msg_parse_params(&params, path, types+2, argc-2, &argv[2]))
    {
        trace("<%s> error parsing parameters in /connection/modify, "
              "continuing anyway.\n", mdev_name(md));
    }

    int updated = mapper_connection_set_from_message(c, &params);
    if (updated) {
        // Inform destination device
        mapper_admin_set_bundle_dest_mesh(admin, router->admin_addr);
        mapper_admin_send_connected(admin, router, c, -1, 1);

        // Inform subscribers
        mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_OUT);
        mapper_admin_send_connected(admin, router, c, -1, 1);
        
        // Call local connection handler if it exists
        if (md->connection_cb)
            md->connection_cb(md, &router->props, output,
                              &c->props, MDEV_LOCAL_MODIFIED,
                              md->connection_cb_userdata);
    }

    return 0;
}

/*! Disconnect two signals. */
static int handler_signal_disconnect(const char *path, const char *types,
                                     lo_arg **argv, int argc,
                                     lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_signal sig;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    if (prefix_cmp(src_name, mdev_name(md), &src_signal_name) != 0)
        return 0;

    dest_name = &argv[1]->s;
    dest_signal_name = strchr(dest_name+1, '/');

    if (!dest_signal_name) {
        trace("<%s> destination '%s' has no parameter in /disconnect.\n",
              mdev_name(md), dest_name);
        return 0;
    }

    if (!(sig=mdev_get_output_by_name(md, src_signal_name, 0)))
    {
        trace("<%s> no output signal found for '%s' in /disconnect\n",
              mdev_name(md), src_name);
        return 0;
    }

    mapper_router r = mapper_router_find_by_dest_name(md->routers,
                                                      dest_name);
    if (!r) {
        trace("<%s> ignoring /disconnect, no router found for '%s'\n",
              mdev_name(md), dest_name);
        return 0;
    }

    mapper_connection c =
        mapper_router_find_connection_by_names(r, src_signal_name,
                                               dest_signal_name);
    if (!c) {
        trace("<%s> ignoring /disconnect, "
              "no connection found for '%s' -> '%s'\n",
              mdev_name(md), src_name, dest_name);
        return 0;
    }

    // Call local connection handler if it exists
    if (md->connection_cb)
        md->connection_cb(md, &r->props, sig, &c->props, MDEV_LOCAL_DESTROYED,
                          md->connection_cb_userdata);

    /* The connection is removed. */
    if (mapper_router_remove_connection(r, c)) {
        return 0;
    }

    // Inform destination and subscribers
    mapper_admin_set_bundle_dest_mesh(admin, r->admin_addr);
    mapper_admin_bundle_message(admin, ADM_DISCONNECTED, 0, "ss",
                                src_name, dest_name);
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_OUT);
    mapper_admin_bundle_message(admin, ADM_DISCONNECTED, 0, "ss",
                                src_name, dest_name);

    return 0;
}

/*! Respond to /disconnected by removing connection from database. */
static int handler_signal_disconnected(const char *path, const char *types,
                                       lo_arg **argv, int argc,
                                       lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_monitor mon = admin->monitor;
    mapper_device md = admin->device;

    const char *src_signal_name, *src_name;
    const char *dest_signal_name, *dest_name;

    if (argc < 2)
        return 0;

    if (types[0] != 's' && types[0] != 'S' && types[1] != 's'
        && types[1] != 'S')
        return 0;

    src_name = &argv[0]->s;
    dest_name = &argv[1]->s;

    if (mon) {
        mapper_db db = mapper_monitor_get_db(mon);

        trace("<monitor> got /disconnected %s %s\n",
              src_name, dest_name);

        mapper_db_remove_connection(db,
            mapper_db_get_connection_by_signal_full_names(db, src_name,
                                                          dest_name));
    }

    if (!md || prefix_cmp(dest_name, mdev_name(md),
                          &dest_signal_name))
        return 0;

    src_signal_name = strchr(src_name+1, '/');
    if (!src_signal_name)
        return 0;

    trace("<%s> got /disconnected %s %s\n",
          mdev_name(md), src_name, dest_name);

    mapper_signal sig;
    if (!(sig=mdev_get_input_by_name(md, dest_signal_name, 0)))
        return 0;

    mapper_receiver r = mapper_receiver_find_by_src_name(md->receivers,
                                                         src_name);
    if (!r) {
        trace("<%s> ignoring /disconnected, no receiver found for '%s'\n",
              mdev_name(md), src_name);
        return 0;
    }

    mapper_connection c =
        mapper_receiver_find_connection_by_names(r, src_signal_name,
                                                 dest_signal_name);
    if (!c) {
        trace("<%s> ignoring /disconnected, "
              "no connection found for '%s' -> '%s'\n",
              mdev_name(md), src_name, dest_name);
        return 0;
    }

    // Inform user code of the destroyed connection if requested
    if (md->connection_cb)
        md->connection_cb(md, &r->props, sig, &c->props, MDEV_LOCAL_DESTROYED,
                          md->connection_cb_userdata);

    // Inform subscribers
    mapper_admin_set_bundle_dest_subscribers(admin, SUB_DEVICE_CONNECTIONS_IN);
    mapper_admin_bundle_message(admin, ADM_DISCONNECTED, 0, "ss",
                                src_name, dest_name);

    /* The connection is removed. */
    if (mapper_receiver_remove_connection(r, c)) {
        return 0;
    }
    return 0;
}

static int handler_sync(const char *path,
                        const char *types,
                        lo_arg **argv, int argc,
                        lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
    mapper_monitor mon = admin->monitor;
    mapper_clock_t *clock = &admin->clock;

    // if I sent this message, ignore it
    if (md && strcmp(mdev_name(md), &argv[0]->s)==0)
        return 0;

    int device_id = crc32(0L, (const Bytef *)&argv[0]->s, strlen(&argv[0]->s));
    int message_id = argv[2]->i;

    // get current time
    mapper_timetag_t now;
    mapper_clock_now(clock, &now);

    // store remote timetag
    clock->remote.device_id = device_id;
    clock->remote.message_id = message_id;
    clock->remote.timetag.sec = now.sec;
    clock->remote.timetag.frac = now.frac;

    lo_timetag then = lo_message_get_timestamp(msg);
    float confidence = argv[3]->f;

    if (mon) {
        mapper_db_device reg = mapper_db_get_device_by_name(&mon->db,
                                                            &argv[0]->s);
        if (!reg)
            mapper_db_add_or_update_device_params(&mon->db, &argv[0]->s, NULL);
        else
            mapper_timetag_cpy(&reg->synced, then);
    }

    // if remote timetag is in the future, adjust to remote time
    double diff = mapper_timetag_difference(then, now);
    mapper_clock_adjust(&admin->clock, diff, 1.0);

    if (!md)
        return 0;

    // look at the second part of the message
    device_id = argv[4]->i;
    if (device_id != mdev_id(md))
        return 0;

    message_id = argv[5]->i;
    if (message_id >= 10)
        return 0;

    // Calculate latency on exchanged /sync messages
    double latency = (mapper_timetag_difference(now, clock->local[message_id].timetag)
                      - argv[6]->d) * 0.5;
    if (latency > 0 && latency < 100)
        mapper_clock_adjust(&admin->clock, diff + latency, confidence);

    return 0;
}
