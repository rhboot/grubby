/*
 * uevent.c -- reads uevents from the kernel
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define _GNU_SOURCE 1

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <termios.h>

#include <argz.h>
#include <envz.h>

#include <nash.h>
#include "util.h"
#include "lib.h"

#include <asm/types.h>
#include <linux/netlink.h>

#ifndef NETLINK_KOBJECT_UEVENT
#define NETLINK_KOBJECT_UEVENT 15
#endif

#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif

struct nash_uevent_handler {
    nashContext *nc;
    int socket; 

    char *msg;
    char *path;
    char **envp;
    char *envz;
};

struct nash_uevent_handler *
nashUEventHandlerNew(nashContext *nc)
{
    struct nash_uevent_handler *handler = NULL;
    struct sockaddr_nl sa;

    if ((handler = calloc(1, sizeof (*handler))) == NULL) {
        nashLogger(nc, NASH_ERROR, "could not allocate uevent handler: %m\n");
        return NULL;
    }
    handler->nc = nc;

    handler->socket = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (handler->socket < 0) {
        nashLogger(nc, NASH_ERROR, "could not open netlink socket: %m\n");
        free(handler);
        return NULL;
    }
    setFdCoe(handler->socket, 1);

    memset(&sa, '\0', sizeof (sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = getpid();
    sa.nl_groups = -1;

    if (bind(handler->socket, (struct sockaddr *)&sa, sizeof (sa)) < 0) {
        nashLogger(nc, NASH_ERROR, "could not bind to netlink socket: %m\n");
        close(handler->socket);
        free(handler);
        return NULL;
    }
    return handler;
}

int
nashUEventHandlerGetFd(struct nash_uevent_handler *handler)
{
    return handler->socket;
}

void _nashUEventHandlerDestroy(struct nash_uevent_handler **handler)
{
    close((*handler)->socket);
    free(*handler);
    *handler = NULL;
}

static int
get_netlink_msg(int fd, nashUEvent *uevent)
{
    size_t len;
    ssize_t size;
    static char buffer[2560];
    char *pos;
    char *msg = NULL, *path = NULL, *envz = NULL;
    char *argv[] = { NULL };
    size_t envz_len;
    error_t errnum;

    size = recv(fd, &buffer, sizeof (buffer), 0);
    if (size < 0)
        return -1;

    if ((size_t)size > sizeof (buffer) - 1)
        size = sizeof (buffer) - 1;
    buffer[size] = '\0';

    len = strcspn(buffer, "@");
    if (!buffer[len])
        return -1;

    if ((errnum = argz_create(argv, &envz, &envz_len)) > 0)
        goto err;

    pos = buffer;
    msg = strndup(pos, len++);
    pos += len;
    path = strdup(pos);

    pos += strlen(pos) + 1;
    if (len < size + 1) {
        while (pos[0]) {
            char *value = strchr(pos, '=');
            if (value)
                *(value++) = '\0';

            if ((errnum = envz_add(&envz, &envz_len, pos, value)) > 0)
                goto err;
            pos += strlen(pos) + 1;
            if (*pos)
                pos += strlen(pos) + 1;
        }
    }

    uevent->msg = msg;
    uevent->path = path;
    uevent->envz = envz;
    uevent->envz_len = envz_len;
    return 0;
err:
    if (msg)
        free(msg);
    if (path)
        free(path);
    while(envz)
        argz_delete(&envz, &envz_len, envz);
    errno = errnum;
    return -1;
}

int 
nashGetUEventPoll(struct nash_uevent_handler *handler, struct timespec *timeout,
    nashUEvent *uevent, struct pollfd *inpd, int npfds)
{
    nashContext *nc = handler->nc;
    int socket = handler->socket;
    struct pollfd *pds;
    struct timespec to = { -1, -1 };
    int errnum;
    int rc;
    int i;

    if ((pds = calloc(npfds+1, sizeof (*pds))) < 0)
        return -1;

    memmove(pds, inpd, npfds * sizeof (*pds));
    pds[npfds].events = POLLIN | POLLPRI | POLLERR | POLLHUP | POLLMSG;
    pds[npfds].revents = 0;
    pds[npfds].fd = handler->socket;

    to = timeout ? *timeout : to;
    while ((rc = nash_ppoll(pds, npfds+1, &to, NULL, 0)) <= 0) {
        if (rc < 0) {
            if (errno == EINTR)
                continue;

            nashLogger(nc, NASH_ERROR, "poll returned error: %m\n");
        } else if (rc == 0) {
            save_errno(errnum, free(pds));
            return 0;
        }
        break;
    }
    memmove(inpd, pds, npfds * sizeof (*pds));

    if (rc == 0) {
        /* timeout */
        errno = 0;
        *timeout = to;
        free(pds);
        return 0;
    }

    for (i = 0; i < npfds+1; i++) {
        if (pds[i].fd != handler->socket)
            continue;

        if (pds[i].revents) {
            if (get_netlink_msg(socket, uevent) < 0) {
                errnum = errno;
                nashLogger(nc, NASH_ERROR, "get_netlink_msg returned %m\n");
                errno = errnum;
                rc = -1;
                break;
            }
        }
    }
    save_errno(errnum, free(pds));
    return rc;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
