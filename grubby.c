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

#define CODE_SEG_SIZE	  128	/* code segment checked by --bootloader-probe */


struct newKernelInfo {
    char * image;
    char * args;
    char * initrd;
    char * title;
    struct singleEntry * template;
};

/* comments get lumped in with indention */
struct lineElement {
    char * item;
    char * indent;
};

enum lineType_e { LT_WHITESPACE, LT_TITLE, LT_KERNEL, LT_INITRD, LT_DEFAULT,
       LT_UNKNOWN, LT_ROOT, LT_FALLBACK, LT_KERNELARGS, LT_BOOT };

struct singleLine {
    char * indent;
    int numElements;
    struct lineElement * elements;
    struct singleLine * next;
    enum lineType_e type;
};

struct singleEntry {
    struct singleLine * lines;
    int skip;
    struct singleEntry * next;
};

#define GRUBBY_BADIMAGE_OKAY	(1 << 0)

#define GRUB_CONFIG_NO_DEFAULT	    (1 << 0)	/* don't write out default=0 */

#define KERNEL_KERNEL	    (1 << 0)
#define KERNEL_INITRD	    (1 << 2)
#define KERNEL_TITLE	    (1 << 3)
#define KERNEL_ARGS	    (1 << 4)

#define MAIN_DEFAULT	    (1 << 0)
#define DEFAULT_SAVED       -2

#define KERNEL_PATH "/boot/vmlinuz-"
#define INITRD_PATH "/boot/initrd-"

struct keywordTypes {
    char * key;
    enum lineType_e type;
    char nextChar;
} ;

struct configFileInfo {
    char * defaultConfig;
    struct keywordTypes * keywords;
    int defaultIsIndex;
    int defaultSupportSaved;
    enum lineType_e entrySeparator;
    int needsBootPrefix;
};

struct keywordTypes grubKeywords[] = {
    { "title",	    LT_TITLE,	    ' ' },
    { "root",	    LT_ROOT,	    ' ' },
    { "default",    LT_DEFAULT,	    ' ' },
    { "fallback",   LT_FALLBACK,    ' ' },
    { "kernel",	    LT_KERNEL,	    ' ' },
    { "initrd",	    LT_INITRD,	    ' ' },
    { "#boot",	    LT_BOOT,	    '=' },
    { NULL,	    0 },
};

struct configFileInfo grubConfigType = {
    "/boot/grub/grub.conf",		    /* defaultConfig */
    grubKeywords,			    /* keywords */
    1,					    /* defaultIsIndex */
    1,					    /* defaultSupportSaved */
    LT_TITLE,				    /* entrySeparator */
    1,					    /* needsBootPrefix */
};

struct keywordTypes liloKeywords[] = {
    { "label",	    LT_TITLE,	    '=' },
    { "root",	    LT_ROOT,	    '=' },
    { "default",    LT_DEFAULT,	    '=' },
    { "image",	    LT_KERNEL,	    '=' },
    { "initrd",	    LT_INITRD,	    '=' },
    { "append",	    LT_KERNELARGS,  '=' },
    { "boot",	    LT_BOOT,	    '=' },
    { NULL,	    0 },
};

struct configFileInfo liloConfigType = {
#ifdef __ia64__
    "/boot/efi/elilo.conf",		    /* defaultConfig */
#else
    "/etc/lilo.conf",			    /* defaultConfig */
#endif
    liloKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_KERNEL,				    /* entrySeparator */
    0,					    /* needsBootPrefix */
};

struct grubConfig {
    struct singleLine * theLines;
    struct singleEntry * entries;
    char * primaryIndent;
    char * secondaryIndent;
    int defaultImage;		    /* -1 if none specified -- this value is
				     * written out, overriding original */
    int fallbackImage;		    /* just like defaultImage */
    int flags;
    struct configFileInfo * cfi;
};


struct singleEntry * findEntryByIndex(struct grubConfig * cfg, int index);
static char * strndup(char * from, int len);
static int readFile(int fd, char ** bufPtr);
static void lineInit(struct singleLine * line);
static void lineFree(struct singleLine * line);
static int lineWrite(FILE * out, struct singleLine * line);
static int getNextLine(char ** bufPtr, struct singleLine * line,
		       struct keywordTypes * keywords);

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
static int getNextLine(char ** bufPtr, struct singleLine * line,
		       struct keywordTypes * keywords) {
    char * end;
    char * start = *bufPtr;
    char * chptr;
    int elementsAlloced = 0;
    struct lineElement * element;
    int first = 1;
    int i;

    lineFree(line);

    end = strchr(start, '\n');
    *end = '\0';
    *bufPtr = end + 1;

    for (chptr = start; *chptr && isspace(*chptr); chptr++) ;

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
    else {
	for (i = 0; keywords[i].key; i++) 
	    if (!strcmp(line->elements[0].item, keywords[i].key)) break;

	if (keywords[i].key) {
	    line->type = keywords[i].type;
	} else {
	    line->type = LT_UNKNOWN;

	    /* this is awkward, but we need to be able to handle keywords
	       that begin with a # (specifically for #boot in grub.conf),
	       but still make comments lines with no elements (everything
	       stored in the indent */
	    if (*line->elements[0].item == '#') {
		char * fullLine;
		int len;
		int i;

		len = strlen(line->indent);
		for (i = 0; i < line->numElements; i++)
		    len += strlen(line->elements[i].item) + 
			   strlen(line->elements[i].indent);

		fullLine = malloc(len + 1);
		strcpy(fullLine, line->indent);
		free(line->indent);
		line->indent = fullLine;

		for (i = 0; i < line->numElements; i++) {
		    strcat(fullLine, line->elements[i].item);
		    strcat(fullLine, line->elements[i].indent);
		    free(line->elements[i].item);
		    free(line->elements[i].indent);
		}

		line->type = LT_WHITESPACE;
		line->numElements = 0;
	    }
	}
    }

    return 0;
}

static struct grubConfig * readConfig(const char * inName,
				      struct configFileInfo * cfi) {
    int in;
    char * incoming, * head;
    int rc;
    int sawEntry = 0;
    struct grubConfig * cfg;
    struct singleLine * last = NULL, * line, * defaultLine = NULL;
    char * end;
    struct singleEntry * entry = NULL;
    int i;

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
    cfg->flags = GRUB_CONFIG_NO_DEFAULT;
    cfg->cfi = cfi;
    cfg->theLines = NULL;
    cfg->entries = NULL;

    /* copy everything we have */
    while (*head) {
	line = malloc(sizeof(*line));
	lineInit(line);

	if (getNextLine(&head, line, cfi->keywords)) {
	    free(line);
	    /* XXX memory leak of everything in cfg */
	    return NULL;
	}

	if (!sawEntry && line->numElements) {
	    free(cfg->primaryIndent);
	    cfg->primaryIndent = strdup(line->indent);
	} else if (line->numElements) {
	    free(cfg->secondaryIndent);
	    cfg->secondaryIndent = strdup(line->indent);
	}

	if (line->type == cfi->entrySeparator) {
	    sawEntry = 1;
	    if (!entry) {
		cfg->entries = malloc(sizeof(*entry));
		entry = cfg->entries;
	    } else {
		entry->next = malloc(sizeof(*entry));
		entry = entry->next;
	    }

	    entry->skip = 0;
	    entry->lines = NULL;
	    entry->next = NULL;
	} else if (line->type == LT_DEFAULT && line->numElements == 2) {
	    cfg->flags &= ~GRUB_CONFIG_NO_DEFAULT;
	    defaultLine = line;
	} else if (line->type == LT_FALLBACK && line->numElements == 2) {
	    cfg->fallbackImage = strtol(line->elements[1].item, &end, 10);
	    if (*end) cfg->fallbackImage = -1;
	}

	if (sawEntry) {
	    if (!entry->lines)
		entry->lines = line;
	    else
		last->next = line;
	} else {
	    if (!cfg->theLines)
		cfg->theLines = line;
	    else {
		last->next = line;
	    }
	}

	last = line;
    }

    free(incoming);

    if (defaultLine) {
	if (cfi->defaultSupportSaved && 
		!strncmp(defaultLine->elements[1].item, "saved", 5)) {
	    cfg->defaultImage = DEFAULT_SAVED;
	} else if (cfi->defaultIsIndex) {
	    cfg->defaultImage = strtol(defaultLine->elements[1].item, &end, 10);
	    if (*end) cfg->defaultImage = -1;
	} else if (defaultLine->numElements >= 2) {
	    i = 0;
	    while ((entry = findEntryByIndex(cfg, i))) {
		for (line = entry->lines; line; line = line->next)
		    if (line->type == LT_TITLE) break;

		if (line && !strcmp(defaultLine->elements[1].item,
				    line->elements[1].item)) break;

		i++;
	    }

	    if (entry) cfg->defaultImage = i;
	}
    }

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
	line = nki->template->lines;
    else
	line = NULL;

    needs = KERNEL_KERNEL | KERNEL_INITRD | KERNEL_TITLE | KERNEL_ARGS;

    for (i = 0; cfg->cfi->keywords[i].key; i++)
	if (cfg->cfi->keywords[i].type == cfg->cfi->entrySeparator) break;

    switch (cfg->cfi->keywords[i].type) {
	case LT_KERNEL:	    needs &= ~KERNEL_KERNEL, chptr = nki->image; break;
	case LT_TITLE:	    needs &= ~KERNEL_TITLE, chptr = nki->title; break;
	default:	    abort();
    }

    indent = line ? nki->template->lines->indent : cfg->primaryIndent;
    fprintf(out, "%s%s%c%s\n", indent, cfg->cfi->keywords[i].key,
	    cfg->cfi->keywords[i].nextChar, chptr);

    if (line) line = line->next;

    indent = NULL;

    while (line) {
	/* skip comments */
	if (line->numElements)
	    indent = line->indent;

	if (line->type == LT_KERNEL && line->numElements > 2) {
	    /* this always rights out args, even if a LT_KERNELARGS is
	       present -- okay for config file types I've seen */
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

	    needs &= ~(KERNEL_KERNEL | KERNEL_ARGS);
	} else if (line->type == LT_INITRD && line->numElements == 2 &&
		   nki->initrd) {
	    fprintf(out, "%s%s%s%s%s\n", line->indent,
		    line->elements[0].item, line->elements[0].indent,
		    nki->initrd + strlen(prefix),
		    line->elements[1].indent);

	    needs &= ~KERNEL_INITRD;
	} else if (line->type == LT_KERNELARGS) {
	    /* assumes args need to be in " */
	    fprintf(out, "%s%s%s", line->indent,
		    line->elements[0].item, line->elements[0].indent);

	    if (nki->args) {
		fprintf(out, "\"%s\"\n", nki->args);
	    } else {
		for (i = 1; i < line->numElements; i++)
		    fprintf(out, "%s%s", line->elements[i].item,
			    line->elements[i].indent);
		fprintf(out, "\n");
	    }

	    needs &= ~KERNEL_ARGS;
	} else if (line->type == LT_TITLE && line->numElements == 2) {
	    fprintf(out, "%s%s%s%s%s\n", line->indent,
		    line->elements[0].item, line->elements[0].indent,
		    nki->title,
		    line->elements[1].indent);

	    needs &= ~KERNEL_TITLE;
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

    if ((needs & KERNEL_TITLE) && nki->title) {
	for (i = 0; cfg->cfi->keywords[i].key; i++)
	    if (cfg->cfi->keywords[i].type == LT_TITLE) break;

	fprintf(out, "%s%s%c%s\n", indent, cfg->cfi->keywords[i].key,
		cfg->cfi->keywords[i].nextChar, nki->title);
    }

    if ((needs & KERNEL_ARGS) && nki->args) {
	for (i = 0; cfg->cfi->keywords[i].key; i++)
	    if (cfg->cfi->keywords[i].type == LT_KERNELARGS) break;
	if (cfg->cfi->keywords[i].key) {
	    fprintf(out, "%s%s%c\"%s\"\n", indent, cfg->cfi->keywords[i].key,
		    cfg->cfi->keywords[i].nextChar, nki->args);
	    needs &= ~KERNEL_ARGS;
	}
    }

    if (needs & KERNEL_KERNEL) {
	fprintf(out, "%skernel %s", indent, nki->image + strlen(prefix));
	if (nki->args && (needs & KERNEL_ARGS))
	    fprintf(out, " %s", nki->args);
	fprintf(out, "\n");
    }

    if ((needs & KERNEL_INITRD) && nki->initrd) {
	for (i = 0; cfg->cfi->keywords[i].key; i++)
	    if (cfg->cfi->keywords[i].type == LT_INITRD) break;

	fprintf(out, "%s%s%c%s\n", indent, cfg->cfi->keywords[i].key,
		cfg->cfi->keywords[i].nextChar, nki->initrd + strlen(prefix));
    }

    return 0;
}

static void writeDefault(FILE * out, char * indent, 
			 char * separator, struct grubConfig * cfg,
			 struct newKernelInfo * nki) {
    struct singleEntry * entry;
    struct singleLine * line;
    int i;

    if (!cfg->defaultImage && cfg->flags == GRUB_CONFIG_NO_DEFAULT) return;

    if (cfg->defaultImage == DEFAULT_SAVED)
	fprintf(out, "%sdefault%ssaved\n", indent, separator);
    else if (cfg->defaultImage > -1) {
	if (cfg->cfi->defaultIsIndex) {
	    fprintf(out, "%sdefault%s%d\n", indent, separator, 
		    cfg->defaultImage);
	} else if (nki && cfg->defaultImage == 0) {
	    fprintf(out, "%sdefault%s%s\n", indent, separator, 
		    nki->title);
	} else {
	    int image = cfg->defaultImage;

	    if (nki) image--;

	    entry = cfg->entries;
	    while (entry && entry->skip) entry = entry->next;

	    i = 0;
	    while (entry && i < image) {
		entry = entry->next;

		while (entry && entry->skip) entry = entry->next;
		i++;
	    }

	    if (!entry) return;

	    line = entry->lines;
	    while (line && line->type != LT_TITLE) line = line->next;

	    if (line && line->numElements >= 2)
		fprintf(out, "%sdefault%s%s\n", indent, separator, 
			line->elements[1].item);
	}
    }
}

static int writeConfig(struct grubConfig * cfg, const char * outName, 
		       struct newKernelInfo * nki, const char * prefix) {
    FILE * out;
    struct singleLine * line;
    struct singleEntry * entry;
    char * tmpOutName;
    int needs = MAIN_DEFAULT;
    struct stat sb;
    int i;

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

	if (!stat(outName, &sb)) {
	    if (chmod(tmpOutName, sb.st_mode)) {
		fprintf(stderr, _("grubby: error setting perms on %s: %s\n"),
		        tmpOutName, strerror(errno));
		fclose(out);
		unlink(tmpOutName);
	    }
	}
    }

    line = cfg->theLines;
    while (line) {
	if (line->type == LT_DEFAULT) {
	    writeDefault(out, line->indent, line->elements[0].indent, cfg,
			 nki);
	    needs &= ~MAIN_DEFAULT;
	} else if (line->type == LT_FALLBACK) {
	    if (cfg->fallbackImage > -1)
		fprintf(out, "%s%s%s%d\n", line->indent, 
			line->elements[0].item, line->elements[0].indent,
			cfg->fallbackImage);
	} else {
	    lineWrite(out, line);
	}

	line = line->next;
    }

    if (needs & MAIN_DEFAULT) {
	writeDefault(out, cfg->primaryIndent, "=", cfg, nki);
	needs &= ~MAIN_DEFAULT;
    }

    if (nki) {
	writeNewKernel(out, cfg, nki, prefix);
	nki = NULL;
    }

    i = 0;
    while ((entry = findEntryByIndex(cfg, i++))) {
	if (entry->skip) continue;

	line = entry->lines;
	while (line) {
	    lineWrite(out, line);
	    line = line->next;
	}
    }

    if (tmpOutName) {
	if (rename(tmpOutName, outName)) {
	    fprintf(stderr, _("grubby: error moving %s to %s: %s\n"),
		    tmpOutName, outName, strerror(errno));
	    unlink(outName);
	}
    }

    return 0;
}

int suitableImage(struct singleEntry * entry, const char * bootPrefix,
		  int skipRemoved, int flags) {
    struct singleLine * line;
    char * fullName;
    int i;
    struct stat sb, sb2;
    char * dev;
    char * end;

    line = entry->lines;

    while (line && line->type != LT_KERNEL) line = line->next;

    if (!line) return 0;
    if (skipRemoved && entry->skip) return 0;
    if (line->numElements < 2) return 0;

    if (flags & GRUBBY_BADIMAGE_OKAY) return 1;

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

struct singleEntry * findEntryByIndex(struct grubConfig * cfg, int index) {
    struct singleEntry * entry;

    entry = cfg->entries;
    while (index && entry) {
	entry = entry->next;
	index--;
    }

    return entry;
}

/* Find a good template to use for the new kernel. An entry is
 * good if the kernel and mkinitrd exist (even if the entry
 * is going to be removed). Try and use the default entry, but
 * if that doesn't work just take the first. If we can't find one,
 * bail. */
struct singleEntry * findTemplate(struct grubConfig * cfg, const char * prefix,
				 int * indexPtr, int skipRemoved, int flags) {
    struct singleEntry * entry;
    int index;

    if (cfg->defaultImage > -1) {
	entry = findEntryByIndex(cfg, cfg->defaultImage);
	if (entry && suitableImage(entry, prefix, skipRemoved, flags)) {
	    if (indexPtr) *indexPtr = cfg->defaultImage;
	    return entry;
	}
    }

    index = 0;
    while ((entry = findEntryByIndex(cfg, index))) {
	if (suitableImage(entry, prefix, skipRemoved, flags)) {
	    if (indexPtr) *indexPtr = index;
	    return entry;
	}

	index++;
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
    struct singleLine * line;
    struct singleEntry * entry;
    int i;

    if (!image) return;

    i = 0;
    while ((entry = findEntryByIndex(cfg, i++))) {
	line = entry->lines;
	while (line && line->type != LT_KERNEL)
	    line = line->next;

	if (!line || line->numElements < 2) continue;

	if (!strcmp(line->elements[1].item, image + strlen(prefix))) 
	    entry->skip = 1;
    }
}

void setDefaultImage(struct grubConfig * config, int hasNew, 
		     const char * defaultKernelPath, int newIsDefault,
		     const char * prefix, int flags) {
    struct singleLine * line;
    struct singleEntry * entry, * entry2, * newDefault;
    int i, j;

    if (newIsDefault) {
	config->defaultImage = 0;
	return;
    } else if (defaultKernelPath) {
	i = 0;
	while ((entry = findEntryByIndex(config, i))) {
	    if (entry->skip) {
		i++;
		continue;
	    }

	    line = entry->lines;
	    while (line && line->type != LT_KERNEL) line=line->next;

	    if (!line || line->numElements < 2) {
		config->defaultImage = -1;
		return;
	    }

	    if (!strcmp(line->elements[1].item, defaultKernelPath + 
						strlen(prefix))) {
		config->defaultImage = i;
		break;
	    }

	    i++;
	}

	if (!line) {
	    config->defaultImage = -1;
	    return;
	}
    } 

    /* defaultImage now points to what we'd like to use, but before any order 
       changes */

    if (config->defaultImage == DEFAULT_SAVED) 
      /* default is set to saved, we don't want to change it */
      return;

    if (config->defaultImage > -1) 
	entry = findEntryByIndex(config, config->defaultImage);

    if (entry && !entry->skip) {
	/* we can preserve the default */

	if (hasNew)
	    config->defaultImage++;
	
	/* count the number of entries erased before this one */
	for (j = 0; j < config->defaultImage; j++) {
	    entry2 = findEntryByIndex(config, j);
	    if (entry2->skip) config->defaultImage--;
	}
    } else if (hasNew) {
	config->defaultImage = 0;
    } else {
	/* Either we just erased the default (or the default line was bad
	 * to begin with) and didn't put a new one in. We'll use the first
	 * valid image. */
	newDefault = findTemplate(config, prefix, &config->defaultImage, 1,
				  flags);
	if (!newDefault)
	    config->defaultImage = -1;
    }
}

void setFallbackImage(struct grubConfig * config, int hasNew) {
    struct singleEntry * entry, * entry2;
    int j;

    if (config->fallbackImage == -1) return;

    entry = findEntryByIndex(config, config->fallbackImage);
    if (!entry || entry->skip) {
	config->fallbackImage = -1;
	return;
    }

    if (hasNew)
	config->fallbackImage++;
    
    /* count the number of entries erased before this one */
    for (j = 0; j < config->fallbackImage; j++) {
	entry2 = findEntryByIndex(config, j);
	if (entry2->skip) config->fallbackImage--;
    }
}

int displayInfo(struct grubConfig * config, char * kernel,
		const char * prefix) {
    int i = 0;
    struct singleEntry * entry;
    struct singleLine * line;
    char * root = NULL;

    while ((entry = findEntryByIndex(config, i))) {
	/* entries can't be removed in this mode; don't need to check
	   entry->skip */

	line = entry->lines;
	while (line && line->type != LT_KERNEL) line=line->next;

	if (!line || line->numElements < 2) {
	    return 1;
	}

	if (!strcmp(line->elements[1].item, kernel + strlen(prefix)))
	    break;

	i++;
    }

    if (!entry) return 1;

    if (line->numElements >= 3) {
	printf("args=\"");
	i = 2;
	while (i < line->numElements) {
	    if (!strncmp(line->elements[i].item, "root=", 5)) {
		root = line->elements[i].item + 5;
	    } else {
		printf("%s%s", line->elements[i].item, 
			       line->elements[i].indent);
	    }

	    i++;
	}
	printf("\"\n");
    } else {
	line = entry->lines;
	while (line && line->type != LT_KERNELARGS) line=line->next;
	
	if (line) {
	    char * s;

	    printf("args=\"");
	    i = 1;
	    while (i < line->numElements) {
		if (!strncmp(line->elements[i].item, "root=", 5)) {
		    root = line->elements[i].item + 5;
		} else {
		    s = line->elements[i].item;
		    if (*s == '"') s++;

		    printf("%s%s", s, line->elements[i].indent);
		}

		i++;
	    }

	    s = line->elements[i - 1].indent;
	    if (s[strlen(s) - 1] != '"') printf("\"");
	    printf("\n");
	}
    }

    if (!root) {
	line = entry->lines;
	while (line && line->type != LT_ROOT) line=line->next;

	if (line && line->numElements >= 2)
	    root=line->elements[1].item;
    }

    if (root) {
	char * s = alloca(strlen(root) + 1);
	
	strcpy(s, root);
	if (s[strlen(s) - 1] == '"')
	    s[strlen(s) - 1] = '\0';
	/* make sure the root doesn't have a trailing " */
	printf("root=%s\n", s);
    }

    line = entry->lines;
    while (line && line->type != LT_INITRD) line=line->next;

    if (line && line->numElements >= 2) {
	printf("initrd=%s", prefix);
	for (i = 1; i < line->numElements; i++)
	    printf("%s%s", line->elements[i].item, line->elements[i].indent);
	printf("\n");
    }

    return 0;
}

int checkDeviceBootloader(const char * device, const unsigned char * boot) {
    int fd;
    unsigned char bootSect[512];
    int offset;

    fd = open(device, O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, _("grubby: unable to open %s: %s\n"),
		device, strerror(errno));
	return 1;
    }

    if (read(fd, bootSect, 512) != 512) {
	fprintf(stderr, _("grubby: unable to read %s: %s\n"),
		device, strerror(errno));
	return 1;
    }
    close(fd);

    /* first three bytes should match, a jmp short should be in there */
    if (memcmp(boot, bootSect, 3))
	return 0;

    if (boot[1] == 0xeb) {
	offset = boot[2] + 2;
    } else if (boot[1] == 0xe8 || boot[1] == 0xe9) {
	offset = (boot[3] << 8) + boot[2] + 2;
    } else if (boot[0] == 0xeb) {
	offset = boot[1] + 2;
    } else if (boot[0] == 0xe8 || boot[0] == 0xe9) {
	offset = (boot[2] << 8) + boot[1] + 2;
    } else {
	return 0;
    }

    if (memcmp(boot + offset, bootSect + offset, CODE_SEG_SIZE))
	return 0;

    return 2;
}

int checkLiloOnRaid(char * mdDev, const char * boot) {
    FILE * f;
    char line[500];
    int inSection = 0;
    char * end;
    char * chptr;
    int rc;

    /* it's on raid; we need to parse /etc/raidtab and check all of the
       *raw* devices listed in there */
    if (!(f = fopen("/etc/raidtab", "r"))) {
	fprintf(stderr, _("grubby: failed to open /etc/raidtab: %s\n"),
		strerror(errno));
	return 2;
    }

    while (fgets(line, sizeof(line), f)) {
	chptr = line;
	while (*chptr && isspace(*chptr)) chptr++;
	if (!*chptr) continue;

	if (!strncmp(chptr, "raiddev", 7) && isspace(*(chptr + 7))) {
	    /* we're done! */
	    if (inSection) break;

	    chptr += 7;
	    while (*chptr && isspace(*chptr)) chptr++;
	    if (!*chptr) continue;

	    end = chptr;
	    while (*end != '\n' && *end) end++;
	    *end = '\0';

	    if (!strcmp(mdDev, chptr))
		inSection = 1;
	} else if (inSection && !strncmp(chptr, "device", 6) && 
		   isspace(*(chptr + 6))) {
	    chptr += 6;

	    while (*chptr && isspace(*chptr)) chptr++;
	    if (!*chptr || *chptr != '/') continue;

	    end = chptr;
	    while (*end != '\n' && *end) end++;
	    end--;
	    while (isdigit(*end)) end--;
	    end++;
	    *end = '\0';

	    rc = checkDeviceBootloader(chptr, boot);
	    if (rc != 2) {
		fclose(f);
		return rc;
	    }
        }
    }

    fclose(f);

    return 2;
}

int checkForLilo(struct grubConfig * config) {
    int fd;
    unsigned char boot[512];
    struct singleLine * line;

    for (line = config->theLines; line; line = line->next)
	if (line->type == LT_BOOT) break;

    if (!line) { 
	fprintf(stderr, 
		_("grubby: no boot line found in lilo configuration\n"));
	return 1;
    }

    if (line->numElements != 2) return 1;

    fd = open("/boot/boot.b", O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, _("grubby: unable to open %s: %s\n"),
		"/boot/boot.b", strerror(errno));
	return 1;
    }

    if (read(fd, boot, 512) != 512) {
	fprintf(stderr, _("grubby: unable to read %s: %s\n"),
		"/boot/boot.b", strerror(errno));
	return 1;
    }
    close(fd);

    if (!strncmp("/dev/md", line->elements[1].item, 7))
	return checkDeviceBootloader(line->elements[1].item, boot);

    return checkDeviceBootloader(line->elements[1].item, boot);
}

int checkForGrub(struct grubConfig * config) {
    int fd;
    unsigned char boot[512];
    struct singleLine * line;

    for (line = config->theLines; line; line = line->next)
	if (line->type == LT_BOOT) break;

    if (!line) { 
	fprintf(stderr, 
		_("grubby: no boot line found in grub configuration\n"));
	return 1;
    }

    if (line->numElements != 2) return 1;

    fd = open("/boot/grub/stage1", O_RDONLY);
    if (fd < 0) {
	/* this doesn't exist if grub hasn't been installed */
	return 0;
    }

    if (read(fd, boot, 512) != 512) {
	fprintf(stderr, _("grubby: unable to read %s: %s\n"),
		"/boot/grub/stage1", strerror(errno));
	return 1;
    }
    close(fd);

    return checkDeviceBootloader(line->elements[1].item, boot);
}

int main(int argc, const char ** argv) {
    poptContext optCon;
    char * grubConfig = NULL;
    char * outputFile = NULL;
    int arg;
    int flags = 0;
    int badImageOkay = 0;
    int configureLilo = 0;
    int configureGrub = 0;
    int bootloaderProbe = 0;
    char * newKernelPath = NULL;
    char * oldKernelPath = NULL;
    char * newKernelArgs = NULL;
    char * newKernelInitrd = NULL;
    char * newKernelTitle = NULL;
    char * newKernelVersion = NULL;
    char * bootPrefix = NULL;
    char * defaultKernel = NULL;
    char * kernelInfo = NULL;
#ifdef __ia64__
    struct configFileInfo * cfi = &liloConfigType;
#else
    struct configFileInfo * cfi = &grubConfigType;
#endif
    struct grubConfig * config;
    struct newKernelInfo newKernel;
    struct singleEntry * template = NULL;
    int copyDefault = 0, makeDefault = 0;
    int displayDefault = 0;
    struct poptOption options[] = {
	{ "add-kernel", 0, POPT_ARG_STRING, &newKernelPath, 0,
	    _("add an entry for the specified kernel"), _("kernel-path") },
	{ "args", 0, POPT_ARG_STRING, &newKernelArgs, 0, _("default arguments for the new kernel"), _("args") },
	{ "bad-image-okay", 0, 0, &badImageOkay, 0,
	    _("don't sanity check images in boot entries (for testing only)"), 
	    NULL },
	{ "boot-filesystem", 0, POPT_ARG_STRING, &bootPrefix, 0,
	    _("filestystem which contains /boot directory (for testing only)"),
	    _("bootfs") },
#ifdef __i386__
	{ "bootloader-probe", 0, POPT_ARG_NONE, &bootloaderProbe, 0,
	    _("check if lilo is installed on lilo.conf boot sector") },
#endif
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
	{ "grub", 0, POPT_ARG_NONE, &configureGrub, 0,
	    _("configure grub instead of lilo") },
	{ "info", 0, POPT_ARG_STRING, &kernelInfo, 0,
	    _("display boot information for specified kernel"),
	    _("kernel-path") },
	{ "initrd", 0, POPT_ARG_STRING, &newKernelInitrd, 0,
	    _("initrd image for the new kernel"), _("initrd-path") },
	{ "lilo", 0, POPT_ARG_NONE, &configureLilo, 0,
	    _("configure lilo instead of grub") },
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
	      "the default"), _("kernel-path") },
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

    if (configureLilo && configureGrub) {
	fprintf(stderr, _("grubby: cannot specify --grub and --lilo\n"));
	return 1;
    } else if (bootloaderProbe && grubConfig) {
	fprintf(stderr, 
	    _("grubby: cannot specify config file with --bootloader-probe\n"));
	return 1;
    } else if (configureLilo) {
	cfi = &liloConfigType;
    } else if (configureGrub) {
	cfi = &grubConfigType;
    }

    if (!grubConfig) 
	grubConfig = cfi->defaultConfig;

    if (bootloaderProbe && (displayDefault || kernelInfo || newKernelVersion ||
			  newKernelPath || oldKernelPath || makeDefault ||
			  defaultKernel)) {
	fprintf(stderr, _("grubby: --bootloader-probe may not be used with "
			  "specified option"));
	return 1;
    }

    if ((displayDefault || kernelInfo) && (newKernelVersion || newKernelPath ||
			   oldKernelPath)) {
	fprintf(stderr, _("grubby: --default-kernel and --info may not "
			  "be used when adding or removing kernels\n"));
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

    if (!oldKernelPath && !newKernelPath && !displayDefault && !defaultKernel
	&& !kernelInfo && !bootloaderProbe) {
	fprintf(stderr, _("grubby: no action specified\n"));
	return 1;
    }

    flags |= badImageOkay ? GRUBBY_BADIMAGE_OKAY : 0;

    if (cfi->needsBootPrefix) {
	if (!bootPrefix) {
	    bootPrefix = findBootPrefix();
	    if (!bootPrefix) return 1;
	} else {
	    /* this shouldn't end with a / */
	    if (bootPrefix[strlen(bootPrefix) - 1] == '/')
		bootPrefix[strlen(bootPrefix) - 1] = '\0';
	}
    } else {
	bootPrefix = "";
    }

    if (bootloaderProbe) {
	int lrc = 0, grc = 0;
	struct grubConfig * lconfig, * gconfig;

	if (!access(grubConfigType.defaultConfig, F_OK)) {
	    gconfig = readConfig(grubConfigType.defaultConfig, &grubConfigType);
	    if (!gconfig)
		grc = 1;
	    else
		grc = checkForGrub(gconfig);
	} 

	if (!access(liloConfigType.defaultConfig, F_OK)) {
	    lconfig = readConfig(liloConfigType.defaultConfig, &liloConfigType);
	    if (!lconfig)
		lrc = 1;
	    else
		lrc = checkForLilo(lconfig);
	} 

	if (lrc == 1 || grc == 1) return 1;

	if (lrc == 2) printf("lilo\n");
	if (grc == 2) printf("grub\n");

	return 0;
    }

    config = readConfig(grubConfig, cfi);
    if (!config) return 1;

    if (displayDefault) {
	struct singleLine * line;
	struct singleEntry * entry;

	if (config->defaultImage == -1) return 0;
	entry = findEntryByIndex(config, config->defaultImage);
	if (!entry) return 0;
	if (!suitableImage(entry, bootPrefix, 0, flags)) return 0;

	line = entry->lines;
	while (line && line->type != LT_KERNEL) line = line->next;
	if (!line) return 0;

	printf("%s%s\n", bootPrefix, line->elements[1].item);

	return 0;
    } else if (kernelInfo)
	return displayInfo(config, kernelInfo, bootPrefix);

    if (copyDefault) {
	template = findTemplate(config, bootPrefix, NULL, 0, flags);
	if (!template) return 1;
    }

    markRemovedImage(config, oldKernelPath, bootPrefix);
    setDefaultImage(config, newKernelPath != NULL, defaultKernel, makeDefault, 
		    bootPrefix, flags);
    setFallbackImage(config, newKernelPath != NULL);

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
