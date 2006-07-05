/*
* hotplug.c -- firmware loading/hotplug event handling
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

static pid_t ppid = -1;

static int netlink = -1;

/* Set the 'loading' attribute for a firmware device.
 * 1 == currently loading
 * 0 == done loading
 * -1 == error
 */
static inline int
set_loading(int fd, const char *device, int value)
{
    char buf[10] = {'\0'};
    int rc;

    if (fd < 0) {
        char loading_path[1024];

        snprintf(loading_path, sizeof(loading_path), "/sys/%s/loading", device);
        loading_path[sizeof(loading_path)-1] = '\0';
        fd = open(loading_path, O_RDWR | O_SYNC );
        if (fd < 0)
            return fd;
    }
    if ((rc = snprintf(buf, 9, "%d", value)) < 0 ||
            (rc = write(fd, buf, strlen(buf) + 1)) < 0) {
        close(fd);
        return rc;
    }
    return fd;
}

static inline int
set_timeout(int value)
{
    char buf[10] = {'\0'};
    int fd, rc;

    fd = open("/sys/class/firmware/timeout", O_RDWR | O_SYNC);
    if (fd < 0)
        return fd;

    if ((rc = snprintf(buf, 9, "%d", value)) < 0)
        rc = write(fd, buf, strlen(buf) + 1);
    close(fd);
    return rc;
}

static inline int
file_map(const char *filename, char **buf, size_t *bufsize)
{
    struct stat stats;
    int fd;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    if (fstat(fd, &stats) < 0) {
        close(fd);
        return -1;
    }

    *buf = mmap(NULL, stats.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (*buf == MAP_FAILED) {
        close(fd);
        return -1;
    }
    *bufsize = stats.st_size;

    close(fd);

    return 0;
}

static inline void
file_unmap(void *buf, size_t bufsize)
{
    munmap(buf, bufsize);
}

static char fw_dir[1024] = "/lib/firmware/";

int
nashSetFirmwareDir(char *dir)
{
    int n;

    if (!dir) {
        errno = EINVAL;
        return 0;
    }

    if (access(dir, X_OK))
        return -1;

    n = strlen(dir);
    if (dir[n] != '/' && n > 1022) {
        errno = E2BIG;
        return 0;
    }
    strcpy(fw_dir, dir);
    if (dir[n] != '/')
        fw_dir[++n] = '/';
    fw_dir[n+1] = '\0';

    return 0;
}

static void
load_firmware(void)
{
    char *physdevbus = NULL, *physdevdriver = NULL, *physdevpath = NULL,
         *devpath = NULL, *firmware = NULL;
    char fw[1024] = "", driver[1024] = "/sys/bus/", data[1024] = "/sys";
    int dfd=-1, lfd=-1;
    int timeout = 0, loading = -2;
    char *fw_buf = NULL;
    size_t fw_len=0;
    size_t count;

    strcpy(fw, fw_dir);
    devpath = getenv("DEVPATH");
    firmware = getenv("FIRMWARE");
    physdevbus = getenv("PHYSDEVBUS");
    physdevdriver = getenv("PHYSDEVDRIVER");
    physdevpath = getenv("PHYSDEVPATH");
    
    if (!devpath || !firmware || !physdevbus || !physdevdriver || !physdevpath) {
        fprintf(stderr, "couldn't get environment\n");
        fprintf(stderr, "%s: %s\n", "DEVPATH", devpath);
        fprintf(stderr, "%s: %s\n", "FIRMWARE", firmware);
        fprintf(stderr, "%s: %s\n", "PHYSDEVBUS", physdevbus);
        fprintf(stderr, "%s: %s\n", "PHYSDEVDRIVER", physdevdriver);
        fprintf(stderr, "%s: %s\n", "PHYSDEVPATH", physdevpath);
        return;
    }

    driver[9] = '\0';
    strcat(driver, physdevbus);
    strcat(driver, "/drivers/");
    strcat(driver, physdevdriver);
    while (access(driver, F_OK)) {
        fprintf(stderr, "waiting for %s\n", driver);
        udelay(100);
    }

    lfd = set_loading(lfd, devpath, 1);

    fw[14] = '\0';
    strcat(fw, firmware);
    if (file_map(fw, &fw_buf, &fw_len) < 0) {
        fw_buf = NULL;
        loading = -2;
        goto out;
    }

    data[4] = '\0';
    strcat(data, devpath);
    strcat(data, "/data");
    dfd = open(data, O_RDWR);
    if (dfd < 0) {
        fprintf(stderr, "failed to open %s\n", data);
        loading = -1;
        goto out;
    }
    count = 0;
    while (count < fw_len) {
        ssize_t c;
        c = write(dfd, fw_buf + count, fw_len - count);
        if (c <= 0) {
            loading = -1;
            goto out;
        }
        count += c;
    }
    loading = 0;

out:
    if (timeout)
        fprintf(stderr, "timeout loading %s\n", firmware);

    if (dfd >= 0)
        close(dfd);
    if (fw_buf)
        file_unmap(fw_buf, fw_len);
    if (loading != -2)
        lfd = set_loading(lfd, devpath, loading);
    if (lfd != -1)
        close(lfd);

    return;
}

static int
get_netlink_msg(int fd, char **msg, char **path)
{
    size_t len;
    ssize_t size;
    static char buffer[2560];
    char *pos;

    size = recv(fd, &buffer, sizeof (buffer), 0);
    if (size < 0)
        return -1;

    if ((size_t)size > sizeof (buffer) - 1)
        size = sizeof (buffer) - 1;
    buffer[size] = '\0';

    len = strcspn(buffer, "@");
    if (!buffer[len])
        return -1;

    pos = buffer;
    *msg = strndup(pos, len++);
    pos += len;
    *path = strdup(pos);

    pos += strlen(pos) + 1;
    if (len < size + 1) {
        clearenv();
        putenv("MALLOC_PERTURB_=204");

        while (pos[0]) {
            putenv(pos);
            pos += strlen(pos) + 1;
        }
    }
    return 0;
}

static void
handle_events(struct nash_context *nc)
{
    fd_set fds;
    int maxfd;
    int fdcount;
    int doexit = 0;
    typedef enum states {
        IDLE,
        HANDLE_FIRMWARE_ADD,
        HANDLE_FIRMWARE_REMOVE,
        END_OF_LIST,
    } states;
    states state = IDLE;
    char *token;
    long prev = 0;

    do {
        maxfd = netlink;
        FD_ZERO(&fds);
        if (nc->hp_childfd >= 0) {
            if (nc->hp_childfd > netlink)
                maxfd = nc->hp_childfd;
            FD_SET(nc->hp_childfd, &fds);
        }
        FD_SET(netlink, &fds);

        fdcount = select(maxfd+1, &fds, NULL, NULL, NULL);
        if (fdcount < 0)
            continue;

        if (FD_ISSET(netlink, &fds)) {
            char *action = NULL, *subsystem = NULL, *seqnum = NULL;
            long cur;
            char *msg = NULL, *path = NULL;
            int done = 0;

            if (get_netlink_msg(netlink, &msg, &path) < 0)
                goto testexit;
                
            assert(msg);
            assert(path);

            action = getenv("ACTION");
            subsystem = getenv("SUBSYSTEM");
            seqnum = getenv("SEQNUM");

            if (!action || !subsystem || !seqnum) {
                fprintf(stderr, "couldn't get environment\n");
                fprintf(stderr, "ACTION=\"%s\" SUBSYSTEM=\"%s\" SEQNUM=\"%s\"\n",
                        action, subsystem, seqnum);
                return;
            }
            assert(subsystem);
            assert(action);
            assert(seqnum);

            cur = strtol(seqnum, NULL, 0);
            if (cur < prev)
                fprintf(stderr, "WARNING: events out of order: %ld -> %ld\n", prev, cur);
            if (cur == prev)
                fprintf(stderr, "WARNING: duplicate event %ld\n", cur);
            prev = cur;

            while (!done) switch (state) {
                case IDLE:
                    if (!strcmp(action, "add") && !strcmp(subsystem, "firmware")) {
                        state = HANDLE_FIRMWARE_ADD;
                        token=strdup(getenv("FIRMWARE"));
                        if (ppid != -1)
                            kill(ppid, SIGALRM);
                        load_firmware();
                    } else {
                        //fprintf(stderr, "unkown action %s %s\n", action, subsystem);
                    }
                    done = 1;
                    break;
                case HANDLE_FIRMWARE_ADD:
                    if (!strcmp(msg, "remove") && !strcmp(subsystem, "firmware")) {
                        if (strcmp(token, getenv("FIRMWARE")))
                            fprintf(stderr, "WARNING: add firmware %s followed by remove firmware %s\n", token, getenv("FIRMWARE"));
                        free(token);
                        token = NULL;
                        state = HANDLE_FIRMWARE_REMOVE;
                    } else {
                        //fprintf(stderr, "unkown action %s %s\n", action, subsystem);
                        state = IDLE;
                        done = 1;
                    }
                    break;
                case HANDLE_FIRMWARE_REMOVE:
                default:
                    state = IDLE;
                    done = 1;
                    break;
            }
            free(msg);
            free(path);
        }

testexit:
        if (nc->hp_childfd >= 0 && FD_ISSET(nc->hp_childfd, &fds)) {
            char buf[32] = {'\0'};
            size_t count = 0;
            int tries = 0;
            int rc;

            while ((rc = read(nc->hp_childfd, buf+count, 13)) >= 0 && count+rc < 13) {
                if (rc == 0)
                    tries++;
                else
                    tries=0;
                if (tries == 2) {
                    nashLogger(nc, ERROR, "parent exited without telling us\n");
                    close(nc->hp_childfd);
                    nc->hp_childfd = -1;
                    break;
                }
                count += rc;
            }
            if (rc < 0)
                nashLogger(nc, ERROR, "read didn't work: %m\n");
            count = 0;
            while (rc != count) {
                if (!strncmp(buf + count, "die udev die", 13)) {
                    count += 13;
                    doexit=1;
                } else if (!strncmp(buf + count, "set new root", 13)) {
                    count += 13;
                    chdir("/sysroot");
                    chroot("/sysroot");
                } else if (!strncmp(buf + count, "great egress", 13)) {
                    count += 13;
                    ppid = -1;
                }
            }
            rc = 0;
            continue;
        }
    } while (!doexit);
    if (nc->hp_childfd >= 0)
        close(nc->hp_childfd);
}

static int
send_hotplug_message(struct nash_context *nc, char buf[13])
{
    if (nc->hp_parentfd > 0) {
        write(nc->hp_parentfd, buf, 13);
        fdatasync(nc->hp_parentfd);
        tcdrain(nc->hp_parentfd);
        return 1;
    }
    return 0;
}

void
nashHotplugKill(struct nash_context *nc)
{
    if (send_hotplug_message(nc, "die udev die")) {
        close(nc->hp_parentfd);
        nc->hp_parentfd = -1;
        nc->hp_childfd = -1;
    }
}

void
nashHotplugNewRoot(struct nash_context *nc) 
{
    send_hotplug_message(nc, "set new root");
}

void
nashHotplugNotifyExit(struct nash_context *nc)
{
    send_hotplug_message(nc, "great egress");
}

#ifdef FWDEBUG
struct nash_context *_hotplug_nash_context = NULL;

static void
nashHotplugKill_signal(int signal)
{
    nashHotplugKill(_hotplug_nash_context);
}
#endif

static int
daemonize(struct nash_context *nc)
{
    int i;
    struct sockaddr_nl sa;

    netlink = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (netlink < 0) {
        nashLogger(nc, ERROR, "could not open netlink socket: %m\n");
        close(nc->hp_parentfd);
        return -1;
    }
    setFdCoe(netlink, 1);

    memset(&sa, '\0', sizeof (sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = getpid();
    sa.nl_groups = -1;

    if (bind(netlink, (struct sockaddr *)&sa, sizeof (sa)) < 0) {
        nashLogger(nc, ERROR, "could not bind to netlink socket: %m\n");
        close(netlink);
        close(nc->hp_parentfd);
        return -1;
    }

    ppid = getpid();
    if (fork() > 0) {
        /* parent */
        close(netlink);

#ifdef FWDEBUG
        signal(SIGINT, nashHotplugKill_signal);
#endif
        usleep(250000);
        return 0;
    }
    /* child */

    prctl(PR_SET_NAME, "nash-hotplug", 0, 0, 0);
    chdir("/");

    close(0);
    close(nc->hp_parentfd);
    if (nc->hp_childfd != 0)
        dup2(nc->hp_childfd, 0);
    setFdCoe(0, 1);
#if 0
    close(1);
    dup2(nc->hp_childfd, 1);
    setFdCoe(1, 1);
    close(2);
    dup2(nc->hp_childfd, 2);
    setFdCoe(2, 1);
#else
    close(1);
    i = open("/dev/zero", O_RDWR);
    if (i != 1) {
        dup2(i, 1);
        close(i);
        i = 1;
    }
    setFdCoe(i, 1);

    close(2);
    i = open("/dev/zero", O_RDWR);
    if (i != 2) {
        dup2(i, 2);
        close(i);
        i = 2;
    }
    setFdCoe(i, 1);
#endif
#ifndef FWDEBUG
    for (i = 3; i < getdtablesize(); i++) {
        if (i != nc->hp_childfd && i != netlink)
            close(i);
    }

    setsid();

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
#else
    signal(SIGINT, SIG_IGN);
#endif

    i = open("/proc/self/oom_adj", O_RDWR);
    if (i >= 0) {
        write(i, "-17", 3);
        close(i);
    }

    set_timeout(10);
    handle_events(nc);
    exit(0);
}

int 
nashHotplugInit(struct nash_context *nc) {
    if (nc->hp_parentfd == -1) {
        int filedes[2] = {0,0};
        long flags;

        pipe(filedes);

        nc->hp_childfd = filedes[0];
        flags = 0;
        setFdCoe(nc->hp_childfd, 1);
        fcntl(nc->hp_childfd, F_GETFL, &flags);
        flags |= O_SYNC;
        flags &= ~O_NONBLOCK;
        fcntl(nc->hp_childfd, F_SETFL, flags);

        nc->hp_parentfd = filedes[1];
        flags = 0;
        setFdCoe(nc->hp_parentfd, 1);
        fcntl(nc->hp_parentfd, F_GETFL, &flags);
        flags |= O_SYNC;
        flags &= ~O_NONBLOCK;
        fcntl(nc->hp_parentfd, F_SETFL, flags);

        /* child never returns from this, only parent */
        if (daemonize(nc) < 0)
            return 1;
    }
    return 0;
}

#ifdef FWDEBUG
int main(int argc, char *argv[]) {
    putenv("MALLOC_PERTURB_=204");
    _hotplug_nash_context = nashNewContext();
    nashHotplugInit(_hotplug_nash_context);
    while (sleep(86400) > 0)
        ;
    nashFreeContext(_hotplug_nash_context);
    return 0;
}
#endif

/*
 * vim:ts=8:sw=4:sts=4:et
 */
