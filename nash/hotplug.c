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

#include <argz.h>
#include <envz.h>

#include <nash.h>
#include "util.h"
#include "lib.h"

#include <asm/types.h>

#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif

static pid_t ppid = -1;

static struct nash_uevent_handler *uh = NULL;

/* Set the 'loading' attribute for a firmware device.
 * 1 == currently loading
 * 0 == done loading
 * -1 == error
 */
static inline int
get_loading_fd(const char *device)
{
    int fd = -1;
    char *loading_path = NULL;

    if (asprintfa(&loading_path, "%s/loading", device) < 0)
        return -1;
    fd = open(loading_path, O_RDWR | O_SYNC );
    return fd;
}

static inline int
set_loading(int fd, int value)
{
    int rc;

    if (value == -1)
        rc = write(fd, "-1", 3);
    else if (value == 0)
        rc = write(fd, "0", 2);
    else if (value == 1)
        rc = write(fd, "1", 2);
    fsync(fd);
    fdatasync(fd);

    return rc < 0 ? rc : 0;
}

static inline int
set_timeout(int value)
{
    FILE *f = NULL;

    if (!(f = fopen("/sys/class/firmware/timeout", "w+")))
        return -1;

    fprintf(f, "%d", value);
    fclose(f);
    return 0;
}

static inline int
fd_map(int fd, char **buf, size_t *bufsize)
{
    struct stat stats;
    int en = 0;

    if (fstat(fd, &stats) < 0) {
        en = errno;
        close(fd);
        errno = en;
        return -1;
    }

    *buf = mmap(NULL, stats.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (*buf == MAP_FAILED) {
        *buf = NULL;
        en = errno;
        close(fd);
        errno = en;
        return -1;
    }
    *bufsize = stats.st_size;
    return 0;
}

static inline int
file_map(const char *filename, char **buf, size_t *bufsize, int flags)
{
    int fd, en, rc = 0;

    if ((fd = open(filename, flags ? flags : O_RDONLY)) < 0)
        return -1;

    if (fd_map(fd, buf, bufsize) < 0)
        rc = -1;

    en = errno;
    close(fd);
    errno = en;

    return rc;
}

static inline void
file_unmap(void *buf, size_t bufsize)
{
    munmap(buf, bufsize);
}

static int
nashDefaultFileFetcher(char *inpath, int outfd)
{
    char *inbuf = NULL;
    size_t inlen;
    int count;
    int en = 0;

    errno = 0;
    if (access(inpath, F_OK))
        goto out;

    if (file_map(inpath, &inbuf, &inlen, O_RDONLY) < 0)
        goto out;

    lseek(outfd, 0, SEEK_SET);
    ftruncate(outfd, 0);
    ftruncate(outfd, inlen);

    count = 0;
    while (count < inlen) {
        ssize_t c;
        c = write(outfd, inbuf + count, inlen - count);
        if (c <= 0)
            goto out;
        count += c;
    }

out:
    en = errno;
    if (inbuf)
        file_unmap(inbuf, inlen);
    if (en) {
        errno = en;
        return -1;
    }
    return 0;
}

void
nashSetFileFetcher(nashContext *nc, nashFileFetcher_t fetcher)
{
    if (!fetcher)
        nc->fetcher = nashDefaultFileFetcher;
    else
        nc->fetcher = fetcher;
}

nashFileFetcher_t
nashGetFileFetcher(nashContext *nc)
{
    return nc->fetcher;
}

void
nashSetDelayParent(nashContext *nc, nashDelayFunction_t delayParent)
{
    nc->delayParent = delayParent;
}

int
nashSetFirmwarePath(nashContext *nc, char *dir)
{
    char *old = nc->fw_pathz, *new = NULL;
    size_t old_len = nc->fw_pathz_len;

    nc->fw_pathz = NULL;
    nc->fw_pathz_len = -1;
    if (!dir) {
        if (old)
            free(old);
        return 0;
    }

    if (strlen(dir) > 1023) {
        errno = E2BIG;
        goto out;
    }

    if ((new = strdup(dir)) == NULL)
        goto out;

    nc->fw_pathz = NULL;
    nc->fw_pathz_len = 0;
    if (argz_create_sep(new, ':', &nc->fw_pathz, &nc->fw_pathz_len) != 0)
        goto out;

    if (old)
        free(old);

    return 0;
out:
    if (new)
        free(new);
    nc->fw_pathz = old;
    nc->fw_pathz_len = old_len;

    return -1;
}

char *
nashGetFirmwarePath(nashContext *nc)
{
    static char path[1024];
    char *pathz = NULL;
    size_t n;

    argz_stringify(nc->fw_pathz, nc->fw_pathz_len, ':');
    n = strlen(nc->fw_pathz);
    memmove(path, nc->fw_pathz, n > 1023 ? 1023 : n);
    path[1023] = '\0';

    n = 0;
    argz_create_sep(nc->fw_pathz, ':', &pathz, &n);
    nc->fw_pathz = pathz;
    nc->fw_pathz_len = n;

    return path;
}

static int
_load_firmware(nashContext *nc, int fw_fd, char *sysdir, int timeout)
{
    int rc;
    char *fw_buf = NULL, *data = NULL;
    size_t fw_len = 0;
    int dfd = -1, lfd = -1;
    int loading = -2;
    size_t count;

    if ((lfd = get_loading_fd(sysdir)) < 0) {
        nashLogger(nc, NASH_ERROR, "loading fd failed\n");
        return lfd;
    }

    timeout *= 1000000;
    while (access(sysdir, F_OK) && timeout) {
        nashLogger(nc, NASH_DEBUG, "waiting for %s\n", sysdir);
        udelay(100);
        timeout -= 100;
    }
    if (!timeout) {
        nashLogger(nc, NASH_ERROR, "timeout loading firmware\n");
        return -ENOENT;
    }

    nashLogger(nc, NASH_DEBUG, "Writing firmware to '%s/data'\n", sysdir);
    set_loading(lfd, 1);
    loading = -1;

    if (fd_map(fw_fd, &fw_buf, &fw_len) < 0) {
        rc = -errno;
        nashLogger(nc, NASH_ERROR, "load_firmware: mmap: %m\n");
        goto out;
    }

    if (asprintfa(&data, "%s/data", sysdir) < 0) {
        rc = -errno;
        nashLogger(nc, NASH_ERROR, "%s: %d: %m\n", __func__, __LINE__);
        goto out;
    }
    if ((dfd = open(data, O_RDWR)) < 0) {
        rc = -errno;
        nashLogger(nc, NASH_ERROR, "failed to open %s: %m\n", data);
        goto out;
    }
    count = 0;
    while (count < fw_len) {
        ssize_t c;
        if ((c = write(dfd, fw_buf + count, fw_len - count)) <= 0) {
            nashLogger(nc, NASH_ERROR, "load_firmware: write: %m\n");
            goto out;
        }
        count += c;
    }
    loading = 0;

out:
    if (dfd >= 0)
        close(dfd);
    if (fw_buf)
        file_unmap(fw_buf, fw_len);
    if (loading != -2)
        set_loading(lfd, loading);
    if (lfd >= 0)
        close(lfd);

    return rc;
}

static void
load_firmware(nashContext *nc, nashUEvent *ue)
{
    char *physdevbus = NULL, *physdevdriver = NULL, *physdevpath = NULL,
         *devpath = NULL, *firmware = NULL, *timeout;
    char *dp = NULL;
    char *fw_file = NULL, *sys_file = NULL;
    char *entry;
    int timeout_secs;
    char template[] = "nash-hotplug-firmware-XXXXXX";
    int fd = -1;

    devpath = envz_get(ue->envz, ue->envz_len, "DEVPATH");
    firmware = envz_get(ue->envz, ue->envz_len, "FIRMWARE");
    physdevbus = envz_get(ue->envz, ue->envz_len, "PHYSDEVBUS");
    physdevdriver = envz_get(ue->envz, ue->envz_len, "PHYSDEVDRIVER");
    physdevpath = envz_get(ue->envz, ue->envz_len, "PHYSDEVPATH");
    timeout = envz_get(ue->envz, ue->envz_len, "TIMEOUT");
    
    if (!devpath || !firmware || !physdevbus || !physdevdriver || !physdevpath) {
        nashLogger(nc, NASH_ERROR, "couldn't get environment\n");
        nashLogger(nc, NASH_ERROR, "%s: %s\n", "DEVPATH", devpath);
        nashLogger(nc, NASH_ERROR, "%s: %s\n", "FIRMWARE", firmware);
        nashLogger(nc, NASH_ERROR, "%s: %s\n", "PHYSDEVBUS", physdevbus);
        nashLogger(nc, NASH_ERROR, "%s: %s\n", "PHYSDEVDRIVER", physdevdriver);
        nashLogger(nc, NASH_ERROR, "%s: %s\n", "PHYSDEVPATH", physdevpath);
        nashLogger(nc, NASH_ERROR, "%s: %s\n", "TIMEOUT", timeout);
        return;
    }
    nashLogger(nc, NASH_DEBUG, "DEVPATH: %s\n", devpath);
    nashLogger(nc, NASH_DEBUG, "FIRMWARE: %s\n", firmware);
    nashLogger(nc, NASH_DEBUG, "PHYSDEVBUS: %s\n", physdevbus);
    nashLogger(nc, NASH_DEBUG, "PHYSDEVDRIVER: %s\n", physdevdriver);
    nashLogger(nc, NASH_DEBUG, "PHYSDEVPATH: %s\n", physdevpath);
    nashLogger(nc, NASH_DEBUG, "TIMEOUT: %s\n", timeout);

    timeout_secs = strtol(timeout, NULL, 10);

    if ((fd = mkstemp(template)) < 0)
        return;

    /* find the file */
    for (entry = nc->fw_pathz; entry;
            entry = argz_next(nc->fw_pathz, nc->fw_pathz_len, entry)) {
        if (asprintf(&fw_file, "%s/%s", entry, firmware) < 0) {
            nashLogger(nc, NASH_ERROR, "%s: %d: %m\n", __func__, __LINE__);
            return;
        }
        if (nc->fetcher(fw_file, fd) >= 0)
            break;

        free(fw_file);
        fw_file = NULL;
        if (errno == ENOENT || errno == EPERM)
            continue;
        break;
    }
    if (!fw_file) {
        nashLogger(nc, NASH_ERROR, "Firmware '%s' could not be read\n", fw_file);
        goto out;
    }

    /* try the new way first */
    /* PJFIX this is messy */
    dp = strdup(devpath + 2 + strcspn(devpath+1, "/"));
    if (!dp)
        goto out;
    dp[strcspn(dp, "/")] = ':';

    if (asprintf(&sys_file, "/sys%s/%s/", physdevpath, dp) < 0) {
        free(dp);
        nashLogger(nc, NASH_ERROR, "%s: %d: %m\n", __func__, __LINE__);
        goto out;
    }
    free(dp);
    if (_load_firmware(nc, fd, sys_file, timeout_secs) >= 0)
        goto out;

    /* ok, try the old way */
    free(sys_file);
    sys_file = NULL;

    if (asprintf(&sys_file, "/sys%s/", devpath) < 0) {
        nashLogger(nc, NASH_ERROR, "%s: %d: %m\n", __func__, __LINE__);
        goto out;
    }
    _load_firmware(nc, fd, sys_file, timeout_secs);
out:
    if (fw_file)
        free(fw_file);
    if (sys_file)
        free(sys_file);
    if (fd != -1)
        close(fd);
}

static int
handle_single_uevent(nashContext *nc, nashUEvent *ue, long prev)
{
    typedef enum states {
        IDLE,
        HANDLE_FIRMWARE_ADD,
        HANDLE_FIRMWARE_REMOVE,
        END_OF_LIST,
    } states;
    states state = IDLE;
    char *token;
    char *action = NULL, *subsystem = NULL, *seqnum = NULL;
    long cur;
    int done;

    action = envz_get(ue->envz, ue->envz_len, "ACTION");
    subsystem = envz_get(ue->envz, ue->envz_len, "SUBSYSTEM");
    seqnum = envz_get(ue->envz, ue->envz_len, "SEQNUM");

    if (!action || !subsystem || !seqnum) {
        nashLogger(nc, NASH_ERROR, "couldn't get environment\n");
        nashLogger(nc, NASH_ERROR, "ACTION=\"%s\" SUBSYSTEM=\"%s\" SEQNUM=\"%s\"\n",
            action, subsystem, seqnum);
        return prev;
    }

    cur = strtol(seqnum, NULL, 0);
    if (cur < prev)
        nashLogger(nc, NASH_ERROR, "WARNING: events out of order: %ld -> %ld\n", prev, cur);
    else if (cur == prev)
        nashLogger(nc, NASH_ERROR, "WARNING: duplicate event %ld\n", cur);

    while (!done) switch (state) {
        case IDLE:
            if (!strcmp(action, "add") && !strcmp(subsystem, "firmware")) {
                state = HANDLE_FIRMWARE_ADD;
                token=strdup(envz_get(ue->envz, ue->envz_len, "FIRMWARE"));
                if (nc->delayParent)
                    nc->delayParent(nc, 500000);
                load_firmware(nc, ue);
            } else {
                //nashLogger(nc, NASH_ERROR, "unkown action %s %s\n", action, subsystem);
            }
            done = 1;
            break;
        case HANDLE_FIRMWARE_ADD:
            if (!strcmp(ue->msg, "remove") &&
                    !strcmp(subsystem, "firmware")) {
                if (strcmp(token, envz_get(ue->envz, ue->envz_len, "FIRMWARE")))
                    nashLogger(nc, NASH_ERROR, "WARNING: add firmware %s followed by remove firmware %s\n", token, envz_get(ue->envz, ue->envz_len, "FIRMWARE"));
                free(token);
                token = NULL;
                state = HANDLE_FIRMWARE_REMOVE;
            } else {
                //nashLogger(nc, NASH_ERROR, "unkown action %s %s\n", action, subsystem);
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

    return cur;
}

static void
handle_events(nashContext *nc)
{
    int seqnum = -1, ret;
    nashUEvent uevent;
    struct timespec timeout = {-1,-1};
    struct pollfd pd = {
        .events = POLLIN | POLLPRI | POLLERR | POLLHUP | POLLMSG,
        .revents = 0,
        .fd = nc->hp_childfd,
    };
    int doexit = 0;

    memset(&uevent, '\0', sizeof (uevent));

    while ((ret = nashGetUEventPoll(uh, &timeout, &uevent, &pd, 1)) >= 0 &&
            !doexit) {
        seqnum = handle_single_uevent(nc, &uevent, seqnum);
        if (uevent.msg)
            free(uevent.msg);
        if (uevent.path)
            free(uevent.path);
        while (uevent.envz)
            argz_delete(&uevent.envz, &uevent.envz_len, uevent.envz);
        memset(&uevent, '\0', sizeof (uevent));

        if (pd.revents) {
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
                    nashLogger(nc, NASH_ERROR, "parent exited without telling us\n");
                    close(nc->hp_childfd);
                    nc->hp_childfd = -1;
                    break;
                }
                count += rc;
            }
            if (rc < 0)
                nashLogger(nc, NASH_ERROR, "read didn't work: %m\n");
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
            pd.revents = 0;
        }
    }
    if (nc->hp_childfd >= 0)
        close(nc->hp_childfd);
}

static int
send_hotplug_message(nashContext *nc, char buf[13])
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
nashHotplugKill(nashContext *nc)
{
    if (send_hotplug_message(nc, "die udev die")) {
        close(nc->hp_parentfd);
        nc->hp_parentfd = -1;
        nc->hp_childfd = -1;
    }
}

void
nashHotplugNewRoot(nashContext *nc) 
{
    send_hotplug_message(nc, "set new root");
}

void
nashHotplugNotifyExit(nashContext *nc)
{
    send_hotplug_message(nc, "great egress");
}

#ifdef FWDEBUG
nashContext *_hotplug_nash_context = NULL;

static void
kill_hotplug_signal(int signal)
{
    nashHotplugKill(_hotplug_nash_context);
}
#endif

static int
daemonize(nashContext *nc)
{
    int i;
    int socket;

    nc->hp_parent_pid = getpid();
    nc->hp_child_pid = fork();
    if (nc->hp_child_pid < 0) {
        nashLogger(nc, NASH_ERROR, "could not fork hotplug handler: %m\n");
        close(nc->hp_parentfd);
        nc->hp_parentfd = -1;
        close(nc->hp_childfd);
        nc->hp_childfd = -1;
        return -1;
    }
    if (nc->hp_child_pid > 0) {
        /* parent */
#ifdef FWDEBUG
        signal(SIGINT, kill_hotplug_signal);
#endif
        usleep(250000);
        return 0;
    }

    /* child */
    if (!(uh = nashUEventHandlerNew(nc))) {
        nashLogger(nc, NASH_ERROR, "could not open netlink socket: %m\n");
        close(nc->hp_parentfd);
        nc->hp_parentfd = -1;
        exit(1);
    }
    socket = nashUEventHandlerGetFd(uh);

    prctl(PR_SET_NAME, "nash-hotplug", 0, 0, 0);
    chdir("/");

#ifndef FWDEBUG
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
    for (i = 3; i < getdtablesize(); i++) {
        if (i != nc->hp_childfd && i != socket)
            close(i);
    }

    setsid();

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
#else /* FWDEBUG */
    signal(SIGINT, SIG_IGN);
#endif /* FWDEBUG */

    i = open("/proc/self/oom_adj", O_RDWR);
    if (i >= 0) {
        write(i, "-17", 3);
        close(i);
    }

    set_timeout(10);
    handle_events(nc);
    exit(0);
}

static void
unset_sysctl_hotplug(void)
{
    int fd;
    
    if ((fd = open("/proc/sys/kernel/hotplug", O_RDWR)) < 0)
        return;
    ftruncate(fd, 0);
    write(fd, "\n", 1);
}

int 
nashHotplugInit(nashContext *nc) {
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

        unset_sysctl_hotplug();
        /* child never returns from this, only parent */
        if (daemonize(nc) < 0)
            return 1;
    }
    return 0;
}

#ifdef FWDEBUG

int logger(nashContext *nc, const nash_log_level level, const char *fmt, va_list ap)
{
    FILE *f;
    int ret;
    va_list apc;

    va_copy(apc, ap);
    f = fopen("/dev/tty8", "a+");
    if (!f)
        f = stderr;
    ret = vfprintf(f, fmt, apc);
    va_end(apc);
    fclose(f);
    return ret;
}

static void
testAlarmHandler(int signum)
{
    udelay(500000);

    signal(signum, testAlarmHandler);
}

static void 
sendParentAlarm(nashContext *nc, int usec)
{
    kill(nc->hp_parent_pid, SIGALRM);
}

int main(int argc, char *argv[]) {
    putenv("MALLOC_PERTURB_=204");
    signal(SIGALRM, testAlarmHandler);

    _hotplug_nash_context = nashNewContext();
    nashSetDelayParent(_hotplug_nash_context, sendParentAlarm);
    nashSetLogger(_hotplug_nash_context, logger);
    nashSetFirmwarePath(_hotplug_nash_context, "/firmware");
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
