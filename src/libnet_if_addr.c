/*
 *  $Id: libnet_if_addr.c,v 1.23 2004/04/13 17:32:28 mike Exp $
 *
 *  libnet
 *  libnet_if_addr.c - interface selection code
 *
 *  Copyright (c) 1998 - 2004 Mike D. Schiffman <mike@infonexus.com>
 *  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "common.h"

#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#include "../include/ifaddrlist.h"

#define MAX_IP_ADDR 512
#define IPADDR_INITIAL_BUFSIZE MAX_IP_ADDR
#define IPADDR_GROW_FACTOR 1.5

#if !(__WIN32__)

/*
 * By testing if we can retrieve the FLAGS of an iface
 * we can know if it exists or not and if it is up.
 */
int 
libnet_check_iface(libnet_t *l)
{
    struct ifreq ifr;
    int fd, res;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, "%s() socket: %s", __func__,
                strerror(errno));
        return (-1);
    }

    strncpy(ifr.ifr_name, l->device, sizeof(ifr.ifr_name) -1);
    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';
    
    res = ioctl(fd, SIOCGIFFLAGS, (int8_t *)&ifr);
    if (res < 0)
    {
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, "%s() ioctl: %s", __func__,
                strerror(errno));
    }
    else
    {
        if ((ifr.ifr_flags & IFF_UP) == 0)
        {
            snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, "%s(): %s is down",
                    __func__, l->device);
	    res = -1;
        }
    }
    close(fd);

    return (res);
}

#endif

#if defined(__OpenBSD__) ||  defined(__linux__)
#include <sys/types.h>
    #ifdef __OpenBSD__
    #include <sys/socket.h>
    #endif
#include <ifaddrs.h>

int
libnet_ifaddrlist(register struct libnet_ifaddr_list **ipaddrp, char *dev, register char *errbuf)
{
    (void)dev; /* unused */
    static struct libnet_ifaddr_list *ifaddrlist = NULL, *ifaddrlist_tmp = NULL;
    static size_t ifaddrlist_len = 0;
    struct ifaddrs *ifap, *ifa;
    int i = 0;

    if (!ifaddrlist) {
        ifaddrlist = calloc(IPADDR_INITIAL_BUFSIZE, sizeof(struct libnet_ifaddr_list));
        if (!ifaddrlist) {
            snprintf(errbuf, LIBNET_ERRBUF_SIZE, "%s(): OOM when allocating initial ifaddrlist", __func__);
            return 0;
        }

        ifaddrlist_len = IPADDR_INITIAL_BUFSIZE;
    }

    if (getifaddrs(&ifap) != 0)
    {
        snprintf(errbuf, LIBNET_ERRBUF_SIZE, "%s(): getifaddrs: %s",
                    __func__, strerror(errno));
        return 0;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_flags & IFF_LOOPBACK || ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET )
        {
            ifaddrlist[i].device = strdup(ifa->ifa_name);
            if (ifaddrlist[i].device == NULL) {
                snprintf(errbuf, LIBNET_ERRBUF_SIZE, "%s(): OOM", __func__);
                continue;
            }
            ifaddrlist[i].addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
            ++i;
        }

        if (i == ifaddrlist_len) {
            ifaddrlist_tmp = realloc(ifaddrlist, ifaddrlist_len * IPADDR_GROW_FACTOR);
            if (!ifaddrlist_tmp) {
                snprintf(errbuf, LIBNET_ERRBUF_SIZE, "%s(): OOM when reallocating larger ifaddrlist", __func__);
                break;
            }

            ifaddrlist = ifaddrlist_tmp;
            ifaddrlist_len = ifaddrlist_len * IPADDR_GROW_FACTOR;
        }
    }

    freeifaddrs(ifap);
    *ipaddrp = ifaddrlist;
    return (i);
}


#else
#if !(__WIN32__)


/*
 *  Return the interface list
 */

#ifdef HAVE_SOCKADDR_SA_LEN
#define NEXTIFR(i) \
((struct ifreq *)((u_char *)&i->ifr_addr + i->ifr_addr.sa_len))
#else
#define NEXTIFR(i) (i + 1)
#endif

#ifndef BUFSIZE
#define BUFSIZE 2048
#endif

#ifdef HAVE_LINUX_PROCFS
#define PROC_DEV_FILE "/proc/net/dev"
#endif

int
libnet_ifaddrlist(register struct libnet_ifaddr_list **ipaddrp, char *dev, register char *errbuf)
{
    register struct libnet_ifaddr_list *al;
    struct ifreq *ifr, *lifr, *pifr, nifr;
    char device[sizeof(nifr.ifr_name)];
    static struct libnet_ifaddr_list ifaddrlist[MAX_IPADDR];
    
    char *p;
    struct ifconf ifc;
    struct ifreq ibuf[MAX_IPADDR];
    register int fd, nipaddr;
    
#ifdef HAVE_LINUX_PROCFS
    FILE *fp;
    char buf[2048];
#endif
    
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
	snprintf(errbuf, LIBNET_ERRBUF_SIZE, "%s(): socket error: %s",
                __func__, strerror(errno));
	return (-1);
    }

#ifdef HAVE_LINUX_PROCFS
    fp = fopen(PROC_DEV_FILE, "r");
    if (!fp)
    {
	snprintf(errbuf, LIBNET_ERRBUF_SIZE,
                "%s(): fopen(proc_dev_file) failed: %s",  __func__,
                strerror(errno));
	goto bad;
    }
#endif

    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_len = sizeof(ibuf);
    ifc.ifc_buf = (caddr_t)ibuf;

    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0)
    {
	snprintf(errbuf, LIBNET_ERRBUF_SIZE,
                "%s(): ioctl(SIOCGIFCONF) error: %s", 
                __func__, strerror(errno));
	goto bad;
    }

    pifr = NULL;
    lifr = (struct ifreq *)&ifc.ifc_buf[ifc.ifc_len];
    
    al = ifaddrlist;
    nipaddr = 0;

#ifdef HAVE_LINUX_PROCFS
    while (fgets(buf, sizeof(buf), fp))
    {
        p = strchr(buf, ':');
	if (!p)
            continue;

        *p = '\0';
        for (p = buf; *p == ' '; p++)
	    ;
	
        strncpy(nifr.ifr_name, p, sizeof(nifr.ifr_name) - 1);
        nifr.ifr_name[sizeof(nifr.ifr_name) - 1] = '\0';
	
#else /* !HAVE_LINUX_PROCFS */

    for (ifr = ifc.ifc_req; ifr < lifr; ifr = NEXTIFR(ifr))
    {
	/* XXX LINUX SOLARIS ifalias */
        p = strchr(ifr->ifr_name, ':');
	if (p)
            *p = '\0';

	if (pifr && strcmp(ifr->ifr_name, pifr->ifr_name) == 0)
            continue;

	strncpy(nifr.ifr_name, ifr->ifr_name, sizeof(nifr.ifr_name) - 1);
	nifr.ifr_name[sizeof(nifr.ifr_name) - 1] = '\0';
#endif

        /* save device name */
        strncpy(device, nifr.ifr_name, sizeof(device) - 1);
        device[sizeof(device) - 1] = '\0';

        if (ioctl(fd, SIOCGIFFLAGS, &nifr) < 0)
        {
            pifr = ifr;
            continue;
	}
        if ((nifr.ifr_flags & IFF_UP) == 0)
	{
            pifr = ifr;
            continue;	
	}

        if (dev == NULL && LIBNET_ISLOOPBACK(&nifr))
	{
            pifr = ifr;
            continue;
	}
	
        strncpy(nifr.ifr_name, device, sizeof(device) - 1);
        nifr.ifr_name[sizeof(nifr.ifr_name) - 1] = '\0';
        if (ioctl(fd, SIOCGIFADDR, (int8_t *)&nifr) < 0)
        {
            if (errno != EADDRNOTAVAIL)
            {
                snprintf(errbuf, LIBNET_ERRBUF_SIZE,
                        "%s(): SIOCGIFADDR: dev=%s: %s", __func__, device,
                        strerror(errno));
                goto bad;
	    }
            else /* device has no IP address => set to 0 */
            {
                al->addr = 0;
            }
        }
        else
        {
            al->addr = ((struct sockaddr_in *)&nifr.ifr_addr)->sin_addr.s_addr;
        }
        
        free(al->device);

        al->device = strdup(device);
        if (al->device == NULL)
        {
            snprintf(errbuf, LIBNET_ERRBUF_SIZE, 
                    "%s(): strdup not enough memory", __func__);
            goto bad;
        }

        ++al;
        ++nipaddr;

#ifndef HAVE_LINUX_PROCFS
        pifr = ifr;
#endif

    } /* while|for */
	
#ifdef HAVE_LINUX_PROCFS
    if (ferror(fp))
    {
        snprintf(errbuf, LIBNET_ERRBUF_SIZE,
                "%s(): ferror: %s", __func__, strerror(errno));
	goto bad;
    }
    fclose(fp);
#endif

    close(fd);
    *ipaddrp = ifaddrlist;

    return (nipaddr);

    bad:
#ifdef HAVE_LINUX_PROCFS
    if (fp)
	fclose(fp);
#endif
    close(fd);
    return (-1);
}
#else
/* WIN32 support *
 * TODO move win32 support into win32 specific source file */

/* From tcptraceroute, convert a numeric IP address to a string */
#define IPTOSBUFFERS    12
static int8_t *iptos(uint32_t in)
{
    static int8_t output[IPTOSBUFFERS][ 3 * 4 + 3 + 1];
    static int16_t which;
    uint8_t *p;

    p = (uint8_t *)&in;
    which = (which + 1 == IPTOSBUFFERS ? 0 : which + 1);
    snprintf(output[which], IPTOSBUFFERS, "%d.%d.%d.%d", 
            p[0], p[1], p[2], p[3]);
    return output[which];
}

int
libnet_ifaddrlist(register struct libnet_ifaddr_list **ipaddrp, char *dev_unused, register char *errbuf)
{
    int nipaddr = 0;
    int i = 0;
    static struct libnet_ifaddr_list ifaddrlist[MAX_IPADDR];
    pcap_if_t *devlist = NULL;
    pcap_if_t *dev = NULL;
    int8_t err[PCAP_ERRBUF_SIZE];

    /* Retrieve the interfaces list */
    if (pcap_findalldevs(&devlist, err) == -1)
    {
        snprintf(errbuf, LIBNET_ERRBUF_SIZE, 
                "%s(): error in pcap_findalldevs: %s", __func__, err);
        return (-1);
    }

    for (dev = devlist; dev; dev = dev->next)
    {
        struct pcap_addr* pcapaddr;
        for(pcapaddr = dev->addresses; pcapaddr; pcapaddr = pcapaddr->next) {
            struct sockaddr* addr = pcapaddr->addr;
#if 0
            printf("if name '%s' description '%s' loop? %d\n", dev->name, dev->description, dev->flags);
            {
                char p[NI_MAXHOST] = "";
                int sz = sizeof(struct sockaddr_storage);
                int r;
                r = getnameinfo(addr, sz, p, sizeof(p), NULL,0, NI_NUMERICHOST);
                printf("  addr %s\n", r ? gai_strerror(r) : p);
            }
#endif

            if (dev->flags & PCAP_IF_LOOPBACK)
                continue;

            /* this code ignores IPv6 addresses, a limitation of the libnet_ifaddr_list struct */

            if (addr->sa_family == AF_INET) {
                ifaddrlist[i].device = strdup(dev->name);
                ifaddrlist[i].addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
                ++i;
                ++nipaddr;
            }
        }
    }

    pcap_freealldevs(devlist);

    *ipaddrp = ifaddrlist;

    return nipaddr;
}
#endif /* __WIN32__ */

#endif /* __OpenBSD__ */

int
libnet_select_device(libnet_t *l)
{
    int c, i;
    struct libnet_ifaddr_list *address_list, *al;
    uint32_t addr;


    if (l == NULL)
    { 
        return (-1);
    }

    if (l->device && !isdigit(l->device[0]))
    {
#if !(__WIN32__)
	if (libnet_check_iface(l) < 0)
	{
            /* err msg set in libnet_check_iface() */
	    return (-1);
	}
#endif
	return (1);
    }

    /*
     *  Number of interfaces.
     */
    c = libnet_ifaddrlist(&address_list, l->device, l->err_buf);
    if (c < 0)
    {
        return (-1);
    }
    else if (c == 0)
    {
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE,
                "%s(): no network interface found", __func__);
        return (-1);
    }
	
    al = address_list;
    if (l->device)
    {
        addr = libnet_name2addr4(l, l->device, LIBNET_DONT_RESOLVE);

        for (i = c; i; --i, ++address_list)
        {
            if (
                    0 == strcmp(l->device, address_list->device)
                    || 
                    address_list->addr == addr
               )
            {
                /* free the "user supplied device" - see libnet_init() */
                free(l->device);
                l->device =  strdup(address_list->device);
                goto good;
            }
        }
        if (i <= 0)
        {
            snprintf(l->err_buf, LIBNET_ERRBUF_SIZE,
                    "%s(): can't find interface for IP %s", __func__,
                    l->device);
	    goto bad;
        }
    }
    else
    {
        l->device = strdup(address_list->device);
    }

good:
    for (i = 0; i < c; i++)
    {
        free(al[i].device);
        al[i].device = NULL;
    }
    return (1);

bad:
    for (i = 0; i < c; i++)
    {
        free(al[i].device);
        al[i].device = NULL;
    }
    return (-1);
}

/**
 * Local Variables:
 *  indent-tabs-mode: nil
 *  c-file-style: "stroustrup"
 * End:
 */
