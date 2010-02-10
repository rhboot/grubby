/*
 * grubby.c
 *
 * Copyright (C) 2001-2008 Red Hat, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <popt.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <execinfo.h>
#include <signal.h>
#include <blkid/blkid.h>

#define DEBUG 0

#if DEBUG
#define dbgPrintf(format, args...) fprintf(stderr, format , ## args)
#else
#define dbgPrintf(format, args...)
#endif

#define _(A) (A)

#define MAX_EXTRA_INITRDS	  16	/* code segment checked by --bootloader-probe */
#define CODE_SEG_SIZE	  128	/* code segment checked by --bootloader-probe */

/* comments get lumped in with indention */
struct lineElement {
    char * item;
    char * indent;
};

enum lineType_e { 
    LT_WHITESPACE = 1 << 0,
    LT_TITLE      = 1 << 1,
    LT_KERNEL     = 1 << 2,
    LT_INITRD     = 1 << 3,
    LT_HYPER      = 1 << 4,
    LT_DEFAULT    = 1 << 5,
    LT_MBMODULE   = 1 << 6,
    LT_ROOT       = 1 << 7,
    LT_FALLBACK   = 1 << 8,
    LT_KERNELARGS = 1 << 9,
    LT_BOOT       = 1 << 10,
    LT_BOOTROOT   = 1 << 11,
    LT_LBA        = 1 << 12,
    LT_OTHER      = 1 << 13,
    LT_GENERIC    = 1 << 14,
    LT_UNKNOWN    = 1 << 15,
};

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
    int multiboot;
    struct singleEntry * next;
};

#define GRUBBY_BADIMAGE_OKAY	(1 << 0)

#define GRUB_CONFIG_NO_DEFAULT	    (1 << 0)	/* don't write out default=0 */

/* These defines are (only) used in addNewKernel() */
#define NEED_KERNEL  (1 << 0)
#define NEED_INITRD  (1 << 1)
#define NEED_TITLE   (1 << 2)
#define NEED_ARGS    (1 << 3)
#define NEED_MB      (1 << 4)

#define MAIN_DEFAULT	    (1 << 0)
#define DEFAULT_SAVED       -2

struct keywordTypes {
    char * key;
    enum lineType_e type;
    char nextChar;
    char separatorChar;
};

struct configFileInfo {
    char * defaultConfig;
    struct keywordTypes * keywords;
    int defaultIsIndex;
    int defaultSupportSaved;
    enum lineType_e entrySeparator;
    int needsBootPrefix;
    int argsInQuotes;
    int maxTitleLength;
    int titleBracketed;
    int mbHyperFirst;
    int mbInitRdIsModule;
    int mbConcatArgs;
    int mbAllowExtraInitRds;
};

struct keywordTypes grubKeywords[] = {
    { "title",	    LT_TITLE,	    ' ' },
    { "root",	    LT_BOOTROOT,    ' ' },
    { "default",    LT_DEFAULT,	    ' ' },
    { "fallback",   LT_FALLBACK,    ' ' },
    { "kernel",	    LT_KERNEL,	    ' ' },
    { "initrd",	    LT_INITRD,	    ' ',	' ' },
    { "module",     LT_MBMODULE,    ' ' },
    { "kernel",     LT_HYPER,       ' ' },
    { NULL,	    0, 0 },
};

struct configFileInfo grubConfigType = {
    "/etc/grub.conf",			    /* defaultConfig */
    grubKeywords,			    /* keywords */
    1,					    /* defaultIsIndex */
    1,					    /* defaultSupportSaved */
    LT_TITLE,				    /* entrySeparator */
    1,					    /* needsBootPrefix */
    0,					    /* argsInQuotes */
    0,					    /* maxTitleLength */
    0,                                      /* titleBracketed */
    1,                                      /* mbHyperFirst */
    1,                                      /* mbInitRdIsModule */
    0,                                      /* mbConcatArgs */
    1,                                      /* mbAllowExtraInitRds */
};

struct keywordTypes yabootKeywords[] = {
    { "label",	    LT_TITLE,	    '=' },
    { "root",	    LT_ROOT,	    '=' },
    { "default",    LT_DEFAULT,	    '=' },
    { "image",	    LT_KERNEL,	    '=' },
    { "bsd",	    LT_GENERIC,	    '=' },
    { "macos",	    LT_GENERIC,	    '=' },
    { "macosx",	    LT_GENERIC,	    '=' },
    { "magicboot",  LT_GENERIC,	    '=' },
    { "darwin",	    LT_GENERIC,	    '=' },
    { "timeout",    LT_GENERIC,	    '=' },
    { "install",    LT_GENERIC,	    '=' },
    { "fstype",	    LT_GENERIC,	    '=' },
    { "hfstype",    LT_GENERIC,	    '=' },
    { "delay",	    LT_GENERIC,	    '=' },
    { "defaultos",  LT_GENERIC,     '=' },
    { "init-message", LT_GENERIC,   '=' },
    { "enablecdboot", LT_GENERIC,   ' ' },
    { "enableofboot", LT_GENERIC,   ' ' },
    { "enablenetboot", LT_GENERIC,  ' ' },
    { "nonvram",    LT_GENERIC,	    ' ' },
    { "hide",	    LT_GENERIC,	    ' ' },
    { "protect",    LT_GENERIC,	    ' ' },
    { "nobless",    LT_GENERIC,	    ' ' },
    { "nonvram",    LT_GENERIC,	    ' ' },
    { "brokenosx",  LT_GENERIC,	    ' ' },
    { "usemount",   LT_GENERIC,	    ' ' },
    { "mntpoint",   LT_GENERIC,	    '=' },
    { "partition",  LT_GENERIC,	    '=' },
    { "device",	    LT_GENERIC,	    '=' },
    { "fstype",	    LT_GENERIC,	    '=' },
    { "initrd",	    LT_INITRD,	    '=',	';' },
    { "append",	    LT_KERNELARGS,  '=' },
    { "boot",	    LT_BOOT,	    '=' },
    { "lba",	    LT_LBA,	    ' ' },
    { NULL,	    0, 0 },
};

struct keywordTypes liloKeywords[] = {
    { "label",	    LT_TITLE,	    '=' },
    { "root",	    LT_ROOT,	    '=' },
    { "default",    LT_DEFAULT,	    '=' },
    { "image",	    LT_KERNEL,	    '=' },
    { "other",	    LT_OTHER,	    '=' },
    { "initrd",	    LT_INITRD,	    '=' },
    { "append",	    LT_KERNELARGS,  '=' },
    { "boot",	    LT_BOOT,	    '=' },
    { "lba",	    LT_LBA,	    ' ' },
    { NULL,	    0, 0 },
};

struct keywordTypes eliloKeywords[] = {
    { "label",	    LT_TITLE,	    '=' },
    { "root",	    LT_ROOT,	    '=' },
    { "default",    LT_DEFAULT,	    '=' },
    { "image",	    LT_KERNEL,	    '=' },
    { "initrd",	    LT_INITRD,	    '=' },
    { "append",	    LT_KERNELARGS,  '=' },
    { "vmm",	    LT_HYPER,       '=' },
    { NULL,	    0, 0 },
};

struct keywordTypes siloKeywords[] = {
    { "label",	    LT_TITLE,	    '=' },
    { "root",	    LT_ROOT,	    '=' },
    { "default",    LT_DEFAULT,	    '=' },
    { "image",	    LT_KERNEL,	    '=' },
    { "other",	    LT_OTHER,	    '=' },
    { "initrd",	    LT_INITRD,	    '=' },
    { "append",	    LT_KERNELARGS,  '=' },
    { "boot",	    LT_BOOT,	    '=' },
    { NULL,	    0, 0 },
};

struct keywordTypes ziplKeywords[] = {
    { "target",     LT_BOOTROOT,    '=' },
    { "image",      LT_KERNEL,      '=' },
    { "ramdisk",    LT_INITRD,      '=' },
    { "parameters", LT_KERNELARGS,  '=' },
    { "default",    LT_DEFAULT,     '=' },
    { NULL,         0, 0 },
};

struct keywordTypes extlinuxKeywords[] = {
    { "label",	    LT_TITLE,	    ' ' },
    { "root",	    LT_ROOT,	    ' ' },
    { "default",    LT_DEFAULT,	    ' ' },
    { "kernel",	    LT_KERNEL,	    ' ' },
    { "initrd",	    LT_INITRD,      ' ',	',' },
    { "append",	    LT_KERNELARGS,  ' ' },
    { "prompt",     LT_UNKNOWN,     ' ' },
    { NULL,	    0, 0 },
};
int useextlinuxmenu;
struct configFileInfo eliloConfigType = {
    "/boot/efi/EFI/redhat/elilo.conf",	    /* defaultConfig */
    eliloKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_KERNEL,				    /* entrySeparator */
    1,			                    /* needsBootPrefix */
    1,					    /* argsInQuotes */
    0,					    /* maxTitleLength */
    0,                                      /* titleBracketed */
    0,                                      /* mbHyperFirst */
    0,                                      /* mbInitRdIsModule */
    1,                                      /* mbConcatArgs */
    0,                                      /* mbAllowExtraInitRds */
};

struct configFileInfo liloConfigType = {
    "/etc/lilo.conf",			    /* defaultConfig */
    liloKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_KERNEL,				    /* entrySeparator */
    0,					    /* needsBootPrefix */
    1,					    /* argsInQuotes */
    15,					    /* maxTitleLength */
    0,                                      /* titleBracketed */
    0,                                      /* mbHyperFirst */
    0,                                      /* mbInitRdIsModule */
    0,                                      /* mbConcatArgs */
    0,                                      /* mbAllowExtraInitRds */
};

struct configFileInfo yabootConfigType = {
    "/etc/yaboot.conf",			    /* defaultConfig */
    yabootKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_KERNEL,				    /* entrySeparator */
    1,					    /* needsBootPrefix */
    1,					    /* argsInQuotes */
    15,					    /* maxTitleLength */
    0,                                      /* titleBracketed */
    0,                                      /* mbHyperFirst */
    0,                                      /* mbInitRdIsModule */
    0,                                      /* mbConcatArgs */
    1,                                      /* mbAllowExtraInitRds */
};

struct configFileInfo siloConfigType = {
    "/etc/silo.conf",			    /* defaultConfig */
    siloKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_KERNEL,				    /* entrySeparator */
    1,					    /* needsBootPrefix */
    1,					    /* argsInQuotes */
    15,					    /* maxTitleLength */
    0,                                      /* titleBracketed */
    0,                                      /* mbHyperFirst */
    0,                                      /* mbInitRdIsModule */
    0,                                      /* mbConcatArgs */
    0,                                      /* mbAllowExtraInitRds */
};

struct configFileInfo ziplConfigType = {
    "/etc/zipl.conf",			    /* defaultConfig */
    ziplKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_TITLE,				    /* entrySeparator */
    0,					    /* needsBootPrefix */
    1,					    /* argsInQuotes */
    0,					    /* maxTitleLength */
    1,                                      /* titleBracketed */
    0,                                      /* mbHyperFirst */
    0,                                      /* mbInitRdIsModule */
    0,                                      /* mbConcatArgs */
    0,                                      /* mbAllowExtraInitRds */
};

struct configFileInfo extlinuxConfigType = {
    "/boot/extlinux/extlinux.conf",         /* defaultConfig */
    extlinuxKeywords,                       /* keywords */
    0,                                      /* defaultIsIndex */
    0,                                      /* defaultSupportSaved */
    LT_TITLE,                               /* entrySeparator */
    1,                                      /* needsBootPrefix */
    0,                                      /* argsInQuotes */
    255,                                    /* maxTitleLength */
    0,                                      /* titleBracketed */
    0,                                      /* mbHyperFirst */
    0,                                      /* mbInitRdIsModule */
    0,                                      /* mbConcatArgs */
    1,                                      /* mbAllowExtraInitRds */
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

blkid_cache blkid;

struct singleEntry * findEntryByIndex(struct grubConfig * cfg, int index);
struct singleEntry * findEntryByPath(struct grubConfig * cfg, 
				     const char * path, const char * prefix,
				     int * index);
static int readFile(int fd, char ** bufPtr);
static void lineInit(struct singleLine * line);
struct singleLine * lineDup(struct singleLine * line);
static void lineFree(struct singleLine * line);
static int lineWrite(FILE * out, struct singleLine * line,
		     struct configFileInfo * cfi);
static int getNextLine(char ** bufPtr, struct singleLine * line,
		       struct configFileInfo * cfi);
static char * getRootSpecifier(char * str);
static void insertElement(struct singleLine * line,
			  const char * item, int insertHere,
			  struct configFileInfo * cfi);
static void removeElement(struct singleLine * line, int removeHere);
static struct keywordTypes * getKeywordByType(enum lineType_e type,
					      struct configFileInfo * cfi);
static enum lineType_e getTypeByKeyword(char * keyword, 
					struct configFileInfo * cfi);
static struct singleLine * getLineByType(enum lineType_e type,
					 struct singleLine * line);
static int checkForExtLinux(struct grubConfig * config);
struct singleLine * addLineTmpl(struct singleEntry * entry,
                                struct singleLine * tmplLine,
                                struct singleLine * prevLine,
                                const char * val,
				struct configFileInfo * cfi);
struct singleLine *  addLine(struct singleEntry * entry,
                             struct configFileInfo * cfi,
                             enum lineType_e type, char * defaultIndent,
                             const char * val);

static char * sdupprintf(const char *format, ...)
#ifdef __GNUC__
        __attribute__ ((format (printf, 1, 2)));
#else
        ;
#endif

static char * sdupprintf(const char *format, ...) {
    char *buf = NULL;
    char c;
    va_list args;
    size_t size = 0;
    va_start(args, format);
    
    /* XXX requires C99 vsnprintf behavior */
    size = vsnprintf(&c, 1, format, args) + 1;
    if (size == -1) {
	printf("ERROR: vsnprintf behavior is not C99\n");
	abort();
    }

    va_end(args);
    va_start(args, format);

    buf = malloc(size);
    if (buf == NULL)
	return NULL;
    vsnprintf(buf, size, format, args);
    va_end (args);

    return buf;
}

static struct keywordTypes * getKeywordByType(enum lineType_e type,
					      struct configFileInfo * cfi) {
    struct keywordTypes * kw;
    for (kw = cfi->keywords; kw->key; kw++) {
	if (kw->type == type)
	    return kw;
    }
    return NULL;
}

static char * getpathbyspec(char *device) {
    if (!blkid)
        blkid_get_cache(&blkid, NULL);

    return blkid_get_devname(blkid, device, NULL);
}

static char * getuuidbydev(char *device) {
    if (!blkid)
	blkid_get_cache(&blkid, NULL);

    return blkid_get_tag_value(blkid, "UUID", device);
}

static enum lineType_e getTypeByKeyword(char * keyword, 
					struct configFileInfo * cfi) {
    struct keywordTypes * kw;
    for (kw = cfi->keywords; kw->key; kw++) {
	if (!strcmp(keyword, kw->key))
	    return kw->type;
    }
    return LT_UNKNOWN;
}

static struct singleLine * getLineByType(enum lineType_e type,
					 struct singleLine * line) {
    dbgPrintf("getLineByType(%d): ", type);
    for (; line; line = line->next) {
	dbgPrintf("%d:%s ", line->type, 
		  line->numElements ? line->elements[0].item : "(empty)");
	if (line->type & type) break;
    }
    dbgPrintf(line ? "\n" : " (failed)\n");
    return line;
}

static int isBracketedTitle(struct singleLine * line) {
    if (line->numElements == 1 && *line->elements[0].item == '[') {
        int len = strlen(line->elements[0].item);
        if (*(line->elements[0].item + len - 1) == ']') {
            /* FIXME: this is a hack... */
            if (strcmp(line->elements[0].item, "[defaultboot]")) {
                return 1;
            }
        }
    }
    return 0;
}

static int isEntrySeparator(struct singleLine * line,
                            struct configFileInfo * cfi) {
    return line->type == cfi->entrySeparator || line->type == LT_OTHER ||
	(cfi->titleBracketed && isBracketedTitle(line));
}

/* extract the title from within brackets (for zipl) */
static char * extractTitle(struct singleLine * line) {
    /* bracketed title... let's extract it (leaks a byte) */
    char * title;
    title = strdup(line->elements[0].item);
    title++;
    *(title + strlen(title) - 1) = '\0';
    return title;
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
        free(buf);
	return 1;
    }

    buf = realloc(buf, size + 2);
    if (size == 0)
        buf[size++] = '\n';
    else 
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

struct singleLine * lineDup(struct singleLine * line) {
    int i;
    struct singleLine * newLine = malloc(sizeof(*newLine));

    newLine->indent = strdup(line->indent);
    newLine->next = NULL;
    newLine->type = line->type;
    newLine->numElements = line->numElements;
    newLine->elements = malloc(sizeof(*newLine->elements) * 
			       newLine->numElements);

    for (i = 0; i < newLine->numElements; i++) {
	newLine->elements[i].indent = strdup(line->elements[i].indent);
	newLine->elements[i].item = strdup(line->elements[i].item);
    }

    return newLine;
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

static int lineWrite(FILE * out, struct singleLine * line,
		     struct configFileInfo * cfi) {
    int i;

    if (fprintf(out, "%s", line->indent) == -1) return -1;

    for (i = 0; i < line->numElements; i++) {
	if (i == 1 && line->type == LT_KERNELARGS && cfi->argsInQuotes)
	    if (fputc('"', out) == EOF) return -1;

	if (fprintf(out, "%s", line->elements[i].item) == -1) return -1;
	if (i < line->numElements - 1)
	    if (fprintf(out, "%s", line->elements[i].indent) == -1) return -1;
    }

    if (line->type == LT_KERNELARGS && cfi->argsInQuotes)
	if (fputc('"', out) == EOF) return -1;

    if (fprintf(out, "\n") == -1) return -1;

    return 0;
}

/* we've guaranteed that the buffer ends w/ \n\0 */
static int getNextLine(char ** bufPtr, struct singleLine * line,
                       struct configFileInfo * cfi) {
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

        /* lilo actually accepts the pathological case of append = " foo " */
        if (*start == '=')
            chptr = start + 1;
        else 
            chptr = start;

        do {
            for (; *chptr && isspace(*chptr); chptr++);
            if (*chptr == '=')
                chptr = chptr + 1;
        } while (isspace(*chptr));

	element->indent = strndup(start, chptr - start);
	start = chptr;

	line->numElements++;
	first = 0;
    }

    if (!line->numElements)
	line->type = LT_WHITESPACE;
    else {
	line->type = getTypeByKeyword(line->elements[0].item, cfi);
	if (line->type == LT_UNKNOWN) {
            /* zipl does [title] instead of something reasonable like all
             * the other boot loaders.  kind of ugly */
            if (cfi->titleBracketed && isBracketedTitle(line)) {
                line->type = LT_TITLE;
            }

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
	} else {
		struct keywordTypes *kw;

		kw = getKeywordByType(line->type, cfi);

		/* space isn't the only separator, we need to split
		 * elements up more
		 */
		if (!isspace(kw->separatorChar)) {
		    int i;
		    char indent[2] = "";
		    indent[0] = kw->separatorChar;
		    for (i = 1; i < line->numElements; i++) {
			char *p;
			int j;
			int numNewElements;

			numNewElements = 0;
			p = line->elements[i].item;
			while (*p != '\0') {
				if (*p == kw->separatorChar)
					numNewElements++;
				p++;
			}
			if (line->numElements + numNewElements >= elementsAlloced) {
				elementsAlloced += numNewElements + 5;
				line->elements = realloc(line->elements,
					    sizeof(*line->elements) * elementsAlloced);
			}

			for (j = line->numElements; j > i; j--) {
				line->elements[j + numNewElements] = line->elements[j];
			}
			line->numElements += numNewElements;

			p = line->elements[i].item;
			while (*p != '\0') {

				while (*p != kw->separatorChar && *p != '\0') p++;
				if (*p == '\0') {
					break;
				}

				free(line->elements[i].indent);
				line->elements[i].indent = strdup(indent);
				*p++ = '\0';
				i++;
				line->elements[i].item = strdup(p);
				line->elements[i].indent = strdup("");
				p = line->elements[i].item;
			}
		    }
		}
	}
    }

    return 0;
}

static struct grubConfig * readConfig(const char * inName,
				      struct configFileInfo * cfi) {
    int in;
    char * incoming = NULL, * head;
    int rc;
    int sawEntry = 0;
    int movedLine = 0;
    struct grubConfig * cfg;
    struct singleLine * last = NULL, * line, * defaultLine = NULL;
    char * end;
    struct singleEntry * entry = NULL;
    int i, len;
    char * buf;

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
    cfg->fallbackImage = 0;

    /* copy everything we have */
    while (*head) {
	line = malloc(sizeof(*line));
	lineInit(line);

	if (getNextLine(&head, line, cfi)) {
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

	if (isEntrySeparator(line, cfi)) {
	    sawEntry = 1;
	    if (!entry) {
		cfg->entries = malloc(sizeof(*entry));
		entry = cfg->entries;
	    } else {
		entry->next = malloc(sizeof(*entry));
		entry = entry->next;
	    }

	    entry->skip = 0;
            entry->multiboot = 0;
	    entry->lines = NULL;
	    entry->next = NULL;
	}

	if (line->type == LT_DEFAULT && line->numElements == 2) {
	    cfg->flags &= ~GRUB_CONFIG_NO_DEFAULT;
	    defaultLine = line;

        } else if (line->type == LT_KERNEL) {
	    /* if by some freak chance this is multiboot and the "module"
	     * lines came earlier in the template, make sure to use LT_HYPER 
	     * instead of LT_KERNEL now
	     */
	    if (entry->multiboot)
		line->type = LT_HYPER;

        } else if (line->type == LT_MBMODULE) {
	    /* go back and fix the LT_KERNEL line to indicate LT_HYPER
	     * instead, now that we know this is a multiboot entry.
	     * This only applies to grub, but that's the only place we
	     * should find LT_MBMODULE lines anyway.
	     */
	    struct singleLine * l;
	    for (l = entry->lines; l; l = l->next) {
		if (l->type == LT_HYPER)
		    break;
		else if (l->type == LT_KERNEL) {
		    l->type = LT_HYPER;
		    break;
		}
	    }
            entry->multiboot = 1;

	} else if (line->type == LT_HYPER) {
	    entry->multiboot = 1;

	} else if (line->type == LT_FALLBACK && line->numElements == 2) {
	    cfg->fallbackImage = strtol(line->elements[1].item, &end, 10);
	    if (*end) cfg->fallbackImage = -1;

	} else if (line->type == LT_TITLE && line->numElements > 1) {
	    /* make the title a single argument (undoing our parsing) */
	    len = 0;
	    for (i = 1; i < line->numElements; i++) {
		len += strlen(line->elements[i].item);
		len += strlen(line->elements[i].indent);
	    }
	    buf = malloc(len + 1);
	    *buf = '\0';

	    for (i = 1; i < line->numElements; i++) {
		strcat(buf, line->elements[i].item);
		free(line->elements[i].item);

		if ((i + 1) != line->numElements) {
		    strcat(buf, line->elements[i].indent);
		    free(line->elements[i].indent);
		}
	    }

	    line->elements[1].indent = 
		    line->elements[line->numElements - 1].indent;
	    line->elements[1].item = buf;
	    line->numElements = 2;

	} else if (line->type == LT_KERNELARGS && cfi->argsInQuotes) {
	    /* Strip off any " which may be present; they'll be put back
	       on write. This is one of the few (the only?) places that grubby
	       canonicalizes the output */

	    if (line->numElements >= 2) {
		int last, len;

		if (*line->elements[1].item == '"')
		    memmove(line->elements[1].item, line->elements[1].item + 1,
			    strlen(line->elements[1].item + 1) + 1);

		last = line->numElements - 1;
		len = strlen(line->elements[last].item) - 1;
		if (line->elements[last].item[len] == '"')
		    line->elements[last].item[len] = '\0';
	    }
	}

	/* If we find a generic config option which should live at the
	   top of the file, move it there. Old versions of grubby were
	   probably responsible for putting new images in the wrong 
	   place in front of it anyway. */
	if (sawEntry && line->type == LT_GENERIC) {
		struct singleLine **l = &cfg->theLines;
		struct singleLine **last_nonws = &cfg->theLines;
		while (*l) {
			if ((*l)->type != LT_WHITESPACE)
				last_nonws = &((*l)->next);
			l = &((*l)->next);
		}
		line->next = *last_nonws;
		*last_nonws = line;
		movedLine = 1;
		continue; /* without setting 'last' */
	}

	/* If a second line of whitespace happens after a generic option
	   which was moved, drop it. */
	if (movedLine && line->type == LT_WHITESPACE && last->type == LT_WHITESPACE) {
		lineFree(line);
		free(line);
		movedLine = 0;
		continue;
	}
	movedLine = 0;

	if (sawEntry) {
	    if (!entry->lines)
		entry->lines = line;
	    else
		last->next = line;
	    dbgPrintf("readConfig added %d to %p\n", line->type, entry);
	} else {
	    if (!cfg->theLines)
		cfg->theLines = line;
	    else
		last->next = line;
	    dbgPrintf("readConfig added %d to cfg\n", line->type);
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

                if (!cfi->titleBracketed) {
                    if (line && (line->numElements >= 2) && 
                        !strcmp(defaultLine->elements[1].item,
                                line->elements[1].item)) break;
                } else if (line) {
                    if (!strcmp(defaultLine->elements[1].item, 
                                extractTitle(line))) break;
                }
		i++;
		entry = NULL;
	    }

	    if (entry){
	        cfg->defaultImage = i;
	    }else{
	        cfg->defaultImage = -1;
	    }
	}
    } else {
        cfg->defaultImage = 0;
    }

    return cfg;
}

static void writeDefault(FILE * out, char * indent, 
			 char * separator, struct grubConfig * cfg) {
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
	} else {
	    int image = cfg->defaultImage;

	    entry = cfg->entries;
	    while (entry && entry->skip) entry = entry->next;

	    i = 0;
	    while (entry && i < image) {
		entry = entry->next;

		while (entry && entry->skip) entry = entry->next;
		i++;
	    }

	    if (!entry) return;

	    line = getLineByType(LT_TITLE, entry->lines);

	    if (line && line->numElements >= 2)
		fprintf(out, "%sdefault%s%s\n", indent, separator, 
			line->elements[1].item);
            else if (line && (line->numElements == 1) && 
                     cfg->cfi->titleBracketed) {
		fprintf(out, "%sdefault%s%s\n", indent, separator, 
                        extractTitle(line));
            }
	}
    }
}

static int writeConfig(struct grubConfig * cfg, char * outName, 
		       const char * prefix) {
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
	if (!lstat(outName, &sb) && S_ISLNK(sb.st_mode)) {
	    char * buf;
	    int len = 256;
	    int rc;

	    /* most likely the symlink is relative, so change our
	       directory to the dir of the symlink */
            rc = chdir(dirname(strdupa(outName)));
	    do {
		buf = alloca(len + 1);
		rc = readlink(basename(outName), buf, len);
		if (rc == len) len += 256;
	    } while (rc == len);
	    
	    if (rc < 0) {
		fprintf(stderr, _("grubby: error readlink link %s: %s\n"), 
			outName, strerror(errno));
		return 1;
	    }

	    outName = buf;
	    outName[rc] = '\0';
	}

	tmpOutName = alloca(strlen(outName) + 2);
	sprintf(tmpOutName, "%s-", outName);
	out = fopen(tmpOutName, "w");
	if (!out) {
	    fprintf(stderr, _("grubby: error creating %s: %s\n"), tmpOutName, 
		    strerror(errno));
	    return 1;
	}

	if (!stat(outName, &sb)) {
	    if (chmod(tmpOutName, sb.st_mode & ~(S_IFMT))) {
		fprintf(stderr, _("grubby: error setting perms on %s: %s\n"),
		        tmpOutName, strerror(errno));
		fclose(out);
		unlink(tmpOutName);
                return 1;
	    }
	} 
    }

    line = cfg->theLines;
    while (line) {
	if (line->type == LT_DEFAULT) {
	    writeDefault(out, line->indent, line->elements[0].indent, cfg);
	    needs &= ~MAIN_DEFAULT;
	} else if (line->type == LT_FALLBACK) {
	    if (cfg->fallbackImage > -1)
		fprintf(out, "%s%s%s%d\n", line->indent, 
			line->elements[0].item, line->elements[0].indent,
			cfg->fallbackImage);
	} else {
	    if (lineWrite(out, line, cfg->cfi) == -1) {
                fprintf(stderr, _("grubby: error writing %s: %s\n"),
                        tmpOutName, strerror(errno));
                fclose(out);
                unlink(tmpOutName);
                return 1;
            }
	}

	line = line->next;
    }

    if (needs & MAIN_DEFAULT) {
	writeDefault(out, cfg->primaryIndent, "=", cfg);
	needs &= ~MAIN_DEFAULT;
    }

    i = 0;
    while ((entry = findEntryByIndex(cfg, i++))) {
	if (entry->skip) continue;

	line = entry->lines;
	while (line) {
	    if (lineWrite(out, line, cfg->cfi) == -1) {
                fprintf(stderr, _("grubby: error writing %s: %s\n"),
                        tmpOutName, strerror(errno));
                fclose(out);
                unlink(tmpOutName);
                return 1;
            }
	    line = line->next;
	}
    }

    if (tmpOutName) {
	if (rename(tmpOutName, outName)) {
	    fprintf(stderr, _("grubby: error moving %s to %s: %s\n"),
		    tmpOutName, outName, strerror(errno));
	    unlink(outName);
            return 1;
	}
    }

    return 0;
}

static int numEntries(struct grubConfig *cfg) {
    int i = 0;
    struct singleEntry * entry;

    entry = cfg->entries;
    while (entry) {
        if (!entry->skip)
            i++;
        entry = entry->next;
    }
    return i;
}

static char *findDiskForRoot()
{
    int fd;
    char buf[65536];
    char *devname;
    char *chptr;
    int rc;

    if ((fd = open(_PATH_MOUNTED, O_RDONLY)) < 0) {
        fprintf(stderr, "grubby: failed to open %s: %s\n",
                _PATH_MOUNTED, strerror(errno));
        return NULL;
    }

    rc = read(fd, buf, sizeof(buf) - 1);
    if (rc <= 0) {
        fprintf(stderr, "grubby: failed to read %s: %s\n",
                _PATH_MOUNTED, strerror(errno));
        close(fd);
        return NULL;
    }
    close(fd);
    buf[rc] = '\0';
    chptr = buf;

    while (chptr && chptr != buf+rc) {
        devname = chptr;

        /*
         * The first column of a mtab entry is the device, but if the entry is a
         * special device it won't start with /, so move on to the next line.
         */
        if (*devname != '/') {
            chptr = strchr(chptr, '\n');
            if (chptr)
                chptr++;
            continue;
        }

        /* Seek to the next space */
        chptr = strchr(chptr, ' ');
        if (!chptr) {
            fprintf(stderr, "grubby: error parsing %s: %s\n",
                    _PATH_MOUNTED, strerror(errno));
            return NULL;
        }

        /*
         * The second column of a mtab entry is the mount point, we are looking
         * for '/' obviously.
         */
        if (*(++chptr) == '/' && *(++chptr) == ' ') {
            /*
             * Move back 2, which is the first space after the device name, set
             * it to \0 so strdup will just get the devicename.
             */
            chptr -= 2;
            *chptr = '\0';
            return strdup(devname);
        }

        /* Next line */
        chptr = strchr(chptr, '\n');
        if (chptr)
            chptr++;
    }

    return NULL;
}

int suitableImage(struct singleEntry * entry, const char * bootPrefix,
		  int skipRemoved, int flags) {
    struct singleLine * line;
    char * fullName;
    int i;
    char * dev;
    char * rootspec;
    char * rootdev;

    if (skipRemoved && entry->skip) return 0;

    line = getLineByType(LT_KERNEL|LT_HYPER, entry->lines);
    if (!line || line->numElements < 2) return 0;

    if (flags & GRUBBY_BADIMAGE_OKAY) return 1;

    fullName = alloca(strlen(bootPrefix) + 
		      strlen(line->elements[1].item) + 1);
    rootspec = getRootSpecifier(line->elements[1].item);
    sprintf(fullName, "%s%s", bootPrefix, 
            line->elements[1].item + (rootspec ? strlen(rootspec) : 0));
    if (access(fullName, R_OK)) return 0;

    for (i = 2; i < line->numElements; i++) 
	if (!strncasecmp(line->elements[i].item, "root=", 5)) break;
    if (i < line->numElements) {
	dev = line->elements[i].item + 5;
    } else {
	/* look for a lilo style LT_ROOT line */
	line = getLineByType(LT_ROOT, entry->lines);

	if (line && line->numElements >= 2) {
	    dev = line->elements[1].item;
	} else {
	    /* didn't succeed in finding a LT_ROOT, let's try LT_KERNELARGS.
	     * grub+multiboot uses LT_MBMODULE for the args, so check that too.
	     */
	    line = getLineByType(LT_KERNELARGS|LT_MBMODULE, entry->lines);

            /* failed to find one */
            if (!line) return 0;

	    for (i = 1; i < line->numElements; i++) 
	        if (!strncasecmp(line->elements[i].item, "root=", 5)) break;
	    if (i < line->numElements)
	        dev = line->elements[i].item + 5;
	    else {
		/* it failed too...  can't find root= */
	        return 0;
            }
	}
    }

    dev = getpathbyspec(dev);
    if (!dev)
        return 0;

    rootdev = findDiskForRoot();
    if (!rootdev)
	return 0;


    if (strcmp(getuuidbydev(rootdev), getuuidbydev(dev))) {
	free(rootdev);
        return 0;
    }

    free(rootdev);

    return 1;
}

/* returns the first match on or after the one pointed to by index (if index 
   is not NULL) which is not marked as skip */
struct singleEntry * findEntryByPath(struct grubConfig * config, 
				     const char * kernel, const char * prefix,
				     int * index) {
    struct singleEntry * entry = NULL;
    struct singleLine * line;
    int i;
    char * chptr;
    char * rootspec = NULL;
    enum lineType_e checkType = LT_KERNEL;

    if (isdigit(*kernel)) {
	int * indexVars = alloca(sizeof(*indexVars) * strlen(kernel));

	i = 0;
	indexVars[i] = strtol(kernel, &chptr, 10);
	while (*chptr == ',') {
	    i++;
	    kernel = chptr + 1;
	    indexVars[i] = strtol(kernel, &chptr, 10);
	}

	if (*chptr) {
	    /* can't parse it, bail */
	    return NULL;
	}

	indexVars[i + 1] = -1;
	
	i = 0;
	if (index) {
	    while (i < *index) i++;
	    if (indexVars[i] == -1) return NULL;
	}

	entry = findEntryByIndex(config, indexVars[i]);
	if (!entry) return NULL;

	line = getLineByType(LT_KERNEL|LT_HYPER, entry->lines);
	if (!line) return NULL;

	if (index) *index = indexVars[i];
	return entry;
    }
    
    if (!strcmp(kernel, "DEFAULT")) {
	if (index && *index > config->defaultImage) {
	    entry = NULL;
	} else {
	    entry = findEntryByIndex(config, config->defaultImage);
	    if (entry && entry->skip) 
		entry = NULL;
	    else if (index) 
		*index = config->defaultImage;
	}
    } else if (!strcmp(kernel, "ALL")) {
	if (index)
	    i = *index;
	else
	    i = 0;

	while ((entry = findEntryByIndex(config, i))) {
	    if (!entry->skip) break;
	    i++;
	}

	if (entry && index)
	    *index = i;
    } else {
	if (index)
	    i = *index;
	else
	    i = 0;

	if (!strncmp(kernel, "TITLE=", 6)) {
	    prefix = "";
	    checkType = LT_TITLE;
	    kernel += 6;
	}

	for (entry = findEntryByIndex(config, i); entry; entry = entry->next, i++) {
	    if (entry->skip) continue;

	    dbgPrintf("findEntryByPath looking for %d %s in %p\n", checkType, kernel, entry);

	    /* check all the lines matching checkType */
	    for (line = entry->lines; line; line = line->next) {
		line = getLineByType(entry->multiboot && checkType == LT_KERNEL ? 
				     LT_KERNEL|LT_MBMODULE|LT_HYPER : 
				     checkType, line);
		if (!line) break;  /* not found in this entry */

		if (line && line->numElements >= 2) {
		    rootspec = getRootSpecifier(line->elements[1].item);
		    if (!strcmp(line->elements[1].item + 
				((rootspec != NULL) ? strlen(rootspec) : 0),
				kernel + strlen(prefix)))
			break;
		}
	    }

	    /* make sure this entry has a kernel identifier; this skips
	     * non-Linux boot entries (could find netbsd etc, though, which is
	     * unfortunate)
	     */
	    if (line && getLineByType(LT_KERNEL|LT_HYPER, entry->lines))
		break; /* found 'im! */
	}

	if (index) *index = i;
    }

    return entry;
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
    struct singleEntry * entry, * entry2;
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
            int j;
            for (j = 0; j < index; j++) {
                entry2 = findEntryByIndex(cfg, j);
                if (entry2->skip) index--;
            }
	    if (indexPtr) *indexPtr = index;

	    return entry;
	}

	index++;
    }

    fprintf(stderr, _("grubby fatal error: unable to find a suitable template\n"));

    return NULL;
}

char * findBootPrefix(void) {
    struct stat sb, sb2;

    stat("/", &sb);
#ifdef __ia64__
    stat("/boot/efi/EFI/redhat/", &sb2);
#else
    stat("/boot", &sb2);
#endif

    if (sb.st_dev == sb2.st_dev)
	return strdup("");

#ifdef __ia64__
    return strdup("/boot/efi/EFI/redhat/");
#else
    return strdup("/boot");
#endif
}

void markRemovedImage(struct grubConfig * cfg, const char * image, 
		      const char * prefix) {
    struct singleEntry * entry;

    if (!image) return;

    while ((entry = findEntryByPath(cfg, image, prefix, NULL)))
	entry->skip = 1;
}

void setDefaultImage(struct grubConfig * config, int hasNew, 
		     const char * defaultKernelPath, int newIsDefault,
		     const char * prefix, int flags) {
    struct singleEntry * entry, * entry2, * newDefault;
    int i, j;

    if (newIsDefault) {
	config->defaultImage = 0;
	return;
    } else if (defaultKernelPath) {
	i = 0;
	if (findEntryByPath(config, defaultKernelPath, prefix, &i)) {
	    config->defaultImage = i;
	} else {
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
    else
	entry = NULL;

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

void displayEntry(struct singleEntry * entry, const char * prefix, int index) {
    struct singleLine * line;
    char * root = NULL;
    int i;

    printf("index=%d\n", index);

    line = getLineByType(LT_KERNEL|LT_HYPER, entry->lines);
    if (!line) {
        printf("non linux entry\n");
        return;
    }

    printf("kernel=%s\n", line->elements[1].item);

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
	line = getLineByType(LT_KERNELARGS, entry->lines);
	if (line) {
	    char * s;

	    printf("args=\"");
	    i = 1;
	    while (i < line->numElements) {
		if (!strncmp(line->elements[i].item, "root=", 5)) {
		    root = line->elements[i].item + 5;
		} else {
		    s = line->elements[i].item;

		    printf("%s%s", s, line->elements[i].indent);
		}

		i++;
	    }

	    s = line->elements[i - 1].indent;
	    printf("\"\n");
	}
    }

    if (!root) {
	line = getLineByType(LT_ROOT, entry->lines);
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

    line = getLineByType(LT_INITRD, entry->lines);

    if (line && line->numElements >= 2) {
	printf("initrd=%s", prefix);
	for (i = 1; i < line->numElements; i++)
	    printf("%s%s", line->elements[i].item, line->elements[i].indent);
	printf("\n");
    }
}

int parseSysconfigGrub(int * lbaPtr, char ** bootPtr) {
    FILE * in;
    char buf[1024];
    char * chptr;
    char * start;
    char * param;

    in = fopen("/etc/sysconfig/grub", "r");
    if (!in) return 1;

    if (lbaPtr) *lbaPtr = 0;
    if (bootPtr) *bootPtr = NULL;

    while (fgets(buf, sizeof(buf), in)) {
	start = buf;
	while (isspace(*start)) start++;
	if (*start == '#') continue;

	chptr = strchr(start, '=');
	if (!chptr) continue;
	chptr--;
	while (*chptr && isspace(*chptr)) chptr--;
	chptr++;
	*chptr = '\0';

	param = chptr + 1;
	while (*param && isspace(*param)) param++;
	if (*param == '=') {
	    param++;
	    while (*param && isspace(*param)) param++;
	}

	chptr = param;
	while (*chptr && !isspace(*chptr)) chptr++;
	*chptr = '\0';

	if (!strcmp(start, "forcelba") && !strcmp(param, "1") && lbaPtr)
	    *lbaPtr = 1;
	else if (!strcmp(start, "boot") && bootPtr)
	    *bootPtr = strdup(param);
    }

    fclose(in);

    return 0;
}

void dumpSysconfigGrub(void) {
    char * boot;
    int lba;

    if (!parseSysconfigGrub(&lba, &boot)) {
	if (lba) printf("lba\n");
	if (boot) printf("boot=%s\n", boot);
    }
}

int displayInfo(struct grubConfig * config, char * kernel,
		const char * prefix) {
    int i = 0;
    struct singleEntry * entry;
    struct singleLine * line;

    entry = findEntryByPath(config, kernel, prefix, &i);
    if (!entry) {
	fprintf(stderr, _("grubby: kernel not found\n"));
	return 1;
    }

    /* this is a horrible hack to support /etc/sysconfig/grub; there must
       be a better way */
    if (config->cfi == &grubConfigType) {
	dumpSysconfigGrub();
    } else {
	line = getLineByType(LT_BOOT, config->theLines);
	if (line && line->numElements >= 1) {
	    printf("boot=%s\n", line->elements[1].item);
	}

	line = getLineByType(LT_LBA, config->theLines);
	if (line) printf("lba\n");
    }

    displayEntry(entry, prefix, i);

    i++;
    while ((entry = findEntryByPath(config, kernel, prefix, &i))) {
	displayEntry(entry, prefix, i);
	i++;
    }

    return 0;
}

struct singleLine * addLineTmpl(struct singleEntry * entry,
				struct singleLine * tmplLine,
				struct singleLine * prevLine,
				const char * val,
				struct configFileInfo * cfi)
{
    struct singleLine * newLine = lineDup(tmplLine);

    if (val) {
	/* override the inherited value with our own.
	 * This is a little weak because it only applies to elements[1]
	 */
	if (newLine->numElements > 1)
	    removeElement(newLine, 1);
	insertElement(newLine, val, 1, cfi);

	/* but try to keep the rootspec from the template... sigh */
	if (tmplLine->type & (LT_HYPER|LT_KERNEL|LT_MBMODULE|LT_INITRD)) {
	    char * rootspec = getRootSpecifier(tmplLine->elements[1].item);
	    if (rootspec != NULL) {
		free(newLine->elements[1].item);
		newLine->elements[1].item = 
		    sdupprintf("%s%s", rootspec, val);
	    }
	}
    }

    dbgPrintf("addLineTmpl(%s)\n", newLine->numElements ? 
	      newLine->elements[0].item : "");

    if (!entry->lines) {
	/* first one on the list */
	entry->lines = newLine;
    } else if (prevLine) {
	/* add after prevLine */
	newLine->next = prevLine->next;
	prevLine->next = newLine;
    }

    return newLine;
}

/* val may be NULL */
struct singleLine *  addLine(struct singleEntry * entry, 
			     struct configFileInfo * cfi, 
			     enum lineType_e type, char * defaultIndent,
			     const char * val) {
    struct singleLine * line, * prev;
    struct keywordTypes * kw;
    struct singleLine tmpl;

    /* NB: This function shouldn't allocate items on the heap, rather on the
     * stack since it calls addLineTmpl which will make copies.
     */

    if (type == LT_TITLE && cfi->titleBracketed) {
	/* we're doing a bracketed title (zipl) */
	tmpl.type = type;
	tmpl.numElements = 1;
	tmpl.elements = alloca(sizeof(*tmpl.elements));
	tmpl.elements[0].item = alloca(strlen(val)+3);
	sprintf(tmpl.elements[0].item, "[%s]", val);
	tmpl.elements[0].indent = "";
	val = NULL;
    } else {
	kw = getKeywordByType(type, cfi);
	if (!kw) abort();
	tmpl.type = type;
	tmpl.numElements = val ? 2 : 1;
	tmpl.elements = alloca(sizeof(*tmpl.elements) * tmpl.numElements);
	tmpl.elements[0].item = kw->key;
	tmpl.elements[0].indent = alloca(2);
	sprintf(tmpl.elements[0].indent, "%c", kw->nextChar);
	if (val) {
	    tmpl.elements[1].item = (char *)val;
	    tmpl.elements[1].indent = "";
	}
    }

    /* The last non-empty line gives us the indention to us and the line
       to insert after. Note that comments are considered empty lines, which
       may not be ideal? If there are no lines or we are looking at the
       first line, we use defaultIndent (the first line is normally indented
       differently from the rest) */ 
    for (line = entry->lines, prev = NULL; line; line = line->next) {
	if (line->numElements) prev = line;
	/* fall back on the last line if prev isn't otherwise set */
	if (!line->next && !prev) prev = line;
    }

    if (prev == entry->lines)
	tmpl.indent = defaultIndent ?: "";
    else
	tmpl.indent = prev->indent;

    return addLineTmpl(entry, &tmpl, prev, val, cfi);
}

void removeLine(struct singleEntry * entry, struct singleLine * line) {
    struct singleLine * prev;
    int i;

    for (i = 0; i < line->numElements; i++) {
	free(line->elements[i].item);
	free(line->elements[i].indent);
    }
    free(line->elements);
    free(line->indent);

    if (line == entry->lines) {
	entry->lines = line->next;
    } else {
	prev = entry->lines;
	while (prev->next != line) prev = prev->next;
	prev->next = line->next;
    }

    free(line);
}

static void insertElement(struct singleLine * line,
			  const char * item, int insertHere,
			  struct configFileInfo * cfi)
{
    struct keywordTypes * kw;
    char indent[2] = "";

    /* sanity check */
    if (insertHere > line->numElements) {
	dbgPrintf("insertElement() adjusting insertHere from %d to %d\n",
		  insertHere, line->numElements);
	insertHere = line->numElements;
    }

    line->elements = realloc(line->elements, (line->numElements + 1) * 
			     sizeof(*line->elements));
    memmove(&line->elements[insertHere+1], 
	    &line->elements[insertHere], 
	    (line->numElements - insertHere) * 
	    sizeof(*line->elements));
    line->elements[insertHere].item = strdup(item);

    kw = getKeywordByType(line->type, cfi);

    if (line->numElements == 0) {
	indent[0] = '\0';
    } else if (insertHere == 0) {
	indent[0] = kw->nextChar;
    } else if (kw->separatorChar != '\0') {
	indent[0] = kw->separatorChar;
    } else {
	indent[0] = ' ';
    }

    if (insertHere > 0 && line->elements[insertHere-1].indent[0] == '\0') {
	/* move the end-of-line forward */
	line->elements[insertHere].indent = 
	    line->elements[insertHere-1].indent;
	line->elements[insertHere-1].indent = strdup(indent);
    } else {
	line->elements[insertHere].indent = strdup(indent);
    }

    line->numElements++;

    dbgPrintf("insertElement(%s, '%s%s', %d)\n",
	      line->elements[0].item,
	      line->elements[insertHere].item,
	      line->elements[insertHere].indent,
	      insertHere);
}

static void removeElement(struct singleLine * line, int removeHere) {
    int i;

    /* sanity check */
    if (removeHere >= line->numElements) return;

    dbgPrintf("removeElement(%s, %d:%s)\n", line->elements[0].item, 
	      removeHere, line->elements[removeHere].item);

    free(line->elements[removeHere].item);

    if (removeHere > 1) {
	/* previous argument gets this argument's post-indentation */
	free(line->elements[removeHere-1].indent);
	line->elements[removeHere-1].indent =
	    line->elements[removeHere].indent;
    } else {
	free(line->elements[removeHere].indent);
    }

    /* now collapse the array, but don't bother to realloc smaller */
    for (i = removeHere; i < line->numElements - 1; i++)
	line->elements[i] = line->elements[i + 1];

    line->numElements--;
}

int argMatch(const char * one, const char * two) {
    char * first, * second;
    char * chptr;

    first = strcpy(alloca(strlen(one) + 1), one);
    second = strcpy(alloca(strlen(two) + 1), two);

    chptr = strchr(first, '=');
    if (chptr) *chptr = '\0';

    chptr = strchr(second, '=');
    if (chptr) *chptr = '\0';

    return strcmp(first, second);
}

int updateActualImage(struct grubConfig * cfg, const char * image,
                      const char * prefix, const char * addArgs,
                      const char * removeArgs, int multibootArgs) {
    struct singleEntry * entry;
    struct singleLine * line, * rootLine;
    int index = 0;
    int i, k;
    const char ** newArgs, ** oldArgs;
    const char ** arg;
    int useKernelArgs, useRoot;
    int firstElement;
    int *usedElements, *usedArgs;
    int doreplace;

    if (!image) return 0;

    if (!addArgs) {
	newArgs = malloc(sizeof(*newArgs));
	*newArgs = NULL;
    } else {
	if (poptParseArgvString(addArgs, NULL, &newArgs)) {
	    fprintf(stderr, 
		    _("grubby: error separating arguments '%s'\n"), addArgs);
	    return 1;
	}
    }

    if (!removeArgs) {
	oldArgs = malloc(sizeof(*oldArgs));
	*oldArgs = NULL;
    } else {
	if (poptParseArgvString(removeArgs, NULL, &oldArgs)) {
	    fprintf(stderr, 
		    _("grubby: error separating arguments '%s'\n"), removeArgs);
            free(newArgs);
	    return 1;
	}
    }


    useKernelArgs = (getKeywordByType(LT_KERNELARGS, cfg->cfi)
		     && (!multibootArgs || cfg->cfi->mbConcatArgs));

    useRoot = (getKeywordByType(LT_ROOT, cfg->cfi)
	       && !multibootArgs);

    for (k = 0, arg = newArgs; *arg; arg++, k++) ;
    usedArgs = calloc(k, sizeof(*usedArgs));

    for (; (entry = findEntryByPath(cfg, image, prefix, &index)); index++) {

	if (multibootArgs && !entry->multiboot)
	    continue;

	/* Determine where to put the args.  If this config supports
	 * LT_KERNELARGS, use that.  Otherwise use
	 * LT_HYPER/LT_KERNEL/LT_MBMODULE lines.
	 */
	if (useKernelArgs) {
	    line = getLineByType(LT_KERNELARGS, entry->lines);
	    if (!line) {
		/* no LT_KERNELARGS, need to add it */
		line = addLine(entry, cfg->cfi, LT_KERNELARGS, 
			       cfg->secondaryIndent, NULL);
	    }
	    firstElement = 1;

	} else if (multibootArgs) {
	    line = getLineByType(LT_HYPER, entry->lines);
	    if (!line) {
		/* a multiboot entry without LT_HYPER? */
		continue;
	    }
	    firstElement = 2;

	} else {
	    line = getLineByType(LT_KERNEL|LT_MBMODULE, entry->lines);
	    if (!line) {
		/* no LT_KERNEL or LT_MBMODULE in this entry? */
		continue;
	    }
	    firstElement = 2;
	}

	/* handle the elilo case which does:
	 *   append="hypervisor args -- kernel args"
	 */
	if (entry->multiboot && cfg->cfi->mbConcatArgs) {
	    /* this is a multiboot entry, make sure there's
	     * -- on the args line
	     */
	    for (i = firstElement; i < line->numElements; i++) {
		if (!strcmp(line->elements[i].item, "--"))
		    break;
	    }
	    if (i == line->numElements) {
		/* assume all existing args are kernel args,
		 * prepend -- to make it official
		 */
		insertElement(line, "--", firstElement, cfg->cfi);
		i = firstElement;
	    }
	    if (!multibootArgs) {
		/* kernel args start after the -- */
		firstElement = i + 1;
	    }
	} else if (cfg->cfi->mbConcatArgs) {
	    /* this is a non-multiboot entry, remove hyper args */
	    for (i = firstElement; i < line->numElements; i++) {
		if (!strcmp(line->elements[i].item, "--"))
		    break;
	    }
	    if (i < line->numElements) {
		/* remove args up to -- */
		while (strcmp(line->elements[firstElement].item, "--"))
		    removeElement(line, firstElement);
		/* remove -- */
		removeElement(line, firstElement);
	    }
	}

        usedElements = calloc(line->numElements, sizeof(*usedElements));

	for (k = 0, arg = newArgs; *arg; arg++, k++) {
            if (usedArgs[k]) continue;

	    doreplace = 1;
	    for (i = firstElement; i < line->numElements; i++) {
		if (multibootArgs && cfg->cfi->mbConcatArgs && 
		    !strcmp(line->elements[i].item, "--")) 
		{
		    /* reached the end of hyper args, insert here */
		    doreplace = 0;
		    break;  
		}
                if (usedElements[i])
                    continue;
		if (!argMatch(line->elements[i].item, *arg)) {
                    usedElements[i]=1;
                    usedArgs[k]=1;
		    break;
                }
            }

	    if (i < line->numElements && doreplace) {
		/* direct replacement */
		free(line->elements[i].item);
		line->elements[i].item = strdup(*arg);

	    } else if (useRoot && !strncmp(*arg, "root=/dev/", 10)) {
		/* root= replacement */
		rootLine = getLineByType(LT_ROOT, entry->lines);
		if (rootLine) {
		    free(rootLine->elements[1].item);
		    rootLine->elements[1].item = strdup(*arg + 5);
		} else {
		    rootLine = addLine(entry, cfg->cfi, LT_ROOT, 
				       cfg->secondaryIndent, *arg + 5);
		}
	    }

	    else {
		/* insert/append */
		insertElement(line, *arg, i, cfg->cfi);
		usedElements = realloc(usedElements, line->numElements *
				       sizeof(*usedElements));
		memmove(&usedElements[i + 1], &usedElements[i],
			line->numElements - i - 1);
		usedElements[i] = 1;

		/* if we updated a root= here even though there is a
		   LT_ROOT available we need to remove the LT_ROOT entry
		   (this will happen if we switch from a device to a label) */
		if (useRoot && !strncmp(*arg, "root=", 5)) {
		    rootLine = getLineByType(LT_ROOT, entry->lines);
		    if (rootLine)
			removeLine(entry, rootLine);
		}
	    }
	}

        free(usedElements);

	for (arg = oldArgs; *arg; arg++) {
	    for (i = firstElement; i < line->numElements; i++) {
		if (multibootArgs && cfg->cfi->mbConcatArgs && 
		    !strcmp(line->elements[i].item, "--")) 
		    /* reached the end of hyper args, stop here */
		    break;
		if (!argMatch(line->elements[i].item, *arg)) {
		    removeElement(line, i);
		    break;
		}
	    }
	    /* handle removing LT_ROOT line too */
	    if (useRoot && !strncmp(*arg, "root=", 5)) {
		rootLine = getLineByType(LT_ROOT, entry->lines);
		if (rootLine)
		    removeLine(entry, rootLine);
	    }
	}

	if (line->numElements == 1) {
	    /* don't need the line at all (note it has to be a
	       LT_KERNELARGS for this to happen */
	    removeLine(entry, line);
	}
    }

    free(usedArgs);
    free(newArgs);
    free(oldArgs);

    return 0;
}

int updateImage(struct grubConfig * cfg, const char * image,
                const char * prefix, const char * addArgs,
                const char * removeArgs, 
                const char * addMBArgs, const char * removeMBArgs) {
    int rc = 0;

    if (!image) return rc;

    /* update the main args first... */
    if (addArgs || removeArgs)
        rc = updateActualImage(cfg, image, prefix, addArgs, removeArgs, 0);
    if (rc) return rc;

    /* and now any multiboot args */
    if (addMBArgs || removeMBArgs)
        rc = updateActualImage(cfg, image, prefix, addMBArgs, removeMBArgs, 1);
    return rc;
}

int updateInitrd(struct grubConfig * cfg, const char * image,
                 const char * prefix, const char * initrd) {
    struct singleEntry * entry;
    struct singleLine * line, * kernelLine;
    int index = 0;

    if (!image) return 0;

    for (; (entry = findEntryByPath(cfg, image, prefix, &index)); index++) {
        kernelLine = getLineByType(LT_KERNEL, entry->lines);
        if (!kernelLine) continue;

        line = getLineByType(LT_INITRD, entry->lines);
        if (line)
            removeLine(entry, line);
        if (prefix) {
            int prefixLen = strlen(prefix);
            if (!strncmp(initrd, prefix, prefixLen))
                initrd += prefixLen;
        }
        line = addLine(entry, cfg->cfi, LT_INITRD, kernelLine->indent, initrd);
        if (!line) return 1;
        break;
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

int checkLiloOnRaid(char * mdDev, const unsigned char * boot) {
    int fd;
    char buf[65536];
    char * end;
    char * chptr;
    char * chptr2;
    int rc;

    /* it's on raid; we need to parse /proc/mdstat and check all of the
       *raw* devices listed in there */

    if (!strncmp(mdDev, "/dev/", 5))
	mdDev += 5;

    if ((fd = open("/proc/mdstat", O_RDONLY)) < 0) {
	fprintf(stderr, _("grubby: failed to open /proc/mdstat: %s\n"),
		strerror(errno));
	return 2;
    }

    rc = read(fd, buf, sizeof(buf) - 1);
    if (rc < 0 || rc == (sizeof(buf) - 1)) {
	fprintf(stderr, _("grubby: failed to read /proc/mdstat: %s\n"),
		strerror(errno));
	close(fd);
	return 2;
    }
    close(fd);
    buf[rc] = '\0';

    chptr = buf;
    while (*chptr) {
	end = strchr(chptr, '\n');
	if (!end) break;
	*end = '\0';

	if (!strncmp(chptr, mdDev, strlen(mdDev)) && 
	    chptr[strlen(mdDev)] == ' ') {

	    /* found the device */
	    while (*chptr && *chptr != ':') chptr++;
	    chptr++;
	    while (*chptr && isspace(*chptr)) chptr++;

	    /* skip the "active" bit */
	    while (*chptr && !isspace(*chptr)) chptr++;
	    while (*chptr && isspace(*chptr)) chptr++;

	    /* skip the raid level */
	    while (*chptr && !isspace(*chptr)) chptr++;
	    while (*chptr && isspace(*chptr)) chptr++;

	    /* everything else is partition stuff */
	    while (*chptr) {
		chptr2 = chptr;
		while (*chptr2 && *chptr2 != '[') chptr2++;
		if (!*chptr2) break;

		/* yank off the numbers at the end */
		chptr2--;
		while (isdigit(*chptr2) && chptr2 > chptr) chptr2--;
		chptr2++;
		*chptr2 = '\0';

		/* Better, now we need the /dev/ back. We're done with
		 * everything before this point, so we can just put
		 * the /dev/ part there. There will always be room. */
		memcpy(chptr - 5, "/dev/", 5);
		rc = checkDeviceBootloader(chptr - 5, boot);
		if (rc != 2) {
		    return rc;
		}

		chptr = chptr2 + 1;
		/* skip the [11] bit */
		while (*chptr && !isspace(*chptr)) chptr++;
		/* and move to the next one */
		while (*chptr && isspace(*chptr)) chptr++;
	    }

	    /*  we're good to go */
	    return 2;
	}

	chptr = end + 1;
    }

    fprintf(stderr, 
	    _("grubby: raid device /dev/%s not found in /proc/mdstat\n"),
	    mdDev);
    return 0;
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
	return checkLiloOnRaid(line->elements[1].item, boot);

    return checkDeviceBootloader(line->elements[1].item, boot);
}

int checkForGrub(struct grubConfig * config) {
    int fd;
    unsigned char bootSect[512];
    char * boot;

    if (parseSysconfigGrub(NULL, &boot))
	return 0;

    /* assume grub is not installed -- not an error condition */
    if (!boot)
	return 0;

    fd = open("/boot/grub/stage1", O_RDONLY);
    if (fd < 0)
	/* this doesn't exist if grub hasn't been installed */
	return 0;

    if (read(fd, bootSect, 512) != 512) {
	fprintf(stderr, _("grubby: unable to read %s: %s\n"),
		"/boot/grub/stage1", strerror(errno));
 	close(fd);
	return 1;
    }
    close(fd);

    return checkDeviceBootloader(boot, bootSect);
}

int checkForExtLinux(struct grubConfig * config) {
    int fd;
    unsigned char bootSect[512];
    char * boot;
    char executable[] = "/boot/extlinux/extlinux";

    printf("entered: checkForExtLinux()\n");

    if (parseSysconfigGrub(NULL, &boot))
	return 0;

    /* assume grub is not installed -- not an error condition */
    if (!boot)
	return 0;

    fd = open(executable, O_RDONLY);
    if (fd < 0)
	/* this doesn't exist if grub hasn't been installed */
	return 0;

    if (read(fd, bootSect, 512) != 512) {
	fprintf(stderr, _("grubby: unable to read %s: %s\n"),
		executable, strerror(errno));
	return 1;
    }
    close(fd);

    return checkDeviceBootloader(boot, bootSect);
}

static char * getRootSpecifier(char * str) {
    char * idx, * rootspec = NULL;

    if (*str == '(') {
        idx = rootspec = strdup(str);
        while(*idx && (*idx != ')') && (!isspace(*idx))) idx++;
        *(++idx) = '\0';
    }
    return rootspec;
}

static char * getInitrdVal(struct grubConfig * config,
			   const char * prefix, struct singleLine *tmplLine,
			   const char * newKernelInitrd,
			   char ** extraInitrds, int extraInitrdCount)
{
    char *initrdVal, *end;
    int i;
    size_t totalSize;
    size_t prefixLen;
    char separatorChar;

    prefixLen = strlen(prefix);
    totalSize = strlen(newKernelInitrd) - prefixLen + 1 /* \0 */;

    for (i = 0; i < extraInitrdCount; i++) {
	totalSize += sizeof(separatorChar);
	totalSize += strlen(extraInitrds[i]) - prefixLen;
    }

    initrdVal = end = malloc(totalSize);

    end = stpcpy (end, newKernelInitrd + prefixLen);

    separatorChar = getKeywordByType(LT_INITRD, config->cfi)->separatorChar;
    for (i = 0; i < extraInitrdCount; i++) {
	const char *extraInitrd;
	int j;

	extraInitrd = extraInitrds[i] + prefixLen;
	/* Don't add entries that are already there */
	if (tmplLine != NULL) {
	    for (j = 2; j < tmplLine->numElements; j++)
		if (strcmp(extraInitrd, tmplLine->elements[j].item) == 0)
		    break;

	    if (j != tmplLine->numElements)
		continue;
	}

	*end++ = separatorChar;
	end = stpcpy(end, extraInitrd);
    }

    return initrdVal;
}

int addNewKernel(struct grubConfig * config, struct singleEntry * template, 
	         const char * prefix,
		 char * newKernelPath, char * newKernelTitle,
		 char * newKernelArgs, char * newKernelInitrd,
		 char ** extraInitrds, int extraInitrdCount,
                 char * newMBKernel, char * newMBKernelArgs) {
    struct singleEntry * new;
    struct singleLine * newLine = NULL, * tmplLine = NULL, * masterLine = NULL;
    int needs;
    char * chptr;

    if (!newKernelPath) return 0;

    /* if the newKernelTitle is too long silently munge it into something
     * we can live with. truncating is first check, then we'll just mess with
     * it until it looks better */
    if (config->cfi->maxTitleLength && 
	    (strlen(newKernelTitle) > config->cfi->maxTitleLength)) {
	char * buf = alloca(config->cfi->maxTitleLength + 7);
	char * numBuf = alloca(config->cfi->maxTitleLength + 1);
	int i = 1;

	sprintf(buf, "TITLE=%.*s", config->cfi->maxTitleLength, newKernelTitle);
	while (findEntryByPath(config, buf, NULL, NULL)) {
	    sprintf(numBuf, "%d", i++);
	    strcpy(buf + strlen(buf) - strlen(numBuf), numBuf);
	}

	newKernelTitle = buf + 6;
    }

    new = malloc(sizeof(*new));
    new->skip = 0;
    new->multiboot = 0;
    new->next = config->entries;
    new->lines = NULL;
    config->entries = new;

    /* copy/update from the template */
    needs = NEED_KERNEL | NEED_TITLE;
    if (newKernelInitrd)
	needs |= NEED_INITRD;
    if (newMBKernel) {
        needs |= NEED_MB;
        new->multiboot = 1;
    }

    if (template) {
	for (masterLine = template->lines; 
	     masterLine && (tmplLine = lineDup(masterLine)); 
	     lineFree(tmplLine), masterLine = masterLine->next) 
	{
	    dbgPrintf("addNewKernel processing %d\n", tmplLine->type);

	    /* skip comments */
	    chptr = tmplLine->indent;
	    while (*chptr && isspace(*chptr)) chptr++;
	    if (*chptr == '#') continue;

	    if (tmplLine->type == LT_KERNEL && 
		tmplLine->numElements >= 2) {
		if (!template->multiboot && (needs & NEED_MB)) {
		    /* it's not a multiboot template and this is the kernel
		     * line.  Try to be intelligent about inserting the
		     * hypervisor at the same time.
		     */
		    if (config->cfi->mbHyperFirst) {
			/* insert the hypervisor first */
			newLine = addLine(new, config->cfi, LT_HYPER, 
					  tmplLine->indent,
					  newMBKernel + strlen(prefix));
			/* set up for adding the kernel line */
			free(tmplLine->indent);
			tmplLine->indent = strdup(config->secondaryIndent);
			needs &= ~NEED_MB;
		    }
		    if (needs & NEED_KERNEL) {
			/* use addLineTmpl to preserve line elements,
			 * otherwise we could just call addLine.  Unfortunately
			 * this means making some changes to the template
			 * such as the indent change above and the type
			 * change below.
			 */
			struct keywordTypes * mbm_kw = 
			    getKeywordByType(LT_MBMODULE, config->cfi);
			if (mbm_kw) {
			    tmplLine->type = LT_MBMODULE;
			    free(tmplLine->elements[0].item);
			    tmplLine->elements[0].item = strdup(mbm_kw->key);
			}
			newLine = addLineTmpl(new, tmplLine, newLine,
					      newKernelPath + strlen(prefix), config->cfi);
			needs &= ~NEED_KERNEL;
		    }
		    if (needs & NEED_MB) { /* !mbHyperFirst */
			newLine = addLine(new, config->cfi, LT_HYPER, 
					  config->secondaryIndent,
					  newMBKernel + strlen(prefix));
			needs &= ~NEED_MB;
		    }
		} else if (needs & NEED_KERNEL) {
		    newLine = addLineTmpl(new, tmplLine, newLine, 
					  newKernelPath + strlen(prefix), config->cfi);
		    needs &= ~NEED_KERNEL;
		}

	    } else if (tmplLine->type == LT_HYPER && 
		       tmplLine->numElements >= 2) {
		if (needs & NEED_MB) {
		    newLine = addLineTmpl(new, tmplLine, newLine, 
					  newMBKernel + strlen(prefix), config->cfi);
		    needs &= ~NEED_MB;
		}

	    } else if (tmplLine->type == LT_MBMODULE && 
		       tmplLine->numElements >= 2) {
		if (new->multiboot) {
		    if (needs & NEED_KERNEL) {
			newLine = addLineTmpl(new, tmplLine, newLine, 
					      newKernelPath + 
					      strlen(prefix), config->cfi);
			needs &= ~NEED_KERNEL;
		    } else if (config->cfi->mbInitRdIsModule &&
			       (needs & NEED_INITRD)) {
			char *initrdVal;
			initrdVal = getInitrdVal(config, prefix, tmplLine,
						 newKernelInitrd, extraInitrds,
						 extraInitrdCount);
			newLine = addLineTmpl(new, tmplLine, newLine,
					      initrdVal, config->cfi);
			free(initrdVal);
			needs &= ~NEED_INITRD;
		    }
		} else if (needs & NEED_KERNEL) {
		    /* template is multi but new is not, 
		     * insert the kernel in the first module slot
		     */
		    tmplLine->type = LT_KERNEL;
		    free(tmplLine->elements[0].item);
		    tmplLine->elements[0].item = 
			strdup(getKeywordByType(LT_KERNEL, config->cfi)->key);
		    newLine = addLineTmpl(new, tmplLine, newLine, 
					  newKernelPath + strlen(prefix), config->cfi);
		    needs &= ~NEED_KERNEL;
		} else if (needs & NEED_INITRD) {
		    char *initrdVal;
		    /* template is multi but new is not,
		     * insert the initrd in the second module slot
		     */
		    tmplLine->type = LT_INITRD;
		    free(tmplLine->elements[0].item);
		    tmplLine->elements[0].item = 
			strdup(getKeywordByType(LT_INITRD, config->cfi)->key);
		    initrdVal = getInitrdVal(config, prefix, tmplLine, newKernelInitrd, extraInitrds, extraInitrdCount);
		    newLine = addLineTmpl(new, tmplLine, newLine, initrdVal, config->cfi);
		    free(initrdVal);
		    needs &= ~NEED_INITRD;
		}

	    } else if (tmplLine->type == LT_INITRD && 
		       tmplLine->numElements >= 2) {
		if (needs & NEED_INITRD &&
		    new->multiboot && !template->multiboot &&
		    config->cfi->mbInitRdIsModule) {
		    /* make sure we don't insert the module initrd
		     * before the module kernel... if we don't do it here,
		     * it will be inserted following the template.
		     */
		    if (!needs & NEED_KERNEL) {
			char *initrdVal;
	
			initrdVal = getInitrdVal(config, prefix, tmplLine, newKernelInitrd, extraInitrds, extraInitrdCount);
			newLine = addLine(new, config->cfi, LT_MBMODULE,
					  config->secondaryIndent, 
					  initrdVal);
			free(initrdVal);
			needs &= ~NEED_INITRD;
		    }
		} else if (needs & NEED_INITRD) {
		    char *initrdVal;
		    initrdVal = getInitrdVal(config, prefix, tmplLine, newKernelInitrd, extraInitrds, extraInitrdCount);
		    newLine = addLineTmpl(new, tmplLine, newLine, initrdVal, config->cfi);
		    free(initrdVal);
		    needs &= ~NEED_INITRD;
		}

	    } else if (tmplLine->type == LT_TITLE && 
		       (needs & NEED_TITLE)) {
		if (tmplLine->numElements >= 2) {
		    newLine = addLineTmpl(new, tmplLine, newLine, 
					  newKernelTitle, config->cfi);
		    needs &= ~NEED_TITLE;
		} else if (tmplLine->numElements == 1 &&
			   config->cfi->titleBracketed) {
		    /* addLineTmpl doesn't handle titleBracketed */
		    newLine = addLine(new, config->cfi, LT_TITLE,
				      tmplLine->indent, newKernelTitle);
		    needs &= ~NEED_TITLE;
		}

	    } else {
		/* pass through other lines from the template */
		newLine = addLineTmpl(new, tmplLine, newLine, NULL, config->cfi);
	    }
	}

    } else {
	/* don't have a template, so start the entry with the 
	 * appropriate starting line 
	 */
	switch (config->cfi->entrySeparator) {
	    case LT_KERNEL:
		if (new->multiboot && config->cfi->mbHyperFirst) {
		    /* fall through to LT_HYPER */
		} else {
		    newLine = addLine(new, config->cfi, LT_KERNEL,
				      config->primaryIndent,
				      newKernelPath + strlen(prefix));
		    needs &= ~NEED_KERNEL;
		    break;
		}

	    case LT_HYPER:
		newLine = addLine(new, config->cfi, LT_HYPER,
				  config->primaryIndent,
				  newMBKernel + strlen(prefix));
		needs &= ~NEED_MB;
		break;

	    case LT_TITLE:
		if( useextlinuxmenu != 0 ){	// We just need useextlinuxmenu to not be zero (set above)
			char * templabel;
			int x = 0, y = 0;

			templabel = strdup(newKernelTitle);
			while( templabel[x]){
				if( templabel[x] == ' ' ){
					y = x;
					while( templabel[y] ){
						templabel[y] = templabel[y+1];
						y++;
					}
				}
				x++;
			}
			newLine = addLine(new, config->cfi, LT_TITLE,
					  config->primaryIndent, templabel);
			free(templabel);
		}else{
			newLine = addLine(new, config->cfi, LT_TITLE,
					  config->primaryIndent, newKernelTitle);
		}
		needs &= ~NEED_TITLE;
		break;

	    default:
		abort();
	}
    } 

    /* add the remainder of the lines, i.e. those that either
     * weren't present in the template, or in the case of no template,
     * all the lines following the entrySeparator.
     */
    if (needs & NEED_TITLE) {
	newLine = addLine(new, config->cfi, LT_TITLE, 
			  config->secondaryIndent, 
			  newKernelTitle);
	needs &= ~NEED_TITLE;
    }
    if ((needs & NEED_MB) && config->cfi->mbHyperFirst) {
	newLine = addLine(new, config->cfi, LT_HYPER, 
			  config->secondaryIndent, 
			  newMBKernel + strlen(prefix));
	needs &= ~NEED_MB;
    }
    if (needs & NEED_KERNEL) {
	newLine = addLine(new, config->cfi, 
			  (new->multiboot && getKeywordByType(LT_MBMODULE, 
							      config->cfi)) ?
			  LT_MBMODULE : LT_KERNEL, 
			  config->secondaryIndent, 
			  newKernelPath + strlen(prefix));
	needs &= ~NEED_KERNEL;
    }
    if (needs & NEED_MB) {
	newLine = addLine(new, config->cfi, LT_HYPER, 
			  config->secondaryIndent,
			  newMBKernel + strlen(prefix));
	needs &= ~NEED_MB;
    }
    if (needs & NEED_INITRD) {
	char *initrdVal;
	initrdVal = getInitrdVal(config, prefix, NULL, newKernelInitrd, extraInitrds, extraInitrdCount);
	newLine = addLine(new, config->cfi,
			  (new->multiboot && getKeywordByType(LT_MBMODULE,
							      config->cfi)) ?
			  LT_MBMODULE : LT_INITRD, 
			  config->secondaryIndent, 
			  initrdVal);
	free(initrdVal);
	needs &= ~NEED_INITRD;
    }

    if (needs) {
	printf(_("grubby: needs=%d, aborting\n"), needs);
	abort();
    }

    if (updateImage(config, "0", prefix, newKernelArgs, NULL, 
                    newMBKernelArgs, NULL)) return 1;

    return 0;
}

static void traceback(int signum)
{
    void *array[40];
    size_t size;

    signal(SIGSEGV, SIG_DFL);
    memset(array, '\0', sizeof (array));
    size = backtrace(array, 40);

    fprintf(stderr, "grubby recieved SIGSEGV!  Backtrace (%ld):\n",
            (unsigned long)size);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

int main(int argc, const char ** argv) {
    poptContext optCon;
    char * grubConfig = NULL;
    char * outputFile = NULL;
    int arg = 0;
    int flags = 0;
    int badImageOkay = 0;
    int configureLilo = 0, configureELilo = 0, configureGrub = 0;
    int configureYaboot = 0, configureSilo = 0, configureZipl = 0;
    int configureExtLinux = 0;
    int bootloaderProbe = 0;
    int extraInitrdCount = 0;
    char * updateKernelPath = NULL;
    char * newKernelPath = NULL;
    char * removeKernelPath = NULL;
    char * newKernelArgs = NULL;
    char * newKernelInitrd = NULL;
    char * newKernelTitle = NULL;
    char * newKernelVersion = NULL;
    char * newMBKernel = NULL;
    char * newMBKernelArgs = NULL;
    char * removeMBKernelArgs = NULL;
    char * removeMBKernel = NULL;
    char * bootPrefix = NULL;
    char * defaultKernel = NULL;
    char * removeArgs = NULL;
    char * kernelInfo = NULL;
    char * extraInitrds[MAX_EXTRA_INITRDS] = { NULL };
    const char * chptr = NULL;
    struct configFileInfo * cfi = NULL;
    struct grubConfig * config;
    struct singleEntry * template = NULL;
    int copyDefault = 0, makeDefault = 0;
    int displayDefault = 0;
    struct poptOption options[] = {
	{ "add-kernel", 0, POPT_ARG_STRING, &newKernelPath, 0,
	    _("add an entry for the specified kernel"), _("kernel-path") },
	{ "add-multiboot", 0, POPT_ARG_STRING, &newMBKernel, 0,
	    _("add an entry for the specified multiboot kernel"), NULL },
	{ "args", 0, POPT_ARG_STRING, &newKernelArgs, 0, 
	    _("default arguments for the new kernel or new arguments for "
	      "kernel being updated"), _("args") },
	{ "mbargs", 0, POPT_ARG_STRING, &newMBKernelArgs, 0, 
	    _("default arguments for the new multiboot kernel or "
              "new arguments for multiboot kernel being updated"), NULL },
	{ "bad-image-okay", 0, 0, &badImageOkay, 0,
	    _("don't sanity check images in boot entries (for testing only)"), 
	    NULL },
	{ "boot-filesystem", 0, POPT_ARG_STRING, &bootPrefix, 0,
	    _("filestystem which contains /boot directory (for testing only)"),
	    _("bootfs") },
#if defined(__i386__) || defined(__x86_64__)
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
	{ "elilo", 0, POPT_ARG_NONE, &configureELilo, 0,
	    _("configure elilo bootloader") },
	{ "extlinux", 0, POPT_ARG_NONE, &configureExtLinux, 0,
	    _("configure extlinux bootloader (from syslinux)") },
	{ "grub", 0, POPT_ARG_NONE, &configureGrub, 0,
	    _("configure grub bootloader") },
	{ "info", 0, POPT_ARG_STRING, &kernelInfo, 0,
	    _("display boot information for specified kernel"),
	    _("kernel-path") },
	{ "initrd", 0, POPT_ARG_STRING, &newKernelInitrd, 0,
	    _("initrd image for the new kernel"), _("initrd-path") },
	{ "extra-initrd", 'i', POPT_ARG_STRING, NULL, 'i',
	    _("auxilliary initrd image for things other than the new kernel"), _("initrd-path") },
	{ "lilo", 0, POPT_ARG_NONE, &configureLilo, 0,
	    _("configure lilo bootloader") },
	{ "make-default", 0, 0, &makeDefault, 0,
	    _("make the newly added entry the default boot entry"), NULL },
	{ "output-file", 'o', POPT_ARG_STRING, &outputFile, 0,
	    _("path to output updated config file (\"-\" for stdout)"), 
	    _("path") },
	{ "remove-args", 0, POPT_ARG_STRING, &removeArgs, 0,
            _("remove kernel arguments"), NULL },
        { "remove-mbargs", 0, POPT_ARG_STRING, &removeMBKernelArgs, 0,
	    _("remove multiboot kernel arguments"), NULL },
	{ "remove-kernel", 0, POPT_ARG_STRING, &removeKernelPath, 0,
	    _("remove all entries for the specified kernel"), 
	    _("kernel-path") },
	{ "remove-multiboot", 0, POPT_ARG_STRING, &removeMBKernel, 0,
            _("remove all entries for the specified multiboot kernel"), NULL },
	{ "set-default", 0, POPT_ARG_STRING, &defaultKernel, 0,
	    _("make the first entry referencing the specified kernel "
	      "the default"), _("kernel-path") },
	{ "silo", 0, POPT_ARG_NONE, &configureSilo, 0,
	    _("configure silo bootloader") },
	{ "title", 0, POPT_ARG_STRING, &newKernelTitle, 0,
	    _("title to use for the new kernel entry"), _("entry-title") },
	{ "update-kernel", 0, POPT_ARG_STRING, &updateKernelPath, 0,
	    _("updated information for the specified kernel"), 
	    _("kernel-path") },
	{ "version", 'v', 0, NULL, 'v',
	    _("print the version of this program and exit"), NULL },
	{ "yaboot", 0, POPT_ARG_NONE, &configureYaboot, 0,
	    _("configure yaboot bootloader") },
	{ "zipl", 0, POPT_ARG_NONE, &configureZipl, 0,
	    _("configure zipl bootloader") },
	POPT_AUTOHELP
	{ 0, 0, 0, 0, 0 }
    };

    useextlinuxmenu=0;

    signal(SIGSEGV, traceback);

    optCon = poptGetContext("grubby", argc, argv, options, 0);
    poptReadDefaultConfig(optCon, 1);

    while ((arg = poptGetNextOpt(optCon)) >= 0) {
	switch (arg) {
	  case 'v':
	    printf("grubby version %s\n", VERSION);
	    exit(0);
	    break;
	  case 'i':
	    if (extraInitrdCount < MAX_EXTRA_INITRDS) {
	    	extraInitrds[extraInitrdCount++] = strdup(poptGetOptArg(optCon));
	    } else {
		fprintf(stderr, _("grubby: extra initrd maximum is %d\n"), extraInitrdCount);
		return 1;
	    }
	    break;
	}
    }

    if (arg < -1) {
	fprintf(stderr, _("grubby: bad argument %s: %s\n"),
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(arg));
	return 1;
    }

    if ((chptr = poptGetArg(optCon))) {
	fprintf(stderr, _("grubby: unexpected argument %s\n"), chptr);
	return 1;
    }

    if ((configureLilo + configureGrub + configureELilo + 
		configureYaboot + configureSilo + configureZipl +
		configureExtLinux ) > 1) {
	fprintf(stderr, _("grubby: cannot specify multiple bootloaders\n"));
	return 1;
    } else if (bootloaderProbe && grubConfig) {
	fprintf(stderr, 
	    _("grubby: cannot specify config file with --bootloader-probe\n"));
	return 1;
    } else if (configureLilo) {
	cfi = &liloConfigType;
    } else if (configureGrub) {
	cfi = &grubConfigType;
    } else if (configureELilo) {
	cfi = &eliloConfigType;
    } else if (configureYaboot) {
	cfi = &yabootConfigType;
    } else if (configureSilo) {
        cfi = &siloConfigType;
    } else if (configureZipl) {
        cfi = &ziplConfigType;
    } else if (configureExtLinux) {
	cfi = &extlinuxConfigType;
	useextlinuxmenu=1;
    }

    if (!cfi) {
      #ifdef __ia64__
	cfi = &eliloConfigType;
      #elif __powerpc__
	cfi = &yabootConfigType;
      #elif __sparc__
        cfi = &siloConfigType;
      #elif __s390__
        cfi = &ziplConfigType;
      #elif __s390x__
        cfi = &ziplConfigtype;
      #else
	cfi = &grubConfigType;
      #endif
    }

    if (!grubConfig) 
	grubConfig = cfi->defaultConfig;

    if (bootloaderProbe && (displayDefault || kernelInfo || newKernelVersion ||
			  newKernelPath || removeKernelPath || makeDefault ||
			  defaultKernel)) {
	fprintf(stderr, _("grubby: --bootloader-probe may not be used with "
			  "specified option"));
	return 1;
    }

    if ((displayDefault || kernelInfo) && (newKernelVersion || newKernelPath ||
			   removeKernelPath)) {
	fprintf(stderr, _("grubby: --default-kernel and --info may not "
			  "be used when adding or removing kernels\n"));
	return 1;
    }

    if (newKernelPath && !newKernelTitle) {
	fprintf(stderr, _("grubby: kernel title must be specified\n"));
	return 1;
    } else if (!newKernelPath && (newKernelTitle  || copyDefault ||
				  (newKernelInitrd && !updateKernelPath)||
				  makeDefault || extraInitrdCount > 0)) {
	fprintf(stderr, _("grubby: kernel path expected\n"));
	return 1;
    }

    if (newKernelPath && updateKernelPath) {
	fprintf(stderr, _("grubby: --add-kernel and --update-kernel may"
		          "not be used together"));
	return 1;
    }

    if (makeDefault && defaultKernel) {
	fprintf(stderr, _("grubby: --make-default and --default-kernel "
			  "may not be used together\n"));
	return 1;
    } else if (defaultKernel && removeKernelPath &&
		!strcmp(defaultKernel, removeKernelPath)) {
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

    if (!removeKernelPath && !newKernelPath && !displayDefault && !defaultKernel
	&& !kernelInfo && !bootloaderProbe && !updateKernelPath 
        && !removeMBKernel) {
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

    if (!cfi->mbAllowExtraInitRds &&
	extraInitrdCount > 0) {
	fprintf(stderr, _("grubby: %s doesn't allow multiple initrds\n"), cfi->defaultConfig);
	return 1;
    }

    if (bootloaderProbe) {
	int lrc = 0, grc = 0, erc = 0;
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

	if (!access(extlinuxConfigType.defaultConfig, F_OK)) {
	    lconfig = readConfig(extlinuxConfigType.defaultConfig, &extlinuxConfigType);
	    if (!lconfig)
		erc = 1;
	    else
		erc = checkForExtLinux(lconfig);
	} 

	if (lrc == 1 || grc == 1) return 1;

	if (lrc == 2) printf("lilo\n");
	if (grc == 2) printf("grub\n");
	if (erc == 2) printf("extlinux\n");

	return 0;
    }

    config = readConfig(grubConfig, cfi);
    if (!config) return 1;

    if (displayDefault) {
	struct singleLine * line;
	struct singleEntry * entry;
        char * rootspec;

	if (config->defaultImage == -1) return 0;
	entry = findEntryByIndex(config, config->defaultImage);
	if (!entry) return 0;
	if (!suitableImage(entry, bootPrefix, 0, flags)) return 0;

	line = getLineByType(LT_KERNEL|LT_HYPER, entry->lines);
	if (!line) return 0;

        rootspec = getRootSpecifier(line->elements[1].item);
        printf("%s%s\n", bootPrefix, line->elements[1].item + 
               ((rootspec != NULL) ? strlen(rootspec) : 0));

	return 0;
    } else if (kernelInfo)
	return displayInfo(config, kernelInfo, bootPrefix);

    if (copyDefault) {
	template = findTemplate(config, bootPrefix, NULL, 0, flags);
	if (!template) return 1;
    }

    markRemovedImage(config, removeKernelPath, bootPrefix);
    markRemovedImage(config, removeMBKernel, bootPrefix);
    setDefaultImage(config, newKernelPath != NULL, defaultKernel, makeDefault, 
		    bootPrefix, flags);
    setFallbackImage(config, newKernelPath != NULL);
    if (updateImage(config, updateKernelPath, bootPrefix, newKernelArgs,
                    removeArgs, newMBKernelArgs, removeMBKernelArgs)) return 1;
    if (updateKernelPath && newKernelInitrd) {
            if (updateInitrd(config, updateKernelPath, bootPrefix,
                             newKernelInitrd)) return 1;
    }
    if (addNewKernel(config, template, bootPrefix, newKernelPath, 
                     newKernelTitle, newKernelArgs, newKernelInitrd, 
                     extraInitrds, extraInitrdCount,
                     newMBKernel, newMBKernelArgs)) return 1;
    

    if (numEntries(config) == 0) {
        fprintf(stderr, _("grubby: doing this would leave no kernel entries. "
                          "Not writing out new config.\n"));
        return 1;
    }

    if (!outputFile)
	outputFile = grubConfig;

    return writeConfig(config, outputFile, bootPrefix);
}
