#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <newt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>

#define _(foo) (foo)

void winStatus(int width, int height, char * title,
		char * text, ...) {
    newtComponent t, f;
    char * buf = NULL;
    int size = 0;
    int i = 0;
    va_list args;

    va_start(args, text);

    do {
	size += 1000;
	if (buf) free(buf);
	buf = malloc(size);
	i = vsnprintf(buf, size, text, args);
    } while (i == size);

    va_end(args);

    newtCenteredWindow(width, height, title);

    t = newtTextbox(1, 1, width - 2, height - 2, NEWT_TEXTBOX_WRAP);
    newtTextboxSetText(t, buf);
    f = newtForm(NULL, NULL, 0);

    free(buf);

    newtFormAddComponent(f, t);

    newtDrawForm(f);
    newtRefresh();
    newtFormDestroy(f);
}

/* Recursive */
int copyDirectory(char * from, char * to) {
    DIR * dir;
    struct dirent * ent;
    int fd, outfd;
    char buf[4096];
    int i;
    int total;
    struct stat sb;
    char filespec[256];
    char filespec2[256];
    char link[1024];

    mkdir(to, 0755);

    if (!(dir = opendir(from))) {
	newtWinMessage(_("Error"), _("OK"),
		       _("Failed to read directory %s: %s"),
		       from, strerror(errno));
	return 1;
    }

    errno = 0;
    while ((ent = readdir(dir))) {
	if (ent->d_name[0] == '.') continue;

	sprintf(filespec, "%s/%s", from, ent->d_name);
	sprintf(filespec2, "%s/%s", to, ent->d_name);

	lstat(filespec, &sb);

	if (S_ISDIR(sb.st_mode)) {
	    if (copyDirectory(filespec, filespec2)) return 1;
	} else if (S_ISLNK(sb.st_mode)) {
	    i = readlink(filespec, link, sizeof(link) - 1);
	    link[i] = '\0';
	    unlink(filespec2);
	    if (symlink(link, filespec2)) {
		fprintf(stderr, "failed to symlink %s to %s: %s",
		    filespec2, link, strerror(errno));
	    }
	} else {
	    fd = open(filespec, O_RDONLY);
	    if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s", filespec,
			   strerror(errno));
		return 1;
	    } 
	    unlink(filespec2);
	    outfd = open(filespec2, O_RDWR | O_TRUNC | O_CREAT, 0644);
	    if (outfd < 0) {
		fprintf(stderr, "failed to create %s: %s", filespec2,
			   strerror(errno));
	    } else {
		fchmod(outfd, sb.st_mode & 07777);

		total = 0;

		while ((i = read(fd, buf, sizeof(buf))) > 0) {
		    write(outfd, buf, i);
		    total += i;
		}

		close(outfd);
		printf("%s %d\n", filespec2, total);
	    }

	    close(fd);
	}

	errno = 0;
    }

    closedir(dir);

    return 0;
}

int main(int argc, char ** argv) {
    int fd;

    if (mount("/", "/", "ext2", MS_REMOUNT | MS_MGC_VAL, NULL)) {
	fprintf(stderr, "failed to remount root read-write: %s\n",
		strerror(errno));
	while(1);
    }

    newtInit();
    newtCls();

    while (1) {
	newtWinMessage(_("Insert Disk"), _("OK"), 
		       _("Insert bootdisk #2 into your floppy drive and press "
			 "Enter to continue."));

	fd = open("/dev/fd0", O_RDONLY);
	if (fd >= 0) {
	    close(fd);
	    if (mount("/dev/fd0", "/mnt", "ext2", 
		       (0xC0ED << 16) | MS_RDONLY, NULL))  {
		newtWinMessage(_("Error"), _("OK"),
			       _("Error mounting floppy device."));
	    } else if (access("/mnt/linuxrc", X_OK)) {
		newtWinMessage(_("Error"), _("OK"),
			       _("Invalid #2 bootdisk."));
	    } else {
		break;
	    }
	} else {
	    newtWinMessage(_("Error"), _("OK"),
			   _("Error opening floppy device."));
	}
    }


    winStatus(50, 3, _("Loading"), _("Loading bootdisk..."));
    copyDirectory("/mnt", "/");
    umount("/mnt");
    newtPopWindow();

    newtFinished();

    execv(argv[0], argv);

    return 0;
}
