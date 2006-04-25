/*
 * nash-network.c
 *
 * Simple network bring-up code for nash.
 * It currently uses libpump and looks a lot like the network code in
 * anaconda
 *
 * Jeremy Katz <katzj@redhat.com>
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <errno.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <popt.h>
#include <pump.h>

#include "lib.h"

/* returns 1 for link, 0 for no link, -1 for unknown */
int get_link_status(char *ifname);

#define NUM_LINK_CHECKS 5
static int waitForLink(char * dev) {
    int tries = 0;

    /* try to wait for a valid link -- if the status is unknown or
     * up continue, else sleep for 1 second and try again for up
     * to five times */
    nashLogger(NOTICE, "waiting for link... ");
    while (tries < NUM_LINK_CHECKS) {
      if (get_link_status(dev) != 0)
            break;
        sleep(1);
        tries++;
    }
    nashLogger(NOTICE, "%d seconds.\n", tries);
    if (tries < NUM_LINK_CHECKS)
        return 0;
    nashLogger(WARNING, "no network link detected on %s\n", dev);
    return 1;
}

static void writeResolvConf(struct pumpNetIntf intf) {
    char * filename = "/etc/resolv.conf";
    FILE * f;
    int i;

    if (!(intf.set & PUMP_NETINFO_HAS_DOMAIN) && !intf.numDns)
        return;

    f = fopen(filename, "w");
    if (!f) {
        eprintf("Cannot create %s: %s\n", filename, strerror(errno));
        return;
    }

    if (intf.set & PUMP_NETINFO_HAS_DOMAIN)
        fprintf(f, "search %s\n", intf.domain);

    for (i = 0; i < intf.numDns; i++) 
        fprintf(f, "nameserver %s\n", inet_ntoa(intf.dnsServers[i]));

    fclose(f);

    res_init();         /* reinit the resolver so DNS changes take affect */

    return;
}


int nashNetworkCommand(char * cmd) {
    int argc;
    char ** argv;
    char * bootProto = NULL, * dev = NULL, * dhcpclass = NULL, * ethtool = NULL, * hostname = NULL;
    char * gateway = NULL, * ip = NULL, * nameserver = NULL, * netmask = NULL;
    int mtu;
    struct pumpNetIntf intf;
    poptContext optCon;
    struct poptOption netOptions[] = {
        { "bootproto", '\0', POPT_ARG_STRING, &bootProto, 0, NULL, NULL },
        { "device", '\0', POPT_ARG_STRING, &dev, 0, NULL, NULL },
        { "dhcpclass", '\0', POPT_ARG_STRING, &dhcpclass, 0, NULL, NULL },
        { "gateway", '\0', POPT_ARG_STRING, &gateway, 'g', NULL, NULL },
        { "ip", '\0', POPT_ARG_STRING, &ip, 'i', NULL, NULL },
        { "nameserver", '\0', POPT_ARG_STRING, &nameserver, 'n', NULL, NULL },
        { "netmask", '\0', POPT_ARG_STRING, &netmask, 'm', NULL, NULL },
        { "ethtool", '\0', POPT_ARG_STRING, &ethtool, 0, NULL, NULL },
        { "mtu", '\0', POPT_ARG_INT, &mtu, 0, NULL, NULL },
        { "hostname", '\0', POPT_ARG_STRING, &hostname, 0, NULL, NULL },
        { 0, 0, 0, 0, 0, 0, 0 }
    };
    
    if (poptParseArgvString(cmd, &argc, (const char ***) &argv) || !argc) {
        eprintf("ERROR: Invalid options to network command\n");
        return 1;
    }
    
    optCon = poptGetContext(NULL, argc, (const char **) argv, 
                            netOptions, 0);    

    if (poptGetNextOpt(optCon) < -1) {
        nashLogger(ERROR, "ERROR: Bad argument to network command\n");
        return 1;
    }

    if (ethtool != NULL) {
        /* FIXME */
        nashLogger(WARNING, "WARNING: ethtool options not currently handled\n");
    }

    memset(&intf,'\0',sizeof(intf));

    if (mtu) {
        intf.mtu = mtu;
        intf.set |= PUMP_INTFINFO_HAS_MTU;
    }

    if (hostname != NULL) {
        intf.hostname = hostname;
        intf.set |= PUMP_NETINFO_HAS_HOSTNAME;
    }

    if (dev == NULL)
        dev = strdup("eth0");

    if ((bootProto != NULL) && (!strncmp(bootProto, "dhcp", 4))) {
        waitForLink(dev);
        nashLogger(NOTICE, "Sending request for IP information through %s\n", dev);
        pumpDhcpClassRun(dev, 0, 0, NULL, 
                         dhcpclass ? dhcpclass : "nash",
                         &intf, NULL);
    } else { /* static IP.  hope enough is specified! */
        struct in_addr addr;
        if (ip && inet_aton(ip, &addr)) {
            intf.ip = addr;
            intf.set |= PUMP_INTFINFO_HAS_IP;
        }
        if (netmask && inet_aton(netmask, &addr)) {
            intf.netmask = addr;
            intf.set |= PUMP_INTFINFO_HAS_NETMASK;
        }
        if (gateway && inet_aton(gateway, &addr)) {
            intf.gateway = addr;
            intf.set |= PUMP_INTFINFO_HAS_NETMASK;
        }
        /* FIXME: still need to do the dns bits.  loader2/net.c:393 or so */
    }

    pumpSetupInterface(&intf);
    if (intf.set & PUMP_NETINFO_HAS_GATEWAY) {
        pumpSetupDefaultGateway(&intf.gateway);
    }
    writeResolvConf(intf);

    sleep(2);
    return 0;
}
