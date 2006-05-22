/*
 * nash.c
 *
 * Simple code to load modules, mount root, and get things going. Uses
 * dietlibc to keep things small.
 *
 * Erik Troan (ewt@redhat.com)
 * Jeremy Katz (katzj@redhat.com)
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2002-2005 Red Hat Software
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* We internalize losetup, mount, raidautorun, and echo commands. Other
   commands are run from the filesystem. Comments and blank lines work as
   well, argument parsing is screwy. */

#define _GNU_SOURCE 1

#include "version.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <termios.h>
#include <mntent.h>
#include <execinfo.h>
#include <time.h>

#include <asm/unistd.h>

#include "lib.h"
#include "hotplug.h"
#include "block.h"
#include "dm.h"
#include "net.h"
#define HAVE_NFS 1
#include "sundries.h"

/* Need to tell loop.h what the actual dev_t type is. */
#undef dev_t
#if defined(__alpha) || (defined(__sparc__) && defined(__arch64__))
#define dev_t unsigned int
#else
#define dev_t unsigned short
#endif
#include <linux/loop.h>
#undef dev_t
#define dev_t dev_t

#define syslog klogctl

#include <linux/cdrom.h>
#define MD_MAJOR 9
#include <linux/raid/md_u.h>

#ifndef RAID_AUTORUN
#define RAID_AUTORUN           _IO (MD_MAJOR, 0x14)
#endif

#ifndef MS_REMOUNT
#define MS_REMOUNT      32
#endif

#ifndef MS_BIND
#define MS_BIND 4096
#endif

#ifndef MS_MOVE
#define MS_MOVE 8192
#endif

#ifndef MNT_FORCE
#define MNT_FORCE 0x1
#endif

#ifndef MNT_DETACH
#define MNT_DETACH 0x2
#endif

#define MAX(a, b) ((a) > (b) ? a : b)

#define PATH "/usr/bin:/bin:/sbin:/usr/sbin"
static char * env[] = {
    "PATH=" PATH,
    "LVM_SUPPRESS_FD_WARNINGS=1",
    NULL
};
static char sysPath[] = PATH;

static pid_t ocPid = -1;
static int exit_status = 0;

static int
searchPath(char *bin, char **resolved)
{
    char *fullPath = NULL;
    char *pathStart;
    char *pathEnd;
    int rc;

    if (!strchr(bin, '/')) {
        pathStart = sysPath;
        while (*pathStart) {
            char pec;
            pathEnd = strchr(pathStart, ':');

            if (!pathEnd)
                pathEnd = pathStart + strlen(pathStart);

            pec = *pathEnd;
            *pathEnd = '\0';

            rc = asprintfa(&fullPath, "%s/%s", pathStart, bin);
            if (!fullPath) {
                int errnum = errno;
                eprintf("error searching path: %m\n");
                return -errnum;
            }

            *pathEnd = pec;
            pathStart = pathEnd;
            if (*pathStart)
                pathStart++;

            if (!access(fullPath, X_OK)) {
                *resolved = strdup(fullPath);
                return 0;
            }
        }
    }

    if (!access(bin, X_OK)) {
        *resolved = strdup(bin);
        if (*resolved)
            return 0;
    }
    return -errno;
}

static char *
getArg(char * cmd, char * end, char ** arg)
{
    char quote = '\0';

    if (!cmd || cmd >= end)
        return NULL;

    while (isspace(*cmd) && cmd < end)
        cmd++;
    if (cmd >= end)
        return NULL;

    if (*cmd == '"')
        cmd++, quote = '"';
    else if (*cmd == '\'')
        cmd++, quote = '\'';

    if (quote) {
        *arg = cmd;

        /* This doesn't support \ escapes */
        while (cmd < end && *cmd != quote)
            cmd++;

        if (cmd == end) {
            eprintf("error: quote mismatch for %s\n", *arg);
            return NULL;
        }

        *cmd = '\0';
        cmd++;
    } else {
        *arg = cmd;
        while (!isspace(*cmd) && cmd < end)
            cmd++;
        *cmd = '\0';
        if (**arg == '$')
            *arg = getenv(*arg+1);
        if (*arg == NULL)
            *arg = "";
    }

    cmd++;

    while (isspace(*cmd))
        cmd++;

    return cmd;
}

/* get the contents of the kernel command line from /proc/cmdline */
static char *
getKernelCmdLine(void)
{
    int fd, i, errnum;
    static char * buf = NULL;

    if (buf)
        return buf;

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        eprintf("getKernelCmdLine: failed to open /proc/cmdline: %m\n");
        return NULL;
    }

    i = readFD(fd, &buf);
    errnum = errno;
    close(fd);
    if (i < 0) {
        eprintf("getKernelCmdLine: failed to read /proc/cmdline: %m\n");
        return NULL;
    }
    return buf;
}

/* get the start of a kernel arg "arg".  returns everything after it
 * (useful for things like getting the args to init=).  so if you only
 * want one arg, you need to terminate it at the n */
static char *
getKernelArg(char * arg)
{
    char * start, * cmdline;
    int len;

    cmdline = start = getKernelCmdLine();
    if (start == NULL)
        return NULL;
    while (*start) {
        if (isspace(*start)) {
            start++;
            continue;
        }
        len = strlen(arg);
        /* don't return if it's some other arg that just starts like
           this one */
        if (strncmp(start, arg, len) == 0) {
            if (start[len] == '=')
                return start + len + 1;
            if (!start[len] || isspace(start[len]))
                return start + len;
        }
        while (*++start && !isspace(*start))
            ;
    }

    return NULL;
}

static int
mountCommand(char * cmd, char * end)
{
    char * fsType = NULL;
    char * device, *spec;
    char * mntPoint;
    char * options = NULL;
    int rc = 0;
    int flags = MS_MGC_VAL;
    char * newOpts;

    if (!(cmd = getArg(cmd, end, &spec))) {
        eprintf(
            "usage: mount [--ro] [-o <opts>] -t <type> <device> <mntpoint>\n");
        return 1;
    }

    while (cmd && *spec == '-') {
        if (!strcmp(spec, "--ro")) {
            flags |= MS_RDONLY;
        } else if (!strcmp(spec, "--bind")) {
            flags = MS_BIND;
            fsType = "none";
        } else if (!strcmp(spec, "--move")) {
            flags = MS_MOVE;
            fsType = "none";
        } else if (!strcmp(spec, "-o")) {
            cmd = getArg(cmd, end, &options);
            if (!cmd) {
                eprintf("mount: -o requires arguments\n");
                return 1;
            }
        } else if (!strcmp(spec, "-t")) {
            if (!(cmd = getArg(cmd, end, &fsType))) {
                eprintf("mount: missing filesystem type\n");
                return 1;
            }
        }

        cmd = getArg(cmd, end, &spec);
    }

    if (!cmd) {
        eprintf("mount: missing device or mountpoint\n");
        return 1;
    }

    if (!(cmd = getArg(cmd, end, &mntPoint))) {
        struct mntent *mnt;
        FILE *fstab;

        fstab = fopen("/etc/fstab", "r");
        if (!fstab) {
            eprintf("mount: missing mount point\n");
            return 1;
        }
        do {
            if (!(mnt = getmntent(fstab))) {
                eprintf("mount: missing mount point\n");
                fclose(fstab);
                return 1;
            }
            if (!strcmp(mnt->mnt_dir, spec)) {
                spec = mnt->mnt_fsname;
                mntPoint = mnt->mnt_dir;

                if (!strcmp(mnt->mnt_type, "bind")) {
                    flags |= MS_BIND;
                    fsType = "none";
                } else
                    fsType = mnt->mnt_type;

                options = mnt->mnt_opts;
                break;
            }
        } while(1);

        fclose(fstab);
    }

    if (!fsType) {
        eprintf("mount: filesystem type expected\n");
        return 1;
    }

    if (cmd && cmd < end) {
        eprintf("mount: unexpected arguments\n");
        return 1;
    }

    /* need to deal with options */
    if (options) {
        char * end;
        char * start = options;

        newOpts = alloca(strlen(options) + 1);
        *newOpts = '\0';

        while (*start) {
            end = strchr(start, ',');
            if (!end) {
                end = start + strlen(start);
            } else {
                *end = '\0';
                end++;
            }

            if (!strcmp(start, "ro"))
                flags |= MS_RDONLY;
            else if (!strcmp(start, "rw"))
                flags &= ~MS_RDONLY;
            else if (!strcmp(start, "nosuid"))
                flags |= MS_NOSUID;
            else if (!strcmp(start, "suid"))
                flags &= ~MS_NOSUID;
            else if (!strcmp(start, "nodev"))
                flags |= MS_NODEV;
            else if (!strcmp(start, "dev"))
                flags &= ~MS_NODEV;
            else if (!strcmp(start, "noexec"))
                flags |= MS_NOEXEC;
            else if (!strcmp(start, "exec"))
                flags &= ~MS_NOEXEC;
            else if (!strcmp(start, "sync"))
                flags |= MS_SYNCHRONOUS;
            else if (!strcmp(start, "async"))
                flags &= ~MS_SYNCHRONOUS;
            else if (!strcmp(start, "nodiratime"))
                flags |= MS_NODIRATIME;
            else if (!strcmp(start, "diratime"))
                flags &= ~MS_NODIRATIME;
            else if (!strcmp(start, "noatime"))
                flags |= MS_NOATIME;
            else if (!strcmp(start, "atime"))
                flags &= ~MS_NOATIME;
            else if (!strcmp(start, "remount"))
                flags |= MS_REMOUNT;
            else if (!strcmp(start, "bind"))
                flags |= MS_BIND;
            else if (!strcmp(start, "defaults"))
                ;
            else {
                if (*newOpts)
                    strcat(newOpts, ",");
                strcat(newOpts, start);
            }

            start = end;
        }

        options = newOpts;
    }

    if (!strncmp(fsType, "nfs", 3)) {
        device = strdupa(spec);
    } else {
        device = getpathbyspec(spec);
    }

    if (!device) {
        eprintf("mount: could not find filesystem '%s'\n", spec);
        return 1;
    }

    if (testing) {
        printf("mount %s%s%s-t '%s' '%s' '%s' (%s%s%s%s%s%s%s)\n",
                options ? "-o '" : "",
                options ? options : "",
                options ? "\' " : "",
                fsType, device, mntPoint,
                (flags & MS_RDONLY) ? "ro " : "",
                (flags & MS_NOSUID) ? "nosuid " : "",
                (flags & MS_NODEV) ? "nodev " : "",
                (flags & MS_NOEXEC) ? "noexec " : "",
                (flags & MS_SYNCHRONOUS) ? "sync " : "",
                (flags & MS_REMOUNT) ? "remount " : "",
                (flags & MS_NOATIME) ? "noatime " : ""
            );
    } else {
        if (!strncmp(fsType, "nfs", 3)) {
            char * foo = NULL;
            if (nfsmount(device, mntPoint, &flags, &foo, &options, 0)) {
                eprintf("nfsmount: error mounting %s on %s as %s: %m\n",
                        device, mntPoint, fsType);
                return 1;
            }
        }
        if (mount(device, mntPoint, fsType, flags, options) < 0) {
            eprintf("mount: error mounting %s on %s as %s: %m\n",
                    device, mntPoint, fsType);
            rc = 1;
        }
    }

    return rc;
}

static int
otherCommand(char * bin, char * cmd, char * end, int doFork)
{
    char ** args;
    char ** nextArg;
    int wpid;
    int status = 0;
    char * stdoutFile = NULL;
    int stdoutFd = 1;

    args = (char **)calloc(128, sizeof (char *));
    if (!args)
        return 1;
    nextArg = args;

    if (access(bin, X_OK)) {
        eprintf("failed to execute %s: %m\n", bin);
        return 1;
    }

    *nextArg = strdup(bin);

    while (cmd && cmd < end) {
        nextArg++;
        cmd = getArg(cmd, end, nextArg);
    }

    if (cmd)
        nextArg++;
    *nextArg = NULL;

    /* if the next-to-last arg is a >, redirect the output properly */
    if (((nextArg - args) >= 2) && !strcmp(*(nextArg - 2), ">")) {
        stdoutFile = *(nextArg - 1);
        *(nextArg - 2) = NULL;

        stdoutFd = open(stdoutFile, O_CREAT | O_RDWR | O_TRUNC, 0600);
        if (stdoutFd < 0) {
            eprintf("nash: failed to open %s: %m\n", stdoutFile);
            return 1;
        }
        setFdCoe(stdoutFd, 0);
    }

    if (testing) {
        printf("%s ", bin);
        nextArg = args + 1;
        while (*nextArg)
            printf(" '%s'", *nextArg++);
        if (stdoutFile)
            printf(" (> %s)", stdoutFile);
        printf("\n");
    } else {
        if (!doFork || !(ocPid = fork())) {
            /* child */
            int errnum;

            dm_cleanup(); /* ARRGH */
            dup2(stdoutFd, 1);
            execve(args[0], args, env);
            errnum = errno; /* so we'll have it after printf */
            eprintf("ERROR: failed in exec of %s: %m\n", args[0]);
            return 1;
        }

        if (stdoutFd != 1)
                close(stdoutFd);

        for (;;) {
            wpid = waitpid(ocPid, &status, 0);
            if (wpid == -1)
                eprintf("ERROR: Failed to wait for process %d: %m\n", wpid);

            if (!WIFEXITED(status) || WEXITSTATUS(status)) {
#if 0
                eprintf("ERROR: %s exited abnormally with value %d (pid %d)\n",
                    args[0], WEXITSTATUS(status), ocPid);
#endif
                status = WEXITSTATUS(status);
            } else {
                status = 0;
            }
            break;
        }
        return status;
    }

    return status;
}

static int
lnCommand(char *cmd, char *end)
{
    char *oldpath, *newpath;
    int symbolic = 0, rc;

    if (!(cmd = getArg(cmd, end, &oldpath))) {
        eprintf("ln: argument expected\n");
        return 1;
    }

    if (!strcmp(cmd, "-s")) {
        symbolic = 1;
        if (!(cmd = getArg(cmd, end, &oldpath))) {
            eprintf("ln: argument expected\n");
            return 1;
        }
    }

    if (!(cmd = getArg(cmd, end, &newpath))) {
        eprintf("ln: argument expected\n");
        return 1;
    }

    if (symbolic)
        rc = symlink(oldpath, newpath);
    else
        rc = link(oldpath, newpath);

    if (rc > 0) {
        eprintf("ln: error: %m\n");
        return 1;
    }

    return 0;
}

#ifdef DEBUG
static int lsdir(char *thedir, char * prefix)
{
    DIR * dir;
    struct dirent * entry;
    struct stat sb;
    char * fn;

    if (!(dir = opendir(thedir))) {
        eprintf("error opening %s: %m\n", thedir);
        return 1;
    }

    fn = calloc(1024, sizeof (char));
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.')
            continue;
        snprintf(fn, 1024, "%s/%s", thedir, entry->d_name);
        stat(fn, &sb);
        printf("%s%s", prefix, fn);

        if (S_ISDIR(sb.st_mode)) {
            char * pfx;
            pfx = calloc(strlen(prefix) + 3, sizeof (char));
            sprintf(pfx, "%s  ", prefix);
            printf("/\n");
        } else if (S_ISCHR(sb.st_mode)) {
            printf(" c %d %d\n", major(sb.st_rdev), minor(sb.st_rdev));
        } else if (S_ISBLK(sb.st_mode)) {
            printf(" b %d %d\n", major(sb.st_rdev), minor(sb.st_rdev));
        } else if (S_ISLNK(sb.st_mode)) {
            char * target;
            target = calloc(1024, sizeof (char));
            readlink(fn, target, 1024);
            printf("->%s\n", target);
            free(target);
        } else {
            printf("\n");
        }
    }
    return 0;
}

static int
catCommand(char * cmd, char * end)
{
    char * file;
    char * buf;
    int fd, n;

    if (!(cmd = getArg(cmd, end, &file))) {
        eprintf("cat: argument expected\n");
        return 1;
    }

    if ((fd = open(file, O_RDONLY)) < 0) {
        eprintf("cat: error opening %s: %m\n", file);
        return 1;
    }

    buf = calloc(1024, sizeof (char));
    while ((n = read(fd, buf, 1024)) > 0) {
        write(1, buf, n);
    }
    return 0;
}

static int
lsCommand(char * cmd, char * end)
{
    char * dir;

    if (!(cmd = getArg(cmd, end, &dir))) {
        eprintf("ls: argument expected\n");
        return 1;
    }

    lsdir(dir, "");
    return 0;
}
#endif

static int
execCommand(char *cmd, char *end)
{
    char *bin, *fullPath = NULL;
    int rc;

    if (!(cmd = getArg(cmd, end, &bin))) {
        eprintf("exec: argument expected\n");
        return 1;
    }
    rc = searchPath(bin, &fullPath);
    if (rc < 0)
        return 1;

    rc = otherCommand(fullPath, cmd, end, 0);
    free(fullPath);
    return rc;
}

static int
losetupCommand(char * cmd, char * end)
{
    char * device;
    char * file;
    int fd;
    struct loop_info loopInfo;
    int dev;

    if (!(cmd = getArg(cmd, end, &device))) {
        eprintf("losetup: missing device\n");
        return 1;
    }

    if (!(cmd = getArg(cmd, end, &file))) {
        eprintf("losetup: missing file\n");
        return 1;
    }

    if (cmd < end) {
        eprintf("losetup: unexpected arguments\n");
        return 1;
    }

    if (testing) {
        printf("losetup '%s' '%s'\n", device, file);
    } else {
        dev = open(device, O_RDWR);
        if (dev < 0) {
            eprintf("losetup: failed to open %s: %m\n", device);
            return 1;
        }

        fd = open(file, O_RDWR);
        if (fd < 0) {
            eprintf("losetup: failed to open %s: %m\n", file);
            close(dev);
            return 1;
        }

        if (ioctl(dev, LOOP_SET_FD, (long)fd)) {
            eprintf("losetup: LOOP_SET_FD failed for fd %d: %m\n", fd);
            close(dev);
            close(fd);
            return 1;
        }

        close(fd);

        memset(&loopInfo, 0, sizeof(loopInfo));
        strcpy(loopInfo.lo_name, file);

        if (ioctl(dev, LOOP_SET_STATUS, &loopInfo))
            eprintf("losetup: LOOP_SET_STATUS failed: %m\n");

        close(dev);
    }

    return 0;
}

static int
hotplugCommand(char *cmd, char *end)
{
    char *new;
    cmd = getArg(cmd, end, &new);
    if (cmd) {
        eprintf("hotplug: unexpected arguments\n");
        return 1;
    }

    init_hotplug();
    return 0;
}

static int
killplugCommand(char *cmd, char *end)
{
    char *new;
    cmd = getArg(cmd, end, &new);
    if (cmd) {
        eprintf("killplug: unexpected arguments\n");
        return 1;
    }

    kill_hotplug();
    return 0;
}


#define RAID_MAJOR 9
static int
raidautorunCommand(char * cmd, char * end)
{
    char * device;
    int fd;

    if (!(cmd = getArg(cmd, end, &device))) {
        eprintf("raidautorun: raid device expected as first argument\n");
        return 1;
    }

    if (cmd < end) {
        eprintf("raidautorun: unexpected arguments\n");
        return 1;
    }

    /* with udev, the raid devices don't exist until they get started.
     * this won't work so well with raidautorun.  so, let's be smart
     * and create them ourselves if we need to */
    if (access(device, R_OK & W_OK)) {
        int minor;
        if (sscanf(device, "/dev/md%d", &minor) != 1) {
            eprintf("raidautorun: unable to autocreate %s\n", device);
            return 1;
        }

        if (smartmknod(device, S_IFBLK | 0600, makedev(RAID_MAJOR, minor))) {
            eprintf("raidautorun: unable to autocreate %s\n", device);
            return 1;
        }
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        eprintf("raidautorun: failed to open %s: %m\n", device);
        return 1;
    }

    if (ioctl(fd, RAID_AUTORUN, 0)) {
        eprintf("raidautorun: RAID_AUTORUN failed: %m\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

/* remove all files/directories below dirName -- don't cross mountpoints */
static int
recursiveRemove(char * dirName)
{
    struct stat sb,rb;
    DIR * dir;
    struct dirent * d;
    char * strBuf = alloca(strlen(dirName) + 1024);

    if (!(dir = opendir(dirName))) {
        eprintf("error opening %s: %m\n", dirName);
        return 0;
    }

    if (fstat(dirfd(dir),&rb)) {
        eprintf("unable to stat %s: %m\n", dirName);
        closedir(dir);
        return 0;
    }

    errno = 0;
    while ((d = readdir(dir))) {
        errno = 0;

        if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
            errno = 0;
            continue;
        }

        strcpy(strBuf, dirName);
        strcat(strBuf, "/");
        strcat(strBuf, d->d_name);

        if (lstat(strBuf, &sb)) {
            eprintf("failed to stat %s: %m\n", strBuf);
            errno = 0;
            continue;
        }

        /* only descend into subdirectories if device is same as dir */
        if (S_ISDIR(sb.st_mode)) {
            if (sb.st_dev == rb.st_dev) {
                recursiveRemove(strBuf);
                if (rmdir(strBuf))
                    eprintf("failed to rmdir %s: %m\n", strBuf);
            }
            errno = 0;
            continue;
        }

        if (unlink(strBuf)) {
            eprintf("failed to remove %s: %m\n", strBuf);
            errno = 0;
            continue;
        }
    }

    if (errno) {
        closedir(dir);
        eprintf("error reading from %s: %m\n", dirName);
        return 1;
    }

    closedir(dir);

    return 0;
}

static void
mountMntEnt(const struct mntent *mnt)
{
    char *start = NULL, *end;
    char *target = NULL;
    struct stat sb;
    
    qprintf("mounting %s\n", mnt->mnt_dir);
    if (asprintfa(&target, ".%s", mnt->mnt_dir) < 0) {
        eprintf("setuproot: out of memory while mounting %s\n",
                mnt->mnt_dir);
        return;
    }
    
    if (stat(target, &sb) < 0)
        return;
    
    if (asprintf(&start, "-o %s -t %s %s .%s\n",
            mnt->mnt_opts, mnt->mnt_type, mnt->mnt_fsname,
            mnt->mnt_dir) < 0) {
        eprintf("setuproot: out of memory while mounting %s\n",
                mnt->mnt_dir);
        return;
    }
    
    end = start + 1;
    while (*end && (*end != '\n'))
        end++;
    /* end points to the \n at the end of the command */
    
    if (mountCommand(start, end) != 0)
        eprintf("setuproot: mount returned error\n");
}

static int
setuprootCommand(char *cmd, char *end)
{
    FILE *fp;
    char *new;

    qprintf("Setting up new root fs\n");

    cmd = getArg(cmd, end, &new);
    if (cmd) {
        eprintf("setuproot: unexpected arguments\n");
        return 1;
    }
    new = "/sysroot";

    if (chdir(new)) {
        eprintf("setuproot: chdir(%s) failed: %m\n", new);
        return 1;
    }

    if (mount("/dev", "./dev", NULL, MS_BIND, NULL) < 0)
        eprintf("setuproot: moving /dev failed: %m\n");

    if (!getKernelArg("nomnt")) {
        fp = setmntent("./etc/fstab.sys", "r");
        if (fp)
            qprintf("using fstab.sys from mounted FS\n");
        else {
            fp = setmntent("/etc/fstab.sys", "r");
            if (fp)
                qprintf("using fstab.sys from initrd\n");
        }
        if (fp) {
            struct mntent *mnt;

            while((mnt = getmntent(fp)))
                mountMntEnt(mnt);
            endmntent(fp);
        } else {
            struct {
                char *source;
                char *target;
                char *type;
                int flags;
                void *data;
            } fstab[] = {
                { "/proc", "./proc", "proc", 0, NULL },
                { "/sys", "./sys", "sysfs", 0, NULL },
#if 0
                { "/dev/pts", "./dev/pts", "devpts", 0, "gid=5,mode=620" },
                { "/dev/shm", "./dev/shm", "tmpfs", 0, NULL },
                { "/selinux", "/selinux", "selinuxfs", 0, NULL },
#endif
                { NULL, }
            };
            int i = 0;

            qprintf("no fstab.sys, mounting internal defaults\n");
            for (; fstab[i].source != NULL; i++) {
                if (mount(fstab[i].source, fstab[i].target, fstab[i].type,
                            fstab[i].flags, fstab[i].data) < 0)
                    eprintf("setuproot: error mounting %s: %m\n",
                            fstab[i].source);
            }
        }
    }

    chdir("/");
    return 0;
}

#define MAX_INIT_ARGS 32
static int
switchrootCommand(char * cmd, char * end)
{
    /*  Don't try to unmount the old "/", there's no way to do it. */
    const char * umounts[] = { "/dev", "/proc", "/sys", NULL };
    const char *initprogs[] = { "/sbin/init", "/etc/init",
                                "/bin/init", "/bin/sh", NULL };
    char *init, **initargs;
    char *cmdline = NULL;
    char *new;
    int fd, i = 0;

    cmd = getArg(cmd, end, &new);
    if (cmd) {
        eprintf("switchroot: unexpected arguments\n");
        return 1;
    }
    new = "/sysroot";

    /* this has to happen before we unmount /proc */
    init = getKernelArg("init");
    if (init == NULL)
        cmdline = getKernelCmdLine();

    fd = open("/", O_RDONLY);
    for (; umounts[i] != NULL; i++) {
        qprintf("unmounting old %s\n", umounts[i]);
        if (umount2(umounts[i], MNT_DETACH) < 0) {
            eprintf("ERROR unmounting old %s: %m\n",umounts[i]);
            eprintf("forcing unmount of %s\n", umounts[i]);
            umount2(umounts[i], MNT_FORCE);
        }
    }
    i=0;

    chdir(new);
    move_hotplug();
    notify_hotplug_of_exit();

    recursiveRemove("/");

    if (mount(new, "/", NULL, MS_MOVE, NULL) < 0) {
        eprintf("switchroot: mount failed: %m\n");
        close(fd);
        return 1;
    }

    if (chroot(".") || chdir("/")) {
        eprintf("switchroot: chroot() failed: %m\n");
        close(fd);
        return 1;
    }

    /* release the old "/" */
    close(fd);

    close(3);
    if ((fd = open("/dev/console", O_RDWR)) < 0) {
        eprintf("ERROR opening /dev/console: %m\n");
        eprintf("Trying to use fd 0 instead.\n");
        fd = dup2(0, 3);
    } else {
        setFdCoe(fd, 0);
        if (fd != 3) {
            dup2(fd, 3);
            close(fd);
            fd = 3;
        }
    }
    close(0);
    dup2(fd, 0);
    close(1);
    dup2(fd, 1);
    close(2);
    dup2(fd, 2);
    close(fd);

    if (init == NULL) {
        for (i = 0; initprogs[i] != NULL; i++) {
            if (!access(initprogs[i], X_OK)) {
                init = strdup(initprogs[i]);
                break;
            }
        }
    }
    i = 0;

    initargs = (char **)calloc(MAX_INIT_ARGS+1, sizeof (char *));
    if (cmdline && init) {
        initargs[i++] = strdup(init);
    } else {
        cmdline = init;
        initargs[0] = NULL;
    }

    if (cmdline != NULL) {
        char * chptr, * start;

        start = chptr = cmdline;
        for (; (i < MAX_INIT_ARGS) && (*start != '\0'); i++) {
            while (*chptr && !isspace(*chptr))
                chptr++;
            if (*chptr != '\0') *(chptr++) = '\0';
            /*
             * On x86_64, the kernel adds a magic command line parameter
             * *after* everything you pass.  Bash doesn't know what "console="
             * means, so it exits, init gets killed, etc, etc.  Bad news.
             *
             * Apparently being removed "soon", but for now, nash needs to
             * special case it.
             */
            if (cmdline == init && !strncmp(start, "console=", 8)) {
                if (!*chptr)
                    initargs[i] = NULL;
                else
                    i--;
                start = chptr;
                continue;
            }
            initargs[i] = strdup(start);
            start = chptr;
        }
    }

    initargs[i] = NULL;

    if (access(initargs[0], X_OK)) {
        eprintf("WARNING: can't access %s\n", initargs[0]);
    }
    dm_cleanup(); /* ARRGH */
    execv(initargs[0], initargs);

    eprintf("exec of init (%s) failed!!!: %m\n", initargs[0]);
    return 1;
}

static int
isEchoQuiet(int fd)
{
    if (!reallyquiet)
        return 0;
    if (fd != 1)
        return 0;
    return 1;
}

static int
echoCommand(char * cmd, char * end)
{
    char * args[256];
    char ** nextArg = args;
    int outFd = 1;
    int num = 0;
    int i;
    int newline = 1;
    int length = 0;
    char *string;

    if (testing)
        qprintf("(echo) ");

    while ((cmd = getArg(cmd, end, nextArg))) {
        if (!strncmp("-n", *nextArg, MAX(2, strlen(*nextArg)))) {
            newline = 0;
        } else {
            length += strlen(*nextArg);
            nextArg++, num++;
        }
    }
    length += num + 1;

    if ((nextArg - args >= 2) && !strcmp(*(nextArg - 2), ">")) {
        outFd = open(*(nextArg - 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outFd < 0) {
            eprintf("echo: cannot open %s for write: %m\n", *(nextArg - 1));
            return 1;
        }

        newline = 0;
        num -= 2;
    }
    string = (char *)calloc(length, sizeof (char));
    *string = '\0';
    for (i = 0; i < num;i ++) {
        if (i)
            strcat(string, " ");
        strncat(string, args[i], strlen(args[i]));
    }

    if (newline)
        strcat(string, "\n");
    if (!isEchoQuiet(outFd))
        write(outFd, string, strlen(string));

    if (outFd != 1)
        close(outFd);
    free(string);

    return 0;
}

static int
umountCommand(char * cmd, char * end)
{
    char * path;

    if (!(cmd = getArg(cmd, end, &path))) {
        eprintf("umount: path expected\n");
        return 1;
    }

    if (cmd < end) {
        eprintf("umount: unexpected arguments\n");
        return 1;
    }

    if (umount(path) < 0) {
        eprintf("umount %s failed: %m\n", path);
        return 1;
    }

    return 0;
}

static int
resolveDeviceCommand(char *cmd, char *end)
{
    char *spec = NULL;
    char *device = NULL;

    if (!(cmd = getArg(cmd, end, &spec))) {
        eprintf("resolveDevice: device spec expected\n");
        return 1;
    }

    device = getpathbyspec(spec);
    if (device) {
        printf("%s\n", device);
        return 0;
    }
    return 1;
}

/* 2.6 magic swsusp stuff */
static int
resumeCommand(char * cmd, char * end)
{
    char * resumedev = NULL;
    char * resume = NULL;
    int fd, n;
    struct stat sb;
    char buf[25];

    if (!(cmd = getArg(cmd, end, &resume))) {
        eprintf("resume: resume device expected\n");
        return 1;
    }

    if (access("/sys/power/resume", W_OK)) {
        /* eprintf("/sys/power/resume doesn't exist, can't resume!\n");*/
        return 0;
    }

    if (strstr(getKernelCmdLine(), "noresume")) {
        qprintf("noresume passed, not resuming...\n");
        return 0;
    }

    resumedev = getKernelArg("resume");
    if (resumedev == NULL) {
        resumedev = resume;
    }
    n = strcspn(resumedev, " \t\r\n");
    resumedev[n] = '\0';

    qprintf("Trying to resume from %s\n", resumedev);

    resume = getpathbyspec(resumedev);
    if (resume)
        resumedev = resume;

    if (access(resumedev, R_OK)) {
        eprintf("Unable to access resume device (%s)\n", resumedev);
        return 1;
    }

    fd = open(resumedev, O_RDONLY);
    if (fd < 0)
        return 1;
    if (lseek(fd, getpagesize() - 10, SEEK_SET) != getpagesize() - 10) {
        close(fd);
        return 1;
    }
    if (read(fd, &buf, 6) != 6) {
        close(fd);
        return 1;
    }
    if (strncmp(buf, "S1SUSP", 6) && strncmp(buf, "S2SUSP", 6)) {
        qprintf("No suspend signature on swap, not resuming.\n");
        close(fd);
        return 1;
    }

    if (fstat(fd, &sb)) {
        close(fd);
        return 1;
    }
    close(fd);

    printf("Resuming from %s.\n", resumedev);
    fflush(stdout);
    fd = open("/sys/power/resume", O_WRONLY);
    memset(buf, '\0', 20);
    snprintf(buf, 20, "%d:%d", major(sb.st_rdev), minor(sb.st_rdev));
    write(fd, buf, 20);
    close(fd);

    eprintf("Resume failed.  Continuing with normal startup.\n");
    return 0;
}

static int
mkrootdevCommand(char *cmd, char *end)
{
    char *chptr = NULL, *root;
    int i;
    FILE *fstab;
    struct mntent mnt = {
        .mnt_fsname = "/dev/root",
        .mnt_dir = "/sysroot",
        .mnt_type = NULL,
        .mnt_opts = NULL,
        .mnt_freq = 0,
        .mnt_passno = 0
    };

    root = getKernelArg("root");
    if (root) {
        char c;
        chptr = root;
        while (*chptr && !isspace(*chptr))
            chptr++;
        c = *chptr;
        *chptr = '\0';
        root = strdupa(root);
        *chptr = c;
        chptr = NULL;
    }

    i = 0;
    while ((cmd = getArg(cmd, end, &chptr))) {
        if (!strcmp(chptr, "-t")) {
            cmd = getArg(cmd, end, &mnt.mnt_type);
            if (!cmd) {
                eprintf("mkrootdev: expected fs type\n");
                return 1;
            }
        } else if (!strcmp(chptr, "-o")) {
            cmd = getArg(cmd, end, &mnt.mnt_opts);
            if (!cmd) {
                eprintf("mkrootdev: expected device name\n");
                return 1;
            }
        } else if (root) {
            if (i) {
                eprintf("mkrootdev: unexpected arguments\n");
                eprintf("cmd: %p end: %p\n", cmd, end);
                eprintf("cmd: %s\n", cmd);
                return 1;
            }
            /* if we get here, we got root from the kernel command line,
               so we don't _really_ care that there wasn't one on the
               mkrootdev command line. */
            i++;
        } else {
            root = chptr;
            i++;
        }
    }
    if (!root) {
        eprintf("mkrootdev: expected device name\n");
        return 1;
    }

    if (!mnt.mnt_type) {
        eprintf("mkrootdev: expected fs type\n");
        return 1;
    }
    if (!mnt.mnt_opts) {
        eprintf("mkrootdev: expected fs options\n");
        return 1;
    }
    /* nfs can't use /dev/root */
    if (!strncmp(mnt.mnt_type, "nfs", 3)) {
        mnt.mnt_fsname = strdup(root);
    }

    umask(0122);
    fstab = fopen("/etc/fstab", "w+");
    if (!fstab) {
        eprintf("mkrootdev: could not create fstab: %m\n");
        return 1;
    }
    addmntent(fstab, &mnt);
    fclose(fstab);

    if (!strncmp(mnt.mnt_type, "nfs", 3)) return 0;    
    return mkpathbyspec(root, "/dev/root") < 0 ? 1 : 0;
}

static int
mkdirCommand(char * cmd, char * end)
{
    char * dir;
    int ignoreExists = 0;

    cmd = getArg(cmd, end, &dir);

    if (cmd && !strcmp(dir, "-p")) {
        ignoreExists = 1;
        cmd = getArg(cmd, end, &dir);
    }

    if (!cmd) {
        eprintf("mkdir: directory expected\n");
        return 1;
    }

    if (mkdir(dir, 0755)) {
        if (!ignoreExists && errno == EEXIST) {
            eprintf("mkdir: failed to create %s: %m\n", dir);
            return 1;
        }
    }

    return 0;
}

static int
accessCommand(char * cmd, char * end)
{
    char * permStr;
    int perms = 0;
    char * file = NULL;

    cmd = getArg(cmd, end, &permStr);
    if (cmd)
        cmd = getArg(cmd, end, &file);

    if (!cmd || *permStr != '-') {
        eprintf("usage: access -[perm] file\n");
        return 1;
    }

    permStr++;
    while (*permStr) {
        switch (*permStr) {
            case 'r': perms |= R_OK; break;
            case 'w': perms |= W_OK; break;
            case 'x': perms |= X_OK; break;
            case 'f': perms |= F_OK; break;
            default:
                eprintf("perms must be -[r][w][x][f]\n");
                return 1;
        }

        permStr++;
    }

    if ((file == NULL) || (access(file, perms)))
        return 1;

    return 0;
}

static int
showLabelsCommand(char *cmd, char *end)
{
    char *new = NULL;
    cmd = getArg(cmd, end, &new);
    if (cmd) {
        eprintf("showlabels: unexpected arguments\n");
        return 1;
    }
    block_show_labels();
    return 0;
}

static int
sleepCommand(char * cmd, char * end)
{
    char *delaystr;
    int delay;

    if (!(cmd = getArg(cmd, end, &delaystr))) {
        eprintf("sleep: delay expected\n");
        return 1;
    }

    delay = atoi(delaystr);
    sleep(delay);

    return 0;
}

static int
stabilizedCommand(char *cmd, char *end)
{
    struct timespec req, rem = {0,0};
    int iterations=-1;
    struct timespec initial = {0,0};
    unsigned long interval=300;
    char *buf = NULL, *file = NULL;
    struct stat sb;
    time_t last = 0;
    int count = 0;

    while ((cmd = getArg(cmd, end, &buf))) {
        if (!strcmp(buf, "--iterations")) {
            if (!(cmd = getArg(cmd, end, &buf))) {
usage:
                eprintf("usage: stabilized [ --iterations N ] "
                        "[ --interval MSECS ] <file>\n");
                return 1;
            }
            iterations = atoi(buf);
            continue;
        }
        if (!strcmp(buf, "--interval")) {
            if (!(cmd = getArg(cmd, end, &buf)))
                goto usage;
                interval = atoi(buf);
            continue;
        }
        if (file)
            goto usage;
        file = buf;
    }

    if (!file)
        goto usage;

    initial.tv_sec = 0;
    initial.tv_nsec = interval * 1000000;
    while (initial.tv_nsec > 999999999) {
        initial.tv_sec += 1;
        initial.tv_nsec -= 999999999;
    }

    memset(&sb, '\0', sizeof(sb));
    do {
        sb.st_mtime = 0;
        if (sb.st_mtime == last) {
            if (++count == 5)
                return 0;
        } else
            count = 0;

        rem.tv_sec = initial.tv_sec;
        rem.tv_nsec = initial.tv_nsec;
        do {
            req.tv_sec = rem.tv_sec;
            req.tv_nsec = rem.tv_nsec;
        } while (nanosleep(&req, &rem) < 0 && errno == EINTR);
        last = sb.st_mtime;
        if (iterations != -1)
            iterations--;
    } while (iterations == -1 || iterations > 0);
    return 1;
}

static int
readlinkCommand(char * cmd, char * end)
{
    char * path;
    char * buf, * respath, * fullpath = NULL;
    struct stat sb;
    int rc = 0;

    if (!(cmd = getArg(cmd, end, &path))) {
        eprintf("readlink: file expected\n");
        return 1;
    }

    if (lstat(path, &sb) == -1) {
        eprintf("unable to stat %s: %m\n", path);
        return 1;
    }

    if (!S_ISLNK(sb.st_mode)) {
        printf("%s\n", path);
        return 0;
    }

    buf = readlinka(path);
    if (buf == NULL) {
        eprintf("error readlink %s: %m\n", path);
        return 1;
    }

    /* symlink is absolute */
    if (buf[0] == '/') {
        printf("%s\n", buf);
        return 0;
    }

    /* nope, need to handle the relative symlink case too */
    respath = strrchr(path, '/');
    if (respath) {
        *respath = '\0';
    }

    asprintfa(&fullpath, "%s/%s", path, buf);
    /* and normalize it */
    respath = NULL;
    respath = canonicalize_file_name(fullpath);
    if (respath == NULL) {
        eprintf("error resolving symbolic link %s: %m\n", fullpath);
        return 1;
    }

    printf("%s\n", respath);
    free(respath);
    return rc;
}

static int
doFind(char * dirName, char * name, mode_t mask)
{
    struct stat sb;
    DIR * dir;
    struct dirent * d;
    char * strBuf = alloca(strlen(dirName) + 1024);

    if (!(dir = opendir(dirName))) {
        eprintf("error opening %s: %m\n", dirName);
        return 0;
    }

    errno = 0;
    while ((d = readdir(dir))) {
        errno = 0;

        strcpy(strBuf, dirName);
        strcat(strBuf, "/");
        strcat(strBuf, d->d_name);

        if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
            errno = 0;
            continue;
        }

        if (lstat(strBuf, &sb)) {
            eprintf("failed to stat %s: %m\n", strBuf);
            errno = 0;
            continue;
        }

        if (!name || !strcmp(d->d_name, name)) {
            if (mask == 0 || (sb.st_mode & mask) == mask)
                printf("%s\n", strBuf);
        }

        if (S_ISDIR(sb.st_mode))
            doFind(strBuf, name, mask);
    }

    if (errno) {
        closedir(dir);
        eprintf("error reading from %s: %m\n", dirName);
        return 1;
    }

    closedir(dir);

    return 0;
}

static int
findCommand(char * cmd, char * end)
{
    char * dir;
    char * name = NULL;
    char * type = NULL;
    mode_t mode = 0;

    if (!(cmd = getArg(cmd, end, &dir))) {
        dir = strdupa(".");
    } else {
        if (!strcmp(dir, "-type")) {
            char *t;
            if (!cmd)
                goto error;
            if (!(cmd = getArg(cmd, end, &type)))
                goto error;
            for (t=type; t && *t; t++) {
                if (*t != 'd') {
                    eprintf("find: error: unknown type '%c'\n", *t);
                    goto error;
                }
            }
            mode = S_IFDIR;
            if (!(cmd = getArg(cmd, end, &dir)))
                dir = strdup(".");
        }
        if ((cmd = getArg(cmd, end, &name))) {
            if (strcmp(name, "-name"))
                goto error;
            if (!(cmd = getArg(cmd, end, &name)))
                goto error;
        }
    }

    return doFind(dir, name, mode);
error:
    eprintf("usage: find [-type type] [path [-name file]]\n");
    return 1;
}

static int
mknodCommand(char * cmd, char * end)
{
    char * path, * type;
    char * majorStr, * minorStr;
    int major;
    int minor;
    char * chptr;
    mode_t mode;

    cmd = getArg(cmd, end, &path);
    if (!path)
        goto err;

    cmd = getArg(cmd, end, &type);
    if (!type)
        goto err;

    cmd = getArg(cmd, end, &majorStr);
    if (!majorStr)
        goto err;

    cmd = getArg(cmd, end, &minorStr);
    if (!minorStr) {
err:
        eprintf("mknod: usage mknod <path> [c|b] <major> <minor>\n");
        return 1;
    }

    if (!strcmp(type, "b")) {
        mode = S_IFBLK;
    } else if (!strcmp(type, "c")) {
        mode = S_IFCHR;
    } else {
        eprintf("mknod: invalid type\n");
        return 1;
    }

    major = strtol(majorStr, &chptr, 10);
    if (*chptr) {
        eprintf("invalid major number\n");
        return 1;
    }

    minor = strtol(minorStr, &chptr, 10);
    if (*chptr) {
        eprintf("invalid minor number\n");
        return 1;
    }

    if (smartmknod(path, mode | 0600, makedev(major, minor))) {
        eprintf("mknod: failed to create %s: %m\n", path);
        return 1;
    }

    return 0;
}

static int
dmCommand(char *cmd, char *end)
{
    char *action = NULL;
    char *name = NULL;

    cmd = getArg(cmd, end, &action);
    if (!cmd)
        goto usage;

    if (!strcmp(action, "create")) {
        long long start, length;
        char *type = NULL, *params = NULL;
        char *c = NULL;
        char *uuid = NULL;
        
        cmd = getArg(cmd, end, &name);
        if (!cmd)
            goto usage;

        cmd = getArg(cmd, end, &uuid);
        if (!cmd)
            goto usage;

        if (!strcmp(uuid, "--uuid")) {
            if (!(cmd = getArg(cmd, end, &uuid))) {
                eprintf("dm create: missing uuid argument\n");
                return 1;
            }

            cmd = getArg(cmd, end, &params);
            if (!cmd)
                goto usage;
        } else {
            params = uuid;
            uuid = NULL;
        }

        errno = 0;
        start = strtoll(params, NULL, 0);
        if (errno)
            goto usage;

        cmd = getArg(cmd, end, &params);
        if (!cmd)
            goto usage;
        errno = 0;
        length = strtoll(params, NULL, 0);
        if (errno)
            goto usage;

        cmd = getArg(cmd, end, &type);
        if (!cmd)
            goto usage;

        params = cmd;
        c = strchr(params, '\n');
        if (c)
            *c = '\0';
        if (nashDmCreate(name, uuid, start, length, type, params))
            return 0;
        if (c)
            *c = '\n';
        return 1;
    } else if (!strcmp(action, "remove")) {
        cmd = getArg(cmd, end, &name);
        if (!cmd)
            goto usage;

        if (nashDmRemove(name))
            return 0;
    } else if (!strcmp(action, "partadd")) {
        cmd = getArg(cmd, end, &name);
        if (!cmd)
            goto usage;

        if (nashDmCreatePartitions(name))
            return 0;
        return 1;
    } else if (!strcmp(action, "get_uuid")) {
        char *uuid;

        cmd = getArg(cmd, end, &name);
        if (!cmd)
            goto usage;

        uuid = nashDmGetUUID(name);
        if (uuid) {
            printf("%s\n", uuid);
            free(uuid);
            return 0;
        }
        return 1;
    } else if (!strcmp(action, "list")) {
        const char **names = NULL;
        int m = 0, n = 0;

        names = calloc(m+1, sizeof (char *));
        while((cmd = getArg(cmd, end, &name))) {
            names = realloc(names, sizeof (char *) * (m+2));
            names[m++] = strdupa(name);
            names[m] = NULL;
        }

        qsort(names, m, sizeof (char *), stringsort);
        /* it's too bad qsort doesn't dedupe... */
        for (n = 0; n < m-1; n++) {
            if (!strcmp(*(names+n), *(names+n+1))) {
                memmove(names+(n*sizeof(char *)), names+((n+1) * sizeof (char *)), (m - n) * sizeof (char *));
                m--; n--;
            }
        }

        n = dm_list_sorted(names);
        if (names)
            free(names);
        if (n)
            return 0;
        return 1;
    }
usage:
    eprintf("usage: dm create name start length type PARAMS...\n"
            "       dm remove name\n"
            "       dm partadd path\n"
            "       dm get_uuid path\n"
            "       dm list [name0] [name1] [...] [nameN]\n"
            );
    return 1;
}

static int
mkDMNodCommand(char * cmd, char * end)
{
    int major = getDevNumFromProc("/proc/devices", "misc");
    int minor = getDevNumFromProc("/proc/misc", "device-mapper");

    if ((major == -1) || (minor == -1)) {
        eprintf("Unable to find device-mapper major/minor\n");
        return 1;
    }

    if (!access("/dev/mapper/control", R_OK)) {
        struct stat sb;
        if (stat("/dev/mapper/control", &sb) == 0) {
            if (S_ISCHR(sb.st_mode) && (sb.st_rdev == makedev(major, minor)))
                return 0;
        }

        unlink("/dev/mapper/control");
    }

    if (smartmknod("/dev/mapper/control", S_IFCHR | 0600,
                   makedev(major, minor))) {
        eprintf("failed to create /dev/mapper/control\n");
        return 1;
    }

    return 0;
}

static int
mkblkdevsCommand(char * cmd, char * end)
{
    if (cmd < end) {
        eprintf("mkblkdevs: unexpected arguments\n");
        return 1;
    }

    sysfs_blkdev_probe("/sys/block");
    return 1;
}

static int
rmpartsCommand(char *cmd, char *end)
{
    char *devname = NULL;

    cmd = getArg(cmd, end, &devname);
    if (!cmd) {
        printf("usage: rmparts hda\n");
        return 1;
    }

    if (block_disable_partitions(devname) < 1)
        return 1;
    return 0;
}

struct commandHandler {
    char *name;
    int (*fp)(char *cmd, char *end);
};

static const struct commandHandler *getCommandHandler(const char *name);

static int
statusCommand(char *cmd, char *end)
{
    char *si = NULL;
    long i;

    cmd = getArg(cmd, end, &si);
    if (!si) {
        printf("%d\n", exit_status);
        return exit_status;
    }

    i = strtoul(si, NULL, 0);
    if (i == ULONG_MAX && errno) {
        eprintf("status: error: %m\n");
        return 1;
    }

    return i;
}

static int
condCommand(char *cmd, char *end)
{
    char *op = NULL;
    int conditional = 0;
    int internal = 0;
    int rc;

    while((cmd = getArg(cmd, end, &op))) {
        char *stval = NULL;
        unsigned long tval;

        if (!op)
            return 1;

        if (op[0] != '-')
            break;

        cmd = getArg(cmd, end, &stval);
        if (stval)
            tval = strtoul(stval, NULL, 0);
        if (!stval || (tval == ULONG_MAX && errno)) {
            if (errno)
                eprintf("cond: error: %m\n");
            else
                eprintf("cond: error: invalid value '%s' for '%s' operator\n",
                        stval, op);
            return 1;
        }

        if (!strcmp(op, "-lt")) { 
            if (exit_status >= tval)
                return exit_status;
            conditional = 1;
        } else if (!strcmp(op, "-le")) {
            if (exit_status > tval)
                return exit_status;
            conditional = 1;
        } else if (!strcmp(op, "-et")) {
            if (exit_status != tval)
                return exit_status;
            conditional = 1;
        } else if (!strcmp(op, "-ne")) {
            if (exit_status == tval)
                return exit_status;
            conditional = 1;
        } else if (!strcmp(op, "-ge")) {
            if (exit_status < tval)
                return exit_status;
            conditional = 1;
        } else if (!strcmp(op, "-gt")) {
            if (exit_status <= tval)
                return exit_status;
            conditional = 1;
        }
    }

    if (!conditional && exit_status != 0)
        return exit_status;

    if (strncmp(op, "nash-", 5)) {
        char *fullPath = NULL;

        rc = searchPath(op, &fullPath);
        if (rc >= 0) {
            rc = otherCommand(fullPath, cmd, end, 1);
            free(fullPath);
            return rc;
        } else
            internal = 1;
    } else {
        op += 5;
        internal = 1;
    }

    rc = 1;
    if (internal) {
        const struct commandHandler *handler;

        handler = getCommandHandler(cmd);
        if (handler->name != NULL)
            rc = (handler->fp)(cmd, end);
    }
    return rc;
}

static int
networkCommand(char *cmd, char *end)
{
    char * ncmd = cmd;
    int rc;
    int len = 9; /* "network " */

    /* popt expects to get network --args here */
    if (!cmd || cmd >= end)
        return 1;
    while (*ncmd && (*ncmd++ != '\n')) len++;
    
    ncmd = malloc(len);
    ncmd = memset(ncmd, 0, len);
    snprintf(ncmd, len, "network %s", cmd);
    rc = nashNetworkCommand(ncmd);
    free(ncmd);
    return rc;
}

static int
setQuietCommand(char * cmd, char * end)
{
    char *quietcmd;

    quietcmd = getKernelArg("quiet");
    if (quietcmd)
        reallyquiet = 1;

    quietcmd = getKernelArg("noquiet");
    if (quietcmd)
        reallyquiet = 0;

    /* reallyquiet may be set elsewhere */
    if (reallyquiet)
          quiet = 1;

    return 0;
}

static const struct commandHandler handlers[] = {
    { "access", accessCommand },
#ifdef DEBUG
    { "cat", catCommand },
#endif
    { "cond", condCommand },
    { "dm", dmCommand },
    { "echo", echoCommand },
    { "exec", execCommand },
    { "find", findCommand },
    { "mkblkdevs", mkblkdevsCommand },
    { "mkdir", mkdirCommand },
    { "mkdmnod", mkDMNodCommand },
    { "mknod", mknodCommand },
    { "mkrootdev", mkrootdevCommand },
    { "mount", mountCommand },
    { "network", networkCommand },
    { "hotplug", hotplugCommand },
    { "killplug", killplugCommand },
    { "losetup", losetupCommand },
    { "ln", lnCommand },
#ifdef DEBUG
    { "ls", lsCommand },
#endif
    { "raidautorun", raidautorunCommand },
    { "readlink", readlinkCommand },
    { "resume", resumeCommand },
    { "resolveDevice", resolveDeviceCommand },
    { "rmparts", rmpartsCommand },
    { "setquiet", setQuietCommand },
    { "setuproot", setuprootCommand },
    { "showlabels", showLabelsCommand },
    { "sleep", sleepCommand },
    { "stabilized", stabilizedCommand },
    { "status", statusCommand },
    { "switchroot", switchrootCommand },
    { "umount", umountCommand },
    { NULL, },
};

static const struct commandHandler *
getCommandHandler(const char *name)
{
    const struct commandHandler *handler = NULL;

    for (handler = &handlers[0]; handler->name; handler++)
        if (!strcmp(name, handler->name))
            return handler;
    return handler;
}

static int
runStartup(int fd, char *name)
{
    char *contents;
    int i;
    char * start, * end;
    char * chptr;
    int rc;
    const struct commandHandler *handler;

    i = readFD(fd, &contents);
    if (i < 0) {
        eprintf("Failed to read startup file %s", name);
        return 1;
    }

    start = contents;
    while (*start) {
        while (isspace(*start) && *start && (*start != '\n'))
            start++;

        if (*start == '#') {
            while (*start && (*start != '\n'))
                start++;
            if (*start == '\n')
                start++;
        }

        if (*start == '\n') {
            start++;
            continue;
        }

        if (!*start) {
            eprintf("(last line in %s is empty)\n", name);
            continue;
        }

        /* start points to the beginning of the command */
        end = start + 1;
        while (*end && (*end != '\n'))
            end++;
        if (!*end) {
            eprintf("(last line in %s missing newline -- skipping)\n", name);
            start = end;
            continue;
        }

        /* end points to the \n at the end of the command */

        chptr = start;
        while (chptr < end && !isspace(*chptr))
            chptr++;

        i = 0;
        rc = 1;
        *(chptr++) = '\0';
        if (strncmp(start, "nash-", 5))  {
            char *fullPath = NULL;
            rc = searchPath(start, &fullPath);
            if (rc >= 0) {
                rc = otherCommand(fullPath, chptr, end, 1);
                free(fullPath);
            } else
                i = 1;
        } else {
            start += 5;
            i = 1;
        }

        if (i == 1) {
            handler = getCommandHandler(start);
            if (handler->name != NULL)
                rc = (handler->fp)(chptr, end);
        }
        exit_status = rc;
        start = end + 1;
    }

    free(contents);
    return rc;
}

void delayOnSignal(int signum) {
    if (ocPid != -1)
        kill(ocPid, SIGSTOP);
    udelay(500000);
    if (ocPid != -1)
        kill(ocPid, SIGCONT);

    signal(signum, delayOnSignal);
}

static void traceback(int signum)
{
    void *array[20];
    size_t size;
    char **strings;
    size_t i;

    signal(SIGSEGV, SIG_DFL);

    size = backtrace(array, 20);
    strings = backtrace_symbols(array, size);

    printf("nash received SIGSEGV!  Backtrace:\n");
    for (i = 0; i < size; i++)
        printf("%s\n", strings[i]);

    free(strings);
    exit(1);
}

int main(int argc, char **argv) {
    int fd = 0;
    char * name;
    int rc;
    int force = 0;

    signal(SIGSEGV, traceback);

    name = strrchr(argv[0], '/');
    if (!name)
        name = argv[0];
    else
        name++;

    if (!strcmp(name, "modprobe")) {
        exit(0);
    }

    testing = (getppid() != 0) && (getppid() != 1);
    argv++, argc--;

    while (argc && **argv == '-') {
        if (!strcmp(*argv, "--forcequiet")) {
            force = 1;
            quiet = 1;
            argv++, argc--;
            testing = 0;
        } else if (!strcmp(*argv, "--force")) {
            force = 1;
            argv++, argc--;
            testing = 0;
        } else if (!strcmp(*argv, "--quiet")) {
            quiet = 1;
            argv++, argc--;
        } else if (!strcmp(*argv, "--reallyquiet")) {
            reallyquiet = 1;
            argv++, argc--;
        } else {
            eprintf("unknown argument %s\n", *argv);
            return 1;
        }
    }

    if (force)
        qprintf("(forcing normal run)\n");

    if (testing)
        qprintf("(running in test mode).\n");

    qprintf("Red Hat nash version %s starting\n", VERSION);

    if (*argv) {
        fd = open(*argv, O_RDONLY, 0);
        if (fd < 0) {
            eprintf("nash: cannot open %s: %m\n", *argv);
            exit(1);
        }
    }

    signal(SIGALRM, delayOnSignal);

    /* runStartup closes fd */
    rc = runStartup(fd, *argv);

    kill_hotplug();
    return rc;
}
