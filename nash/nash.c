/*
 * nash.c
 * 
 * Simple code to load modules, mount root, and get things going. It's designed
 * not to be linked against libc, which keeps it small.
 *
 * Erik Troan (ewt@redhat.com)
 *
 * Copyright 2001 Red Hat Software 
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

#if USE_MINILIBC
#include "minilibc.h"
#ifndef SOCK_STREAM
# define SOCK_STREAM 1
#endif 
#else
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
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
#endif

#include <linux/cdrom.h>
#define MD_MAJOR 9
#include <linux/raid/md_u.h>

#ifndef RAID_AUTORUN
#define RAID_AUTORUN           _IO (MD_MAJOR, 0x14)
#endif

#ifndef MS_REMOUNT
#define MS_REMOUNT      32
#endif

#define MAX(a, b) ((a) > (b) ? a : b)

int testing = 0;

#define PATH "/usr/bin:/bin:/sbin:/usr/sbin"

char * env[] = {
    "PATH=" PATH,
    NULL
};

char * getArg(char * cmd, char * end, char ** arg) {
    char quote = '\0';

    if (cmd >= end) return NULL;

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
	    printf("error: quote mismatch for %s\n", *arg);
	    return NULL;
	}

	*cmd = '\0';
	cmd++;
    } else {
	*arg = cmd;
	while (!isspace(*cmd) && cmd < end) cmd++;
	*cmd = '\0';
    }

    cmd++;

    while (isspace(*cmd)) cmd++;

    return cmd;
}

void mountCommand(char * cmd, char * end) {
    char * fsType;
    char * device;
    char * mntPoint;

    if (!(cmd = getArg(cmd, end, &fsType)) || strcmp(fsType, "-t")) {
	printf("mount: -t must be first argument\n");
	return;
    }

    if (!(cmd = getArg(cmd, end, &fsType))) {
	printf("mount: missing filesystem type\n");
	return;
    }

    if (!(cmd = getArg(cmd, end, &device))) {
	printf("mount: missing device\n");
	return;
    }

    if (!(cmd = getArg(cmd, end, &mntPoint))) {
	printf("mount: missing mount point\n");
	return;
    }

    if (cmd < end) {
	printf("mount: unexpected arguments\n");
	return;
    }

    if (testing) {
	printf("mount -t '%s' '%s' '%s'\n", fsType, device, mntPoint);
    } else {
	if (mount(device, mntPoint, fsType, MS_MGC_VAL, NULL)) {
	    printf("mount: error %d mounting %s\n", errno, fsType);
	}
    }
}

void otherCommand(char * bin, char * cmd, char * end) {
    char * args[32];
    char ** nextArg;
    int pid;
    int status;
    char fullPath[255];
    const static char * sysPath = PATH;
    const char * pathStart;
    const char * pathEnd;

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

    *nextArg = bin;

    while (cmd && cmd < end) {
	nextArg++;
	cmd = getArg(cmd, end, nextArg);
    }
	
    if (cmd) nextArg++;
    *nextArg = NULL;

    if (testing) {
	printf("%s ", bin);
	nextArg = args + 1;
	while (*nextArg)
	    printf(" '%s'", *nextArg++);
	printf("\n");
    } else {
	if (!(pid = fork())) {
	    /* child */
	    execve(args[0], args, env);
	    printf("ERROR: failed in exec of %s\n", args[0]);
	    exit(1);
	}

	wait4(-1, &status, 0, NULL);
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	    printf("ERROR: %s exited abnormally!\n", args[0]);
	}
    }
}

void losetupCommand(char * cmd, char * end) {
    char * device;
    char * file;
    int fd;
    struct loop_info loopInfo;
    int dev;

    if (!(cmd = getArg(cmd, end, &device))) {
	printf("losetup: missing device\n");
	return;
    }

    if (!(cmd = getArg(cmd, end, &file))) {
	printf("losetup: missing file\n");
	return;
    }

    if (cmd < end) {
	printf("losetup: unexpected arguments\n");
	return;
    }

    if (testing) {
	printf("losetup '%s' '%s'\n", device, file);
    } else {
	dev = open(device, O_RDWR, 0);
	if (dev < 0) {
	    printf("losetup: failed to open %s: %d\n", device, errno);
	    return;
	}

	fd = open(file, O_RDWR, 0);
	if (fd < 0) {
	    printf("losetup: failed to open %s: %d\n", file, errno);
	    close(dev);
	    return;
	}

	if (ioctl(dev, LOOP_SET_FD, (void *) fd)) {
	    printf("losetup: LOOP_SET_FD failed: %d\n", errno);
	    close(dev);
	    close(fd);
	    return;
	}

	close(fd);

	memset(&loopInfo, 0, sizeof(loopInfo));
	strcpy(loopInfo.lo_name, file);

	if (ioctl(dev, LOOP_SET_STATUS, &loopInfo)) 
	    printf("losetup: LOOP_SET_STATUS failed: %d\n", errno);

	close(dev);
    }
}

void raidautorunCommand(char * cmd, char * end) {
    char * device;
    int fd;

    if (!(cmd = getArg(cmd, end, &device))) {
	printf("raidautorun: raid device expected as first argument\n");
	return;
    }

    if (cmd < end) {
	printf("raidautorun: unexpected arguments\n");
	return;
    }

    fd = open(device, O_RDWR, 0);
    if (fd < 0) {
	printf("raidautorun: failed to open %s: %d\n", device, errno);
	return;
    }

    if (ioctl(fd, RAID_AUTORUN, 0)) {
	printf("raidautorun: RAID_AUTORUN faileds: %d\n", errno);
    }

    close(fd);
}

void echoCommand(char * cmd, char * end) {
    char * args[256];
    char ** nextArg = args;
    int outFd = 1;
    int num = 0;
    int i;

    if (testing)
	printf("(echo) ");

    while ((cmd = getArg(cmd, end, nextArg)))
	nextArg++, num++;

    if ((nextArg - args >= 2) && !strcmp(*(nextArg - 2), ">")) {
	outFd = open(*(nextArg - 1), O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (outFd < 0) {
	    printf("echo: cannot open %s for write: %d\n", 
		    *(nextArg - 1), errno);
	    return;
	}

	num -= 2;
    }

    for (i = 0; i < num;i ++) {
	if (i)
	    write(i, " ", 1);
	write(outFd, args[i], strlen(args[i]));
    }

    write(outFd, "\n", 1);
}

int runStartup(int fd) {
    char contents[32768];
    int i;
    char * start, * end;
    char * chptr;

    i = read(fd, contents, sizeof(contents) - 1);
    if (i == (sizeof(contents) - 1)) {
	printf("Failed to read /startup.rc -- file too large.\n");
	return 1;
    }

    contents[i] = '\0';

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
	    printf("(last line in /startup.rc is empty)\n");
	    continue;
	}

	/* start points to the beginning of the command */
	end = start + 1;
	while (*end && (*end != '\n')) end++;
	if (!*end) {
	    printf("(last line in /startup.rc missing \\n -- skipping)\n");
	    start = end;
	    continue;
	}

	/* end points to the \n at the end of the command */

	chptr = start;
	while (chptr < end && !isspace(*chptr)) chptr++;

	if (!strncmp(start, "mount", MAX(5, chptr - start)))
	    mountCommand(chptr, end);
	else if (!strncmp(start, "losetup", MAX(7, chptr - start)))
	    losetupCommand(chptr, end);
	else if (!strncmp(start, "echo", MAX(4, chptr - start)))
	    echoCommand(chptr, end);
	else if (!strncmp(start, "raidautorun", MAX(11, chptr - start)))
	    raidautorunCommand(chptr, end);
	else {
	    *chptr = '\0';
	    otherCommand(start, chptr, end);
	}

	start = end + 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    int fd = 0;
    char ** nextArg = argv + 1;
    char * name;

    name = strchr(argv[0], '/');
    if (!name) 
	name = argv[0];
    else
	name++;

    if (!strcmp(name, "modprobe"))
	exit(0);

    if (argc > 1 && !strcmp(*nextArg, "--force")) {
	printf("(forcing normal run)\n");
	nextArg++;
    } else
	testing = (getppid() != 0) && (getppid() != 1);

    if (testing)
	printf("(running in test mode).\n");

    printf("Red Hat nash version %s starting\n", VERSION);

    if (*nextArg) {
	fd = open(*nextArg, O_RDONLY, 0);
	if (fd < 0) {
	    printf("nash: cannot open %s: %d\n", *nextArg, errno);
	    exit(1);
	}
    }

    runStartup(fd);
    close(fd);

    return 0;
}
