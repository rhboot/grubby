/* Copyright (C) 2001 Red Hat, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the General Public License as published 
   by the Free Software Foundation; either version 2 of the License, or 
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <popt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define _(A) (A)

struct newKernelInfo {
    char * image;
    char * args;
    char * initrd;
    char * title;
    struct singleLine * template;
};

/* comments get lumped in with indention */
struct lineElement {
    char * item;
    char * indent;
};

struct singleLine {
    char * indent;
    int numElements;
    struct lineElement * elements;
    enum { LT_WHITESPACE, LT_TITLE, LT_KERNEL, LT_INITRD, LT_DEFAULT,
	   LT_UNKNOWN, LT_ROOT } type;
    struct singleLine * next;
    int skip;
};

struct grubConfig {
    struct singleLine * lines;
    char * primaryIndent;
    char * secondaryIndent;
    int defaultImage;		    /* -1 if none specified -- this value is
				     * written out, overriding original */
};

#define KERNEL_IMAGE	    (1 << 0)
#define KERNEL_INITRD	    (1 << 2)

#define KERNEL_PATH "/boot/vmlinuz-"
#define INITRD_PATH "/boot/initrd-"

static char * strndup(char * from, int len) {
    char * to;

    to = malloc(len + 1);
    strncpy(to, from, len);
    to[len] = '\0';

    return to;
}

static int readFile(int fd, char ** bufPtr) {
    int alloced = 0, size = 0, i = 0;
    char * buf = NULL;

    do {
	size += i;
	if ((size + 1024) > alloced) {
	    alloced += 4096;
	    buf = realloc(buf, alloced + 1);
	}
    } while ((i = read(fd, buf + size, 1024)) > 0);

    if (i < 0) {
	fprintf(stderr, _("error reading input: %s\n"), strerror(errno));
	return 1;
    }

    buf = realloc(buf, size + 2);
    if (buf[size - 1] != '\n')
	buf[size++] = '\n';
    buf[size] = '\0';

    *bufPtr = buf;

    return 0;
}

static void lineInit(struct singleLine * line) {
    line->indent = NULL;
    line->elements = NULL;
    line->numElements = 0;
    line->next = NULL;
    line->skip = 0;
}

static void lineFree(struct singleLine * line) {
    int i;

    if (line->indent) free(line->indent);

    for (i = 0; i < line->numElements; i++) {
	free(line->elements[i].item); 
	free(line->elements[i].indent); 
    }

    if (line->elements) free(line->elements);
    lineInit(line);
}

static int lineWrite(FILE * out, struct singleLine * line) {
    int i;

    fprintf(out, line->indent);
    for (i = 0; i < line->numElements; i++) {
	fprintf(out, line->elements[i].item);
	fprintf(out, line->elements[i].indent);
    }

    fprintf(out, "\n");

    return 0;
}

/* we've guaranteed that the buffer ends w/ \n\0 */
static int getNextLine(char ** bufPtr, struct singleLine * line) {
    char * end;
    char * start = *bufPtr;
    char * chptr;
    int elementsAlloced = 0;
    struct lineElement * element;
    int first = 1;

    lineFree(line);

    end = strchr(start, '\n');
    *end = '\0';
    *bufPtr = end + 1;

    for (chptr = start; *chptr && isspace(*chptr); chptr++) ;
    if (*chptr == '#') chptr = end;

    line->indent = strndup(start, chptr - start);
    start = chptr;

    while (start < end) {
	/* we know !isspace(*start) */

	if (elementsAlloced == line->numElements) {
	    elementsAlloced += 5;
	    line->elements = realloc(line->elements,
				sizeof(*line->elements) * elementsAlloced);
	}

	element = line->elements + line->numElements;

	chptr = start;
	while (*chptr && !isspace(*chptr)) {
	    if (first && *chptr == '=') break;
	    chptr++;
	}
	element->item = strndup(start, chptr - start);
	start = chptr;

	if (*start == '=')
	    chptr = start + 1;
	else
	    for (chptr = start; *chptr && isspace(*chptr); chptr++) ;

	element->indent = strndup(start, chptr - start);
	start = chptr;

	line->numElements++;
	first = 0;
    }

    if (!line->numElements)
	line->type = LT_WHITESPACE;
    else if (!strcmp(line->elements[0].item, "title"))
	line->type = LT_TITLE;
    else if (!strcmp(line->elements[0].item, "root"))
	line->type = LT_ROOT;
    else if (!strcmp(line->elements[0].item, "default"))
	line->type = LT_DEFAULT;
    else if (!strcmp(line->elements[0].item, "kernel"))
	line->type = LT_KERNEL;
    else if (!strcmp(line->elements[0].item, "initrd"))
	line->type = LT_INITRD;

    return 0;
}

static struct grubConfig * readConfig(const char * inName) {
    int in;
    char * incoming, * head;
    int rc;
    int sawTitle = 0;
    struct grubConfig * cfg;
    struct singleLine * last = NULL, * line;
    char * end;

    if (!strcmp(inName, "-")) {
	in = 0;
    } else {
	if ((in = open(inName, O_RDONLY)) < 0) {
	    fprintf(stderr, _("error opening %s for read: %s\n"),
		    inName, strerror(errno));
	    return NULL;
	}
    }

    rc = readFile(in, &incoming);
    close(in);
    if (rc) return NULL;

    head = incoming;
    cfg = malloc(sizeof(*cfg));
    cfg->primaryIndent = strdup("");
    cfg->secondaryIndent = strdup("\t");

    /* copy everything we have */
    while (*head) {
	line = malloc(sizeof(*line));
	lineInit(line);

	if (getNextLine(&head, line)) {
	    free(line);
	    /* XXX memory leak of everything in cfg */
	    return NULL;
	}

	if (last)
	    last->next = line;
	else
	    cfg->lines = line;
	last = line;

	if (!sawTitle && line->numElements) {
	    free(cfg->primaryIndent);
	    cfg->primaryIndent = strdup(line->indent);
	} else if (line->numElements) {
	    free(cfg->secondaryIndent);
	    cfg->secondaryIndent = strdup(line->indent);
	}

	if (line->type == LT_TITLE) {
	    sawTitle = 1;
	} else if (line->type == LT_DEFAULT && line->numElements == 2) {
	    cfg->defaultImage = strtol(line->elements[1].item, &end, 10);
	    if (*end) cfg->defaultImage = -1;
	}
    }

    free(incoming);

    return cfg;
}

int writeNewKernel(FILE * out, struct grubConfig * cfg, 
		   struct newKernelInfo * nki, const char * prefix) {
    struct singleLine * line;
    char * indent;
    int needs;
    int i;
    char * chptr;

    if (!nki) return 0;

    if (nki->template)
	line = nki->template;
    else
	line = NULL;

    indent = line ? nki->template->indent : indent = cfg->primaryIndent;
    fprintf(out, "%stitle %s\n", indent, nki->title);

    if (line) line = line->next;
    needs = KERNEL_IMAGE | KERNEL_INITRD;

    indent = NULL;

    while (line && line->type != LT_TITLE) {
	indent = line->indent;

	if (line->type == LT_KERNEL && line->numElements > 2) {
	    fprintf(out, "%s%s%s%s%s", line->indent,
		    line->elements[0].item, line->elements[0].indent,
		    nki->image + strlen(prefix),
		    line->elements[1].indent);
	    if (nki->args) {
		if (!strlen(line->elements[1].indent))
		    fprintf(out, " ");
		fprintf(out, "%s\n", nki->args);
	    } else {
		for (i = 2; i < line->numElements; i++)
		    fprintf(out, "%s%s", line->elements[i].item,
			    line->elements[i].indent);
		fprintf(out, "\n");
	    }

	    needs &= ~KERNEL_IMAGE;
	} else if (line->type == LT_INITRD && line->numElements == 2 &&
		   nki->initrd) {
	    fprintf(out, "%s%s%s%s%s\n", line->indent,
		    line->elements[0].item, line->elements[0].indent,
		    nki->initrd + strlen(prefix),
		    line->elements[1].indent);

	    needs &= ~KERNEL_INITRD;
	} else if (line->type == LT_KERNEL || line->type == LT_INITRD) {
	    /* skip these if they are badly formed */
	} else if (!line->numElements) {
	    /* skip comments; they probably don't apply anyway */
	    chptr = line->indent;
	    while (*chptr && isspace(*chptr)) chptr++;
	    if (*chptr != '#')
		lineWrite(out, line);
	} else {
	    lineWrite(out, line);
	}

	line = line->next;
    }

    if (!indent) indent = cfg->secondaryIndent;

    if (needs & KERNEL_IMAGE) {
	fprintf(out, "%skernel %s", indent, nki->image + strlen(prefix));
	if (nki->args)
	    fprintf(out, " %s", nki->args);
	fprintf(out, "\n");
    }

    if ((needs & KERNEL_INITRD) && nki->initrd) {
	fprintf(out, "%sinitrd %s\n", indent, nki->initrd + strlen(prefix));
    }

    return 0;
}

static int writeConfig(struct grubConfig * cfg, const char * outName, 
		       struct newKernelInfo * nki, const char * prefix) {
    FILE * out;
    struct singleLine * line;
    char * tmpOutName;

    if (!strcmp(outName, "-")) {
	out = stdout;
	tmpOutName = NULL;
    } else {
	tmpOutName = alloca(strlen(outName) + 2);
	sprintf(tmpOutName, "%s-", outName);
	out = fopen(tmpOutName, "w");
	if (!out) {
	    fprintf(stderr, _("grubby: error creating %s: %s"), tmpOutName, 
		    strerror(errno));
	    return 1;
	}
    }

    line = cfg->lines;
    while (line) {
	if (!line->skip) {
	    if (line->type == LT_TITLE && nki) {
		writeNewKernel(out, cfg, nki, prefix);
		nki = NULL;
		lineWrite(out, line);
	    } else if (line->type == LT_DEFAULT) {
		if (cfg->defaultImage != -1) {
		    fprintf(out, "%s%s%s%d\n", line->indent,
			    line->elements[0].item,
			    line->elements[0].indent, cfg->defaultImage);
		}
	    } else {
		lineWrite(out, line);
	    }
	}

	line = line->next;
    }

    if (tmpOutName) {
	if (rename(tmpOutName, outName)) {
	    fprintf(stderr, _("grubb: error moving %s to %s: %s\n"),
		    tmpOutName, outName, strerror(errno));
	    unlink(outName);
	}
    }

    return 0;
}

int suitableImage(struct singleLine * line, const char * bootPrefix) {
    char * fullName;
    int i;
    struct stat sb, sb2;
    char * dev;
    char * end;

    do {
	line = line->next;
    } while (line && (line->type != LT_TITLE) && (line->type != LT_KERNEL));

    if (!line) return 0;
    if (line->type == LT_TITLE) return 0;
    if (line->numElements < 2) return 0;

    fullName = alloca(strlen(bootPrefix) + 
		      strlen(line->elements[1].item) + 1);
    sprintf(fullName, "%s%s", bootPrefix, line->elements[1].item);
    if (access(fullName, R_OK)) return 0;

    for (i = 2; i < line->numElements; i++)
	if (!strncasecmp(line->elements[i].item, "root=", 5)) break;
    if (i == line->numElements) return 0;

    dev = line->elements[i].item + 5;
    if (*dev == '/') {
	if (stat(line->elements[i].item + 5, &sb))
	    return 0;
    } else {
	sb.st_rdev = strtol(dev, &end, 16);
	if (*end) return 0;
    }
    stat("/", &sb2);

    if (sb.st_rdev != sb2.st_dev) return 0;

    return 1;
}

struct singleLine * findTitleByIndex(struct grubConfig * cfg, int index) {
    struct singleLine * line;

    line = cfg->lines;

    do {
	while (line && line->type != LT_TITLE) line = line->next;
	if (line && !index) break;
	line = line->next;
	index--;
    } while (line);

    return line;
}

/* Find a good template to use for the new kernel. An entry is
 * old good if the kernel and mkinitrd exist (even if the entry
 * is going to be removed). Try and use the default entry, but
 * if that doesn't work just take the first. If we can't find one,
 * bail. */
struct singleLine * findTemplate(struct grubConfig * cfg, const char * prefix,
				 int * indexPtr, int skipRemoved) {
    struct singleLine * line;
    int index;

    if (cfg->defaultImage != -1) {
	line = findTitleByIndex(cfg, cfg->defaultImage);
	if (line && suitableImage(line, prefix)) {
	    if (indexPtr) *indexPtr = cfg->defaultImage;
	    return line;
	}
    }

    line = cfg->lines;
    index = 0;
    while (line) {
	while (line && ((line->type != LT_TITLE) ||
		        (skipRemoved && line->skip)))
	    line = line->next;
	if (!line) break;

	if (suitableImage(line, prefix)) {
	    if (indexPtr) *indexPtr = index;
	    return line;
	}

	index++;
	line = line->next;
    }

    fprintf(stderr, _("fatal error: unable to find a suitable template\n"));

    return NULL;
}

char * findBootPrefix(void) {
    struct stat sb, sb2;

    stat("/", &sb);
    stat("/boot", &sb2);

    if (sb.st_dev == sb2.st_dev)
	return strdup("");

    return strdup("/boot");
}

void markRemovedImage(struct grubConfig * cfg, const char * image, 
		      const char * prefix) {
    struct singleLine * line, * title = NULL;

    if (!image) return;

    line = cfg->lines;
    while (line) {
	if (line->type == LT_TITLE) {
	    title = line;
	    line = line->next;
	} else if (title && line->type == LT_KERNEL && line->numElements >= 2
		   && !strcmp(line->elements[1].item, image + strlen(prefix))){
	    title->skip = 1;
	    title = title->next;
	    while (title && title->type != LT_TITLE) {
		title->skip = 1;
		title = title->next;
	    }

	    line = title;
	    title = NULL;
	} else {
	    line = line->next;
	}
    }
}

void setDefaultImage(struct grubConfig * config, int hasNew, 
		     const char * defaultKernelPath, int newIsDefault,
		     const char * prefix) {
    struct singleLine * line, * line2;
    struct singleLine * newDefault;
    int i;

    if (newIsDefault) {
	config->defaultImage = 0;
	return;
    } else if (defaultKernelPath) {
	line = config->lines;
	i = 0;
	while (line) {
	    while (line && line->type != LT_TITLE) line = line->next;
	    if (!line) {
		config->defaultImage = -1;
		return;
	    }

	    if (line->skip) {
		while (line && line->type != LT_TITLE) line = line->next;
		continue;
	    }

	    while (line && line->type != LT_KERNEL) line=line->next;
	    if (!line || line->numElements < 2) {
		config->defaultImage = -1;
		return;
	    }

	    if (!strcmp(line->elements[1].item, defaultKernelPath + 
						strlen(prefix))) {
		config->defaultImage = i;
		return;
	    }

	    line = line->next;
	    i++;
	}
    }

    /* try and keep the same default */
    if (config->defaultImage != -1)
	line = findTitleByIndex(config, config->defaultImage);
    else
	line = NULL;

    if (line && !line->skip) {
	if (hasNew)
	    config->defaultImage++;
	
	/* see if we erased something before this */
	line2 = config->lines;
	while (line2 != line) {
	    if (line2->type == LT_TITLE && line2->skip)
		config->defaultImage--;
	    line2 = line2->next;
	}
    } else if (hasNew) {
	config->defaultImage = 0;
    } else {
	/* Either we just erased the default (or the default line was bad
	 * to begin with) and didn't put a new one in. We'll use the first
	 * valid image. */
	newDefault = findTemplate(config, prefix, &config->defaultImage, 1);
	if (!newDefault)
	    config->defaultImage = -1;
    }
}

int main(int argc, const char ** argv) {
    poptContext optCon;
    char * grubConfig = "/boot/grub/grub.conf";
    char * outputFile = NULL;
    int arg;
    char * newKernelPath = NULL;
    char * oldKernelPath = NULL;
    char * newKernelArgs = NULL;
    char * newKernelInitrd = NULL;
    char * newKernelTitle = NULL;
    char * newKernelVersion = NULL;
    char * bootPrefix;
    char * defaultKernel = NULL;
    struct grubConfig * config;
    struct newKernelInfo newKernel;
    struct singleLine * template = NULL;
    int copyDefault = 0, makeDefault = 0;
    int displayDefault = 0;
    struct poptOption options[] = {
	{ "add-kernel", 0, POPT_ARG_STRING, &newKernelPath, 0,
	    _("add an entry for the specified kernel"), _("kernel-path") },
	{ "args", 0, POPT_ARG_STRING, &newKernelArgs, 0, _("default arguments for the new kernel"), _("args") },
	{ "config-file", 'c', POPT_ARG_STRING, &grubConfig, 0,
	    _("path to grub config file to update (\"-\" for stdin)"), 
	    _("path") },
	{ "copy-default", 0, 0, &copyDefault, 0,
	    _("use the default boot entry as a template for the new entry "
	      "being added; if the default is not a linux image, or if "
	      "the kernel referenced by the default image does not exist, "
	      "the first linux entry whose kernel does exist is used as the "
	      "template"), NULL },
	{ "default-kernel", 0, 0, &displayDefault, 0,
	    _("display the path of the default kernel") },
	{ "initrd", 0, POPT_ARG_STRING, &newKernelInitrd, 0,
	    _("initrd image for the new kernel"), _("args") },
	{ "make-default", 0, 0, &makeDefault, 0,
	    _("make the newly added entry the default boot entry"), NULL },
	{ "output-file", 'o', POPT_ARG_STRING, &outputFile, 0,
	    _("path to output updated config file (\"-\" for stdout)"), 
	    _("path") },
	{ "remove-kernel", 0, POPT_ARG_STRING, &oldKernelPath, 0,
	    _("remove all entries for the specified kernel"), 
	    _("kernel-path") },
	{ "set-default", 0, POPT_ARG_STRING, &defaultKernel, 0,
	    _("make the first entry referencing the specified kernel "
	      "the default") },
	{ "title", 0, POPT_ARG_STRING, &newKernelTitle, 0,
	    _("title to use for the new kernel entry"), _("entry-title") },
	{ "version", 'v', 0, NULL, 'v',
	    _("print the version of this program and exit"), NULL },
	POPT_AUTOHELP
	{ 0, 0, 0, 0, 0 }
    };

    optCon = poptGetContext("grubby", argc, argv, options, 0);
    poptReadDefaultConfig(optCon, 1);

    while ((arg = poptGetNextOpt(optCon)) >= 0) {
	switch (arg) {
	  case 'v':
	    printf("grubby version %s\n", VERSION);
	    exit(0);
	    break;
	}
    }

    if (arg < -1) {
	fprintf(stderr, _("grubby: bad argument %s: %s\n"),
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(arg));
	return 1;
    }

    if (displayDefault && (newKernelVersion || newKernelPath ||
			   oldKernelPath)) {
	fprintf(stderr, _("grubby: --display-default may not be used "
			"when adding or removing kernels\n"));
	return 1;
    }

    if (newKernelPath && !newKernelTitle) {
	fprintf(stderr, _("grubby: kernel title must be specified\n"));
	return 1;
    } else if (!newKernelPath && (newKernelTitle  || newKernelInitrd ||
				  newKernelInitrd || copyDefault     ||
				  makeDefault)) {
	fprintf(stderr, _("grubby: kernel path expected\n"));
	return 1;
    }

    if (makeDefault && defaultKernel) {
	fprintf(stderr, _("grubby: --make-default and --default-kernel "
			  "may not be used together\n"));
	return 1;
    } else if (defaultKernel && oldKernelPath &&
		!strcmp(defaultKernel, oldKernelPath)) {
	fprintf(stderr, _("grubby: cannot make removed kernel the default\n"));
	return 1;
    } else if (defaultKernel && newKernelPath &&
		!strcmp(defaultKernel, newKernelPath)) {
	makeDefault = 1;
	defaultKernel = NULL;
    }

    if (!strcmp(grubConfig, "-") && !outputFile) {
	fprintf(stderr, _("grubby: output file must be specified if stdin "
			"is used\n"));
	return 1;
    }

    if (!oldKernelPath && !newKernelPath && !displayDefault && !defaultKernel) {
	fprintf(stderr, _("grubby: no action specified\n"));
	return 1;
    }

    bootPrefix = findBootPrefix();
    if (!bootPrefix) return 1;

    config = readConfig(grubConfig);
    if (!config) return 1;

    if (displayDefault) {
	struct singleLine * image;

	if (config->defaultImage == -1) return 0;
	image = findTitleByIndex(config, config->defaultImage);
	if (!image) return 0;
	if (!suitableImage(image, bootPrefix)) return 0;

	while (image->type != LT_KERNEL) image = image->next;
	printf("%s%s\n", bootPrefix, image->elements[1].item);

	return 0;
    }

    if (copyDefault) {
	template = findTemplate(config, bootPrefix, NULL, 0);
	if (!template) return 1;
    }

    markRemovedImage(config, oldKernelPath, bootPrefix);
    setDefaultImage(config, newKernelPath != NULL, defaultKernel, makeDefault, 
		    bootPrefix);

    newKernel.title = newKernelTitle;
    newKernel.image = newKernelPath;
    newKernel.args = newKernelArgs;
    newKernel.initrd = newKernelInitrd;
    newKernel.template = template;

    if (!outputFile)
	outputFile = grubConfig;

    return writeConfig(config, outputFile, newKernelPath ? &newKernel : NULL,
		       bootPrefix);
}
