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
 * public license.
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

#include <asm/unistd.h>

#include "mount_by_label.h"

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

extern dev_t name_to_dev_t(char *name);
extern int display_uuid_cache(void);

#define MAX(a, b) ((a) > (b) ? a : b)

int testing = 0, quiet = 0, reallyquiet = 0;

int qprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int eprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));

int
qprintf(const char *format, ...)
{
    va_list ap;
    int ret;

    if (quiet)
        return 0;

    va_start(ap, format);
    ret = vprintf(format, ap);
    va_end(ap);

    fflush(stdout);
    return ret;
}

int
eprintf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = vfprintf(stderr, format, ap);
    va_end(ap);

    return ret;
}

#define PATH "/usr/bin:/bin:/sbin:/usr/sbin"

char * env[] = {
    "PATH=" PATH,
    "LVM_SUPPRESS_FD_WARNINGS=1",
    NULL
};

int smartmknod(char * device, mode_t mode, dev_t dev) {
    char buf[256];
    char * end;

    strncpy(buf, device, 256);

    end = buf;
    while (*end) {
	if (*end == '/') {
	    *end = '\0';
	    if (access(buf, F_OK) && errno == ENOENT) 
		mkdir(buf, 0755);
	    *end = '/';
	}

	end++;
    }

    return mknod(device, mode, dev);
}

char * getArg(char * cmd, char * end, char ** arg) {
    char quote = '\0';

    if (!cmd || cmd >= end) return NULL;

    while (isspace(*cmd) && cmd < end) cmd++;
    if (cmd >= end) return NULL;

    if (*cmd == '"')
	cmd++, quote = '"';
    else if (*cmd == '\'')
	cmd++, quote = '\'';

    if (quote) {
	*arg = cmd;

	/* This doesn't support \ escapes */
	while (cmd < end && *cmd != quote) cmd++;

	if (cmd == end) {
	    eprintf("error: quote mismatch for %s\n", *arg);
	    return NULL;
	}

	*cmd = '\0';
	cmd++;
    } else {
	*arg = cmd;
	while (!isspace(*cmd) && cmd < end) cmd++;
	*cmd = '\0';
	if (**arg == '$')
            *arg = getenv(*arg+1);
        if (*arg == NULL)
            *arg = "";
    }

    cmd++;

    while (isspace(*cmd)) cmd++;

    return cmd;
}

/* taken from anaconda/isys/probe.c */
static int readFD (int fd, char **buf)
{
    char *p;
    size_t size = 16384;
    int s, filesize;

    *buf = calloc (16384, sizeof (char));
    if (*buf == 0) {
	eprintf("calloc failed: %s\n", strerror(errno));
	return -1;
    }

    filesize = 0;
    do {
	p = &(*buf) [filesize];
	s = read (fd, p, 16384);
	if (s < 0)
	    break;
	filesize += s;
	/* only exit for empty reads */
	if (s == 0)
	    break;
	size += s;
	*buf = realloc (*buf, size);
    } while (1);

    if (filesize == 0 && s < 0) {
	free (*buf);
	*buf = NULL;
	return -1;
    }

    return filesize;
}

#ifdef __powerpc__
#define CMDLINESIZE 256
#else
#define CMDLINESIZE 1024
#endif

/* get the contents of the kernel command line from /proc/cmdline */
static char * getKernelCmdLine(void) {
    int fd, i, errnum;
    static char * buf = NULL;

    if (buf)
        return buf;

    fd = open("/proc/cmdline", O_RDONLY, 0);
    if (fd < 0) {
	eprintf("getKernelCmdLine: failed to open /proc/cmdline: %s\n",
                strerror(errno));
	return NULL;
    }

    i = readFD(fd, &buf);
    errnum = errno;
    close(fd);
    if (i < 0) {
	eprintf("getKernelCmdLine: failed to read /proc/cmdline: %s\n",
                strerror(errnum));
	return NULL;
    }
    return buf;
}

/* get the start of a kernel arg "arg".  returns everything after it
 * (useful for things like getting the args to init=).  so if you only
 * want one arg, you need to terminate it at the n */
static char * getKernelArg(char * arg) {
    char * start, * cmdline;
    int len;

    cmdline = start = getKernelCmdLine();
    if (start == NULL) return NULL;
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
            if (start[len] == ' ' || start[len] == '\t')
                return start + len;
        }
	while (*++start && !isspace(*start))
	    ;
    }

    return NULL;
}

int mountCommand(char * cmd, char * end) {
    char * fsType = NULL;
    char * device;
    char * mntPoint;
    char * deviceDir = NULL;
    char * options = NULL;
    int mustRemove = 0;
    int mustRemoveDir = 0;
    int rc = 0;
    int flags = MS_MGC_VAL;
    char * newOpts;

    cmd = getArg(cmd, end, &device);
    if (!cmd) {
	eprintf(
            "usage: mount [--ro] [-o <opts>] -t <type> <device> <mntpoint>\n");
	return 1;
    }

    while (cmd && *device == '-') {
	if (!strcmp(device, "--ro")) {
	    flags |= MS_RDONLY;
        } else if (!strcmp(device, "--bind")) {
            flags = MS_BIND;
            fsType = "none";
	} else if (!strcmp(device, "-o")) {
	    cmd = getArg(cmd, end, &options);
	    if (!cmd) {
		eprintf("mount: -o requires arguments\n");
		return 1;
	    }
	} else if (!strcmp(device, "-t")) {
	    if (!(cmd = getArg(cmd, end, &fsType))) {
		eprintf("mount: missing filesystem type\n");
		return 1;
	    }
	}

	cmd = getArg(cmd, end, &device);
    }

    if (!cmd) {
	eprintf("mount: missing device\n");
	return 1;
    }

    if (!(cmd = getArg(cmd, end, &mntPoint))) {
	eprintf("mount: missing mount point\n");
	return 1;
    }

    if (!fsType) {
	eprintf("mount: filesystem type expected\n");
	return 1;
    }

    if (cmd < end) {
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

    if (!strncmp("LABEL=", device, 6)) {
	int major, minor;
	char * devName;
	char * ptr;
	int i;

	devName = get_spec_by_volume_label(device + 6, &major, &minor);

	if (devName) {
	    device = devName;
	    if (access(device, F_OK)) {
	        ptr = device;
		i = 0;
		while (*ptr)
		    if (*ptr++ == '/')
			i++;
		if (i > 2) {
		    deviceDir = alloca(strlen(device) + 1);
		    strcpy(deviceDir, device);
		    ptr = deviceDir + (strlen(device) - 1);
		    while (*ptr != '/')
			*ptr-- = '\0';
		    if (mkdir(deviceDir, 0644)) {
		        eprintf("mkdir: cannot create directory %s: %s\n",
                                deviceDir, strerror(errno));
		    } else {
		      mustRemoveDir = 1;
		    }
		}
		if (smartmknod(device, S_IFBLK | 0600, makedev(major, minor))) {
		    printf("mount: cannot create device %s (%d,%d)\n",
			   device, major, minor);
		    return 1;
		}
		mustRemove = 1;
	    }
	}
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
	if (mount(device, mntPoint, fsType, flags, options)) {
	    eprintf("mount: error %s mounting %s on %s as %s\n",
                    strerror(errno), device, mntPoint, fsType);
	    rc = 1;
	}
    }

    if (mustRemove) unlink(device);
    if (mustRemoveDir) rmdir(deviceDir);

    return rc;
}

int otherCommand(char * bin, char * cmd, char * end, int doFork) {
    char ** args;
    char ** nextArg;
    int pid, wpid;
    int status;
    char fullPath[255];
    static const char * sysPath = PATH;
    const char * pathStart;
    const char * pathEnd;
    char * stdoutFile = NULL;
    int stdoutFd = 0;

    args = (char **)calloc(128, sizeof (char *));
    if (!args)
        return 1;
    nextArg = args;

    if (!strchr(bin, '/')) {
	pathStart = sysPath;
	while (*pathStart) {
	    pathEnd = strchr(pathStart, ':');

	    if (!pathEnd) pathEnd = pathStart + strlen(pathStart);

	    strncpy(fullPath, pathStart, pathEnd - pathStart);
	    fullPath[pathEnd - pathStart] = '/';
	    strcpy(fullPath + (pathEnd - pathStart + 1), bin); 

	    pathStart = pathEnd;
	    if (*pathStart) pathStart++;

	    if (!access(fullPath, X_OK)) {
		bin = fullPath;
		break;
	    }
	}
    }

    *nextArg = strdup(bin);

    while (cmd && cmd < end) {
	nextArg++;
	cmd = getArg(cmd, end, nextArg);
    }
	
    if (cmd) nextArg++;
    *nextArg = NULL;

    /* if the next-to-last arg is a >, redirect the output properly */
    if (((nextArg - args) >= 2) && !strcmp(*(nextArg - 2), ">")) {
	stdoutFile = *(nextArg - 1);
	*(nextArg - 2) = NULL;

	stdoutFd = open(stdoutFile, O_CREAT | O_RDWR | O_TRUNC, 0600);
	if (stdoutFd < 0) {
	    eprintf("nash: failed to open %s: %s\n", stdoutFile,
                    strerror(errno));
	    return 1;
	}
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
	if (!doFork || !(pid = fork())) {
	    /* child */
	    dup2(stdoutFd, 1);
	    execve(args[0], args, env);
	    eprintf("ERROR: failed in exec of %s: %s\n", args[0],
                    strerror(errno));
	    return 1;
	}

	close(stdoutFd);

	for (;;) {
	    wpid = wait4(-1, &status, 0, NULL);
	    if (wpid == -1) {
	        eprintf("ERROR: Failed to wait for process %d: %s\n", wpid,
                        strerror(errno));
	    }

	    if (wpid != pid)
		continue;

            if (!WIFEXITED(status) || WEXITSTATUS(status)) {
                eprintf("ERROR: %s exited abnormally with value %d (pid %d)\n",
                    args[0], WEXITSTATUS(status), pid);
		return 1;
	    }
	    break;
        }
    }

    return 0;
}

#ifdef DEBUG
static int lsdir(char *thedir, char * prefix) {
    DIR * dir;
    struct dirent * entry;
    struct stat sb;
    char * fn;

    if (!(dir = opendir(thedir))) {
        eprintf("error opening %s: %s\n", thedir, strerror(errno));
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

int catCommand(char * cmd, char * end) {
    char * file;
    char * buf;
    int fd, n;

    if (!(cmd = getArg(cmd, end, &file))) {
	eprintf("cat: argument expected\n");
	return 1;
    }

    if ((fd = open(file, O_RDONLY)) < 0) {
        eprintf("cat: error opening %s: %s\n", file, strerror(errno));
        return 1;
    }

    buf = calloc(1024, sizeof (char));
    while ((n = read(fd, buf, 1024)) > 0) {
        write(1, buf, n);
    }
    return 0;
}

int lsCommand(char * cmd, char * end) {
    char * dir;

    if (!(cmd = getArg(cmd, end, &dir))) {
	eprintf("ls: argument expected\n");
	return 1;
    }

    lsdir(dir, "");
    return 0;
}
#endif

int execCommand(char * cmd, char * end) {
    char * bin;

    if (!(cmd = getArg(cmd, end, &bin))) {
	eprintf("exec: argument expected\n");
	return 1;
    }

    return otherCommand(bin, cmd, end, 0);
}

int losetupCommand(char * cmd, char * end) {
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
	dev = open(device, O_RDWR, 0);
	if (dev < 0) {
	    eprintf("losetup: failed to open %s: %s\n", device,strerror(errno));
	    return 1;
	}

	fd = open(file, O_RDWR, 0);
	if (fd < 0) {
	    eprintf("losetup: failed to open %s: %s\n", file, strerror(errno));
	    close(dev);
	    return 1;
	}

	if (ioctl(dev, LOOP_SET_FD, (long) fd)) {
	    eprintf("losetup: LOOP_SET_FD failed for fd %d: %s\n", fd,
                    strerror(errno));
	    close(dev);
	    close(fd);
	    return 1;
	}

	close(fd);

	memset(&loopInfo, 0, sizeof(loopInfo));
	strcpy(loopInfo.lo_name, file);

	if (ioctl(dev, LOOP_SET_STATUS, &loopInfo)) 
	    eprintf("losetup: LOOP_SET_STATUS failed: %s\n", strerror(errno));

	close(dev);
    }

    return 0;
}

#define RAID_MAJOR 9
int raidautorunCommand(char * cmd, char * end) {
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

    fd = open(device, O_RDWR, 0);
    if (fd < 0) {
	eprintf("raidautorun: failed to open %s: %s\n", device,strerror(errno));
	return 1;
    }

    if (ioctl(fd, RAID_AUTORUN, 0)) {
	eprintf("raidautorun: RAID_AUTORUN failed: %s\n", strerror(errno));
	close(fd);
	return 1;
    }

    close(fd);
    return 0;
}

/* remove all files/directories below dirName -- don't cross mountpoints */
int recursiveRemove(char * dirName) {
    struct stat sb,rb;
    DIR * dir;
    struct dirent * d;
    char * strBuf = alloca(strlen(dirName) + 1024);

    if (!(dir = opendir(dirName))) {
	eprintf("error opening %s: %s\n", dirName, strerror(errno));
	return 0;
    }

    if (fstat(dirfd(dir),&rb)) {
	eprintf("unable to stat %s: %s\n", dirName, strerror(errno));
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
	    eprintf("failed to stat %s: %s\n", strBuf, strerror(errno));
	    errno = 0;
	    continue;
	}

	/* only descend into subdirectories if device is same as dir */
	if (S_ISDIR(sb.st_mode)) {
	    if (sb.st_dev == rb.st_dev) {
	        recursiveRemove(strBuf);
	        if (rmdir(strBuf))
		    eprintf("failed to rmdir %s: %s\n", strBuf,
                            strerror(errno));
	    }
	    errno = 0;
	    continue;
	}

	if (unlink(strBuf)) {
	    eprintf("failed to remove %s: %s\n", strBuf, strerror(errno));
	    errno = 0;
	    continue;
	}
    }

    if (errno) {
	closedir(dir);
	eprintf("error reading from %s: %s\n", dirName, strerror(errno));
	return 1;
    }

    closedir(dir);

    return 0;
}

#define MAX_INIT_ARGS 32
/* 2.6 magic not-pivot-root but kind of similar stuff.
 * This is based on code from klibc/utils/run_init.c
 */
int switchrootCommand(char * cmd, char * end) {
    char * new;
    const char * initprogs[] = { "/sbin/init", "/etc/init", 
                                 "/bin/init", "/bin/sh", NULL };
    char * init, * cmdline = NULL;
    char ** initargs;
    /*  Don't try to unmount the old "/", there's no way to do it. */
    const char * umounts[] = { "/dev", "/proc", "/sys", NULL };
    int fd, i = 0;
    int moveDev = 0;

    cmd = getArg(cmd, end, &new);
    if (cmd && !strcmp(new, "--movedev")) {
        moveDev = 1;
        cmd = getArg(cmd, end, &new);
    }

    if (!cmd) {
	eprintf("switchroot: new root mount point expected\n");
	return 1;
    }

    if (chdir(new)) {
        eprintf("switchroot: chdir(%s) failed: %s\n", new, strerror(errno));
        return 1;
    }

    init = getKernelArg("init");
    if (init == NULL)
        cmdline = getKernelCmdLine();

    if (moveDev) {
        i = 1;
        mount("/dev", "./dev", NULL, MS_MOVE, NULL);
    }

    if ((fd = open("./dev/console", O_RDWR)) < 0) {
        eprintf("ERROR opening /dev/console: %s\n", strerror(errno));
        fd = 0;
    }

    if (dup2(fd, 0) != 0) eprintf("error dup2'ing fd of %d to 0: %s\n", fd,
            strerror(errno));
    if (dup2(fd, 1) != 0) eprintf("error dup2'ing fd of %d to 1: %s\n", fd,
            strerror(errno));
    if (dup2(fd, 2) != 0) eprintf("error dup2'ing fd of %d to 2: %s\n", fd,
            strerror(errno));
    if (fd > 2)
        close(fd);

    recursiveRemove("/");

    fd = open("/", O_RDONLY);
    for (; umounts[i] != NULL; i++) {
        qprintf("unmounting old %s\n", umounts[i]);
        if (umount2(umounts[i], MNT_DETACH)) {
            eprintf("ERROR unmounting old %s: %s\n",umounts[i],strerror(errno));
            eprintf("forcing unmount of %s\n", umounts[i]);
            umount2(umounts[i], MNT_FORCE);
        }
    }
    i=0;

    if (mount(".", "/", NULL, MS_MOVE, NULL)) {
        eprintf("switchroot: mount failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    if (chroot(".") || chdir("/")) {
        eprintf("switchroot: chroot() failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    /* release the old "/" */
    close(fd);

    if (init == NULL) {
        int j;
        for (j = 0; initprogs[j] != NULL; j++) {
            if (!access(initprogs[j], X_OK)) {
                init = strdup(initprogs[j]);
                break;
            }
        }
    }

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
            while (*chptr && !isspace(*chptr)) chptr++;
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
    execv(initargs[0], initargs);
    eprintf("exec of init (%s) failed!!!: %s\n", initargs[0], strerror(errno));
    return 1;
}

int isEchoQuiet(int fd) {
    if (!reallyquiet) return 0;
    if (fd != 1) return 0;
    return 1;
}

int echoCommand(char * cmd, char * end) {
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
	    eprintf("echo: cannot open %s for write: %s\n", *(nextArg - 1),
                    strerror(errno));
	    return 1;
	}

        newline = 0;
	num -= 2;
    }
    string = (char *)calloc(length, sizeof (char));
    *string = '\0';
    for (i = 0; i < num;i ++) {
	if (i) strcat(string, " ");
        strncat(string, args[i], strlen(args[i]));
    }

    if (newline) strcat(string, "\n");
    if (!isEchoQuiet(outFd)) write(outFd, string, strlen(string));

    if (outFd != 1) close(outFd);
    free(string);

    return 0;
}

int umountCommand(char * cmd, char * end) {
    char * path;

    if (!(cmd = getArg(cmd, end, &path))) {
	eprintf("umount: path expected\n");
	return 1;
    }

    if (cmd < end) {
	eprintf("umount: unexpected arguments\n");
	return 1;
    }

    if (umount(path)) {
	eprintf("umount %s failed: %s\n", path, strerror(errno));
	return 1;
    }

    return 0;
}

int mkpathbyspec(char * spec, char * path) {
    int major, minor;

    if (!spec)
        return -1;

    if (!strncmp(spec, "LABEL=", 6)) {
	if (get_spec_by_volume_label(spec + 6, &major, &minor)) {
	    if (smartmknod(path, S_IFBLK | 0600, makedev(major, minor))) {
		eprintf("mkdev: cannot create device %s (%d,%d)\n",
		       path, major, minor);
		return 1;
	    }

            qprintf("created path for %s: %d/%d\n", spec, major, minor);
	    return 0;
	}

        eprintf("label %s not found\n", spec + 6);
	return 1;
    }

    if (!strncmp(spec, "UUID=", 5)) {
        if (get_spec_by_uuid(spec+5, &major, &minor)) {
            if (smartmknod(path, S_IFBLK | 0600, makedev(major, minor))) {
                eprintf("mkdev: cannot create device %s (%d,%d)\n",
                       path, major, minor);
                return 1;
            }

            return 0;
        }

        eprintf("mkdev: UUID %s not found\n", spec+5);
        return 1;

    }
#if 0
    qprintf("mkdev: '%s' is not a UUID or LABEL spec\n", spec);
#endif
    return -1;
}

/* 2.6 magic swsusp stuff */
int resumeCommand(char * cmd, char * end) {
    char * resumedev = NULL;
    char * resume = NULL;
    int fd;
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

    qprintf("Trying to resume from %s\n", resumedev);

    if (mkpathbyspec(resumedev, "/dev/swsuspresume") == 0)
        resumedev = strdup("/dev/swsuspresume");

    if (access(resumedev, R_OK)) {
        eprintf("Unable to access resume device (%s)\n", resumedev);
        return 1;
    }

    if ((fd = open(resumedev, O_RDONLY)) < 0)
        return 1;
    if (lseek(fd, getpagesize() - 10, SEEK_SET) != getpagesize() - 10)
        return 1;
    if (read(fd, &buf, 6) != 6)
        return 1;
    if (strncmp(buf, "S1SUSP", 6) && strncmp(buf, "S2SUSP", 6)) {
        qprintf("No suspend signature on swap, not resuming.\n");
        return 1;
    }

    if (fstat(fd, &sb))
        return 1;
    close(fd);

    printf("Resuming from %s.\n", resumedev);
    fflush(stdout);
    fd = open("/sys/power/resume", O_WRONLY);
    memset(buf, 20, '\0');
    snprintf(buf, 20, "%d:%d", major(sb.st_rdev), minor(sb.st_rdev));
    write(fd, buf, 20);
    close(fd);

    eprintf("Resume failed.  Continuing with normal startup.\n");
    return 0;
}

int mkrootdevCommand(char * cmd, char * end) {
    char * path;
    char *root, * chptr;
    int devNum = 0;
    int fd;
    int i;
    char *buf;
    const char real_root_dev[] = "/proc/sys/kernel/real-root-dev";

    if (!(cmd = getArg(cmd, end, &path))) {
	eprintf("mkrootdev: path expected\n");
	return 1;
    }

    if (cmd < end) {
	eprintf("mkrootdev: unexpected arguments\n");
	return 1;
    }

    root = getKernelArg("root");

    if (root) {
	chptr = root;
	while (*chptr && !isspace(*chptr)) chptr++;
	*chptr = '\0';
    }

    if (root && !access(root, R_OK)) {
        if (!symlink(root, "/dev/root"))
            return 0;
    }

    if ((i = mkpathbyspec(root, path)) != -1)
        return i;

    fd = open(real_root_dev, O_RDONLY, 0);
    if (fd < 0) {
	eprintf("mkrootdev: failed to open %s: %s\n", real_root_dev, 
                strerror(errno));
	return 1;
    }

    i = readFD(fd, &buf);
    if (i < 0) {
	eprintf("mkrootdev: failed to read from real-root-dev: %s\n",
                strerror(errno));
	close(fd);
	return 1;
    }

    close(fd);
    if (i == 0)
        buf[i] = '\0';
    else
        buf[i - 1] = '\0';

    devNum = atoi(buf);
    if (devNum < 0) {
	eprintf("mkrootdev: bad device %s\n", buf);
	free(buf);
	return 1;
    }
    free(buf);

    if (!devNum && root)
	devNum = name_to_dev_t(root);

    if (smartmknod(path, S_IFBLK | 0700, devNum)) {
	eprintf("mkrootdev: mknod %s 0x%08x failed: %s\n", path, devNum, 
                strerror(errno));
	return 1;
    }

    return 0;
}

int mkdirCommand(char * cmd, char * end) {
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
	    eprintf("mkdir: failed to create %s: %s\n", dir, strerror(errno));
	    return 1;
	}
    }

    return 0;
}

int accessCommand(char * cmd, char * end) {
    char * permStr;
    int perms = 0;
    char * file = NULL;

    cmd = getArg(cmd, end, &permStr);
    if (cmd) cmd = getArg(cmd, end, &file);

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

int sleepCommand(char * cmd, char * end) {
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

int readlinkCommand(char * cmd, char * end) {
    char * path;
    char * buf, * respath, * fullpath;
    struct stat sb;
    int rc = 0;

    if (!(cmd = getArg(cmd, end, &path))) {
        eprintf("readlink: file expected\n");
        return 1;
    }

    if (lstat(path, &sb) == -1) {
        eprintf("unable to stat %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (!S_ISLNK(sb.st_mode)) {
        printf("%s\n", path);
        return 0;
    }
    
    buf = calloc(512, sizeof (char));
    if (readlink(path, buf, 512) == -1) {
	eprintf("error readlink %s: %s\n", path, strerror(errno));
        free(buf);
	return 1;
    }

    /* symlink is absolute */
    if (buf[0] == '/') {
        printf("%s\n", buf);
        free(buf);
        return 0;
    } 
   
    /* nope, need to handle the relative symlink case too */
    respath = strrchr(path, '/');
    if (respath) {
        *respath = '\0';
    }

    fullpath = calloc(512, sizeof (char));
    /* and normalize it */
    snprintf(fullpath, 512, "%s/%s", path, buf);
    respath = NULL;
    respath = canonicalize_file_name(fullpath);
    if (respath == NULL) {
        eprintf("error resolving symbolic link %s: %s\n", fullpath,
                strerror(errno));
        rc = 1;
        goto readlinkout;
    }

    printf("%s\n", respath);
    free(respath);
 readlinkout:
    free(buf);
    free(fullpath);
    return rc;
}

int doFind(char * dirName, char * name) {
    struct stat sb;
    DIR * dir;
    struct dirent * d;
    char * strBuf = alloca(strlen(dirName) + 1024);

    if (!(dir = opendir(dirName))) {
	eprintf("error opening %s: %s\n", dirName, strerror(errno));
	return 0;
    }

    errno = 0;
    while ((d = readdir(dir))) {
	errno = 0;

	strcpy(strBuf, dirName);
	strcat(strBuf, "/");
	strcat(strBuf, d->d_name);

	if (!strcmp(d->d_name, name))
	    printf("%s\n", strBuf);

	if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
	    errno = 0;
	    continue;
	}

	if (lstat(strBuf, &sb)) {
	    eprintf("failed to stat %s: %s\n", strBuf, strerror(errno));
	    errno = 0;
	    continue;
	}

	if (S_ISDIR(sb.st_mode))
	    doFind(strBuf, name);
    }

    if (errno) {
	closedir(dir);
	eprintf("error reading from %s: %s\n", dirName, strerror(errno));
	return 1;
    }

    closedir(dir);

    return 0;
}

int findCommand(char * cmd, char * end) {
    char * dir;
    char * name;

    cmd = getArg(cmd, end, &dir);
    if (cmd) cmd = getArg(cmd, end, &name);
    if (cmd && strcmp(name, "-name")) {
	eprintf("usage: find [path] -name [file]\n");
	return 1;
    }

    if (cmd) cmd = getArg(cmd, end, &name);
    if (!cmd) {
	eprintf("usage: find [path] -name [file]\n");
	return 1;
    }

    return doFind(dir, name);
}

int findlodevCommand(char * cmd, char * end) {
    char devName[20];
    int devNum;
    int fd;
    struct loop_info loopInfo;
    char separator[2] = "";

    if (*end != '\n') {
	eprintf("usage: findlodev\n");
	return 1;
    }

    if (!access("/dev/.devfsd", X_OK))
	strcpy(separator, "/");

    for (devNum = 0; devNum < 256; devNum++) {
	sprintf(devName, "/dev/loop%s%d", separator, devNum);
	if ((fd = open(devName, O_RDONLY)) < 0) return 0;

	if (ioctl(fd, LOOP_GET_STATUS, &loopInfo)) {
	    close(fd);
	    printf("%s\n", devName);
	    return 0;
	}

	close(fd);
    }

    return 0;
}

int mknodCommand(char * cmd, char * end) {
    char * path, * type;
    char * majorStr, * minorStr;
    int major;
    int minor;
    char * chptr;
    mode_t mode;

    cmd = getArg(cmd, end, &path);
    cmd = getArg(cmd, end, &type);
    cmd = getArg(cmd, end, &majorStr);
    cmd = getArg(cmd, end, &minorStr);
    if (!minorStr) {
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
	eprintf("mknod: failed to create %s: %s\n", path, strerror(errno));
	return 1;
    }

    return 0;
}

static int getDevNumFromProc(char * file, char * device) {
    char buf[32768], line[4096];
    char * start, *end;
    int num;
    int fd;

    if ((fd = open(file, O_RDONLY)) == -1) {
        eprintf("can't open file %s: %s\n", file, strerror(errno));
        return -1;
    }

    num = read(fd, buf, sizeof(buf));
    if (num < 1) {
        close(fd);
        eprintf("failed to read %s: %s\n", file, strerror(errno));
        return -1;
    }
    buf[num] = '\0';
    close(fd);

    start = buf;
    end = strchr(start, '\n');
    while (start && end) {
        *end++ = '\0';
        if ((sscanf(start, "%d %s", &num, line)) == 2) {
            if (!strncmp(device, line, strlen(device)))
                return num;
        }
        start = end;
        end = strchr(start, '\n');
    }
    return -1;
}

int mkDMNodCommand(char * cmd, char * end) {
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

static int parse_sysfs_devnum(const char *path, dev_t *dev)
{
    char *first = NULL, *second;
    int major, minor;
    int fd, len = strlen(path);
    char devname[len + 5];
    
    sprintf(devname, "%s/dev", path);
    fd = open(devname, O_RDONLY);
    if (fd < 0) {
        // eprintf("open on %s failed: %s\n", devname, strerror(errno));
        return -1;
    }

    len = readFD(fd, &first);

    close(fd);

    second = strchr(first, ':');
    *second++ = '\0';

    errno = 0;
    major = strtol(first, NULL, 10);

    errno = 0;
    minor = strtol(second, NULL, 10);
    free(first);

    *dev = makedev(major, minor);
    return 0;
}

static void sysfs_blkdev_probe(const char *dirname, const char *name)
{
    char *path = NULL, *devpath = NULL;
    dev_t dev = 0;
    int ret;
    DIR *dir;
    struct dirent *dent;

    asprintf(&path, "%s/%s", dirname, name);
    //qprintf("Testing %s for block device.\n", path);

    ret = parse_sysfs_devnum(path, &dev);
    if (ret < 0) {
        free(path);
        return;
    }

    asprintf(&devpath, "/dev/%s", name);

    smartmknod(devpath, S_IFBLK | 0700, dev);
    free(devpath);

    dir = opendir(path);
    if (dir == NULL) {
        free(path);
        return;
    }

    for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
            continue;

        sysfs_blkdev_probe(path, dent->d_name);
    }
    free(path);
}

static int mkblkdevsCommand(char * cmd, char * end)
{
    DIR *dir;
    struct dirent *dent;

    if (cmd < end) {
	eprintf("mkblkdevs: unexpected arguments\n");
	return 1;
    }

    dir = opendir("/sys/block");
    if (dir == NULL)
        return -1;

    for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
            continue;

        sysfs_blkdev_probe("/sys/block", dent->d_name);
    }
    return 1;
}

int setQuietCommand(char * cmd, char * end) {
    char *quietcmd;

    quietcmd = getKernelArg("quiet");
    if (quietcmd)
        reallyquiet = 1;

    /* reallyquiet may be set elsewhere */
    if (reallyquiet)
          quiet = 1;

    return 0;
}

int runStartup(int fd, char *name) {
    char *contents;
    int i;
    char * start, * end;
    char * chptr;
    int rc;

    i = readFD(fd, &contents);

    if (i < 0) {
        eprintf("Failed to read startup file %s", name);
        return 1;
    }
    close(fd);

    start = contents;
    while (*start) {
	while (isspace(*start) && *start && (*start != '\n')) start++;

	if (*start == '#')
	    while (*start && (*start != '\n')) start++;

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
	while (*end && (*end != '\n')) end++;
	if (!*end) {
	    eprintf("(last line in %s missing newline -- skipping)\n", name);
	    start = end;
	    continue;
	}

	/* end points to the \n at the end of the command */

	chptr = start;
	while (chptr < end && !isspace(*chptr)) chptr++;

	if (!strncmp(start, "mount", MAX(5, chptr - start)))
	    rc = mountCommand(chptr, end);
	else if (!strncmp(start, "losetup", MAX(7, chptr - start)))
	    rc = losetupCommand(chptr, end);
	else if (!strncmp(start, "echo", MAX(4, chptr - start)))
	    rc = echoCommand(chptr, end);
	else if (!strncmp(start, "raidautorun", MAX(11, chptr - start)))
	    rc = raidautorunCommand(chptr, end);
        else if (!strncmp(start, "switchroot", MAX(10, chptr - start)))
            rc = switchrootCommand(chptr, end);
	else if (!strncmp(start, "mkrootdev", MAX(9, chptr - start)))
	    rc = mkrootdevCommand(chptr, end);
	else if (!strncmp(start, "umount", MAX(6, chptr - start)))
	    rc = umountCommand(chptr, end);
	else if (!strncmp(start, "exec", MAX(4, chptr - start)))
	    rc = execCommand(chptr, end);
	else if (!strncmp(start, "mkdir", MAX(5, chptr - start)))
	    rc = mkdirCommand(chptr, end);
	else if (!strncmp(start, "access", MAX(6, chptr - start)))
	    rc = accessCommand(chptr, end);
	else if (!strncmp(start, "find", MAX(4, chptr - start)))
	    rc = findCommand(chptr, end);
	else if (!strncmp(start, "findlodev", MAX(7, chptr - start)))
	    rc = findlodevCommand(chptr, end);
	else if (!strncmp(start, "showlabels", MAX(10, chptr-start)))
	    rc = display_uuid_cache();
	else if (!strncmp(start, "mkblkdevs", MAX(9, chptr-start)))
	    rc = mkblkdevsCommand(chptr, end);
	else if (!strncmp(start, "sleep", MAX(5, chptr-start)))
	    rc = sleepCommand(chptr, end);
	else if (!strncmp(start, "mknod", MAX(5, chptr-start)))
	    rc = mknodCommand(chptr, end);
        else if (!strncmp(start, "mkdmnod", MAX(7, chptr-start)))
            rc = mkDMNodCommand(chptr, end);
        else if (!strncmp(start, "readlink", MAX(8, chptr-start)))
            rc = readlinkCommand(chptr, end);
        else if (!strncmp(start, "setquiet", MAX(8, chptr-start)))
            rc = setQuietCommand(chptr, end);
        else if (!strncmp(start, "resume", MAX(6, chptr-start)))
            rc = resumeCommand(chptr, end);
#ifdef DEBUG
        else if (!strncmp(start, "cat", MAX(3, chptr-start)))
            rc = catCommand(chptr, end);
        else if (!strncmp(start, "ls", MAX(2, chptr-start)))
            rc = lsCommand(chptr, end);
#endif
	else {
	    *chptr = '\0';
	    rc = otherCommand(start, chptr + 1, end, 1);
	}

	start = end + 1;
    }

    free(contents);
    return rc;
}

int main(int argc, char **argv) {
    int fd = 0;
    char * name;
    int rc;
    int force = 0;

    name = strrchr(argv[0], '/');
    if (!name) 
	name = argv[0];
    else
	name++;

    if (!strcmp(name, "modprobe"))
	exit(0);
    if (!strcmp(name, "hotplug")) {
        argv[0] = strdup("/sbin/udev");
        execv(argv[0], argv);
        eprintf("ERROR: exec of udev failed!\n");
        exit(1);
    }

    testing = (getppid() != 0) && (getppid() != 1);
    argv++, argc--;

    while (argc && **argv == '-') {
	if (!strcmp(*argv, "--force")) {
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
	    eprintf("nash: cannot open %s: %s\n", *argv, strerror(errno));
	    exit(1);
	}
    }

    /* runStartup closes fd */
    rc = runStartup(fd, *argv);

    return rc;
}
