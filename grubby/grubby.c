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

#include "mount_by_label.h"

#define _(A) (A)

#define CODE_SEG_SIZE	  128	/* code segment checked by --bootloader-probe */

/* comments get lumped in with indention */
struct lineElement {
    char * item;
    char * indent;
};

enum lineType_e { LT_WHITESPACE, LT_TITLE, LT_KERNEL, LT_INITRD, LT_DEFAULT,
       LT_UNKNOWN, LT_ROOT, LT_FALLBACK, LT_KERNELARGS, LT_BOOT,
       LT_BOOTROOT, LT_LBA};

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
    int argsInQuotes;
};

struct keywordTypes grubKeywords[] = {
    { "title",	    LT_TITLE,	    ' ' },
    { "root",	    LT_BOOTROOT,    ' ' },
    { "default",    LT_DEFAULT,	    ' ' },
    { "fallback",   LT_FALLBACK,    ' ' },
    { "kernel",	    LT_KERNEL,	    ' ' },
    { "initrd",	    LT_INITRD,	    ' ' },
    { NULL,	    0 },
};

struct configFileInfo grubConfigType = {
    "/boot/grub/grub.conf",		    /* defaultConfig */
    grubKeywords,			    /* keywords */
    1,					    /* defaultIsIndex */
    1,					    /* defaultSupportSaved */
    LT_TITLE,				    /* entrySeparator */
    1,					    /* needsBootPrefix */
    0,					    /* argsInQuotes */
};

struct keywordTypes liloKeywords[] = {
    { "label",	    LT_TITLE,	    '=' },
    { "root",	    LT_ROOT,	    '=' },
    { "default",    LT_DEFAULT,	    '=' },
    { "image",	    LT_KERNEL,	    '=' },
    { "other",	    LT_KERNEL,	    '=' },
    { "initrd",	    LT_INITRD,	    '=' },
    { "append",	    LT_KERNELARGS,  '=' },
    { "boot",	    LT_BOOT,	    '=' },
    { "lba",	    LT_LBA,	    ' ' },
    { NULL,	    0 },
};

struct configFileInfo liloConfigType = {
#ifdef __ia64__
    "/boot/efi/redhat/elilo.conf",		    /* defaultConfig */
#else
    "/etc/lilo.conf",			    /* defaultConfig */
#endif
    liloKeywords,			    /* keywords */
    0,					    /* defaultIsIndex */
    0,					    /* defaultSupportSaved */
    LT_KERNEL,				    /* entrySeparator */
#ifdef __ia64__
    1,			
#else
    0,					    /* needsBootPrefix */
#endif
    1,					    /* argsInQuotes */
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
struct singleEntry * findEntryByPath(struct grubConfig * cfg, 
				     const char * path, const char * prefix,
				     int * index);
static char * strndup(char * from, int len);
static int readFile(int fd, char ** bufPtr);
static void lineInit(struct singleLine * line);
static void lineFree(struct singleLine * line);
static int lineWrite(FILE * out, struct singleLine * line,
		     struct configFileInfo * cfi);
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

static int lineWrite(FILE * out, struct singleLine * line,
		     struct configFileInfo * cfi) {
    int i;

    fprintf(out, line->indent);

    for (i = 0; i < line->numElements; i++) {
	if (i == 1 && line->type == LT_KERNELARGS && cfi->argsInQuotes)
	    fputc('"', out);

	fprintf(out, line->elements[i].item);
	fprintf(out, line->elements[i].indent);
    }

    if (line->type == LT_KERNELARGS && cfi->argsInQuotes)
	fputc('"', out);

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
	}

	if (line->type == LT_DEFAULT && line->numElements == 2) {
	    cfg->flags &= ~GRUB_CONFIG_NO_DEFAULT;
	    defaultLine = line;
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
		    memcpy(line->elements[1].item, line->elements[1].item + 1,
			   strlen(line->elements[1].item + 1) + 1);

		last = line->numElements - 1;
		len = strlen(line->elements[last].item) - 1;
		if (line->elements[last].item[len] == '"')
		    line->elements[last].item[len] = '\0';
	    }

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

	    line = entry->lines;
	    while (line && line->type != LT_TITLE) line = line->next;

	    if (line && line->numElements >= 2)
		fprintf(out, "%sdefault%s%s\n", indent, separator, 
			line->elements[1].item);
	}
    }
}

static int writeConfig(struct grubConfig * cfg, const char * outName, 
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
	    writeDefault(out, line->indent, line->elements[0].indent, cfg);
	    needs &= ~MAIN_DEFAULT;
	} else if (line->type == LT_FALLBACK) {
	    if (cfg->fallbackImage > -1)
		fprintf(out, "%s%s%s%d\n", line->indent, 
			line->elements[0].item, line->elements[0].indent,
			cfg->fallbackImage);
	} else {
	    lineWrite(out, line, cfg->cfi);
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
	    lineWrite(out, line, cfg->cfi);
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
    if (i < line->numElements) {
	dev = line->elements[i].item + 5;
    } else {
	/* look for a lilo style LT_ROOT line */
	line = entry->lines;
	while (line && line->type != LT_ROOT) line = line->next;

	if (line && line->numElements >= 2)
	    dev = line->elements[1].item;
	else {
	    /* didn't succeed in finding a LT_ROOT, let's try LT_KERNELARGS */
	    line = entry->lines;

	    while (line && line->type != LT_KERNELARGS) line = line->next;

	    for (i = 1; i < line->numElements; i++) 
	        if (!strncasecmp(line->elements[i].item, "root=", 5)) break;

	    if (i < line->numElements) 
	        dev = line->elements[i].item + 5;
	    else
		/* it failed too...  can't find root= */
	        return 0;
	}
    }

    if (!strncmp(dev, "LABEL=", 6)) {
	dev += 6;
	
	/* check which device has this label */
	dev = get_spec_by_volume_label(dev, &i, &i);
	if (!dev) return 0;
    }

    if (*dev == '/') {
	if (stat(dev, &sb))
	    return 0;
    } else {
	sb.st_rdev = strtol(dev, &end, 16);
	if (*end) return 0;
    }
    stat("/", &sb2);

    if (sb.st_rdev != sb2.st_dev) return 0;

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
	    while (indexVars[i] < *index) i++;
	    if (indexVars[i] == -1) return NULL;
	}

	entry = findEntryByIndex(config, indexVars[i]);
	if (!entry) return NULL;

	line = entry->lines;
	while (line && line->type != LT_KERNEL)
	    line = line->next;

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

	while ((entry = findEntryByIndex(config, i))) {
	    line = entry->lines;
	    while (line && line->type != checkType) line=line->next;

	    if (line && line->numElements >= 2 && !entry->skip &&
	        !strcmp(line->elements[1].item, kernel + strlen(prefix)))
		break;

	    i++;
	}

	if (index) *index = i;
    }

    if (!entry) return NULL;

    /* make sure this entry has a kernel identifier; this skips non-Linux
       boot entries (could find netbsd etc, though, which is unfortunate) */
    line = entry->lines;
    while (line && line->type != LT_KERNEL) line = line->next;
    if (!line) {
	if (!index) index = &i;
	(*index)++;
	return findEntryByPath(config, kernel, prefix, index);
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

    fprintf(stderr, _("grubby fatal error: unable to find a suitable template\n"));

    return NULL;
}

char * findBootPrefix(void) {
    struct stat sb, sb2;

    stat("/", &sb);
#ifdef __ia64__
    stat("/boot/efi/redhat/", &sb2);
#else
    stat("/boot", &sb2);
#endif

    if (sb.st_dev == sb2.st_dev)
	return strdup("");

#ifdef __ia64__
    return strdup("/boot/efi/redhat/");
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

    line = entry->lines;
    while (line && line->type != LT_KERNEL) line = line->next;

    printf("index=%d\n", index);

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

		    printf("%s%s", s, line->elements[i].indent);
		}

		i++;
	    }

	    s = line->elements[i - 1].indent;
	    printf("\"\n");
	}
    }

    if (!root) {
	line = entry->lines;
	while (line && line->type != LT_ROOT) line = line->next;

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
    while (line && line->type != LT_INITRD) line = line->next;

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
	line = config->theLines;
	while (line && line->type != LT_BOOT) line = line->next;
	if (line && line->numElements >= 1) {
	    printf("boot=%s\n", line->elements[1].item);
	}

	line = config->theLines;
	while (line && line->type != LT_LBA) line = line->next;
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

/* val may be NULL */
struct singleLine *  addLine(struct singleEntry * entry, 
			     struct configFileInfo * cfi, 
			     enum lineType_e type, const char * defaultIndent,
			     char * val) {
    struct singleLine * line, * prev;
    int i;

    for (i = 0; cfi->keywords[i].key; i++)
	if (cfi->keywords[i].type == type) break;
    if (!cfi->keywords[i].key) abort();

    /* The last non-empty line gives us the indention to us and the line
       to insert after. Note that comments are considered empty lines, which
       may not be ideal? If there are no lines or we are looking at the
       first line, we use defaultIndent (the first line is normally indented
       differently from the rest) */ 
    if (entry->lines) {
	line = entry->lines;
	prev = NULL;
	while (line) {
	    if (line->numElements) prev = line;
	    line = line->next;
	}
	if (!prev) {
	    /* just use the last line */
	    prev = entry->lines;
	    while (prev->next) prev = prev->next;
	}

	line = prev->next;
	prev->next = malloc(sizeof(*line));
	prev->next->next = line;
	line = prev->next;

	if (prev == entry->lines)
	    line->indent = strdup(defaultIndent);
	else
	    line->indent = strdup(prev->indent);
    } else {
	line = malloc(sizeof(*line));
	line->indent = strdup(defaultIndent);
	line->next = NULL;
    }

    line->type = type;
    line->numElements = val ? 2 : 1;
    line->elements = malloc(sizeof(*line->elements) * line->numElements);
    line->elements[0].item = strdup(cfi->keywords[i].key);
    line->elements[0].indent = malloc(2);
    line->elements[0].indent[0] = cfi->keywords[i].nextChar;
    line->elements[0].indent[1] = '\0';
    
    if (val) {
	line->elements[1].item = val;
	line->elements[1].indent = strdup("");
    }

    return line;
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

int argMatch(const char * one, const char * two) {
    char * first, * second;
    char * chptr;

    first = strcpy(alloca(strlen(one)), one);
    second = strcpy(alloca(strlen(two)), two);

    chptr = strchr(first, '=');
    if (chptr) *chptr = '\0';

    chptr = strchr(second, '=');
    if (chptr) *chptr = '\0';

    return strcmp(first, second);
}

int updateImage(struct grubConfig * cfg, const char * image,
	        const char * prefix, const char * addArgs,
		const char * removeArgs) {
    struct singleEntry * entry;
    struct singleLine * line, * rootLine;
    int index = 0;
    int i, j;
    const char ** newArgs, ** oldArgs;
    const char ** arg;
    const char * chptr;
    int useKernelArgs = 0;
    int useRoot = 0;
    int firstElement;

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
	    return 1;
	}
    }

    for (i = 0; cfg->cfi->keywords[i].key; i++)
	if (cfg->cfi->keywords[i].type == LT_KERNELARGS) break;

    if (cfg->cfi->keywords[i].key)
	useKernelArgs = 1;

    for (i = 0; cfg->cfi->keywords[i].key; i++)
	if (cfg->cfi->keywords[i].type == LT_ROOT) break;

    if (cfg->cfi->keywords[i].key)
	useRoot = 1;

    while ((entry = findEntryByPath(cfg, image, prefix, &index))) {
	index++;

	line = entry->lines;
	while (line && line->type != LT_KERNEL) line = line->next;
	if (!line) continue;
	firstElement = 2;

	if (useKernelArgs) {
	    while (line && line->type != LT_KERNELARGS) line = line->next;
	    firstElement = 1;
	}

	for (arg = newArgs; *arg; arg++) {
	    if (!line) {
		/* no append in there, need to add it */
		line = addLine(entry, cfg->cfi, LT_KERNELARGS, NULL, NULL);
	    }

	    for (i = firstElement; i < line->numElements; i++)
		if (!argMatch(line->elements[i].item, *arg))
		    break;

	    chptr = strchr(*arg, '=');

	    if (i < line->numElements) {
		/* replace */
		free(line->elements[i].item);
		line->elements[i].item = strdup(*arg);
	    } else if (useRoot && !strncmp(*arg, "root=/dev/", 10) && *chptr) {
		rootLine = entry->lines;
		while (rootLine && rootLine->type != LT_ROOT) 
		    rootLine = rootLine->next;
		if (!rootLine) {
		    rootLine = addLine(entry, cfg->cfi, LT_ROOT, NULL, NULL);
		    rootLine->elements = realloc(rootLine->elements,
			    2 * sizeof(*rootLine->elements));
		    rootLine->numElements++;
		    rootLine->elements[1].indent = strdup("");
		    rootLine->elements[1].item = strdup("");
		}

		free(rootLine->elements[1].item);
		rootLine->elements[1].item = strdup(chptr + 1);
	    } else {
		/* append */
		line->elements = realloc(line->elements,
			(line->numElements + 1) * sizeof(*line->elements));
		line->elements[line->numElements].item = strdup(*arg);

		if (line->numElements > 1) {
		    /* add to existing list of arguments */
		    line->elements[line->numElements].indent = 
			line->elements[line->numElements - 1].indent;
		    line->elements[line->numElements - 1].indent = strdup(" ");
		} else {
		    /* First thing on this line; treat a bit differently. Note
		       this is only possible if we've added a LT_KERNELARGS
		       entry */
		    line->elements[line->numElements].indent = strdup("");
		}

		line->numElements++;

		/* if we updated a root= here even though there is a
		   LT_ROOT available we need to remove the LT_ROOT entry
		   (this will happen if we switch from a device to a label) */
		if (useRoot && !strncmp(*arg, "root=", 5)) {
		    rootLine = entry->lines;
		    while (rootLine && rootLine->type != LT_ROOT)
			rootLine = rootLine->next;
		    if (rootLine) {
			removeLine(entry, rootLine);
		    }
		}
	    }
	}

	/* no arguments to remove (i.e. no append line) */
	if (!line) continue;

	/* this won't remove an LT_ROOT item properly (but then again,
	   who cares? */
	for (arg = oldArgs; *arg; arg++) {
	    for (i = firstElement; i < line->numElements; i++)
		if (!argMatch(line->elements[i].item, *arg))
		    break;

	    if (i < line->numElements) {
		/* if this isn't the first argument the previous argument
		   gets this arguments post-indention */
		if (i > firstElement) {
		    free(line->elements[i - 1].indent);
		    line->elements[i - 1].indent = line->elements[i].indent;
		}
		
		free(line->elements[i].item);

		for (j = i + 1; j < line->numElements; j++)
		    line->elements[j - 1] = line->elements[j];

		line->numElements--;
	    }
	}

	if (line->numElements == 1) {
	    /* don't need the line at all (note it has to be a
	       LT_KERNELARGS for this to happen */
	    removeLine(entry, line);
	}
    }

    free(newArgs);
    free(oldArgs);

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
	return 1;
    }
    close(fd);

    return checkDeviceBootloader(boot, bootSect);
}

int addNewKernel(struct grubConfig * config, struct singleEntry * template, 
	         const char * prefix,
		 char * newKernelPath, char * newKernelTitle,
		 char * newKernelArgs, char * newKernelInitrd) {
    struct singleEntry * new;
    struct singleLine * newLine, * tmplLine;
    int needs;
    char * indent = NULL;
    char * chptr;
    int i;
    enum lineType_e type;

    if (!newKernelPath) return 0;

    new = malloc(sizeof(*new));
    new->skip = 0;
    new->next = config->entries;
    new->lines = NULL;
    config->entries = new;

    /* copy/update from the template */
    needs = KERNEL_KERNEL | KERNEL_INITRD | KERNEL_TITLE;

    if (template) {
	for (tmplLine = template->lines; tmplLine; tmplLine = tmplLine->next) {
	    /* remember the indention level; we may need it for new lines */
	    if (tmplLine->numElements)
		indent = tmplLine->indent;

	    /* skip comments */
	    chptr = tmplLine->indent;
	    while (*chptr && isspace(*chptr)) chptr++;
	    if (*chptr == '#') continue;

	    /* we don't need an initrd here */
	    if (tmplLine->type == LT_INITRD && !newKernelInitrd) continue;

	    if (!new->lines) {
		newLine = malloc(sizeof(*newLine));
		new->lines = newLine;
	    } else {
		newLine->next = malloc(sizeof(*newLine));
		newLine = newLine->next;
	    }

	    newLine->indent = strdup(tmplLine->indent);
	    newLine->next = NULL;
	    newLine->type = tmplLine->type;
	    newLine->numElements = tmplLine->numElements;
	    newLine->elements = malloc(sizeof(*newLine->elements) * 
					    newLine->numElements);
	    for (i = 0; i < newLine->numElements; i++) {
		newLine->elements[i].item = strdup(tmplLine->elements[i].item);
		newLine->elements[i].indent = 
				strdup(tmplLine->elements[i].indent);
	    }

	    if (tmplLine->type == LT_KERNEL && tmplLine->numElements >= 2) {
		needs &= ~KERNEL_KERNEL;
		free(newLine->elements[1].item);
		newLine->elements[1].item = strdup(newKernelPath + 
						    strlen(prefix));
	    } else if (tmplLine->type == LT_INITRD && 
			    tmplLine->numElements >= 2) {
		needs &= ~KERNEL_INITRD;
		free(newLine->elements[1].item);
		newLine->elements[1].item = strdup(newKernelInitrd + 
						    strlen(prefix));
	    } else if (tmplLine->type == LT_TITLE && 
			    tmplLine->numElements >= 2) {
		needs &= ~KERNEL_TITLE;

		for (i = 1; i < newLine->numElements; i++) {
		    free(newLine->elements[i].item);
		    free(newLine->elements[i].indent);
		}

		newLine->elements[1].item = strdup(newKernelTitle);
		newLine->elements[1].indent = strdup("");
		newLine->numElements = 2;
	    }
	}
    } else {
	for (i = 0; config->cfi->keywords[i].key; i++)
	    if (config->cfi->keywords[i].type == config->cfi->entrySeparator) 
		break;

	switch (config->cfi->keywords[i].type) {
	    case LT_KERNEL:  needs &= ~KERNEL_KERNEL, 
			     chptr = newKernelPath + strlen(prefix);
			     type = LT_KERNEL; break;
	    case LT_TITLE:   needs &= ~KERNEL_TITLE, chptr = newKernelTitle;
			     type = LT_TITLE; break;
	    default:	    abort();
	}

	newLine = addLine(new, config->cfi, type, config->primaryIndent, chptr);
	new->lines = newLine;
    }

    if (needs & KERNEL_KERNEL)
	newLine = addLine(new, config->cfi, LT_KERNEL, config->secondaryIndent, 
			  newKernelPath + strlen(prefix));
    if (needs & KERNEL_TITLE)
	newLine = addLine(new, config->cfi, LT_TITLE, config->secondaryIndent, 
			  newKernelTitle);
    if (needs & KERNEL_INITRD && newKernelInitrd)
	newLine = addLine(new, config->cfi, LT_INITRD, config->secondaryIndent, 
			  newKernelInitrd + strlen(prefix));

    if (updateImage(config, "0", prefix, newKernelArgs, NULL)) return 1;

    return 0;
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
    char * updateKernelPath = NULL;
    char * newKernelPath = NULL;
    char * removeKernelPath = NULL;
    char * newKernelArgs = NULL;
    char * newKernelInitrd = NULL;
    char * newKernelTitle = NULL;
    char * newKernelVersion = NULL;
    char * bootPrefix = NULL;
    char * defaultKernel = NULL;
    char * removeArgs = NULL;
    char * kernelInfo = NULL;
    const char * chptr;
#ifdef __ia64__
    struct configFileInfo * cfi = &liloConfigType;
#else
    struct configFileInfo * cfi = &grubConfigType;
#endif
    struct grubConfig * config;
    struct singleEntry * template = NULL;
    int copyDefault = 0, makeDefault = 0;
    int displayDefault = 0;
    struct poptOption options[] = {
	{ "add-kernel", 0, POPT_ARG_STRING, &newKernelPath, 0,
	    _("add an entry for the specified kernel"), _("kernel-path") },
	{ "args", 0, POPT_ARG_STRING, &newKernelArgs, 0, 
	    _("default arguments for the new kernel or new arguments for "
	      "kernel being updated"), _("args") },
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
	{ "remove-args", 0, POPT_ARG_STRING, &removeArgs, 0,
	    _("remove kernel arguments"), _("args") },
	{ "remove-kernel", 0, POPT_ARG_STRING, &removeKernelPath, 0,
	    _("remove all entries for the specified kernel"), 
	    _("kernel-path") },
	{ "set-default", 0, POPT_ARG_STRING, &defaultKernel, 0,
	    _("make the first entry referencing the specified kernel "
	      "the default"), _("kernel-path") },
	{ "title", 0, POPT_ARG_STRING, &newKernelTitle, 0,
	    _("title to use for the new kernel entry"), _("entry-title") },
	{ "update-kernel", 0, POPT_ARG_STRING, &updateKernelPath, 0,
	    _("updated information for the specified kernel"), 
	    _("kernel-path") },
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

    if ((chptr = poptGetArg(optCon))) {
	fprintf(stderr, _("grubby: unexpected argument %s\n"), chptr);
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
    } else if (!newKernelPath && (newKernelTitle  || newKernelInitrd ||
				  newKernelInitrd || copyDefault     ||
				  makeDefault)) {
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
	&& !kernelInfo && !bootloaderProbe && !updateKernelPath) {
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

    markRemovedImage(config, removeKernelPath, bootPrefix);
    setDefaultImage(config, newKernelPath != NULL, defaultKernel, makeDefault, 
		    bootPrefix, flags);
    setFallbackImage(config, newKernelPath != NULL);
    if (updateImage(config, updateKernelPath, bootPrefix, newKernelArgs,
		removeArgs)) return 1;
    if (addNewKernel(config, template, bootPrefix, newKernelPath, 
		newKernelTitle, newKernelArgs, newKernelInitrd)) return 1;

    if (!outputFile)
	outputFile = grubConfig;

    return writeConfig(config, outputFile, bootPrefix);
}
