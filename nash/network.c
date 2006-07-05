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

#include <nash.h>
#include "util.h"

/* returns 1 for link, 0 for no link, -1 for unknown */
int get_link_status(char *ifname);

#define NUM_LINK_CHECKS 5
static int waitForLink(char * dev) {
    int tries = 0;

    /* try to wait for a valid link -- if the status is unknown or
     * up continue, else sleep for 1 second and try again for up
     * to five times */
    nashLogger(_nash_context, NOTICE, "waiting for link... ");
    while (tries < NUM_LINK_CHECKS) {
      if (get_link_status(dev) != 0)
            break;
        sleep(1);
        tries++;
    }
    nashLogger(_nash_context, NOTICE, "%d seconds.\n", tries);
    if (tries < NUM_LINK_CHECKS)
        return 0;
    nashLogger(_nash_context, WARNING, "no network link detected on %s\n", dev);
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
    char * gateway = NULL, * ip = NULL, * nameserver = NULL, * netmask = NULL, * dns = NULL, * domain = NULL;
    int mtu = 0, rc;
    char * err = NULL;
    struct pumpNetIntf intf;
    struct in_addr addr;
    poptContext optCon;
    struct poptOption netOptions[] = {
        { "bootproto", '\0', POPT_ARG_STRING, &bootProto, 0, NULL, NULL },
        { "device", '\0', POPT_ARG_STRING, &dev, 0, NULL, NULL },
        { "dhcpclass", '\0', POPT_ARG_STRING, &dhcpclass, 0, NULL, NULL },
        { "dns", '\0', POPT_ARG_STRING, &dns, 0, NULL, NULL },
        { "domain", '\0', POPT_ARG_STRING, &domain, 0, NULL, NULL },
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

    while ((rc = poptGetNextOpt(optCon)) > 0) {}
    if (rc < -1) {
        nashLogger(_nash_context, ERROR, "ERROR: Bad argument to network command\n");
        return 1;
    }

    if (ethtool != NULL) {
        /* FIXME */
        nashLogger(_nash_context, WARNING, "WARNING: ethtool options not currently handled\n");
    }

    memset(&intf,'\0', sizeof(intf));

    if (mtu) {
        intf.mtu = mtu;
        intf.set |= PUMP_INTFINFO_HAS_MTU;
    }

    if (hostname != NULL) {
        intf.hostname = hostname;
        intf.set |= PUMP_NETINFO_HAS_HOSTNAME;
    }
    
    if (domain != NULL) {
        intf.domain = domain;
        intf.set |= PUMP_NETINFO_HAS_DOMAIN;
    }

    if (dns) {
        char *c, *buf = strdup(dns);
        
        c = strtok(buf, ",");
        while ((intf.numDns < MAX_DNS_SERVERS) && (c != NULL) && inet_aton(c, &addr)) {
            intf.dnsServers[intf.numDns] = addr;
            intf.numDns++;
            c = strtok(NULL, ",");
        }
        if (intf.numDns)
            intf.set |= PUMP_NETINFO_HAS_DNS;
    }

    if (dev == NULL)
        dev = strdup("eth0");

    strncpy(intf.device, dev, 9);

    if ((bootProto != NULL) && (!strncmp(bootProto, "dhcp", 4))) {
        waitForLink(dev);
        nashLogger(_nash_context, NOTICE, "Sending request for IP information through %s\n", dev);
        pumpDhcpClassRun(dev, 0, 0, NULL, 
                         dhcpclass ? dhcpclass : "nash",
                         &intf, NULL);
    } else { /* static IP.  hope enough is specified! */
        if (ip && inet_aton(ip, &addr)) {
            intf.ip = addr;
            intf.set |= PUMP_INTFINFO_HAS_IP;
        }
        if (netmask && inet_aton(netmask, &addr)) {
            intf.netmask = addr;
            intf.network.s_addr = intf.ip.s_addr & intf.netmask.s_addr;
            intf.broadcast.s_addr = intf.network.s_addr | ~intf.netmask.s_addr;
            intf.set |= PUMP_INTFINFO_HAS_NETMASK | PUMP_INTFINFO_HAS_NETWORK |
                        PUMP_INTFINFO_HAS_BROADCAST;
        }
        if (gateway && inet_aton(gateway, &addr)) {
            intf.gateway = addr;
            intf.set |= PUMP_NETINFO_HAS_GATEWAY;
        }
    }

    err =  pumpSetupInterface(&intf);
    if (err) {
        nashLogger(_nash_context, ERROR, "ERROR: Interface setup failed: %s\n", err);
        return 1;
    }
    if (intf.set & PUMP_NETINFO_HAS_GATEWAY) {
        pumpSetupDefaultGateway(&intf.gateway);
    }
    writeResolvConf(intf);

    sleep(2);
    return 0;
}
