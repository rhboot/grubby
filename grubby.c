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

#include "log.h"

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
#define dbgPrintf(format, args...) fprintf(stderr, format , ## args)
#else
#define dbgPrintf(format, args...)
#endif

int debug = 0;			/* Currently just for template debugging */

#define _(A) (A)

#define MAX_EXTRA_INITRDS	  16	/* code segment checked by --bootloader-probe */
#define CODE_SEG_SIZE	  128	/* code segment checked by --bootloader-probe */

#define NOOP_OPCODE 0x90
#define JMP_SHORT_OPCODE 0xeb
#define MTAB_FILE_BUF_SIZE (1024 * 1024)
#define INTER_RETRY_MAX 20

int isEfi = 0;

#if defined(__aarch64__)
#define isEfiOnly	1
#else
#define isEfiOnly	0
#endif

char *saved_command_line = NULL;

const char *mounts = "/proc/mounts";

/* comments get lumped in with indention */
struct lineElement {
	char *item;
	char *indent;
};

enum lineType_e {
	LT_UNIDENTIFIED = 0,
	LT_WHITESPACE = 1 << 0,
	LT_TITLE = 1 << 1,
	LT_KERNEL = 1 << 2,
	LT_INITRD = 1 << 3,
	LT_HYPER = 1 << 4,
	LT_DEFAULT = 1 << 5,
	LT_MBMODULE = 1 << 6,
	LT_ROOT = 1 << 7,
	LT_FALLBACK = 1 << 8,
	LT_KERNELARGS = 1 << 9,
	LT_BOOT = 1 << 10,
	LT_BOOTROOT = 1 << 11,
	LT_LBA = 1 << 12,
	LT_OTHER = 1 << 13,
	LT_GENERIC = 1 << 14,
	LT_ECHO = 1 << 16,
	LT_MENUENTRY = 1 << 17,
	LT_ENTRY_END = 1 << 18,
	LT_SET_VARIABLE = 1 << 19,
	LT_KERNEL_EFI = 1 << 20,
	LT_INITRD_EFI = 1 << 21,
	LT_KERNEL_16 = 1 << 22,
	LT_INITRD_16 = 1 << 23,
	LT_DEVTREE = 1 << 24,
	LT_UNKNOWN = 1 << 25,
};

struct singleLine {
	char *indent;
	int numElements;
	struct lineElement *elements;
	struct singleLine *next;
	enum lineType_e type;
};

struct singleEntry {
	struct singleLine *lines;
	int skip;
	int multiboot;
	struct singleEntry *next;
};

#define GRUBBY_BADIMAGE_OKAY	(1 << 0)

#define GRUB_CONFIG_NO_DEFAULT	    (1 << 0)	/* don't write out default=0 */

/* These defines are (only) used in addNewKernel() */
#define NEED_KERNEL  (1 << 0)
#define NEED_INITRD  (1 << 1)
#define NEED_TITLE   (1 << 2)
#define NEED_ARGS    (1 << 3)
#define NEED_MB      (1 << 4)
#define NEED_END     (1 << 5)
#define NEED_DEVTREE (1 << 6)

#define MAIN_DEFAULT	    (1 << 0)
#define FIRST_ENTRY_INDEX    0	/* boot entry index value begin and increment
				   from this initial value */
#define NO_DEFAULT_ENTRY    -1	/* indicates that no specific default boot
				   entry was set or currently exists */
#define DEFAULT_SAVED       -2
#define DEFAULT_SAVED_GRUB2 -3

struct keywordTypes {
	char *key;
	enum lineType_e type;
	char nextChar;
	char separatorChar;
};

struct configFileInfo;

typedef const char *(*findConfigFunc) (struct configFileInfo *);
typedef const int (*writeLineFunc) (struct configFileInfo *,
				    struct singleLine * line);
typedef char *(*getEnvFunc) (struct configFileInfo *, char *name);
typedef int (*setEnvFunc) (struct configFileInfo *, char *name, char *value);

struct configFileInfo {
	char *defaultConfig;
	findConfigFunc findConfig;
	writeLineFunc writeLine;
	getEnvFunc getEnv;
	setEnvFunc setEnv;
	struct keywordTypes *keywords;
	int caseInsensitive;
	int defaultIsIndex;
	int defaultIsVariable;
	int defaultSupportSaved;
	int defaultIsSaved;
	int defaultIsUnquoted;
	enum lineType_e entryStart;
	enum lineType_e entryEnd;
	int needsBootPrefix;
	int argsInQuotes;
	int maxTitleLength;
	int titleBracketed;
	int titlePosition;
	int mbHyperFirst;
	int mbInitRdIsModule;
	int mbConcatArgs;
	int mbAllowExtraInitRds;
	char *envFile;
};

struct keywordTypes grubKeywords[] = {
	{"title", LT_TITLE, ' '},
	{"root", LT_BOOTROOT, ' '},
	{"default", LT_DEFAULT, ' '},
	{"fallback", LT_FALLBACK, ' '},
	{"kernel", LT_KERNEL, ' '},
	{"initrd", LT_INITRD, ' ', ' '},
	{"module", LT_MBMODULE, ' '},
	{"kernel", LT_HYPER, ' '},
	{NULL, 0, 0},
};

const char *grubFindConfig(struct configFileInfo *cfi)
{
	static const char *configFiles[] = {
		"/boot/grub/menu.lst",
		"/etc/grub.conf",
		NULL
	};
	static int i = -1;

	if (i == -1) {
		for (i = 0; configFiles[i] != NULL; i++) {
			dbgPrintf("Checking \"%s\": ", configFiles[i]);
			if (!access(configFiles[i], R_OK)) {
				dbgPrintf("found\n");
				return configFiles[i];
			}
			dbgPrintf("not found\n");
		}
	}
	return configFiles[i];
}

struct configFileInfo grubConfigType = {
	.findConfig = grubFindConfig,
	.keywords = grubKeywords,
	.defaultIsIndex = 1,
	.defaultSupportSaved = 1,
	.entryStart = LT_TITLE,
	.needsBootPrefix = 1,
	.mbHyperFirst = 1,
	.mbInitRdIsModule = 1,
	.mbAllowExtraInitRds = 1,
	.titlePosition = 1,
};

struct keywordTypes grub2Keywords[] = {
	{"menuentry", LT_MENUENTRY, ' '},
	{"}", LT_ENTRY_END, ' '},
	{"echo", LT_ECHO, ' '},
	{"set", LT_SET_VARIABLE, ' ', '='},
	{"root", LT_BOOTROOT, ' '},
	{"default", LT_DEFAULT, ' '},
	{"fallback", LT_FALLBACK, ' '},
	{"linux", LT_KERNEL, ' '},
	{"linuxefi", LT_KERNEL_EFI, ' '},
	{"linux16", LT_KERNEL_16, ' '},
	{"initrd", LT_INITRD, ' ', ' '},
	{"initrdefi", LT_INITRD_EFI, ' ', ' '},
	{"initrd16", LT_INITRD_16, ' ', ' '},
	{"module", LT_MBMODULE, ' '},
	{"kernel", LT_HYPER, ' '},
	{"devicetree", LT_DEVTREE, ' '},
	{NULL, 0, 0},
};

const char *grub2FindConfig(struct configFileInfo *cfi)
{
	static const char *configFiles[] = {
		"/etc/grub2-efi.cfg",
		"/etc/grub2.cfg",
		"/boot/grub2/grub.cfg",
		"/boot/grub2-efi/grub.cfg",
		NULL
	};
	static int i = -1;
	static const char *grub_cfg = "/boot/grub/grub.cfg";
	int rc = -1;

	if (i == -1) {
		for (i = 0; configFiles[i] != NULL; i++) {
			dbgPrintf("Checking \"%s\": ", configFiles[i]);
			if ((rc = access(configFiles[i], R_OK))) {
				if (errno == EACCES) {
					printf
					    ("Unable to access bootloader configuration file "
					     "\"%s\": %m\n", configFiles[i]);
					exit(1);
				}
				continue;
			} else {
				dbgPrintf("found\n");
				return configFiles[i];
			}
		}
	}

	/* Ubuntu renames grub2 to grub, so check for the grub.d directory
	 * that isn't in grub1, and if it exists, return the config file path
	 * that they use. */
	if (configFiles[i] == NULL && !access("/etc/grub.d/", R_OK)) {
		dbgPrintf("found\n");
		return grub_cfg;
	}

	dbgPrintf("not found\n");
	return configFiles[i];
}

/* kind of hacky.  It'll give the first 1024 bytes, ish. */
static char *grub2GetEnv(struct configFileInfo *info, char *name)
{
	static char buf[1025];
	char *s = NULL;
	char *ret = NULL;
	char *envFile = info->envFile ? info->envFile : "/boot/grub2/grubenv";
	int rc =
	    asprintf(&s, "grub2-editenv %s list | grep '^%s='", envFile, name);

	if (rc < 0)
		return NULL;

	FILE *f = popen(s, "r");
	if (!f)
		goto out;

	memset(buf, '\0', sizeof(buf));
	ret = fgets(buf, 1024, f);
	pclose(f);

	if (ret) {
		ret += strlen(name) + 1;
		ret[strlen(ret) - 1] = '\0';
	}
	dbgPrintf("grub2GetEnv(%s): %s\n", name, ret);
out:
	free(s);
	return ret;
}

static int sPopCount(const char *s, const char *c)
{
	int ret = 0;
	if (!s)
		return -1;
	for (int i = 0; s[i] != '\0'; i++)
		for (int j = 0; c[j] != '\0'; j++)
			if (s[i] == c[j])
				ret++;
	return ret;
}

static char *shellEscape(const char *s)
{
	int l = strlen(s) + sPopCount(s, "'") * 2;

	char *ret = calloc(l + 1, sizeof(*ret));
	if (!ret)
		return NULL;
	for (int i = 0, j = 0; s[i] != '\0'; i++, j++) {
		if (s[i] == '\'')
			ret[j++] = '\\';
		ret[j] = s[i];
	}
	return ret;
}

static void unquote(char *s)
{
	int l = strlen(s);

	if ((s[l - 1] == '\'' && s[0] == '\'')
	    || (s[l - 1] == '"' && s[0] == '"')) {
		memmove(s, s + 1, l - 2);
		s[l - 2] = '\0';
	}
}

static int grub2SetEnv(struct configFileInfo *info, char *name, char *value)
{
	char *s = NULL;
	int rc = 0;
	char *envFile = info->envFile ? info->envFile : "/boot/grub2/grubenv";

	unquote(value);
	value = shellEscape(value);
	if (!value)
		return -1;

	rc = asprintf(&s, "grub2-editenv %s set '%s=%s'", envFile, name, value);
	free(value);
	if (rc < 0)
		return -1;

	dbgPrintf("grub2SetEnv(%s): %s\n", name, s);
	rc = system(s);
	free(s);
	return rc;
}

/* this is a gigantic hack to avoid clobbering grub2 variables... */
static int is_special_grub2_variable(const char *name)
{
	if (!strcmp(name, "\"${next_entry}\""))
		return 1;
	if (!strcmp(name, "\"${prev_saved_entry}\""))
		return 1;
	return 0;
}

int sizeOfSingleLine(struct singleLine *line)
{
	int count = 0;

	for (int i = 0; i < line->numElements; i++) {
		int indentSize = 0;

		count = count + strlen(line->elements[i].item);

		indentSize = strlen(line->elements[i].indent);
		if (indentSize > 0)
			count = count + indentSize;
		else
			/* be extra safe and add room for whitespaces */
			count = count + 1;
	}

	/* room for trailing terminator */
	count = count + 1;

	return count;
}

static int isquote(char q)
{
	if (q == '\'' || q == '\"')
		return 1;
	return 0;
}

static int iskernel(enum lineType_e type)
{
	return (type == LT_KERNEL || type == LT_KERNEL_EFI
		|| type == LT_KERNEL_16);
}

static int isinitrd(enum lineType_e type)
{
	return (type == LT_INITRD || type == LT_INITRD_EFI
		|| type == LT_INITRD_16);
}

char *grub2ExtractTitle(struct singleLine *line)
{
	char *current;
	char *current_indent;
	int current_len;
	int current_indent_len;
	int i;

	/* bail out if line does not start with menuentry */
	if (strcmp(line->elements[0].item, "menuentry"))
		return NULL;

	i = 1;
	current = line->elements[i].item;
	current_len = strlen(current);

	/* if second word is quoted, strip the quotes and return single word */
	if (isquote(*current) && isquote(current[current_len - 1])) {
		char *tmp;

		tmp = strdup(current + 1);
		if (!tmp)
			return NULL;
		tmp[strlen(tmp) - 1] = '\0';
		return tmp;
	}

	/* if no quotes, return second word verbatim */
	if (!isquote(*current))
		return current;

	/* second element start with a quote, so we have to find the element
	 * whose last character is also quote (assuming it's the closing one) */
	int resultMaxSize;
	char *result;
	/* need to ensure that ' does not match " as we search */
	char quote_char = *current;

	resultMaxSize = sizeOfSingleLine(line);
	result = malloc(resultMaxSize);
	snprintf(result, resultMaxSize, "%s", ++current);

	i++;
	int result_len = 0;
	for (; i < line->numElements; ++i) {
		current = line->elements[i].item;
		current_len = strlen(current);
		current_indent = line->elements[i].indent;
		current_indent_len = strlen(current_indent);

		memcpy(result + result_len, current_indent, current_indent_len);
		result_len += current_indent_len;

		if (current[current_len - 1] != quote_char) {
			memcpy(result + result_len, current_indent,
			       current_indent_len);
			result_len += current_len;
		} else {
			memcpy(result + result_len, current_indent,
			       current_indent_len);
			result_len += (current_len - 1);
			break;
		}
	}
	result[result_len] = '\0';
	return result;
}

struct configFileInfo grub2ConfigType = {
	.findConfig = grub2FindConfig,
	.getEnv = grub2GetEnv,
	.setEnv = grub2SetEnv,
	.keywords = grub2Keywords,
	.defaultIsIndex = 1,
	.defaultSupportSaved = 1,
	.defaultIsVariable = 1,
	.entryStart = LT_MENUENTRY,
	.entryEnd = LT_ENTRY_END,
	.titlePosition = 1,
	.needsBootPrefix = 1,
	.mbHyperFirst = 1,
	.mbInitRdIsModule = 1,
	.mbAllowExtraInitRds = 1,
};

struct keywordTypes yabootKeywords[] = {
	{"label", LT_TITLE, '='},
	{"root", LT_ROOT, '='},
	{"default", LT_DEFAULT, '='},
	{"image", LT_KERNEL, '='},
	{"bsd", LT_GENERIC, '='},
	{"macos", LT_GENERIC, '='},
	{"macosx", LT_GENERIC, '='},
	{"magicboot", LT_GENERIC, '='},
	{"darwin", LT_GENERIC, '='},
	{"timeout", LT_GENERIC, '='},
	{"install", LT_GENERIC, '='},
	{"fstype", LT_GENERIC, '='},
	{"hfstype", LT_GENERIC, '='},
	{"delay", LT_GENERIC, '='},
	{"defaultos", LT_GENERIC, '='},
	{"init-message", LT_GENERIC, '='},
	{"enablecdboot", LT_GENERIC, ' '},
	{"enableofboot", LT_GENERIC, ' '},
	{"enablenetboot", LT_GENERIC, ' '},
	{"nonvram", LT_GENERIC, ' '},
	{"hide", LT_GENERIC, ' '},
	{"protect", LT_GENERIC, ' '},
	{"nobless", LT_GENERIC, ' '},
	{"nonvram", LT_GENERIC, ' '},
	{"brokenosx", LT_GENERIC, ' '},
	{"usemount", LT_GENERIC, ' '},
	{"mntpoint", LT_GENERIC, '='},
	{"partition", LT_GENERIC, '='},
	{"device", LT_GENERIC, '='},
	{"fstype", LT_GENERIC, '='},
	{"initrd", LT_INITRD, '=', ';'},
	{"append", LT_KERNELARGS, '='},
	{"boot", LT_BOOT, '='},
	{"lba", LT_LBA, ' '},
	{NULL, 0, 0},
};

struct keywordTypes liloKeywords[] = {
	{"label", LT_TITLE, '='},
	{"root", LT_ROOT, '='},
	{"default", LT_DEFAULT, '='},
	{"image", LT_KERNEL, '='},
	{"other", LT_OTHER, '='},
	{"initrd", LT_INITRD, '='},
	{"append", LT_KERNELARGS, '='},
	{"boot", LT_BOOT, '='},
	{"lba", LT_LBA, ' '},
	{NULL, 0, 0},
};

struct keywordTypes eliloKeywords[] = {
	{"label", LT_TITLE, '='},
	{"root", LT_ROOT, '='},
	{"default", LT_DEFAULT, '='},
	{"image", LT_KERNEL, '='},
	{"initrd", LT_INITRD, '='},
	{"append", LT_KERNELARGS, '='},
	{"vmm", LT_HYPER, '='},
	{NULL, 0, 0},
};

struct keywordTypes siloKeywords[] = {
	{"label", LT_TITLE, '='},
	{"root", LT_ROOT, '='},
	{"default", LT_DEFAULT, '='},
	{"image", LT_KERNEL, '='},
	{"other", LT_OTHER, '='},
	{"initrd", LT_INITRD, '='},
	{"append", LT_KERNELARGS, '='},
	{"boot", LT_BOOT, '='},
	{NULL, 0, 0},
};

struct keywordTypes ziplKeywords[] = {
	{"target", LT_BOOTROOT, '='},
	{"image", LT_KERNEL, '='},
	{"ramdisk", LT_INITRD, '='},
	{"parameters", LT_KERNELARGS, '='},
	{"default", LT_DEFAULT, '='},
	{NULL, 0, 0},
};

struct keywordTypes extlinuxKeywords[] = {
	{"label", LT_TITLE, ' '},
	{"root", LT_ROOT, ' '},
	{"default", LT_DEFAULT, ' '},
	{"kernel", LT_KERNEL, ' '},
	{"initrd", LT_INITRD, ' ', ','},
	{"append", LT_KERNELARGS, ' '},
	{"prompt", LT_UNKNOWN, ' '},
	{"fdt", LT_DEVTREE, ' '},
	{"fdtdir", LT_DEVTREE, ' '},
	{NULL, 0, 0},
};

int useextlinuxmenu;
struct configFileInfo eliloConfigType = {
	.defaultConfig = "/boot/efi/EFI/redhat/elilo.conf",
	.keywords = eliloKeywords,
	.entryStart = LT_KERNEL,
	.needsBootPrefix = 1,
	.argsInQuotes = 1,
	.mbConcatArgs = 1,
	.titlePosition = 1,
};

struct configFileInfo liloConfigType = {
	.defaultConfig = "/etc/lilo.conf",
	.keywords = liloKeywords,
	.entryStart = LT_KERNEL,
	.argsInQuotes = 1,
	.maxTitleLength = 15,
	.titlePosition = 1,
};

struct configFileInfo yabootConfigType = {
	.defaultConfig = "/etc/yaboot.conf",
	.keywords = yabootKeywords,
	.entryStart = LT_KERNEL,
	.needsBootPrefix = 1,
	.argsInQuotes = 1,
	.maxTitleLength = 15,
	.mbAllowExtraInitRds = 1,
	.titlePosition = 1,
};

struct configFileInfo siloConfigType = {
	.defaultConfig = "/etc/silo.conf",
	.keywords = siloKeywords,
	.entryStart = LT_KERNEL,
	.needsBootPrefix = 1,
	.argsInQuotes = 1,
	.maxTitleLength = 15,
	.titlePosition = 1,
};

struct configFileInfo ziplConfigType = {
	.defaultConfig = "/etc/zipl.conf",
	.keywords = ziplKeywords,
	.entryStart = LT_TITLE,
	.argsInQuotes = 1,
	.titleBracketed = 1,
};

struct configFileInfo extlinuxConfigType = {
	.defaultConfig = "/boot/extlinux/extlinux.conf",
	.keywords = extlinuxKeywords,
	.caseInsensitive = 1,
	.entryStart = LT_TITLE,
	.needsBootPrefix = 1,
	.maxTitleLength = 255,
	.mbAllowExtraInitRds = 1,
	.defaultIsUnquoted = 1,
	.titlePosition = 1,
};

struct grubConfig {
	struct singleLine *theLines;
	struct singleEntry *entries;
	char *primaryIndent;
	char *secondaryIndent;
	int defaultImage;	/* -1 if none specified -- this value is
				 * written out, overriding original */
	int fallbackImage;	/* just like defaultImage */
	int flags;
	struct configFileInfo *cfi;
	int isModified;		/* assumes only one entry added
				   per invocation of grubby */
};

blkid_cache blkid;

struct singleEntry *findEntryByIndex(struct grubConfig *cfg, int index);
struct singleEntry *findEntryByPath(struct grubConfig *cfg,
				    const char *path, const char *prefix,
				    int *index);
struct singleEntry *findEntryByTitle(struct grubConfig *cfg, char *title,
				     int *index);
static int readFile(int fd, char **bufPtr);
static void lineInit(struct singleLine *line);
struct singleLine *lineDup(struct singleLine *line);
static void lineFree(struct singleLine *line);
static int lineWrite(FILE * out, struct singleLine *line,
		     struct configFileInfo *cfi);
static int getNextLine(char **bufPtr, struct singleLine *line,
		       struct configFileInfo *cfi);
static size_t getRootSpecifier(const char *str);
static void requote(struct singleLine *line, struct configFileInfo *cfi);
static void insertElement(struct singleLine *line,
			  const char *item, int insertHere,
			  struct configFileInfo *cfi);
static void removeElement(struct singleLine *line, int removeHere);
static struct keywordTypes *getKeywordByType(enum lineType_e type,
					     struct configFileInfo *cfi);
static enum lineType_e getTypeByKeyword(char *keyword,
					struct configFileInfo *cfi);
static struct singleLine *getLineByType(enum lineType_e type,
					struct singleLine *line);
static int checkForExtLinux(struct grubConfig *config);
struct singleLine *addLineTmpl(struct singleEntry *entry,
			       struct singleLine *tmplLine,
			       struct singleLine *prevLine,
			       const char *val, struct configFileInfo *cfi);
struct singleLine *addLine(struct singleEntry *entry,
			   struct configFileInfo *cfi,
			   enum lineType_e type, char *defaultIndent,
			   const char *val);

static char *sdupprintf(const char *format, ...)
#ifdef __GNUC__
    __attribute__ ((format(printf, 1, 2)));
#else
;
#endif

static char *sdupprintf(const char *format, ...)
{
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
	va_end(args);

	return buf;
}

static inline int
kwcmp(struct keywordTypes *kw, const char * label, int case_insensitive)
{
    int kwl = strlen(kw->key);
    int ll = strlen(label);
    int rc;
    int (*snc)(const char *s1, const char *s2, size_t n) =
           case_insensitive ? strncasecmp : strncmp;
    int (*sc)(const char *s1, const char *s2) =
           case_insensitive ? strcasecmp : strcmp;

    rc = snc(kw->key, label, kwl);
    if (rc)
       return rc;

    for (int i = kwl; i < ll; i++) {
       if (isspace(label[i]))
           return 0;
       if (kw->separatorChar && label[i] == kw->separatorChar)
           return 0;
       else if (kw->nextChar && label[i] == kw->nextChar)
           return 0;
       return sc(kw->key+kwl, label+kwl);
    }
    return 0;
}

static enum lineType_e preferredLineType(enum lineType_e type,
					 struct configFileInfo *cfi)
{
	if (isEfi && cfi == &grub2ConfigType) {
		switch (type) {
		case LT_KERNEL:
			return isEfiOnly ? LT_KERNEL : LT_KERNEL_EFI;
		case LT_INITRD:
			return isEfiOnly ? LT_INITRD : LT_INITRD_EFI;
		default:
			return type;
		}
#if defined(__i386__) || defined(__x86_64__)
	} else if (cfi == &grub2ConfigType) {
		switch (type) {
		case LT_KERNEL:
			return LT_KERNEL_16;
		case LT_INITRD:
			return LT_INITRD_16;
		default:
			return type;
		}
#endif
	}
	return type;
}

static struct keywordTypes *getKeywordByType(enum lineType_e type,
					     struct configFileInfo *cfi)
{
	for (struct keywordTypes * kw = cfi->keywords; kw->key; kw++) {
		if (kw->type == type)
			return kw;
	}
	return NULL;
}

static char *getKeyByType(enum lineType_e type, struct configFileInfo *cfi)
{
	struct keywordTypes *kt = getKeywordByType(type, cfi);
	if (kt)
		return kt->key;
	return "unknown";
}

static char *getpathbyspec(char *device)
{
	if (!blkid)
		blkid_get_cache(&blkid, NULL);

	return blkid_get_devname(blkid, device, NULL);
}

static char *getuuidbydev(char *device)
{
	if (!blkid)
		blkid_get_cache(&blkid, NULL);

	return blkid_get_tag_value(blkid, "UUID", device);
}

static enum lineType_e getTypeByKeyword(char *keyword,
					struct configFileInfo *cfi)
{
	for (struct keywordTypes * kw = cfi->keywords; kw->key; kw++) {
		if (!kwcmp(kw, keyword, cfi->caseInsensitive))
			return kw->type;
	}
	return LT_UNKNOWN;
}

static struct singleLine *getLineByType(enum lineType_e type,
					struct singleLine *line)
{
	dbgPrintf("getLineByType(%d): ", type);
	for (; line; line = line->next) {
		dbgPrintf("%d:%s ", line->type,
			  line->numElements ? line->elements[0].
			  item : "(empty)");
		if (line->type & type)
			break;
	}
	dbgPrintf(line ? "\n" : " (failed)\n");
	return line;
}

static int isBracketedTitle(struct singleLine *line)
{
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

static int isEntryStart(struct singleLine *line, struct configFileInfo *cfi)
{
	return line->type == cfi->entryStart || line->type == LT_OTHER ||
	    (cfi->titleBracketed && isBracketedTitle(line));
}

/* extract the title from within brackets (for zipl) */
static char *extractTitle(struct grubConfig *cfg, struct singleLine *line)
{
	/* bracketed title... let's extract it */
	char *title = NULL;
	if (line->type == LT_TITLE) {
		char *tmp = line->elements[cfg->cfi->titlePosition].item;
		if (cfg->cfi->titleBracketed) {
			tmp++;
			title = strdup(tmp);
			*(title + strlen(title) - 1) = '\0';
		} else {
			title = strdup(tmp);
		}
	} else if (line->type == LT_MENUENTRY)
		title = strdup(line->elements[1].item);
	else
		return NULL;
	return title;
}

static int readFile(int fd, char **bufPtr)
{
	int alloced = 0, size = 0, i = 0;
	char *buf = NULL;

	do {
		size += i;
		if ((size + 1024) > alloced) {
			alloced += 4096;
			buf = realloc(buf, alloced + 1);
		}
	} while ((i = read(fd, buf + size, 1024)) > 0);

	if (i < 0) {
		fprintf(stderr, _("error reading input: %s\n"),
			strerror(errno));
		free(buf);
		return 1;
	}

	buf = realloc(buf, size + 2);
	if (size == 0)
		buf[size++] = '\n';
	else if (buf[size - 1] != '\n')
		buf[size++] = '\n';
	buf[size] = '\0';

	*bufPtr = buf;

	return 0;
}

static void lineInit(struct singleLine *line)
{
	line->type = LT_UNIDENTIFIED;
	line->indent = NULL;
	line->elements = NULL;
	line->numElements = 0;
	line->next = NULL;
}

struct singleLine *lineDup(struct singleLine *line)
{
	struct singleLine *newLine = malloc(sizeof(*newLine));

	newLine->indent = strdup(line->indent);
	newLine->next = NULL;
	newLine->type = line->type;
	newLine->numElements = line->numElements;
	newLine->elements = malloc(sizeof(*newLine->elements) *
				   newLine->numElements);

	for (int i = 0; i < newLine->numElements; i++) {
		newLine->elements[i].indent = strdup(line->elements[i].indent);
		newLine->elements[i].item = strdup(line->elements[i].item);
	}

	return newLine;
}

static void lineFree(struct singleLine *line)
{
	if (line->indent)
		free(line->indent);

	for (int i = 0; i < line->numElements; i++) {
		free(line->elements[i].item);
		free(line->elements[i].indent);
	}

	if (line->elements)
		free(line->elements);
	lineInit(line);
}

static int lineWrite(FILE * out, struct singleLine *line,
		     struct configFileInfo *cfi)
{
	if (fprintf(out, "%s", line->indent) == -1)
		return -1;

	for (int i = 0; i < line->numElements; i++) {
		/* Need to handle this, because we strip the quotes from
		 * menuentry when read it. */
		if (line->type == LT_MENUENTRY && i == 1) {
			if (!isquote(*line->elements[i].item)) {
				int substring = 0;
				/* If the line contains nested quotes, we did
				 * not strip the "interna" quotes and we must
				 * use the right quotes again when writing
				 * the updated file. */
				for (int j = i; j < line->numElements; j++) {
					if (strchr(line->elements[i].item, '\'')
					    != NULL) {
						substring = 1;
						fprintf(out, "\"%s\"",
							line->elements[i].item);
						break;
					}
				}
				if (!substring)
					fprintf(out, "\'%s\'",
						line->elements[i].item);
			} else {
				fprintf(out, "%s", line->elements[i].item);
			}
			fprintf(out, "%s", line->elements[i].indent);

			continue;
		}

		if (i == 1 && line->type == LT_KERNELARGS && cfi->argsInQuotes)
			if (fputc('"', out) == EOF)
				return -1;

		if (fprintf(out, "%s", line->elements[i].item) == -1)
			return -1;
		if (i < line->numElements - 1 || line->type == LT_SET_VARIABLE)
			if (fprintf(out, "%s", line->elements[i].indent) == -1)
				return -1;
	}

	if (line->type == LT_KERNELARGS && cfi->argsInQuotes)
		if (fputc('"', out) == EOF)
			return -1;

	if (fprintf(out, "\n") == -1)
		return -1;

	return 0;
}

/* we've guaranteed that the buffer ends w/ \n\0 */
static int getNextLine(char **bufPtr, struct singleLine *line,
		       struct configFileInfo *cfi)
{
	char *end;
	char *start = *bufPtr;
	char *chptr;
	int elementsAlloced = 0;
	struct lineElement *element;
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
						 sizeof(*line->elements) *
						 elementsAlloced);
		}

		element = line->elements + line->numElements;

		chptr = start;
		while (*chptr && !isspace(*chptr)) {
			if (first && *chptr == '=')
				break;
			chptr++;
		}
		if (line->type == LT_UNIDENTIFIED)
			line->type = getTypeByKeyword(start, cfi);
		element->item = strndup(start, chptr - start);
		start = chptr;

		/* lilo actually accepts the pathological case of
		 * append = " foo " */
		if (*start == '=')
			chptr = start + 1;
		else
			chptr = start;

		do {
			for (; *chptr && isspace(*chptr); chptr++) ;
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
			/* zipl does [title] instead of something reasonable
			 * like all the other boot loaders.  kind of ugly */
			if (cfi->titleBracketed && isBracketedTitle(line)) {
				line->type = LT_TITLE;
			}

			/* this is awkward, but we need to be able to handle
			 * keywords that begin with a # (specifically for
			 * #boot in grub.conf), but still make comments lines
			 * with no elements (everything stored in the indent
			 */
			if (*line->elements[0].item == '#') {
				char *fullLine;
				int len;

				len = strlen(line->indent);
				for (int i = 0; i < line->numElements; i++)
					len += strlen(line->elements[i].item) +
					    strlen(line->elements[i].indent);

				fullLine = malloc(len + 1);
				strcpy(fullLine, line->indent);
				free(line->indent);
				line->indent = fullLine;

				for (int i = 0; i < line->numElements; i++) {
					strcat(fullLine,
					       line->elements[i].item);
					strcat(fullLine,
					       line->elements[i].indent);
					free(line->elements[i].item);
					free(line->elements[i].indent);
				}

				line->type = LT_WHITESPACE;
				line->numElements = 0;
			}
		} else if (line->type == LT_INITRD) {
			struct keywordTypes *kw;

			kw = getKeywordByType(line->type, cfi);

			/* space isn't the only separator, we need to split
			 * elements up more
			 */
			if (!isspace(kw->separatorChar)) {
				char indent[2] = "";
				indent[0] = kw->separatorChar;
				for (int i = 1; i < line->numElements; i++) {
					char *p;
					int numNewElements;

					numNewElements = 0;
					p = line->elements[i].item;
					while (*p != '\0') {
						if (*p == kw->separatorChar)
							numNewElements++;
						p++;
					}
					if (line->numElements +
					    numNewElements >= elementsAlloced) {
						elementsAlloced +=
						    numNewElements + 5;
						line->elements =
						    realloc(line->elements,
							    sizeof(*line->
								   elements) *
							    elementsAlloced);
					}

					for (int j = line->numElements; j > i;
					     j--) {
						line->elements[j +
							       numNewElements] =
						    line->elements[j];
					}
					line->numElements += numNewElements;

					p = line->elements[i].item;
					while (*p != '\0') {

						while (*p != kw->separatorChar
						       && *p != '\0')
							p++;
						if (*p == '\0') {
							break;
						}

						line->elements[i + 1].indent =
						    line->elements[i].indent;
						line->elements[i].indent =
						    strdup(indent);
						*p++ = '\0';
						i++;
						line->elements[i].item =
						    strdup(p);
					}
				}
			}
		} else if (line->type == LT_SET_VARIABLE) {
			/* and if it's a "set blah=" we need to split it
			 * yet a third way to avoid rhbz# XXX FIXME :/
			 */
			char *eq;
			int l;
			int numElements = line->numElements;
			struct lineElement *newElements;
			eq = strchr(line->elements[1].item, '=');
			if (!eq)
				return 0;
			l = eq - line->elements[1].item;
			if (eq[1] != 0)
				numElements++;
			newElements = calloc(numElements,sizeof (*newElements));
			memcpy(&newElements[0], &line->elements[0],
			       sizeof (newElements[0]));
			newElements[1].item =
				strndup(line->elements[1].item, l);
			newElements[1].indent = "=";
			*(eq++) = '\0';
			newElements[2].item = strdup(eq);
			free(line->elements[1].item);
			if (line->elements[1].indent)
				newElements[2].indent = line->elements[1].indent;
			for (int i = 2; i < line->numElements; i++) {
				newElements[i+1].item = line->elements[i].item;
				newElements[i+1].indent =
					line->elements[i].indent;
			}
			free(line->elements);
			line->elements = newElements;
			line->numElements = numElements;
		}
	}

	return 0;
}

static int isnumber(const char *s)
{
	int i;
	for (i = 0; s[i] != '\0'; i++)
		if (s[i] < '0' || s[i] > '9')
			return 0;
	return i;
}

static struct grubConfig *readConfig(const char *inName,
				     struct configFileInfo *cfi)
{
	int in;
	char *incoming = NULL, *head;
	int rc;
	int sawEntry = 0;
	int movedLine = 0;
	struct grubConfig *cfg;
	struct singleLine *last = NULL, *line, *defaultLine = NULL;
	char *end;
	struct singleEntry *entry = NULL;
	int len;
	char *buf;

	if (inName == NULL) {
		printf("Could not find bootloader configuration\n");
		exit(1);
	} else if (!strcmp(inName, "-")) {
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
	if (rc)
		return NULL;

	head = incoming;
	cfg = malloc(sizeof(*cfg));
	cfg->primaryIndent = strdup("");
	cfg->secondaryIndent = strdup("\t");
	cfg->flags = GRUB_CONFIG_NO_DEFAULT;
	cfg->cfi = cfi;
	cfg->theLines = NULL;
	cfg->entries = NULL;
	cfg->fallbackImage = 0;
	cfg->isModified = 0;

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

		if (isEntryStart(line, cfi) || (cfg->entries && !sawEntry)) {
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

		if (line->type == LT_SET_VARIABLE) {
			dbgPrintf("found 'set' command (%d elements): ",
				  line->numElements);
			dbgPrintf("%s", line->indent);
			for (int i = 0; i < line->numElements; i++)
				dbgPrintf("\"%s\"%s", line->elements[i].item,
					  line->elements[i].indent);
			dbgPrintf("\n");
			struct keywordTypes *kwType =
			    getKeywordByType(LT_DEFAULT, cfi);
			if (kwType && line->numElements == 3
			    && !strcmp(line->elements[1].item, kwType->key)
			    && !is_special_grub2_variable(
						line->elements[2].item)) {
				dbgPrintf("Line sets default config\n");
				cfg->flags &= ~GRUB_CONFIG_NO_DEFAULT;
				defaultLine = line;
			}
		} else if (iskernel(line->type)) {
			/* if by some freak chance this is multiboot and the
			 * "module" lines came earlier in the template, make
			 * sure to use LT_HYPER instead of LT_KERNEL now
			 */
			if (entry && entry->multiboot)
				line->type = LT_HYPER;

		} else if (line->type == LT_MBMODULE) {
			/* go back and fix the LT_KERNEL line to indicate
			 * LT_HYPER instead, now that we know this is a
			 * multiboot entry.  This only applies to grub, but
			 * that's the only place we should find LT_MBMODULE
			 * lines anyway.
			 */
			for (struct singleLine * l = entry->lines; l;
			     l = l->next) {
				if (l->type == LT_HYPER)
					break;
				else if (iskernel(l->type)) {
					l->type = LT_HYPER;
					break;
				}
			}
			entry->multiboot = 1;

		} else if (line->type == LT_HYPER) {
			entry->multiboot = 1;

		} else if (line->type == LT_FALLBACK && line->numElements == 2) {
			cfg->fallbackImage =
			    strtol(line->elements[1].item, &end, 10);
			if (*end)
				cfg->fallbackImage = -1;

		} else if ((line->type == LT_DEFAULT && cfi->defaultIsUnquoted)
			   || (line->type == LT_TITLE
			       && line->numElements > 1)) {
			/* make the title/default a single argument (undoing
			 * our parsing) */
			len = 0;
			for (int i = 1; i < line->numElements; i++) {
				len += strlen(line->elements[i].item);
				len += strlen(line->elements[i].indent);
			}
			buf = malloc(len + 1);
			*buf = '\0';

			for (int i = 1; i < line->numElements; i++) {
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
		} else if (line->type == LT_MENUENTRY && line->numElements > 3) {
			/* let --remove-kernel="TITLE=what" work */
			len = 0;
			char *extras;
			char *title;
			/* initially unseen value */
			char quote_char = '\0';

			for (int i = 1; i < line->numElements; i++) {
				len += strlen(line->elements[i].item);
				len += strlen(line->elements[i].indent);
			}
			buf = malloc(len + 1);
			*buf = '\0';

			/* allocate mem for extra flags. */
			extras = malloc(len + 1);
			*extras = '\0';

			int buf_len = 0;
			/* get title. */
			for (int i = 0; i < line->numElements; i++) {
				if (!strcmp
				    (line->elements[i].item, "menuentry"))
					continue;
				if (isquote(*line->elements[i].item)
				    && quote_char == '\0') {
					/* ensure we properly pair off quotes */
					quote_char = *line->elements[i].item;
					title = line->elements[i].item + 1;
				} else {
					title = line->elements[i].item;
				}

				len = strlen(title);
				if (title[len - 1] == quote_char) {
					memcpy(buf + buf_len, title, len - 1);
					buf_len += (len - 1);
					break;
				} else {
					memcpy(buf + buf_len, title, len);
					buf_len += len;
					len = strlen(line->elements[i].indent);
					memcpy(buf + buf_len, line->elements[i].indent, len);
					buf_len += len;
				}
			}
			buf[buf_len] = '\0';

			/* get extras */
			int count = 0;
			quote_char = '\0';
			for (int i = 0; i < line->numElements; i++) {
				if (count >= 2) {
					strcat(extras, line->elements[i].item);
					strcat(extras,
					       line->elements[i].indent);
				}

				if (!strcmp
				    (line->elements[i].item, "menuentry"))
					continue;

				/* count ' or ", there should be two in menuentry line. */
				if (isquote(*line->elements[i].item)
				    && quote_char == '\0') {
					/* ensure we properly pair off quotes */
					quote_char = *line->elements[i].item;
					count++;
				}

				len = strlen(line->elements[i].item);

				if (line->elements[i].item[len - 1] ==
				    quote_char)
					count++;

				/* ok, we get the final ' or ", others are extras. */
			}
			line->elements[1].indent =
			    line->elements[line->numElements - 2].indent;
			line->elements[1].item = buf;
			line->elements[2].indent =
			    line->elements[line->numElements - 2].indent;
			line->elements[2].item = extras;
			line->numElements = 3;
		} else if (line->type == LT_KERNELARGS && cfi->argsInQuotes) {
			/* Strip off any " which may be present; they'll be
			 * put back on write. This is one of the few (the
			 * only?) places that grubby canonicalizes the output
			 */
			if (line->numElements >= 2) {
				int last, len;

				if (isquote(*line->elements[1].item))
					memmove(line->elements[1].item,
						line->elements[1].item + 1,
						strlen(line->elements[1].item +
						       1) + 1);

				last = line->numElements - 1;
				len = strlen(line->elements[last].item) - 1;
				if (isquote(line->elements[last].item[len]))
					line->elements[last].item[len] = '\0';
			}
		}

		if (line->type == LT_DEFAULT && line->numElements == 2) {
			cfg->flags &= ~GRUB_CONFIG_NO_DEFAULT;
			defaultLine = line;
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
			continue;	/* without setting 'last' */
		}

		/* If a second line of whitespace happens after a generic
		 * option which was moved, drop it. */
		if (movedLine && line->type == LT_WHITESPACE
		    && last->type == LT_WHITESPACE) {
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
			dbgPrintf("readConfig added %s to %p\n",
				  getKeyByType(line->type, cfi), entry);

			/* we could have seen this outside of an entry... if
			 * so, we ignore it like any other line we don't grok
			 */
			if (line->type == LT_ENTRY_END && sawEntry)
				sawEntry = 0;
		} else {
			if (!cfg->theLines)
				cfg->theLines = line;
			else
				last->next = line;
			dbgPrintf("readConfig added %s to cfg\n",
				  getKeyByType(line->type, cfi));
		}

		last = line;
	}

	free(incoming);

	dbgPrintf("defaultLine is %s\n", defaultLine ? "set" : "unset");
	if (defaultLine) {
		if (defaultLine->numElements > 2 &&
		    cfi->defaultSupportSaved &&
		    !strncmp(defaultLine->elements[2].item,
			     "\"${saved_entry}\"", 16)) {
			cfg->cfi->defaultIsSaved = 1;
			cfg->defaultImage = DEFAULT_SAVED_GRUB2;
			if (cfg->cfi->getEnv) {
				char *defTitle =
				    cfi->getEnv(cfg->cfi, "saved_entry");
				if (defTitle) {
					int index = 0;
					if (isnumber(defTitle)) {
						index = atoi(defTitle);
						entry =
						    findEntryByIndex(cfg,
								     index);
					} else {
						entry =
						    findEntryByTitle(cfg,
								     defTitle,
								     &index);
					}
					if (entry)
						cfg->defaultImage = index;
				}
			}
		} else if (cfi->defaultIsVariable) {
			if (defaultLine->numElements == 2) {
				char *value = defaultLine->elements[1].item + 8;
				while (*value && (*value == '"' ||
						  *value == '\'' ||
						  *value == ' ' ||
						  *value == '\t'))
					value++;
				cfg->defaultImage = strtol(value, &end, 10);
				while (*end && (*end == '"' || *end == '\'' ||
						*end == ' ' || *end == '\t'))
					end++;
				if (*end)
					cfg->defaultImage = NO_DEFAULT_ENTRY;
			} else if (defaultLine->numElements == 3) {
				char *value = defaultLine->elements[2].item;
				while (*value && (*value == '"' ||
						  *value == '\'' ||
						  *value == ' ' ||
						  *value == '\t'))
					value++;
				cfg->defaultImage = strtol(value, &end, 10);
				while (*end && (*end == '"' || *end == '\'' ||
						*end == ' ' || *end == '\t'))
					end++;
				if (*end)
					cfg->defaultImage = NO_DEFAULT_ENTRY;
			}
		} else if (cfi->defaultSupportSaved &&
			   !strncmp(defaultLine->elements[1].item, "saved",
				    5)) {
			cfg->defaultImage = DEFAULT_SAVED;
		} else if (cfi->defaultIsIndex) {
			cfg->defaultImage =
			    strtol(defaultLine->elements[1].item, &end, 10);
			if (*end)
				cfg->defaultImage = NO_DEFAULT_ENTRY;
		} else if (defaultLine->numElements >= 2) {
			int i = 0;
			while ((entry = findEntryByIndex(cfg, i))) {
				for (line = entry->lines; line;
				     line = line->next)
					if (line->type == LT_TITLE)
						break;

				if (!cfi->titleBracketed) {
					if (line && (line->numElements >= 2) &&
					    !strcmp(defaultLine->elements[1].
						    item,
						    line->elements[1].item))
						break;
				} else if (line) {
					if (!strcmp
					    (defaultLine->elements[1].item,
					     extractTitle(cfg, line)))
						break;
				}
				i++;
				entry = NULL;
			}

			if (entry) {
				cfg->defaultImage = i;
			} else {
				cfg->defaultImage = NO_DEFAULT_ENTRY;
			}
		}
	} else if (cfg->cfi->defaultIsSaved && cfg->cfi->getEnv) {
		char *defTitle = cfi->getEnv(cfg->cfi, "saved_entry");
		if (defTitle) {
			int index = 0;
			if (isnumber(defTitle)) {
				index = atoi(defTitle);
				entry = findEntryByIndex(cfg, index);
			} else {
				entry = findEntryByTitle(cfg, defTitle, &index);
			}
			if (entry)
				cfg->defaultImage = index;
		}
	} else {
		cfg->defaultImage = FIRST_ENTRY_INDEX;
	}

	return cfg;
}

static void writeDefault(FILE * out, char *indent,
			 char *separator, struct grubConfig *cfg)
{
	struct singleEntry *entry;
	struct singleLine *line;
	int i;

	if (!cfg->defaultImage && cfg->flags == GRUB_CONFIG_NO_DEFAULT)
		return;

	if (cfg->defaultImage == DEFAULT_SAVED)
		fprintf(out, "%sdefault%ssaved\n", indent, separator);
	else if (cfg->cfi->defaultIsSaved) {
		fprintf(out, "%sset default=\"${saved_entry}\"\n", indent);
		if (cfg->defaultImage >= FIRST_ENTRY_INDEX && cfg->cfi->setEnv) {
			char *title;
			int trueIndex, currentIndex;

			trueIndex = 0;
			currentIndex = 0;

			while ((entry = findEntryByIndex(cfg, currentIndex))) {
				if (!entry->skip) {
					if (trueIndex == cfg->defaultImage) {
						break;
					}
					trueIndex++;
				}
				currentIndex++;
			}
			line = getLineByType(LT_MENUENTRY, entry->lines);
			if (!line)
				line = getLineByType(LT_TITLE, entry->lines);
			if (line) {
				title = extractTitle(cfg, line);
				if (title)
					cfg->cfi->setEnv(cfg->cfi,
							 "saved_entry", title);
			}
		}
	} else if (cfg->defaultImage >= FIRST_ENTRY_INDEX) {
		if (cfg->cfi->defaultIsIndex) {
			if (cfg->cfi->defaultIsVariable) {
				fprintf(out, "%sset default=\"%d\"\n", indent,
					cfg->defaultImage);
			} else {
				fprintf(out, "%sdefault%s%d\n", indent,
					separator, cfg->defaultImage);
			}
		} else {
			int image = cfg->defaultImage;

			entry = cfg->entries;
			while (entry && entry->skip)
				entry = entry->next;

			i = 0;
			while (entry && i < image) {
				entry = entry->next;

				while (entry && entry->skip)
					entry = entry->next;
				i++;
			}

			if (!entry)
				return;

			line = getLineByType(LT_TITLE, entry->lines);

			if (line && line->numElements >= 2)
				fprintf(out, "%sdefault%s%s\n", indent,
					separator, line->elements[1].item);
			else if (line && (line->numElements == 1)
				 && cfg->cfi->titleBracketed) {
				char *title = extractTitle(cfg, line);
				if (title) {
					fprintf(out, "%sdefault%s%s\n", indent,
						separator, title);
					free(title);
				}
			}
		}
	}
}

static int writeConfig(struct grubConfig *cfg, char *outName,
		       const char *prefix)
{
	FILE *out;
	struct singleLine *line;
	struct singleEntry *entry;
	char *tmpOutName;
	int needs = MAIN_DEFAULT;
	struct stat sb;
	int i;
	int rc = 0;

	if (!strcmp(outName, "-")) {
		out = stdout;
		tmpOutName = NULL;
	} else {
		if (!lstat(outName, &sb) && S_ISLNK(sb.st_mode)) {
			char *buf;
			int len = 256;
			int rc;

			/* most likely the symlink is relative, so change our
			   directory to the dir of the symlink */
			char *dir = strdupa(outName);
			rc = chdir(dirname(dir));
			do {
				buf = alloca(len + 1);
				rc = readlink(basename(outName), buf, len);
				if (rc == len)
					len += 256;
			} while (rc == len);

			if (rc < 0) {
				fprintf(stderr,
					_
					("grubby: error readlink link %s: %s\n"),
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
			fprintf(stderr, _("grubby: error creating %s: %s\n"),
				tmpOutName, strerror(errno));
			return 1;
		}

		if (!stat(outName, &sb)) {
			if (chmod(tmpOutName, sb.st_mode & ~(S_IFMT))) {
				fprintf(stderr,
					_
					("grubby: error setting perms on %s: %s\n"),
					tmpOutName, strerror(errno));
				fclose(out);
				unlink(tmpOutName);
				return 1;
			}
		}
	}

	line = cfg->theLines;
	struct keywordTypes *defaultKw = getKeywordByType(LT_DEFAULT, cfg->cfi);
	while (line) {
		if (line->type == LT_SET_VARIABLE && defaultKw &&
		    line->numElements == 3 &&
		    !strcmp(line->elements[1].item, defaultKw->key) &&
		    !is_special_grub2_variable(line->elements[2].item)) {
			writeDefault(out, line->indent,
				     line->elements[0].indent, cfg);
			needs &= ~MAIN_DEFAULT;
		} else if (line->type == LT_DEFAULT) {
			writeDefault(out, line->indent,
				     line->elements[0].indent, cfg);
			needs &= ~MAIN_DEFAULT;
		} else if (line->type == LT_FALLBACK) {
			if (cfg->fallbackImage > -1)
				fprintf(out, "%s%s%s%d\n", line->indent,
					line->elements[0].item,
					line->elements[0].indent,
					cfg->fallbackImage);
		} else {
			if (lineWrite(out, line, cfg->cfi) == -1) {
				fprintf(stderr,
					_("grubby: error writing %s: %s\n"),
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
		if (entry->skip)
			continue;

		line = entry->lines;
		while (line) {
			if (lineWrite(out, line, cfg->cfi) == -1) {
				fprintf(stderr,
					_("grubby: error writing %s: %s\n"),
					tmpOutName, strerror(errno));
				fclose(out);
				unlink(tmpOutName);
				return 1;
			}
			line = line->next;
		}
	}

	if (tmpOutName) {
		/* write userspace buffers */
		if (fflush(out))
			rc = 1;

		/* purge the write-back cache with fsync() */
		if (fsync(fileno(out)))
			rc = 1;

		if (fclose(out))
			rc = 1;

		if (rc == 0 && rename(tmpOutName, outName)) {
			unlink(tmpOutName);
			rc = 1;
		}

		/* fsync() the destination directory after rename */
		if (rc == 0) {
			int dirfd;

			dirfd = open(dirname(strdupa(outName)), O_RDONLY);
			if (dirfd < 0)
				rc = 1;
			else if (fsync(dirfd))
				rc = 1;

			if (dirfd >= 0)
				close(dirfd);
		}

		if (rc == 1)
			fprintf(stderr,
				_("grubby: error flushing data: %m\n"));
	}

	return rc;
}

static int numEntries(struct grubConfig *cfg)
{
	int i = 0;
	struct singleEntry *entry;

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
	char *buf;
	char *devname;
	char *chptr;
	int rc;
	int buf_i = 0;
	int ninters = 0;

	buf = (char *)malloc(MTAB_FILE_BUF_SIZE);
	if (!buf) {
		fprintf(stderr, "grubby: failed to allocate file buffer for %s\n", _PATH_MOUNTED);
		return NULL;
	}

	if ((fd = open(_PATH_MOUNTED, O_RDONLY)) < 0) {
		fprintf(stderr, "grubby: failed to open %s: %s\n",
			_PATH_MOUNTED, strerror(errno));
		free(buf);
		return NULL;
	}
	while ((rc = read(fd, buf + buf_i, MTAB_FILE_BUF_SIZE - buf_i - 1 ))) {
		if (rc > 0) {
			buf_i += rc;
			continue;
		}
		if ((errno == EAGAIN || errno == EINTR) && (ninters++ < INTER_RETRY_MAX)) {
			usleep(10 * 1000);
			continue;
		}
		fprintf(stderr, "grubby: failed to read %s: %s\n",
			_PATH_MOUNTED, strerror(errno));
		free(buf);
		close(fd);
		return NULL;
	}
	close(fd);
	buf[buf_i] = '\0';
	chptr = buf;

	char *foundanswer = NULL;

	while (chptr && chptr != (buf + buf_i)) {
		devname = chptr;

		/*
		 * The first column of a mtab entry is the device, but if the
		 * entry is a special device it won't start with /, so move
		 * on to the next line.
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
			fprintf(stderr, "grubby: %s: error parsing or file size over %d bytes\n",
					_PATH_MOUNTED, MTAB_FILE_BUF_SIZE);
			free(buf);
			return NULL;
		}

		/*
		 * The second column of a mtab entry is the mount point, we
		 * are looking for '/' obviously.
		 */
		if (*(++chptr) == '/' && *(++chptr) == ' ') {
			/* remember the last / entry in mtab */
			foundanswer = devname;
		}

		/* Next line */
		chptr = strchr(chptr, '\n');
		if (chptr)
			chptr++;
	}

	/* Return the last / entry found */
	if (foundanswer) {
		chptr = strchr(foundanswer, ' ');
		*chptr = '\0';
		char *new_buf = strdup(foundanswer);
		free(buf);
		return new_buf;
	}

	fprintf(stderr, "grubby: %s: no root mount_point found or file size over %d bytes\n",
			_PATH_MOUNTED, MTAB_FILE_BUF_SIZE);
	free(buf);
	return NULL;
}

void printEntry(struct singleEntry *entry, FILE * f)
{
	int i;
	struct singleLine *line;

	for (line = entry->lines; line; line = line->next) {
		log_message(f, "DBG: %s", line->indent);
		for (i = 0; i < line->numElements; i++) {
			/* Need to handle this, because we strip the quotes from
			 * menuentry when read it. */
			if (line->type == LT_MENUENTRY && i == 1) {
				if (!isquote(*line->elements[i].item))
					log_message(f, "\'%s\'",
						    line->elements[i].item);
				else
					log_message(f, "%s",
						    line->elements[i].item);
				log_message(f, "%s", line->elements[i].indent);

				continue;
			}

			log_message(f, "%s%s",
				    line->elements[i].item,
				    line->elements[i].indent);
		}
		log_message(f, "\n");
	}
}

void notSuitablePrintf(struct singleEntry *entry, int okay, const char *fmt,
		       ...)
{
	static int once;
	va_list argp, argq;

	va_start(argp, fmt);

	va_copy(argq, argp);
	if (!once) {
		log_time(NULL);
		log_message(NULL, "command line: %s\n", saved_command_line);
	}
	log_message(NULL, "DBG: Image entry %s: ",
		    okay ? "succeeded" : "failed");
	log_vmessage(NULL, fmt, argq);

	printEntry(entry, NULL);
	va_end(argq);

	if (!debug) {
		once = 1;
		va_end(argp);
		return;
	}

	if (okay) {
		va_end(argp);
		return;
	}

	if (!once)
		log_message(stderr, "DBG: command line: %s\n",
			    saved_command_line);
	once = 1;
	fprintf(stderr, "DBG: Image entry failed: ");
	vfprintf(stderr, fmt, argp);
	printEntry(entry, stderr);
	va_end(argp);
}

#define beginswith(s, c) ((s) && (s)[0] == (c))

static int endswith(const char *s, char c)
{
	int slen;

	if (!s || !s[0])
		return 0;
	slen = strlen(s) - 1;

	return s[slen] == c;
}

typedef struct {
	const char *start;
	size_t      chars;
} field;

static int iscomma(int c)
{
	return c == ',';
}

static int isequal(int c)
{
	return c == '=';
}

static field findField(const field *in, typeof(isspace) *isdelim, field *out)
{
	field nxt = {};
	size_t off = 0;

	while (off < in->chars && isdelim(in->start[off]))
		off++;

	if (off == in->chars)
		return nxt;

	out->start = &in->start[off];
	out->chars = 0;

	while (off + out->chars < in->chars && !isdelim(out->start[out->chars]))
		out->chars++;

	nxt.start = out->start + out->chars;
	nxt.chars = in->chars - off - out->chars;
	return nxt;
}

static int fieldEquals(const field *in, const char *str)
{
	return in->chars == strlen(str) &&
		strncmp(in->start, str, in->chars) == 0;
}

/* Parse /proc/mounts to determine the subvolume prefix. */
static size_t subvolPrefix(const char *str)
{
	FILE *file = NULL;
	char *line = NULL;
	size_t prfx = 0;
	size_t size = 0;

	file = fopen(mounts, "r");
	if (!file)
		return 0;

	for (ssize_t s; (s = getline(&line, &size, file)) >= 0; ) {
		field nxt = { line, s };
		field dev = {};
		field path = {};
		field type = {};
		field opts = {};
		field opt = {};

		nxt = findField(&nxt, isspace, &dev);
		if (!nxt.start)
			continue;

		nxt = findField(&nxt, isspace, &path);
		if (!nxt.start)
			continue;

		nxt = findField(&nxt, isspace, &type);
		if (!nxt.start)
			continue;

		nxt = findField(&nxt, isspace, &opts);
		if (!nxt.start)
			continue;

		if (!fieldEquals(&type, "btrfs"))
			continue;

		/* We have found a btrfs mount point. */

		nxt = opts;
		while ((nxt = findField(&nxt, iscomma, &opt)).start) {
			field key = {};
			field val = {};

			opt = findField(&opt, isequal, &key);
			if (!opt.start)
				continue;

			opt = findField(&opt, isequal, &val);
			if (!opt.start)
				continue;

			if (!fieldEquals(&key, "subvol"))
				continue;

			/* We have found a btrfs subvolume mount point. */

			if (strncmp(val.start, str, val.chars))
				continue;

			if (val.start[val.chars - 1] != '/' &&
				str[val.chars] != '/')
				continue;

			/* The subvolume mount point matches our input. */

			if (prfx < val.chars)
				prfx = val.chars;
		}
	}

	dbgPrintf("%s(): str: '%s', prfx: '%s'\n", __FUNCTION__, str, prfx);

	fclose(file);
	free(line);
	return prfx;
}

int suitableImage(struct singleEntry *entry, const char *bootPrefix,
		  int skipRemoved, int flags)
{
	struct singleLine *line;
	char *fullName;
	int i;
	char *dev;
	size_t rs;
	char *rootdev;

	if (skipRemoved && entry->skip) {
		notSuitablePrintf(entry, 0, "marked to skip\n");
		return 0;
	}

	line =
	    getLineByType(LT_KERNEL | LT_HYPER | LT_KERNEL_EFI | LT_KERNEL_16,
			  entry->lines);
	if (!line) {
		notSuitablePrintf(entry, 0, "no line found\n");
		return 0;
	}
	if (line->numElements < 2) {
		notSuitablePrintf(entry, 0, "line has only %d elements\n",
				  line->numElements);
		return 0;
	}

	if (flags & GRUBBY_BADIMAGE_OKAY) {
		notSuitablePrintf(entry, 1, "\n");
		return 1;
	}

	fullName = alloca(strlen(bootPrefix) +
			  strlen(line->elements[1].item) + 1);
	rs = getRootSpecifier(line->elements[1].item);
	int hasslash = endswith(bootPrefix, '/') ||
	    beginswith(line->elements[1].item + rs, '/');
	sprintf(fullName, "%s%s%s", bootPrefix, hasslash ? "" : "/",
		line->elements[1].item + rs);
	if (access(fullName, R_OK)) {
		notSuitablePrintf(entry, 0, "access to %s failed\n", fullName);
		return 0;
	}
	for (i = 2; i < line->numElements; i++)
		if (!strncasecmp(line->elements[i].item, "root=", 5))
			break;
	if (i < line->numElements) {
		dev = line->elements[i].item + 5;
	} else {
		/* look for a lilo style LT_ROOT line */
		line = getLineByType(LT_ROOT, entry->lines);

		if (line && line->numElements >= 2) {
			dev = line->elements[1].item;
		} else {
			/* didn't succeed in finding a LT_ROOT, let's try
			 * LT_KERNELARGS.  grub+multiboot uses LT_MBMODULE
			 * for the args, so check that too.
			 */
			line =
			    getLineByType(LT_KERNELARGS | LT_MBMODULE,
					  entry->lines);

			/* failed to find one */
			if (!line) {
				notSuitablePrintf(entry, 0, "no line found\n");
				return 0;
			}

			for (i = 1; i < line->numElements; i++)
				if (!strncasecmp
				    (line->elements[i].item, "root=", 5))
					break;
			if (i < line->numElements)
				dev = line->elements[i].item + 5;
			else {
				notSuitablePrintf(entry, 0,
						  "no root= entry found\n");
				/* it failed too...  can't find root= */
				return 0;
			}
		}
	}

	dev = getpathbyspec(dev);
	if (!getpathbyspec(dev)) {
		notSuitablePrintf(entry, 0, "can't find blkid entry for %s\n",
				  dev);
		return 0;
	} else
		dev = getpathbyspec(dev);

	rootdev = findDiskForRoot();
	if (!rootdev) {
		notSuitablePrintf(entry, 0, "can't find root device\n");
		return 0;
	}

	if (!getuuidbydev(rootdev) || !getuuidbydev(dev)) {
		notSuitablePrintf(entry, 0,
				  "uuid missing: rootdev %s, dev %s\n",
				  getuuidbydev(rootdev), getuuidbydev(dev));
		free(rootdev);
		return 0;
	}

	if (strcmp(getuuidbydev(rootdev), getuuidbydev(dev))) {
		notSuitablePrintf(entry, 0,
				  "uuid mismatch: rootdev %s, dev %s\n",
				  getuuidbydev(rootdev), getuuidbydev(dev));
		free(rootdev);
		return 0;
	}

	free(rootdev);
	notSuitablePrintf(entry, 1, "\n");

	return 1;
}

/* returns the first match on or after the one pointed to by index (if index 
   is not NULL) which is not marked as skip */
struct singleEntry *findEntryByPath(struct grubConfig *config,
				    const char *kernel, const char *prefix,
				    int *index)
{
	struct singleEntry *entry = NULL;
	struct singleLine *line;
	int i;
	char *chptr;
	enum lineType_e checkType = LT_KERNEL;

	if (isdigit(*kernel)) {
		int *indexVars = alloca(sizeof(*indexVars) * strlen(kernel));

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
			while (i < *index) {
				i++;
				if (indexVars[i] == -1)
					return NULL;
			}
		}

		entry = findEntryByIndex(config, indexVars[i]);
		if (!entry)
			return NULL;

		line =
		    getLineByType(LT_KERNEL | LT_HYPER | LT_KERNEL_EFI |
				  LT_KERNEL_16, entry->lines);
		if (!line)
			return NULL;

		if (index)
			*index = indexVars[i];
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
			if (!entry->skip)
				break;
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
			checkType = LT_TITLE | LT_MENUENTRY;
			kernel += 6;
		}

		for (entry = findEntryByIndex(config, i); entry;
		     entry = entry->next, i++) {
			if (entry->skip)
				continue;

			dbgPrintf("findEntryByPath looking for %d %s in %p\n",
				  checkType, kernel, entry);

			/* check all the lines matching checkType */
			for (line = entry->lines; line; line = line->next) {
				enum lineType_e ct = checkType;
				if (entry->multiboot && checkType == LT_KERNEL)
					ct = LT_KERNEL | LT_KERNEL_EFI |
					    LT_MBMODULE | LT_HYPER |
					    LT_KERNEL_16;
				else if (checkType & LT_KERNEL)
					ct = checkType | LT_KERNEL_EFI |
					    LT_KERNEL_16;
				line = getLineByType(ct, line);
				if (!line)
					break;	/* not found in this entry */

				if (line && line->type != LT_MENUENTRY &&
				    line->numElements >= 2) {
					if (!strcmp(line->elements[1].item +
						getRootSpecifier(
							line->elements[1].item),
						kernel + strlen(prefix)))
						break;
				}
				if (line->type == LT_MENUENTRY &&
				    !strcmp(line->elements[1].item, kernel))
					break;
			}

			/* make sure this entry has a kernel identifier; this skips
			 * non-Linux boot entries (could find netbsd etc, though, which is
			 * unfortunate)
			 */
			if (line
			    && getLineByType(LT_KERNEL | LT_HYPER |
					     LT_KERNEL_EFI | LT_KERNEL_16,
					     entry->lines))
				break;	/* found 'im! */
		}

		if (index)
			*index = i;
	}

	return entry;
}

struct singleEntry *findEntryByTitle(struct grubConfig *cfg, char *title,
				     int *index)
{
	struct singleEntry *entry;
	struct singleLine *line;
	int i;
	char *newtitle;

	for (i = 0, entry = cfg->entries; entry; entry = entry->next, i++) {
		if (index && i < *index)
			continue;
		line = getLineByType(LT_TITLE, entry->lines);
		if (!line)
			line = getLineByType(LT_MENUENTRY, entry->lines);
		if (!line)
			continue;
		newtitle = grub2ExtractTitle(line);
		if (!newtitle)
			continue;
		if (!strcmp(title, newtitle))
			break;
	}

	if (!entry)
		return NULL;

	if (index)
		*index = i;
	return entry;
}

struct singleEntry *findEntryByIndex(struct grubConfig *cfg, int index)
{
	struct singleEntry *entry;

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
struct singleEntry *findTemplate(struct grubConfig *cfg, const char *prefix,
				 int *indexPtr, int skipRemoved, int flags)
{
	struct singleEntry *entry, *entry2;
	int index;

	if (cfg->cfi->defaultIsSaved) {
		if (cfg->cfi->getEnv) {
			char *defTitle =
			    cfg->cfi->getEnv(cfg->cfi, "saved_entry");
			if (defTitle) {
				int index = 0;
				if (isnumber(defTitle)) {
					index = atoi(defTitle);
					entry = findEntryByIndex(cfg, index);
				} else {
					entry =
					    findEntryByTitle(cfg, defTitle,
							     &index);
				}
				if (entry
				    && suitableImage(entry, prefix, skipRemoved,
						     flags)) {
					cfg->defaultImage = index;
					if (indexPtr)
						*indexPtr = index;
					return entry;
				}
			}
		}
	} else if (cfg->defaultImage >= FIRST_ENTRY_INDEX) {
		entry = findEntryByIndex(cfg, cfg->defaultImage);
		if (entry && suitableImage(entry, prefix, skipRemoved, flags)) {
			if (indexPtr)
				*indexPtr = cfg->defaultImage;
			return entry;
		}
	}

	index = 0;
	while ((entry = findEntryByIndex(cfg, index))) {
		if (suitableImage(entry, prefix, skipRemoved, flags)) {
			int j, unmodifiedIndex;

			unmodifiedIndex = index;

			for (j = 0; j < unmodifiedIndex; j++) {
				entry2 = findEntryByIndex(cfg, j);
				if (entry2->skip)
					index--;
			}
			if (indexPtr)
				*indexPtr = index;

			return entry;
		}

		index++;
	}

	fprintf(stderr,
		_("grubby fatal error: unable to find a suitable template\n"));

	return NULL;
}

char *findBootPrefix(void)
{
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

void markRemovedImage(struct grubConfig *cfg, const char *image,
		      const char *prefix)
{
	struct singleEntry *entry;

	if (!image)
		return;

	/* check and see if we're removing the default image */
	if (isdigit(*image)) {
		entry = findEntryByPath(cfg, image, prefix, NULL);
		if (entry)
			entry->skip = 1;
		return;
	}

	while ((entry = findEntryByPath(cfg, image, prefix, NULL)))
		entry->skip = 1;
}

void setDefaultImage(struct grubConfig *config, int isAddingBootEntry,
		     const char *defaultKernelPath, int newBootEntryIsDefault,
		     const char *prefix, int flags,
		     int newDefaultBootEntryIndex, int newBootEntryIndex)
{
	struct singleEntry *bootEntry, *newDefault;
	int indexToVerify, firstKernelEntryIndex, currentLookupIndex;

        /* initialize */
        currentLookupIndex = FIRST_ENTRY_INDEX;

	/* handle the two cases where the user explictly picks the default
	 * boot entry index as it would exist post-modification */

	/* Case 1: user chose to make the latest boot entry the default */
	if (newBootEntryIsDefault) {
		config->defaultImage = newBootEntryIndex;
		return;
	}

	/* Case 2: user picked an arbitrary index as the default boot entry */
	if (newDefaultBootEntryIndex >= FIRST_ENTRY_INDEX) {
		indexToVerify = newDefaultBootEntryIndex;

		/* user chose to make latest boot entry the default */
		if (newDefaultBootEntryIndex == newBootEntryIndex) {
			config->defaultImage = newBootEntryIndex;
			return;
		}

		/* the user picks the default index based on the
		 * order of the bootloader configuration after
		 * modification; ensure we are checking for the
		 * existence of the correct entry */
		if (newBootEntryIndex < newDefaultBootEntryIndex) {
			if (!config->isModified)
				indexToVerify--;
		}

		/* verify the user selected index will exist */
		if (findEntryByIndex(config, indexToVerify)) {
			config->defaultImage = newDefaultBootEntryIndex;
		} else {
			config->defaultImage = NO_DEFAULT_ENTRY;
		}

		return;
	}

	/* handle cases where the index value may shift */

	/* check validity of existing default or first-entry-found
	   selection */
	if (defaultKernelPath) {
                /* we must initialize this */
                firstKernelEntryIndex = 0;
		/* user requested first-entry-found */
		if (!findEntryByPath(config, defaultKernelPath,
				     prefix, &firstKernelEntryIndex)) {
			/* don't change default if can't find match */
			config->defaultImage = NO_DEFAULT_ENTRY;
			return;
		}

		config->defaultImage = firstKernelEntryIndex;

		/* this is where we start looking for decrement later */
		currentLookupIndex = config->defaultImage;

		if (isAddingBootEntry && !config->isModified &&
		    (newBootEntryIndex < config->defaultImage)) {
			/* increment because new entry added before default */
			config->defaultImage++;
		}
	} else {
                /* check to see if the default is stored in the environment */
                if (config->defaultImage < FIRST_ENTRY_INDEX) {
                    if (config->defaultImage == DEFAULT_SAVED || config->defaultImage == DEFAULT_SAVED_GRUB2)
                    {
                        if (config->cfi->defaultIsSaved) {
                            if (config->cfi->getEnv) {
                                char *defaultTitle = config->cfi->getEnv(config->cfi, "saved_entry");

                                if (defaultTitle) {
                                    if (isnumber(defaultTitle)) {
                                        currentLookupIndex = atoi(defaultTitle);
                                    } else {
                                        findEntryByTitle(config, defaultTitle, &currentLookupIndex);
                                    }
                                    /* set the default Image to an actual index */
                                    config->defaultImage = currentLookupIndex;
                                }
                            }
                         }
                    }
                } else {
                        /* use pre-existing default entry from the file*/
                        currentLookupIndex = config->defaultImage;
                }

		if (isAddingBootEntry
		    && (newBootEntryIndex <= config->defaultImage)) {
			config->defaultImage++;

			if (config->isModified) {
				currentLookupIndex++;
			}
		}
	}

	/* sanity check - is this entry index valid? */
	bootEntry = findEntryByIndex(config, currentLookupIndex);

	if ((bootEntry && bootEntry->skip) || !bootEntry) {
		/* entry is to be skipped or is invalid */
		if (isAddingBootEntry) {
			config->defaultImage = newBootEntryIndex;
			return;
		}
		newDefault =
		    findTemplate(config, prefix, &config->defaultImage, 1,
				 flags);
		if (!newDefault) {
			config->defaultImage = NO_DEFAULT_ENTRY;
		}

		return;
	}

	currentLookupIndex--;

	/* decrement index by the total number of entries deleted */

	for (indexToVerify = currentLookupIndex;
	     indexToVerify >= FIRST_ENTRY_INDEX; indexToVerify--) {

		bootEntry = findEntryByIndex(config, indexToVerify);

		if (bootEntry && bootEntry->skip) {
			config->defaultImage--;
		}
	}
}

void setFallbackImage(struct grubConfig *config, int hasNew)
{
	struct singleEntry *entry, *entry2;
	int j;

	if (config->fallbackImage == -1)
		return;

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
		if (entry2->skip)
			config->fallbackImage--;
	}
}

void displayEntry(struct grubConfig *config, struct singleEntry *entry, const char *prefix, int index)
{
	struct singleLine *line;
	char *root = NULL;
	int i;
	int j;

	printf("index=%d\n", index);

	line =
	    getLineByType(LT_KERNEL | LT_HYPER | LT_KERNEL_EFI | LT_KERNEL_16,
			  entry->lines);
	if (!line) {
		printf("non linux entry\n");
		return;
	}

	if (!strncmp(prefix, line->elements[1].item, strlen(prefix)))
		printf("kernel=%s\n", line->elements[1].item);
	else
		printf("kernel=%s%s\n", prefix, line->elements[1].item);

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
			char *s;

			printf("args=\"");
			i = 1;
			while (i < line->numElements) {
				if (!strncmp
				    (line->elements[i].item, "root=", 5)) {
					root = line->elements[i].item + 5;
				} else {
					s = line->elements[i].item;

					printf("%s%s", s,
					       line->elements[i].indent);
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
			root = line->elements[1].item;
	}

	if (root) {
		char *s = alloca(strlen(root) + 1);

		strcpy(s, root);
		if (s[strlen(s) - 1] == '"')
			s[strlen(s) - 1] = '\0';
		/* make sure the root doesn't have a trailing " */
		printf("root=%s\n", s);
	}

	line =
	    getLineByType(LT_INITRD | LT_INITRD_EFI | LT_INITRD_16,
			  entry->lines);

	if (line && line->numElements >= 2) {
		if (!strncmp(prefix, line->elements[1].item, strlen(prefix)))
			printf("initrd=");
		else
			printf("initrd=%s", prefix);

		for (i = 1; i < line->numElements; i++)
			printf("%s%s", line->elements[i].item,
			       line->elements[i].indent);
		printf("\n");
	}

	line = getLineByType(LT_TITLE, entry->lines);
	if (line) {
                char *entryTitle;
                /* if we can extractTitle, then it's a zipl config and
                 * if not then we go ahead with what's existed prior */
                entryTitle = extractTitle(config, line);
                if (!entryTitle) {
                    entryTitle=line->elements[1].item;
                }
		printf("title=%s\n", entryTitle);
	} else {
		char *title;
		line = getLineByType(LT_MENUENTRY, entry->lines);
		if (line) {
			title = grub2ExtractTitle(line);
			if (title)
				printf("title=%s\n", title);
		}
	}

	for (j = 0, line = entry->lines; line; line = line->next) {
		if ((line->type & LT_MBMODULE) && line->numElements >= 2) {
			if (!strncmp
			    (prefix, line->elements[1].item, strlen(prefix)))
				printf("mbmodule%d=", j);
			else
				printf("mbmodule%d=%s", j, prefix);

			for (i = 1; i < line->numElements; i++)
				printf("%s%s", line->elements[i].item,
				       line->elements[i].indent);
			printf("\n");
			j++;
		}
	}
}

int isSuseSystem(void)
{
	const char *path;
	const static char default_path[] = "/etc/SuSE-release";

	if ((path = getenv("GRUBBY_SUSE_RELEASE")) == NULL)
		path = default_path;

	if (!access(path, R_OK))
		return 1;
	return 0;
}

int isSuseGrubConf(const char *path)
{
	FILE *grubConf;
	char *line = NULL;
	size_t len = 0, res = 0;

	grubConf = fopen(path, "r");
	if (!grubConf) {
		dbgPrintf("Could not open SuSE configuration file '%s'\n",
			  path);
		return 0;
	}

	while ((res = getline(&line, &len, grubConf)) != -1) {
		if (!strncmp(line, "setup", 5)) {
			fclose(grubConf);
			free(line);
			return 1;
		}
	}

	dbgPrintf("SuSE configuration file '%s' does not appear to be valid\n",
		  path);

	fclose(grubConf);
	free(line);
	return 0;
}

int suseGrubConfGetLba(const char *path, int *lbaPtr)
{
	FILE *grubConf;
	char *line = NULL;
	size_t res = 0, len = 0;

	if (!path)
		return 1;
	if (!lbaPtr)
		return 1;

	grubConf = fopen(path, "r");
	if (!grubConf)
		return 1;

	while ((res = getline(&line, &len, grubConf)) != -1) {
		if (line[res - 1] == '\n')
			line[res - 1] = '\0';
		else if (len > res)
			line[res] = '\0';
		else {
			line = realloc(line, res + 1);
			line[res] = '\0';
		}

		if (!strncmp(line, "setup", 5)) {
			if (strstr(line, "--force-lba")) {
				*lbaPtr = 1;
			} else {
				*lbaPtr = 0;
			}
			dbgPrintf("lba: %i\n", *lbaPtr);
			break;
		}
	}

	free(line);
	fclose(grubConf);
	return 0;
}

int suseGrubConfGetInstallDevice(const char *path, char **devicePtr)
{
	FILE *grubConf;
	char *line = NULL;
	size_t res = 0, len = 0;
	char *lastParamPtr = NULL;
	char *secLastParamPtr = NULL;
	char installDeviceNumber = '\0';
	char *bounds = NULL;

	if (!path)
		return 1;
	if (!devicePtr)
		return 1;

	grubConf = fopen(path, "r");
	if (!grubConf)
		return 1;

	while ((res = getline(&line, &len, grubConf)) != -1) {
		if (strncmp(line, "setup", 5))
			continue;

		if (line[res - 1] == '\n')
			line[res - 1] = '\0';
		else if (len > res)
			line[res] = '\0';
		else {
			line = realloc(line, res + 1);
			line[res] = '\0';
		}

		lastParamPtr = bounds = line + res;

		/* Last parameter in grub may be an optional IMAGE_DEVICE */
		while (!isspace(*lastParamPtr))
			lastParamPtr--;
		lastParamPtr++;

		secLastParamPtr = lastParamPtr - 2;
		dbgPrintf("lastParamPtr: %s\n", lastParamPtr);

		if (lastParamPtr + 3 > bounds) {
			dbgPrintf("lastParamPtr going over boundary");
			fclose(grubConf);
			free(line);
			return 1;
		}
		if (!strncmp(lastParamPtr, "(hd", 3))
			lastParamPtr += 3;
		dbgPrintf("lastParamPtr: %c\n", *lastParamPtr);

		/*
		 * Second last parameter will decide wether last parameter is
		 * an IMAGE_DEVICE or INSTALL_DEVICE
		 */
		while (!isspace(*secLastParamPtr))
			secLastParamPtr--;
		secLastParamPtr++;

		if (secLastParamPtr + 3 > bounds) {
			dbgPrintf("secLastParamPtr going over boundary");
			fclose(grubConf);
			free(line);
			return 1;
		}
		dbgPrintf("secLastParamPtr: %s\n", secLastParamPtr);
		if (!strncmp(secLastParamPtr, "(hd", 3)) {
			secLastParamPtr += 3;
			dbgPrintf("secLastParamPtr: %c\n", *secLastParamPtr);
			installDeviceNumber = *secLastParamPtr;
		} else {
			installDeviceNumber = *lastParamPtr;
		}

		*devicePtr = malloc(6);
		snprintf(*devicePtr, 6, "(hd%c)", installDeviceNumber);
		dbgPrintf("installDeviceNumber: %c\n", installDeviceNumber);
		fclose(grubConf);
		free(line);
		return 0;
	}

	free(line);
	fclose(grubConf);
	return 1;
}

int grubGetBootFromDeviceMap(const char *device, char **bootPtr)
{
	FILE *deviceMap;
	char *line = NULL;
	size_t res = 0, len = 0;
	char *devicePtr;
	char *bounds = NULL;
	const char *path;
	const static char default_path[] = "/boot/grub/device.map";

	if (!device)
		return 1;
	if (!bootPtr)
		return 1;

	if ((path = getenv("GRUBBY_GRUB_DEVICE_MAP")) == NULL)
		path = default_path;

	dbgPrintf("opening grub device.map file from: %s\n", path);
	deviceMap = fopen(path, "r");
	if (!deviceMap)
		return 1;

	while ((res = getline(&line, &len, deviceMap)) != -1) {
		if (!strncmp(line, "#", 1))
			continue;

		if (line[res - 1] == '\n')
			line[res - 1] = '\0';
		else if (len > res)
			line[res] = '\0';
		else {
			line = realloc(line, res + 1);
			line[res] = '\0';
		}

		devicePtr = line;
		bounds = line + res;

		while ((isspace(*line) && ((devicePtr + 1) <= bounds)))
			devicePtr++;
		dbgPrintf("device: %s\n", devicePtr);

		if (!strncmp(devicePtr, device, strlen(device))) {
			devicePtr += strlen(device);
			while (isspace(*devicePtr)
			       && ((devicePtr + 1) <= bounds))
				devicePtr++;

			*bootPtr = strdup(devicePtr);
			break;
		}
	}

	free(line);
	fclose(deviceMap);
	return 0;
}

int suseGrubConfGetBoot(const char *path, char **bootPtr)
{
	char *grubDevice = NULL;

	if (suseGrubConfGetInstallDevice(path, &grubDevice))
		dbgPrintf("error looking for grub installation device\n");
	else
		dbgPrintf("grubby installation device: %s\n", grubDevice);

	if (grubGetBootFromDeviceMap(grubDevice, bootPtr))
		dbgPrintf("error looking for grub boot device\n");
	else
		dbgPrintf("grubby boot device: %s\n", *bootPtr);

	free(grubDevice);
	return 0;
}

int parseSuseGrubConf(int *lbaPtr, char **bootPtr)
{
	/*
	 * This SuSE grub configuration file at this location is not your
	 * average grub configuration file, but instead the grub commands
	 * used to setup grub on that system.
	 */
	const char *path;
	const static char default_path[] = "/etc/grub.conf";

	if ((path = getenv("GRUBBY_SUSE_GRUB_CONF")) == NULL)
		path = default_path;

	if (!isSuseGrubConf(path))
		return 1;

	if (lbaPtr) {
		*lbaPtr = 0;
		if (suseGrubConfGetLba(path, lbaPtr))
			return 1;
	}

	if (bootPtr) {
		*bootPtr = NULL;
		suseGrubConfGetBoot(path, bootPtr);
	}

	return 0;
}

int parseSysconfigGrub(int *lbaPtr, char **bootPtr)
{
	FILE *in;
	char buf[1024];
	char *chptr;
	char *start;
	char *param;

	in = fopen("/etc/sysconfig/grub", "r");
	if (!in)
		return 1;

	if (lbaPtr)
		*lbaPtr = 0;
	if (bootPtr)
		*bootPtr = NULL;

	while (fgets(buf, sizeof(buf), in)) {
		start = buf;
		while (isspace(*start))
			start++;
		if (*start == '#')
			continue;

		chptr = strchr(start, '=');
		if (!chptr)
			continue;
		chptr--;
		while (*chptr && isspace(*chptr))
			chptr--;
		chptr++;
		*chptr = '\0';

		param = chptr + 1;
		while (*param && isspace(*param))
			param++;
		if (*param == '=') {
			param++;
			while (*param && isspace(*param))
				param++;
		}

		chptr = param;
		while (*chptr && !isspace(*chptr))
			chptr++;
		*chptr = '\0';

		if (!strcmp(start, "forcelba") && !strcmp(param, "1") && lbaPtr)
			*lbaPtr = 1;
		else if (!strcmp(start, "boot") && bootPtr)
			*bootPtr = strdup(param);
	}

	fclose(in);

	return 0;
}

void dumpSysconfigGrub(void)
{
	char *boot = NULL;
	int lba;

	if (isSuseSystem()) {
		if (parseSuseGrubConf(&lba, &boot)) {
			free(boot);
			return;
		}
	} else {
		if (parseSysconfigGrub(&lba, &boot)) {
			free(boot);
			return;
		}
	}

	if (lba)
		printf("lba\n");
	if (boot) {
		printf("boot=%s\n", boot);
		free(boot);
	}
}

int displayInfo(struct grubConfig *config, char *kernel, const char *prefix)
{
	int i = 0;
	struct singleEntry *entry;
	struct singleLine *line;

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
		if (line)
			printf("lba\n");
	}

	displayEntry(config, entry, prefix, i);

	i++;
	while ((entry = findEntryByPath(config, kernel, prefix, &i))) {
		displayEntry(config, entry, prefix, i);
		i++;
	}

	return 0;
}

struct singleLine *addLineTmpl(struct singleEntry *entry,
			       struct singleLine *tmplLine,
			       struct singleLine *prevLine,
			       const char *val, struct configFileInfo *cfi)
{
	struct singleLine *newLine = lineDup(tmplLine);

	if (isEfi && cfi == &grub2ConfigType) {
		enum lineType_e old = newLine->type;
		newLine->type = preferredLineType(newLine->type, cfi);
		if (old != newLine->type)
			newLine->elements[0].item =
			    getKeyByType(newLine->type, cfi);
	}

	if (val) {
		/* override the inherited value with our own.
		 * This is a little weak because it only applies to elements[1]
		 */
		if (newLine->numElements > 1)
			removeElement(newLine, 1);
		insertElement(newLine, val, 1, cfi);

		/* but try to keep the rootspec from the template... sigh */
		if (tmplLine->
		    type & (LT_HYPER | LT_KERNEL | LT_MBMODULE | LT_INITRD |
			    LT_KERNEL_EFI | LT_INITRD_EFI | LT_KERNEL_16 |
			    LT_INITRD_16)) {
			const char *prfx = tmplLine->elements[1].item;
			size_t rs = getRootSpecifier(prfx);
			if (isinitrd(tmplLine->type)) {
				for (struct singleLine *l = entry->lines;
				     rs == 0 && l; l = l->next) {
					if (iskernel(l->type)) {
						prfx = l->elements[1].item;
						rs = getRootSpecifier(prfx);
						break;
					}
				}
			}
			if (rs > 0) {
				free(newLine->elements[1].item);
				newLine->elements[1].item = sdupprintf(
					"%.*s%s", (int) rs, prfx, val);
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
struct singleLine *addLine(struct singleEntry *entry,
			   struct configFileInfo *cfi,
			   enum lineType_e type, char *defaultIndent,
			   const char *val)
{
	struct singleLine *line, *prev;
	struct keywordTypes *kw;
	struct singleLine tmpl;

	/* NB: This function shouldn't allocate items on the heap, rather on
	 * the stack since it calls addLineTmpl which will make copies.
	 */
	if (type == LT_TITLE && cfi->titleBracketed) {
		/* we're doing a bracketed title (zipl) */
		tmpl.type = type;
		tmpl.numElements = 1;
		tmpl.elements = alloca(sizeof(*tmpl.elements));
		tmpl.elements[0].item = alloca(strlen(val) + 3);
		sprintf(tmpl.elements[0].item, "[%s]", val);
		tmpl.elements[0].indent = "";
		val = NULL;
	} else if (type == LT_MENUENTRY) {
		char *lineend = "--class gnu-linux --class gnu --class os {";
		if (!val) {
			fprintf(stderr,
				"Line type LT_MENUENTRY requires a value\n");
			abort();
		}
		kw = getKeywordByType(type, cfi);
		if (!kw) {
			fprintf(stderr,
				"Looking up keyword for unknown type %d\n",
				type);
			abort();
		}
		tmpl.indent = "";
		tmpl.type = type;
		tmpl.numElements = 3;
		tmpl.elements =
		    alloca(sizeof(*tmpl.elements) * tmpl.numElements);
		tmpl.elements[0].item = kw->key;
		tmpl.elements[0].indent = alloca(2);
		sprintf(tmpl.elements[0].indent, "%c", kw->nextChar);
		tmpl.elements[1].item = (char *)val;
		tmpl.elements[1].indent = alloca(2);
		sprintf(tmpl.elements[1].indent, "%c", kw->nextChar);
		tmpl.elements[2].item = alloca(strlen(lineend) + 1);
		strcpy(tmpl.elements[2].item, lineend);
		tmpl.elements[2].indent = "";
	} else {
		kw = getKeywordByType(type, cfi);
		if (!kw) {
			fprintf(stderr,
				"Looking up keyword for unknown type %d\n",
				type);
			abort();
		}
		tmpl.type = type;
		tmpl.numElements = val ? 2 : 1;
		tmpl.elements =
		    alloca(sizeof(*tmpl.elements) * tmpl.numElements);
		tmpl.elements[0].item = kw->key;
		tmpl.elements[0].indent = alloca(2);
		sprintf(tmpl.elements[0].indent, "%c", kw->nextChar);
		if (val) {
			tmpl.elements[1].item = (char *)val;
			tmpl.elements[1].indent = "";
		}
	}

	/* The last non-empty line gives us the indention to us and the line
	 * to insert after. Note that comments are considered empty lines,
	 * which may not be ideal? If there are no lines or we are looking at
	 * the first line, we use defaultIndent (the first line is normally
	 * indented differently from the rest) */
	for (line = entry->lines, prev = NULL; line; line = line->next) {
		if (line->numElements)
			prev = line;
		/* fall back on the last line if prev isn't otherwise set */
		if (!line->next && !prev)
			prev = line;
	}

	struct singleLine *menuEntry;
	menuEntry = getLineByType(LT_MENUENTRY, entry->lines);
	if (tmpl.type == LT_ENTRY_END) {
		if (menuEntry)
			tmpl.indent = menuEntry->indent;
		else
			tmpl.indent = defaultIndent ? : "";
	} else if (tmpl.type != LT_MENUENTRY) {
		if (menuEntry)
			tmpl.indent = "\t";
		else if (prev == entry->lines)
			tmpl.indent = defaultIndent ? : "";
		else
			tmpl.indent = prev->indent;
	}

	return addLineTmpl(entry, &tmpl, prev, val, cfi);
}

void removeLine(struct singleEntry *entry, struct singleLine *line)
{
	struct singleLine *prev;
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
		while (prev->next != line)
			prev = prev->next;
		prev->next = line->next;
	}

	free(line);
}

static void requote(struct singleLine *tmplLine, struct configFileInfo *cfi)
{
	struct singleLine newLine = {
		.indent = tmplLine->indent,
		.type = tmplLine->type,
		.next = tmplLine->next,
	};
	int firstQuotedItem = -1;
	int quoteLen = 0;
	int j;
	int element = 0;
	char *c;

	c = malloc(strlen(tmplLine->elements[0].item) + 1);
	strcpy(c, tmplLine->elements[0].item);
	insertElement(&newLine, c, element++, cfi);
	free(c);
	c = NULL;

	for (j = 1; j < tmplLine->numElements; j++) {
		if (firstQuotedItem == -1) {
			quoteLen += strlen(tmplLine->elements[j].item);

			if (isquote(tmplLine->elements[j].item[0])) {
				firstQuotedItem = j;
				quoteLen +=
				    strlen(tmplLine->elements[j].indent);
			} else {
				c = malloc(quoteLen + 1);
				strcpy(c, tmplLine->elements[j].item);
				insertElement(&newLine, c, element++, cfi);
				free(c);
				quoteLen = 0;
			}
		} else {
			int itemlen = strlen(tmplLine->elements[j].item);
			quoteLen += itemlen;
			quoteLen += strlen(tmplLine->elements[j].indent);

			if (isquote(tmplLine->elements[j].item[itemlen - 1])) {
				c = malloc(quoteLen + 1);
				c[0] = '\0';
				for (int i = firstQuotedItem; i < j + 1; i++) {
					strcat(c, tmplLine->elements[i].item);
					strcat(c, tmplLine->elements[i].indent);
				}
				insertElement(&newLine, c, element++, cfi);
				free(c);

				firstQuotedItem = -1;
				quoteLen = 0;
			}
		}
	}
	while (tmplLine->numElements)
		removeElement(tmplLine, 0);
	if (tmplLine->elements)
		free(tmplLine->elements);

	tmplLine->numElements = newLine.numElements;
	tmplLine->elements = newLine.elements;
}

static void insertElement(struct singleLine *line,
			  const char *item, int insertHere,
			  struct configFileInfo *cfi)
{
	struct keywordTypes *kw;
	char indent[2] = "";

	/* sanity check */
	if (insertHere > line->numElements) {
		dbgPrintf
		    ("insertElement() adjusting insertHere from %d to %d\n",
		     insertHere, line->numElements);
		insertHere = line->numElements;
	}

	line->elements = realloc(line->elements, (line->numElements + 1) *
				 sizeof(*line->elements));
	memmove(&line->elements[insertHere + 1],
		&line->elements[insertHere],
		(line->numElements - insertHere) * sizeof(*line->elements));
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

	if (insertHere > 0 && line->elements[insertHere - 1].indent[0] == '\0') {
		/* move the end-of-line forward */
		line->elements[insertHere].indent =
		    line->elements[insertHere - 1].indent;
		line->elements[insertHere - 1].indent = strdup(indent);
	} else {
		line->elements[insertHere].indent = strdup(indent);
	}

	line->numElements++;

	dbgPrintf("insertElement(%s, '%s%s', %d)\n",
		  line->elements[0].item,
		  line->elements[insertHere].item,
		  line->elements[insertHere].indent, insertHere);
}

static void removeElement(struct singleLine *line, int removeHere)
{
	int i;

	/* sanity check */
	if (removeHere >= line->numElements)
		return;

	dbgPrintf("removeElement(%s, %d:%s)\n", line->elements[0].item,
		  removeHere, line->elements[removeHere].item);

	free(line->elements[removeHere].item);

	if (removeHere > 1) {
		/* previous argument gets this argument's post-indentation */
		free(line->elements[removeHere - 1].indent);
		line->elements[removeHere - 1].indent =
		    line->elements[removeHere].indent;
	} else {
		free(line->elements[removeHere].indent);
	}

	/* now collapse the array, but don't bother to realloc smaller */
	for (i = removeHere; i < line->numElements - 1; i++)
		line->elements[i] = line->elements[i + 1];

	line->numElements--;
}

static int argNameMatch(const char *one, const char *two)
{
	char *first, *second;
	char *chptra, *chptrb;
	int rc;

	first = strcpy(alloca(strlen(one) + 1), one);
	second = strcpy(alloca(strlen(two) + 1), two);

	chptra = strchr(first, '=');
	if (chptra)
		*chptra = '\0';

	chptrb = strchr(second, '=');
	if (chptrb)
		*chptrb = '\0';

	rc = strcmp(first, second);

	if (chptra)
		*chptra = '=';
	if (chptrb)
		*chptrb = '=';

	return rc;
}

static int argHasValue(const char *arg)
{
	char *chptr;

	chptr = strchr(arg, '=');
	if (chptr)
		return 1;
	return 0;
}

static int argValueMatch(const char *one, const char *two)
{
	char *first, *second;
	char *chptra, *chptrb;

	first = strcpy(alloca(strlen(one) + 1), one);
	second = strcpy(alloca(strlen(two) + 1), two);

	chptra = strchr(first, '=');
	if (chptra)
		chptra += 1;

	chptrb = strchr(second, '=');
	if (chptrb)
		chptrb += 1;

	if (!chptra && !chptrb)
		return 0;
	else if (!chptra && chptrb)
		return *chptrb - 0;
	else if (!chptrb && chptra)
		return 0 - *chptra;
	else
		return strcmp(chptra, chptrb);
}

int updateActualImage(struct grubConfig *cfg, const char *image,
		      const char *prefix, const char *addArgs,
		      const char *removeArgs, int multibootArgs)
{
	struct singleEntry *entry;
	struct singleLine *line, *rootLine;
	int index = 0;
	int i, k;
	const char **newArgs, **oldArgs;
	const char **arg;
	int useKernelArgs, useRoot;
	int firstElement;
	int *usedElements;
	int doreplace;

	if (!image)
		return 0;

	if (!addArgs) {
		newArgs = malloc(sizeof(*newArgs));
		*newArgs = NULL;
	} else {
		if (poptParseArgvString(addArgs, NULL, &newArgs)) {
			fprintf(stderr,
				_("grubby: error separating arguments '%s'\n"),
				addArgs);
			return 1;
		}
	}

	if (!removeArgs) {
		oldArgs = malloc(sizeof(*oldArgs));
		*oldArgs = NULL;
	} else {
		if (poptParseArgvString(removeArgs, NULL, &oldArgs)) {
			fprintf(stderr,
				_("grubby: error separating arguments '%s'\n"),
				removeArgs);
			free(newArgs);
			return 1;
		}
	}

	useKernelArgs = (getKeywordByType(LT_KERNELARGS, cfg->cfi)
			 && (!multibootArgs || cfg->cfi->mbConcatArgs));

	useRoot = (getKeywordByType(LT_ROOT, cfg->cfi)
		   && !multibootArgs);

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
			line =
			    getLineByType(LT_KERNEL | LT_MBMODULE |
					  LT_KERNEL_EFI | LT_KERNEL_16,
					  entry->lines);
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
				insertElement(line, "--", firstElement,
					      cfg->cfi);
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
				while (strcmp
				       (line->elements[firstElement].item,
					"--"))
					removeElement(line, firstElement);
				/* remove -- */
				removeElement(line, firstElement);
			}
		}

		usedElements = calloc(line->numElements, sizeof(*usedElements));

		for (k = 0, arg = newArgs; *arg; arg++, k++) {

			doreplace = 1;
			for (i = firstElement; i < line->numElements; i++) {
				if (multibootArgs && cfg->cfi->mbConcatArgs &&
				    !strcmp(line->elements[i].item, "--")) {
					/* reached the end of hyper args, insert here */
					doreplace = 0;
					break;
				}
				if (usedElements[i])
					continue;
				if (!argNameMatch(line->elements[i].item, *arg)) {
					usedElements[i] = 1;
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
					rootLine->elements[1].item =
					    strdup(*arg + 5);
				} else {
					rootLine =
					    addLine(entry, cfg->cfi, LT_ROOT,
						    cfg->secondaryIndent,
						    *arg + 5);
				}
			}

			else {
				/* insert/append */
				insertElement(line, *arg, i, cfg->cfi);
				usedElements =
				    realloc(usedElements,
					    line->numElements *
					    sizeof(*usedElements));
				memmove(&usedElements[i + 1], &usedElements[i],
					line->numElements - i - 1);
				usedElements[i] = 1;

				/* if we updated a root= here even though
				 * there is a LT_ROOT available we need to
				 * remove the LT_ROOT entry (this will happen
				 * if we switch from a device to a label) */
				if (useRoot && !strncmp(*arg, "root=", 5)) {
					rootLine =
					    getLineByType(LT_ROOT,
							  entry->lines);
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
				if (!argNameMatch(line->elements[i].item, *arg)) {
					if (!argHasValue(*arg) ||
					    !argValueMatch(line->elements[i].item, *arg)) {
						removeElement(line, i);
						break;
					}
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

	free(newArgs);
	free(oldArgs);

	return 0;
}

int updateImage(struct grubConfig *cfg, const char *image,
		const char *prefix, const char *addArgs,
		const char *removeArgs,
		const char *addMBArgs, const char *removeMBArgs)
{
	int rc = 0;

	if (!image)
		return rc;

	/* update the main args first... */
	if (addArgs || removeArgs)
		rc = updateActualImage(cfg, image, prefix, addArgs, removeArgs,
				       0);
	if (rc)
		return rc;

	/* and now any multiboot args */
	if (addMBArgs || removeMBArgs)
		rc = updateActualImage(cfg, image, prefix, addMBArgs,
				       removeMBArgs, 1);
	return rc;
}

int addMBInitrd(struct grubConfig *cfg, const char *newMBKernel,
		const char *image, const char *prefix, const char *initrd,
		const char *title)
{
	struct singleEntry *entry;
	struct singleLine *line, *kernelLine, *endLine = NULL;
	int index = 0;

	if (!image)
		return 0;

	for (; (entry = findEntryByPath(cfg, image, prefix, &index)); index++) {
		kernelLine = getLineByType(LT_MBMODULE, entry->lines);
		if (!kernelLine)
			continue;

		/* if title is supplied, the entry's title must match it. */
		if (title) {
			char *linetitle;

			line =
			    getLineByType(LT_TITLE | LT_MENUENTRY,
					  entry->lines);
			if (!line)
				continue;

			linetitle = extractTitle(cfg, line);
			if (!linetitle)
				continue;
			if (strcmp(title, linetitle)) {
				free(linetitle);
				continue;
			}
			free(linetitle);
		}

		if (prefix) {
			int prefixLen = strlen(prefix);
			if (!strncmp(initrd, prefix, prefixLen))
				initrd += prefixLen;
		}
		endLine = getLineByType(LT_ENTRY_END, entry->lines);
		if (endLine)
			removeLine(entry, endLine);
		line =
		    addLine(entry, cfg->cfi,
			    preferredLineType(LT_MBMODULE, cfg->cfi),
			    kernelLine->indent, initrd);
		if (!line)
			return 1;
		if (endLine) {
			line = addLine(entry, cfg->cfi, LT_ENTRY_END, "", NULL);
			if (!line)
				return 1;
		}

		break;
	}

	return 0;
}

int updateInitrd(struct grubConfig *cfg, const char *image,
		 const char *prefix, const char *initrd, const char *title)
{
	struct singleEntry *entry;
	struct singleLine *line, *kernelLine, *endLine = NULL;
	int index = 0;

	if (!image)
		return 0;

	for (; (entry = findEntryByPath(cfg, image, prefix, &index)); index++) {
		kernelLine =
		    getLineByType(LT_KERNEL | LT_KERNEL_EFI | LT_KERNEL_16,
				  entry->lines);
		if (!kernelLine)
			continue;

		/* if title is supplied, the entry's title must match it. */
		if (title) {
			char *linetitle;

			line =
			    getLineByType(LT_TITLE | LT_MENUENTRY,
					  entry->lines);
			if (!line)
				continue;

			linetitle = extractTitle(cfg, line);
			if (!linetitle)
				continue;
			if (strcmp(title, linetitle)) {
				free(linetitle);
				continue;
			}
			free(linetitle);
		}

		line =
		    getLineByType(LT_INITRD | LT_INITRD_EFI | LT_INITRD_16,
				  entry->lines);
		if (line)
			removeLine(entry, line);
		if (prefix) {
			int prefixLen = strlen(prefix);
			if (!strncmp(initrd, prefix, prefixLen))
				initrd += prefixLen;
		}
		endLine = getLineByType(LT_ENTRY_END, entry->lines);
		if (endLine)
			removeLine(entry, endLine);
		enum lineType_e lt;
		switch (kernelLine->type) {
		case LT_KERNEL:
			lt = LT_INITRD;
			break;
		case LT_KERNEL_EFI:
			lt = LT_INITRD_EFI;
			break;
		case LT_KERNEL_16:
			lt = LT_INITRD_16;
			break;
		default:
			lt = preferredLineType(LT_INITRD, cfg->cfi);
		}
		line = addLine(entry, cfg->cfi, lt, kernelLine->indent, initrd);
		if (!line)
			return 1;
		if (endLine) {
			line = addLine(entry, cfg->cfi, LT_ENTRY_END, "", NULL);
			if (!line)
				return 1;
		}

		break;
	}

	return 0;
}

int checkDeviceBootloader(const char *device, const unsigned char *boot)
{
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

	if (boot[1] == JMP_SHORT_OPCODE) {
		offset = boot[2] + 2;
	} else if (boot[1] == 0xe8 || boot[1] == 0xe9) {
		offset = (boot[3] << 8) + boot[2] + 2;
	} else if (boot[0] == JMP_SHORT_OPCODE) {
		offset = boot[1] + 2;
		/*
		 * it looks like grub, when copying stage1 into the mbr,
		 * patches stage1 right after the JMP location, replacing
		 * other instructions such as JMPs for NOOPs. So, relax the
		 * check a little bit by skipping those different bytes.
		 */
		if ((bootSect[offset + 1] == NOOP_OPCODE)
		    && (bootSect[offset + 2] == NOOP_OPCODE)) {
			offset = offset + 3;
		}
	} else if (boot[0] == 0xe8 || boot[0] == 0xe9) {
		offset = (boot[2] << 8) + boot[1] + 2;
	} else {
		return 0;
	}

	if (memcmp(boot + offset, bootSect + offset, CODE_SEG_SIZE))
		return 0;

	return 2;
}

int checkLiloOnRaid(char *mdDev, const unsigned char *boot)
{
	int fd;
	char buf[65536];
	char *end;
	char *chptr;
	char *chptr2;
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
		if (!end)
			break;
		*end = '\0';

		if (!strncmp(chptr, mdDev, strlen(mdDev)) &&
		    chptr[strlen(mdDev)] == ' ') {

			/* found the device */
			while (*chptr && *chptr != ':')
				chptr++;
			chptr++;
			while (*chptr && isspace(*chptr))
				chptr++;

			/* skip the "active" bit */
			while (*chptr && !isspace(*chptr))
				chptr++;
			while (*chptr && isspace(*chptr))
				chptr++;

			/* skip the raid level */
			while (*chptr && !isspace(*chptr))
				chptr++;
			while (*chptr && isspace(*chptr))
				chptr++;

			/* everything else is partition stuff */
			while (*chptr) {
				chptr2 = chptr;
				while (*chptr2 && *chptr2 != '[')
					chptr2++;
				if (!*chptr2)
					break;

				/* yank off the numbers at the end */
				chptr2--;
				while (isdigit(*chptr2) && chptr2 > chptr)
					chptr2--;
				chptr2++;
				*chptr2 = '\0';

				/* Better, now we need the /dev/ back. We're
				 * done with everything before this point, so
				 * we can just put the /dev/ part there.
				 * There will always be room. */
				memcpy(chptr - 5, "/dev/", 5);
				rc = checkDeviceBootloader(chptr - 5, boot);
				if (rc != 2) {
					return rc;
				}

				chptr = chptr2 + 1;
				/* skip the [11] bit */
				while (*chptr && !isspace(*chptr))
					chptr++;
				/* and move to the next one */
				while (*chptr && isspace(*chptr))
					chptr++;
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

int checkForLilo(struct grubConfig *config)
{
	int fd;
	unsigned char boot[512];
	struct singleLine *line;

	for (line = config->theLines; line; line = line->next)
		if (line->type == LT_BOOT)
			break;

	if (!line) {
		fprintf(stderr,
			_
			("grubby: no boot line found in lilo configuration\n"));
		return 1;
	}

	if (line->numElements != 2)
		return 1;

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

int checkForGrub2(struct grubConfig *config)
{
	if (!access("/etc/grub.d/", R_OK))
		return 2;

	return 1;
}

int checkForGrub(struct grubConfig *config)
{
	int fd;
	unsigned char bootSect[512];
	char *boot;
	int onSuse = isSuseSystem();

	if (onSuse) {
		if (parseSuseGrubConf(NULL, &boot))
			return 0;
	} else {
		if (parseSysconfigGrub(NULL, &boot))
			return 0;
	}

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

	/* The more elaborate checks do not work on SuSE. The checks done
	 * seem to be reasonble (at least for now), so just return success
	 */
	if (onSuse)
		return 2;

	return checkDeviceBootloader(boot, bootSect);
}

int checkForExtLinux(struct grubConfig *config)
{
	int fd;
	unsigned char bootSect[512];
	char *boot;
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

int checkForYaboot(struct grubConfig *config)
{
	/*
	 * This is a simplistic check that we consider good enough for own puporses
	 *
	 * If we were to properly check if yaboot is *installed* we'd need to:
	 * 1) get the system boot device (LT_BOOT)
	 * 2) considering it's a raw filesystem, check if the yaboot binary matches
	 *    the content on the boot device
	 * 3) if not, copy the binary to a temporary file and run "addnote" on it
	 * 4) check again if binary and boot device contents match
	 */
	if (!access("/etc/yaboot.conf", R_OK))
		return 2;

	return 1;
}

int checkForElilo(struct grubConfig *config)
{
	if (!access("/etc/elilo.conf", R_OK))
		return 2;

	return 1;
}

static size_t getRootSpecifier(const char *str)
{
	size_t rs = 0;

	if (*str == '(') {
		for (; str[rs] != ')' && !isspace(str[rs]); rs++) {
			if (!str[rs])
				return rs;
		}
		rs++;
	}

	return rs + subvolPrefix(str + rs);
}

static char *getInitrdVal(struct grubConfig *config,
			  const char *prefix, struct singleLine *tmplLine,
			  const char *newKernelInitrd,
			  const char **extraInitrds, int extraInitrdCount)
{
	char *initrdVal, *end;
	int i;
	size_t totalSize;
	size_t prefixLen;
	char separatorChar;

	prefixLen = strlen(prefix);
	totalSize = strlen(newKernelInitrd) - prefixLen + 1 /* \0 */ ;

	for (i = 0; i < extraInitrdCount; i++) {
		totalSize += sizeof(separatorChar);
		totalSize += strlen(extraInitrds[i]) - prefixLen;
	}

	initrdVal = end = malloc(totalSize);

	end = stpcpy(end, newKernelInitrd + prefixLen);

	separatorChar = getKeywordByType(LT_INITRD, config->cfi)->separatorChar;
	for (i = 0; i < extraInitrdCount; i++) {
		const char *extraInitrd;
		int j;

		extraInitrd = extraInitrds[i] + prefixLen;
		/* Don't add entries that are already there */
		if (tmplLine != NULL) {
			for (j = 2; j < tmplLine->numElements; j++)
				if (strcmp
				    (extraInitrd,
				     tmplLine->elements[j].item) == 0)
					break;

			if (j != tmplLine->numElements)
				continue;
		}

		*end++ = separatorChar;
		end = stpcpy(end, extraInitrd);
	}

	return initrdVal;
}

int addNewKernel(struct grubConfig *config, struct singleEntry *template,
		 const char *prefix,
		 const char *newKernelPath, const char *newKernelTitle,
		 const char *newKernelArgs, const char *newKernelInitrd,
		 const char **extraInitrds, int extraInitrdCount,
		 const char *newMBKernel, const char *newMBKernelArgs,
		 const char *newDevTreePath, int newIndex)
{
	struct singleEntry *new, *entry, *prev = NULL;
	struct singleLine *newLine = NULL, *tmplLine = NULL, *masterLine = NULL;
	int needs;
	char *indexs;
	char *chptr;
	int rc;

	if (!newKernelPath)
		return 0;

	rc = asprintf(&indexs, "%d", newIndex);
	if (rc < 0)
		return 1;

	/* if the newKernelTitle is too long silently munge it into something
	 * we can live with. truncating is first check, then we'll just mess with
	 * it until it looks better */
	if (config->cfi->maxTitleLength &&
	    (strlen(newKernelTitle) > config->cfi->maxTitleLength)) {
		char *buf = alloca(config->cfi->maxTitleLength + 7);
		char *numBuf = alloca(config->cfi->maxTitleLength + 1);
		int i = 1;

		sprintf(buf, "TITLE=%.*s", config->cfi->maxTitleLength,
			newKernelTitle);
		while (findEntryByPath(config, buf, NULL, NULL)) {
			sprintf(numBuf, "%d", i++);
			strcpy(buf + strlen(buf) - strlen(numBuf), numBuf);
		}

		newKernelTitle = buf + 6;
	}

	new = malloc(sizeof(*new));
	new->skip = 0;
	new->multiboot = 0;
	new->lines = NULL;
	entry = config->entries;
	for (unsigned int i = 0; i < newIndex; i++) {
		if (!entry)
			break;
		prev = entry;
		entry = entry->next;
	}
	new->next = entry;

	if (prev)
		prev->next = new;
	else
		config->entries = new;

	/* copy/update from the template */
	needs = NEED_KERNEL | NEED_TITLE;
	if (newKernelInitrd)
		needs |= NEED_INITRD;
	if (newMBKernel) {
		needs |= NEED_MB;
		new->multiboot = 1;
	}
	if (newDevTreePath && getKeywordByType(LT_DEVTREE, config->cfi))
		needs |= NEED_DEVTREE;

	if (template) {
		for (masterLine = template->lines;
		     masterLine && (tmplLine = lineDup(masterLine));
		     lineFree(tmplLine), masterLine = masterLine->next) {
			dbgPrintf("addNewKernel processing %d\n",
				  tmplLine->type);

			/* skip comments */
			chptr = tmplLine->indent;
			while (*chptr && isspace(*chptr))
				chptr++;
			if (*chptr == '#')
				continue;

			if (iskernel(tmplLine->type)
			    && tmplLine->numElements >= 2) {
				if (!template->multiboot && (needs & NEED_MB)) {
					/* it's not a multiboot template and
					 * this is the kernel line.  Try to
					 * be intelligent about inserting the
					 * hypervisor at the same time.
					 */
					if (config->cfi->mbHyperFirst) {
						/* insert the hypervisor first */
						newLine =
						    addLine(new, config->cfi,
							    LT_HYPER,
							    tmplLine->indent,
							    newMBKernel +
							    strlen(prefix));
						/* set up for adding the
						 * kernel line */
						free(tmplLine->indent);
						tmplLine->indent =
						    strdup(config->
							   secondaryIndent);
						needs &= ~NEED_MB;
					}
					if (needs & NEED_KERNEL) {
						/* use addLineTmpl to
						 * preserve line elements,
						 * otherwise we could just
						 * call addLine.
						 * Unfortunately this means
						 * making some changes to the
						 * template such as the
						 * indent change above and
						 * the type change below.
						 */
						struct keywordTypes *mbm_kw =
						    getKeywordByType
						    (LT_MBMODULE, config->cfi);
						if (mbm_kw) {
							tmplLine->type =
							    LT_MBMODULE;
							free(tmplLine->
							     elements[0].item);
							tmplLine->elements[0].
							    item =
							    strdup(mbm_kw->key);
						}
						newLine =
						    addLineTmpl(new, tmplLine,
								newLine,
								newKernelPath +
								strlen(prefix),
								config->cfi);
						needs &= ~NEED_KERNEL;
					}
					if (needs & NEED_MB) {	/* !mbHyperFirst */
						newLine =
						    addLine(new, config->cfi,
							    LT_HYPER,
							    config->
							    secondaryIndent,
							    newMBKernel +
							    strlen(prefix));
						needs &= ~NEED_MB;
					}
				} else if (needs & NEED_KERNEL) {
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							newKernelPath +
							strlen(prefix),
							config->cfi);
					needs &= ~NEED_KERNEL;
				}

			} else if (tmplLine->type == LT_HYPER &&
				   tmplLine->numElements >= 2) {
				if (needs & NEED_MB) {
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							newMBKernel +
							strlen(prefix),
							config->cfi);
					needs &= ~NEED_MB;
				}

			} else if (tmplLine->type == LT_MBMODULE &&
				   tmplLine->numElements >= 2) {
				if (new->multiboot) {
					if (needs & NEED_KERNEL) {
						newLine =
						    addLineTmpl(new, tmplLine,
								newLine,
								newKernelPath +
								strlen(prefix),
								config->cfi);
						needs &= ~NEED_KERNEL;
					} else if (config->cfi->mbInitRdIsModule
						   && (needs & NEED_INITRD)) {
						char *initrdVal;
						initrdVal =
						    getInitrdVal(config, prefix,
								 tmplLine,
								 newKernelInitrd,
								 extraInitrds,
								 extraInitrdCount);
						newLine =
						    addLineTmpl(new, tmplLine,
								newLine,
								initrdVal,
								config->cfi);
						free(initrdVal);
						needs &= ~NEED_INITRD;
					}
				} else if (needs & NEED_KERNEL) {
					/* template is multi but new is not,
					 * insert the kernel in the first
					 * module slot
					 */
					tmplLine->type =
					    preferredLineType(LT_KERNEL,
							      config->cfi);
					free(tmplLine->elements[0].item);
					tmplLine->elements[0].item =
					    strdup(getKeywordByType
						   (tmplLine->type,
						    config->cfi)->key);
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							newKernelPath +
							strlen(prefix),
							config->cfi);
					needs &= ~NEED_KERNEL;
				} else if (needs & NEED_INITRD) {
					char *initrdVal;
					/* template is multi but new is not,
					 * insert the initrd in the second
					 * module slot
					 */
					tmplLine->type =
					    preferredLineType(LT_INITRD,
							      config->cfi);
					free(tmplLine->elements[0].item);
					tmplLine->elements[0].item =
					    strdup(getKeywordByType
						   (tmplLine->type,
						    config->cfi)->key);
					initrdVal =
					    getInitrdVal(config, prefix,
							 tmplLine,
							 newKernelInitrd,
							 extraInitrds,
							 extraInitrdCount);
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							initrdVal, config->cfi);
					free(initrdVal);
					needs &= ~NEED_INITRD;
				}

			} else if (isinitrd(tmplLine->type)
				   && tmplLine->numElements >= 2) {
				if (needs & NEED_INITRD && new->multiboot
				    && !template->multiboot
				    && config->cfi->mbInitRdIsModule) {
					/* make sure we don't insert the
					 * module initrd before the module
					 * kernel... if we don't do it here,
					 * it will be inserted following the
					 * template.
					 */
					if (!needs & NEED_KERNEL) {
						char *initrdVal;

						initrdVal =
						    getInitrdVal(config, prefix,
								 tmplLine,
								 newKernelInitrd,
								 extraInitrds,
								 extraInitrdCount);
						newLine =
						    addLine(new, config->cfi,
							    LT_MBMODULE,
							    config->
							    secondaryIndent,
							    initrdVal);
						free(initrdVal);
						needs &= ~NEED_INITRD;
					}
				} else if (needs & NEED_INITRD) {
					char *initrdVal;
					initrdVal =
					    getInitrdVal(config, prefix,
							 tmplLine,
							 newKernelInitrd,
							 extraInitrds,
							 extraInitrdCount);
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							initrdVal, config->cfi);
					free(initrdVal);
					needs &= ~NEED_INITRD;
				}

			} else if (tmplLine->type == LT_MENUENTRY &&
				   (needs & NEED_TITLE)) {
				requote(tmplLine, config->cfi);
				char *nkt = malloc(strlen(newKernelTitle) + 3);
				strcpy(nkt, "'");
				strcat(nkt, newKernelTitle);
				strcat(nkt, "'");
				newLine =
				    addLineTmpl(new, tmplLine, newLine, nkt,
						config->cfi);
				free(nkt);
				needs &= ~NEED_TITLE;
			} else if (tmplLine->type == LT_TITLE &&
				   (needs & NEED_TITLE)) {
				if (tmplLine->numElements >= 2) {
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							newKernelTitle,
							config->cfi);
					needs &= ~NEED_TITLE;
				} else if (tmplLine->numElements == 1 &&
					   config->cfi->titleBracketed) {
					/* addLineTmpl doesn't handle
					 * titleBracketed */
					newLine =
					    addLine(new, config->cfi, LT_TITLE,
						    tmplLine->indent,
						    newKernelTitle);
					needs &= ~NEED_TITLE;
				}
			} else if (tmplLine->type == LT_ECHO) {
				requote(tmplLine, config->cfi);
				static const char *prefix = "'Loading ";
				if (tmplLine->numElements > 1 &&
				    strstr(tmplLine->elements[1].item, prefix)
				    && masterLine->next
				    && iskernel(masterLine->next->type)) {
					char *newTitle =
					    malloc(strlen(prefix) +
						   strlen(newKernelTitle) + 2);

					strcpy(newTitle, prefix);
					strcat(newTitle, newKernelTitle);
					strcat(newTitle, "'");
					newLine =
					    addLine(new, config->cfi, LT_ECHO,
						    tmplLine->indent, newTitle);
					free(newTitle);
				} else {
					/* pass through other lines from the
					 * template */
					newLine =
					    addLineTmpl(new, tmplLine, newLine,
							NULL, config->cfi);
				}
			} else if (tmplLine->type == LT_DEVTREE &&
				   tmplLine->numElements == 2
				   && newDevTreePath) {
				newLine =
				    addLineTmpl(new, tmplLine, newLine,
						newDevTreePath + strlen(prefix),
						config->cfi);
				needs &= ~NEED_DEVTREE;
			} else if (tmplLine->type == LT_ENTRY_END
				   && needs & NEED_DEVTREE) {
				const char *ndtp = newDevTreePath;
				if (!strncmp
				    (newDevTreePath, prefix, strlen(prefix)))
					ndtp += strlen(prefix);
				newLine = addLine(new, config->cfi, LT_DEVTREE,
						  config->secondaryIndent,
						  ndtp);
				needs &= ~NEED_DEVTREE;
				newLine =
				    addLineTmpl(new, tmplLine, newLine, NULL,
						config->cfi);
			} else {
				/* pass through other lines from the template */
				newLine =
				    addLineTmpl(new, tmplLine, newLine, NULL,
						config->cfi);
			}
		}

	} else {
		/* don't have a template, so start the entry with the 
		 * appropriate starting line 
		 */
		switch (config->cfi->entryStart) {
		case LT_KERNEL:
		case LT_KERNEL_EFI:
		case LT_KERNEL_16:
			if (new->multiboot && config->cfi->mbHyperFirst) {
				/* fall through to LT_HYPER */
			} else {
				newLine = addLine(new, config->cfi,
						  preferredLineType(LT_KERNEL,
								    config->
								    cfi),
						  config->primaryIndent,
						  newKernelPath +
						  strlen(prefix));
				needs &= ~NEED_KERNEL;
				break;
			}

		case LT_HYPER:
			newLine = addLine(new, config->cfi, LT_HYPER,
					  config->primaryIndent,
					  newMBKernel + strlen(prefix));
			needs &= ~NEED_MB;
			break;

		case LT_MENUENTRY:{
				char *nkt = malloc(strlen(newKernelTitle) + 3);
				strcpy(nkt, "'");
				strcat(nkt, newKernelTitle);
				strcat(nkt, "'");
				newLine =
				    addLine(new, config->cfi, LT_MENUENTRY,
					    config->primaryIndent, nkt);
				free(nkt);
				needs &= ~NEED_TITLE;
				needs |= NEED_END;
				break;
			}
		case LT_TITLE:
			if (useextlinuxmenu != 0) {	// We just need useextlinuxmenu to not be zero (set above)
				char *templabel;
				int x = 0, y = 0;

				templabel = strdup(newKernelTitle);
				while (templabel[x]) {
					if (templabel[x] == ' ') {
						y = x;
						while (templabel[y]) {
							templabel[y] =
							    templabel[y + 1];
							y++;
						}
					}
					x++;
				}
				newLine = addLine(new, config->cfi, LT_TITLE,
						  config->primaryIndent,
						  templabel);
				free(templabel);
			} else {
				newLine = addLine(new, config->cfi, LT_TITLE,
						  config->primaryIndent,
						  newKernelTitle);
			}
			needs &= ~NEED_TITLE;
			break;

		default:
			abort();
		}
	}

	struct singleLine *endLine = NULL;
	endLine = getLineByType(LT_ENTRY_END, new->lines);
	if (endLine) {
		removeLine(new, endLine);
		needs |= NEED_END;
	}

	/* add the remainder of the lines, i.e. those that either
	 * weren't present in the template, or in the case of no template,
	 * all the lines following the entryStart.
	 */
	if (needs & NEED_TITLE) {
		newLine = addLine(new, config->cfi, LT_TITLE,
				  config->secondaryIndent, newKernelTitle);
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
				  (new->multiboot
				   && getKeywordByType(LT_MBMODULE,
						       config->cfi))
				  ? LT_MBMODULE : preferredLineType(LT_KERNEL,
								    config->
								    cfi),
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
		initrdVal =
		    getInitrdVal(config, prefix, NULL, newKernelInitrd,
				 extraInitrds, extraInitrdCount);
		newLine =
		    addLine(new, config->cfi,
			    (new->multiboot
			     && getKeywordByType(LT_MBMODULE, config->cfi))
			    ? LT_MBMODULE : preferredLineType(LT_INITRD,
							      config->cfi),
			    config->secondaryIndent, initrdVal);
		free(initrdVal);
		needs &= ~NEED_INITRD;
	}
	if (needs & NEED_DEVTREE) {
		newLine = addLine(new, config->cfi, LT_DEVTREE,
				  config->secondaryIndent, newDevTreePath);
		needs &= ~NEED_DEVTREE;
	}

	/* NEEDS_END must be last on bootloaders that need it... */
	if (needs & NEED_END) {
		newLine = addLine(new, config->cfi, LT_ENTRY_END,
				  config->secondaryIndent, NULL);
		needs &= ~NEED_END;
	}

	if (needs) {
		printf(_("grubby: needs=%d, aborting\n"), needs);
		abort();
	}

	if (updateImage(config, indexs, prefix, newKernelArgs, NULL,
			newMBKernelArgs, NULL)) {
		config->isModified = 1;
		return 1;
	}

	return 0;
}

int main(int argc, const char **argv)
{
	poptContext optCon;
	const char *grubConfig = NULL;
	char *outputFile = NULL;
	int arg = 0;
	int flags = 0;
	int badImageOkay = 0;
	int configureGrub2 = 0;
	int configureLilo = 0, configureELilo = 0, configureGrub = 0;
	int configureYaboot = 0, configureSilo = 0, configureZipl = 0;
	int configureExtLinux = 0;
	int bootloaderProbe = 0;
	int extraInitrdCount = 0;
	char *updateKernelPath = NULL;
	char *newKernelPath = NULL;
	char *removeKernelPath = NULL;
	char *newKernelArgs = NULL;
	char *newKernelInitrd = NULL;
	char *newKernelTitle = NULL;
	char *newDevTreePath = NULL;
	char *newMBKernel = NULL;
	char *newMBKernelArgs = NULL;
	int newIndex = 0;
	char *removeMBKernelArgs = NULL;
	char *removeMBKernel = NULL;
	char *bootPrefix = NULL;
	char *defaultKernel = NULL;
	char *removeArgs = NULL;
	char *kernelInfo = NULL;
	char *extraInitrds[MAX_EXTRA_INITRDS] = { NULL };
	char *envPath = NULL;
	const char *chptr = NULL;
	struct configFileInfo *cfi = NULL;
	struct grubConfig *config;
	struct singleEntry *template = NULL;
	int copyDefault = 0, makeDefault = 0;
	int displayDefault = 0;
	int displayDefaultIndex = 0;
	int displayDefaultTitle = 0;
	int defaultIndex = -1;
	struct poptOption options[] = {
		{"add-kernel", 0, POPT_ARG_STRING, &newKernelPath, 0,
		 _("add an entry for the specified kernel"), _("kernel-path")},
		{"add-multiboot", 0, POPT_ARG_STRING, &newMBKernel, 0,
		 _("add an entry for the specified multiboot kernel"), NULL},
		{"args", 0, POPT_ARG_STRING, &newKernelArgs, 0,
		 _("default arguments for the new kernel or new arguments for "
		   "kernel being updated"), _("args")},
		{"mbargs", 0, POPT_ARG_STRING, &newMBKernelArgs, 0,
		 _("default arguments for the new multiboot kernel or "
		   "new arguments for multiboot kernel being updated"), NULL},
		{"mounts", 0, POPT_ARG_STRING, &mounts, 0,
		 _("path to fake /proc/mounts file (for testing only)"),
		 _("mounts")},
		{"bad-image-okay", 0, 0, &badImageOkay, 0,
		 _
		 ("don't sanity check images in boot entries (for testing only)"),
		 NULL},
		{"boot-filesystem", 0, POPT_ARG_STRING, &bootPrefix, 0,
		 _
		 ("filesystem which contains /boot directory (for testing only)"),
		 _("bootfs")},
#if defined(__i386__) || defined(__x86_64__) || defined (__powerpc64__) || defined (__ia64__)
		{"bootloader-probe", 0, POPT_ARG_NONE, &bootloaderProbe, 0,
		 _("check which bootloader is installed on boot sector")},
#endif
		{"config-file", 'c', POPT_ARG_STRING, &grubConfig, 0,
		 _("path to grub config file to update (\"-\" for stdin)"),
		 _("path")},
		{"copy-default", 0, 0, &copyDefault, 0,
		 _("use the default boot entry as a template for the new entry "
		   "being added; if the default is not a linux image, or if "
		   "the kernel referenced by the default image does not exist, "
		   "the first linux entry whose kernel does exist is used as the "
		   "template"), NULL},
		{"debug", 0, 0, &debug, 0,
		 _("print debugging information for failures")},
		{"default-kernel", 0, 0, &displayDefault, 0,
		 _("display the path of the default kernel")},
		{"default-index", 0, 0, &displayDefaultIndex, 0,
		 _("display the index of the default kernel")},
		{"default-title", 0, 0, &displayDefaultTitle, 0,
		 _("display the title of the default kernel")},
		{"devtree", 0, POPT_ARG_STRING, &newDevTreePath, 0,
		 _("device tree file for new stanza"), _("dtb-path")},
		{"devtreedir", 0, POPT_ARG_STRING, &newDevTreePath, 0,
		 _("device tree directory for new stanza"), _("dtb-path")},
		{"elilo", 0, POPT_ARG_NONE, &configureELilo, 0,
		 _("configure elilo bootloader")},
		{"efi", 0, POPT_ARG_NONE, &isEfi, 0,
		 _("force grub2 stanzas to use efi")},
		{"env", 0, POPT_ARG_STRING, &envPath, 0,
		 _("path for environment data"),
		 _("path")},
		{"extlinux", 0, POPT_ARG_NONE, &configureExtLinux, 0,
		 _("configure extlinux bootloader (from syslinux)")},
		{"grub", 0, POPT_ARG_NONE, &configureGrub, 0,
		 _("configure grub bootloader")},
		{"grub2", 0, POPT_ARG_NONE, &configureGrub2, 0,
		 _("configure grub2 bootloader")},
		{"info", 0, POPT_ARG_STRING, &kernelInfo, 0,
		 _("display boot information for specified kernel"),
		 _("kernel-path")},
		{"initrd", 0, POPT_ARG_STRING, &newKernelInitrd, 0,
		 _("initrd image for the new kernel"), _("initrd-path")},
		{"extra-initrd", 'i', POPT_ARG_STRING, NULL, 'i',
		 _
		 ("auxiliary initrd image for things other than the new kernel"),
		 _("initrd-path")},
		{"lilo", 0, POPT_ARG_NONE, &configureLilo, 0,
		 _("configure lilo bootloader")},
		{"make-default", 0, 0, &makeDefault, 0,
		 _("make the newly added entry the default boot entry"), NULL},
		{"output-file", 'o', POPT_ARG_STRING, &outputFile, 0,
		 _("path to output updated config file (\"-\" for stdout)"),
		 _("path")},
		{"remove-args", 0, POPT_ARG_STRING, &removeArgs, 0,
		 _("remove kernel arguments"), NULL},
		{"remove-mbargs", 0, POPT_ARG_STRING, &removeMBKernelArgs, 0,
		 _("remove multiboot kernel arguments"), NULL},
		{"remove-kernel", 0, POPT_ARG_STRING, &removeKernelPath, 0,
		 _("remove all entries for the specified kernel"),
		 _("kernel-path")},
		{"remove-multiboot", 0, POPT_ARG_STRING, &removeMBKernel, 0,
		 _("remove all entries for the specified multiboot kernel"),
		 NULL},
		{"set-default", 0, POPT_ARG_STRING, &defaultKernel, 0,
		 _("make the first entry referencing the specified kernel "
		   "the default"), _("kernel-path")},
		{"set-default-index", 0, POPT_ARG_INT, &defaultIndex, 0,
		 _("make the given entry index the default entry"),
		 _("entry-index")},
		{"set-index", 0, POPT_ARG_INT, &newIndex, 0,
		 _("use the given index when creating a new entry"),
		 _("entry-index")},
		{"silo", 0, POPT_ARG_NONE, &configureSilo, 0,
		 _("configure silo bootloader")},
		{"title", 0, POPT_ARG_STRING, &newKernelTitle, 0,
		 _("title to use for the new kernel entry"), _("entry-title")},
		{"update-kernel", 0, POPT_ARG_STRING, &updateKernelPath, 0,
		 _("updated information for the specified kernel"),
		 _("kernel-path")},
		{"version", 'v', 0, NULL, 'v',
		 _("print the version of this program and exit"), NULL},
		{"yaboot", 0, POPT_ARG_NONE, &configureYaboot, 0,
		 _("configure yaboot bootloader")},
		{"zipl", 0, POPT_ARG_NONE, &configureZipl, 0,
		 _("configure zipl bootloader")},
		POPT_AUTOHELP {0, 0, 0, 0, 0}
	};

	useextlinuxmenu = 0;

	int i = 0;
	for (int j = 1; j < argc; j++)
		i += strlen(argv[j]) + 1;

	if (i > 0) {
		saved_command_line = malloc(i);
		if (!saved_command_line) {
			fprintf(stderr, "grubby: %m\n");
			exit(1);
		}

		saved_command_line[0] = '\0';
		int cmdline_len = 0, arg_len;
		for (int j = 1; j < argc; j++) {
			arg_len = strlen(argv[j]);
			memcpy(saved_command_line + cmdline_len, argv[j], arg_len);
			cmdline_len += arg_len;
			if (j != argc - 1) {
				memcpy(saved_command_line + cmdline_len, " ", 1);
				cmdline_len++;
			}
		}
		saved_command_line[cmdline_len] = '\0';
	}

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
				extraInitrds[extraInitrdCount++] =
				    strdup(poptGetOptArg(optCon));
			} else {
				fprintf(stderr,
					_
					("grubby: extra initrd maximum is %d\n"),
					extraInitrdCount);
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

	if ((configureLilo + configureGrub2 + configureGrub + configureELilo +
	     configureYaboot + configureSilo + configureZipl +
	     configureExtLinux) > 1) {
		fprintf(stderr,
			_("grubby: cannot specify multiple bootloaders\n"));
		return 1;
	} else if (bootloaderProbe && grubConfig) {
		fprintf(stderr,
			_
			("grubby: cannot specify config file with --bootloader-probe\n"));
		return 1;
	} else if (configureGrub2) {
		cfi = &grub2ConfigType;
		if (envPath)
			cfi->envFile = envPath;
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
		useextlinuxmenu = 1;
	}

	if (!cfi) {
		if (grub2FindConfig(&grub2ConfigType)) {
			cfi = &grub2ConfigType;
			configureGrub2 = 1;
			if (envPath)
				cfi->envFile = envPath;
		} else {
#ifdef __ia64__
			cfi = &eliloConfigType;
			configureLilo = 1;
#elif defined(__powerpc__)
			cfi = &yabootConfigType;
			configureYaboot = 1;
#elif defined(__sparc__)
			cfi = &siloConfigType;
			configureSilo = 1;
#elif defined(__s390__) || defined(__s390x__)
			cfi = &ziplConfigType;
			configureZipl = 1;
#else
			cfi = &grubConfigType;
			configureGrub = 1;
#endif
		}
	}

	if (!grubConfig) {
		if (cfi->findConfig)
			grubConfig = cfi->findConfig(cfi);
		if (!grubConfig)
			grubConfig = cfi->defaultConfig;
	}

	if (bootloaderProbe && (displayDefault || kernelInfo ||
				newKernelPath || removeKernelPath || makeDefault
				|| defaultKernel || displayDefaultIndex
				|| displayDefaultTitle
				|| (defaultIndex >= 0))) {
		fprintf(stderr,
			_("grubby: --bootloader-probe may not be used with "
			  "specified option"));
		return 1;
	}

	if ((displayDefault || kernelInfo) && (newKernelPath ||
					       removeKernelPath)) {
		fprintf(stderr, _("grubby: --default-kernel and --info may not "
				  "be used when adding or removing kernels\n"));
		return 1;
	}

	if (newKernelPath && !newKernelTitle) {
		fprintf(stderr, _("grubby: kernel title must be specified\n"));
		return 1;
	} else if (!newKernelPath && (copyDefault ||
				      (newKernelInitrd && !updateKernelPath) ||
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
		fprintf(stderr,
			_("grubby: cannot make removed kernel the default\n"));
		return 1;
	} else if (defaultKernel && newKernelPath &&
		   !strcmp(defaultKernel, newKernelPath)) {
		makeDefault = 1;
		defaultKernel = NULL;
	} else if (defaultKernel && (defaultIndex >= 0)) {
		fprintf(stderr,
			_("grubby: --set-default and --set-default-index "
			  "may not be used together\n"));
		return 1;
	}

	if (grubConfig && !strcmp(grubConfig, "-") && !outputFile) {
		fprintf(stderr,
			_("grubby: output file must be specified if stdin "
			  "is used\n"));
		return 1;
	}

	if (!removeKernelPath && !newKernelPath && !displayDefault
	    && !defaultKernel && !kernelInfo && !bootloaderProbe
	    && !updateKernelPath && !removeMBKernel && !displayDefaultIndex
	    && !displayDefaultTitle && (defaultIndex == -1)) {
		fprintf(stderr, _("grubby: no action specified\n"));
		return 1;
	}

	flags |= badImageOkay ? GRUBBY_BADIMAGE_OKAY : 0;

	if (cfi->needsBootPrefix) {
		if (!bootPrefix) {
			bootPrefix = findBootPrefix();
			if (!bootPrefix)
				return 1;
		} else {
			/* this shouldn't end with a / */
			if (bootPrefix[strlen(bootPrefix) - 1] == '/')
				bootPrefix[strlen(bootPrefix) - 1] = '\0';
		}
	} else {
		bootPrefix = "";
	}

	if (!cfi->mbAllowExtraInitRds && extraInitrdCount > 0) {
		fprintf(stderr,
			_("grubby: %s doesn't allow multiple initrds\n"),
			cfi->defaultConfig);
		return 1;
	}

	if (bootloaderProbe) {
		int lrc = 0, grc = 0, gr2c = 0, extrc = 0, yrc = 0, erc = 0;
		struct grubConfig *lconfig, *gconfig, *yconfig, *econfig;

		const char *grub2config = grub2FindConfig(&grub2ConfigType);
		if (grub2config) {
			gconfig = readConfig(grub2config, &grub2ConfigType);
			if (!gconfig)
				gr2c = 1;
			else
				gr2c = checkForGrub2(gconfig);
		}

		const char *grubconfig = grubFindConfig(&grubConfigType);
		if (!access(grubconfig, F_OK)) {
			gconfig = readConfig(grubconfig, &grubConfigType);
			if (!gconfig)
				grc = 1;
			else
				grc = checkForGrub(gconfig);
		}

		if (!access(liloConfigType.defaultConfig, F_OK)) {
			lconfig =
			    readConfig(liloConfigType.defaultConfig,
				       &liloConfigType);
			if (!lconfig)
				lrc = 1;
			else
				lrc = checkForLilo(lconfig);
		}

		if (!access(eliloConfigType.defaultConfig, F_OK)) {
			econfig = readConfig(eliloConfigType.defaultConfig,
					     &eliloConfigType);
			if (!econfig)
				erc = 1;
			else
				erc = checkForElilo(econfig);
		}

		if (!access(extlinuxConfigType.defaultConfig, F_OK)) {
			lconfig =
			    readConfig(extlinuxConfigType.defaultConfig,
				       &extlinuxConfigType);
			if (!lconfig)
				extrc = 1;
			else
				extrc = checkForExtLinux(lconfig);
		}

		if (!access(yabootConfigType.defaultConfig, F_OK)) {
			yconfig = readConfig(yabootConfigType.defaultConfig,
					     &yabootConfigType);
			if (!yconfig)
				yrc = 1;
			else
				yrc = checkForYaboot(yconfig);
		}

		if (lrc == 1 || grc == 1 || gr2c == 1 || extrc == 1 || yrc == 1
		    || erc == 1)
			return 1;

		if (lrc == 2)
			printf("lilo\n");
		if (gr2c == 2)
			printf("grub2\n");
		if (grc == 2)
			printf("grub\n");
		if (extrc == 2)
			printf("extlinux\n");
		if (yrc == 2)
			printf("yaboot\n");
		if (erc == 2)
			printf("elilo\n");

		return 0;
	}

	if (grubConfig == NULL) {
		printf("Could not find bootloader configuration file.\n");
		exit(1);
	}

	config = readConfig(grubConfig, cfi);
	if (!config)
		return 1;

	if (displayDefault) {
		struct singleLine *line;
		struct singleEntry *entry;
		size_t rs;

		if (config->defaultImage == NO_DEFAULT_ENTRY)
			return 0;
		if (config->defaultImage == DEFAULT_SAVED_GRUB2 &&
		    cfi->defaultIsSaved)
			config->defaultImage = FIRST_ENTRY_INDEX;
		entry = findEntryByIndex(config, config->defaultImage);
		if (!entry)
			return 0;

		/* check if is a suitable image but still print it */
		suitableImage(entry, bootPrefix, 0, flags);

		line =
		    getLineByType(LT_KERNEL | LT_HYPER | LT_KERNEL_EFI |
				  LT_KERNEL_16, entry->lines);
		if (!line)
			return 0;

		rs = getRootSpecifier(line->elements[1].item);
		printf("%s%s\n", bootPrefix, line->elements[1].item + rs);

		return 0;

	} else if (displayDefaultTitle) {
		struct singleLine *line;
		struct singleEntry *entry;

		if (config->defaultImage == NO_DEFAULT_ENTRY)
			return 0;
		if (config->defaultImage == DEFAULT_SAVED_GRUB2 &&
		    cfi->defaultIsSaved)
			config->defaultImage = FIRST_ENTRY_INDEX;
		entry = findEntryByIndex(config, config->defaultImage);
		if (!entry)
			return 0;

		if (!configureGrub2) {
			char *title;
			line = getLineByType(LT_TITLE, entry->lines);
			if (!line)
				return 0;
			title = extractTitle(config, line);
			if (!title)
				return 0;
			printf("%s\n", title);
			free(title);
		} else {
			char *title;

			dbgPrintf
			    ("This is GRUB2, default title is embeded in menuentry\n");
			line = getLineByType(LT_MENUENTRY, entry->lines);
			if (!line)
				return 0;
			title = grub2ExtractTitle(line);
			if (title)
				printf("%s\n", title);
		}
		return 0;

	} else if (displayDefaultIndex) {
		if (config->defaultImage == NO_DEFAULT_ENTRY)
			return 0;
		if (config->defaultImage == DEFAULT_SAVED_GRUB2 &&
		    cfi->defaultIsSaved)
			config->defaultImage = FIRST_ENTRY_INDEX;
		printf("%i\n", config->defaultImage);
		return 0;

	} else if (kernelInfo)
		return displayInfo(config, kernelInfo, bootPrefix);

	if (copyDefault) {
		template = findTemplate(config, bootPrefix, NULL, 0, flags);
		if (!template)
			return 1;
	}

	markRemovedImage(config, removeKernelPath, bootPrefix);
	markRemovedImage(config, removeMBKernel, bootPrefix);
	setDefaultImage(config, newKernelPath != NULL, defaultKernel,
			makeDefault, bootPrefix, flags, defaultIndex,
			newIndex);
	setFallbackImage(config, newKernelPath != NULL);
	if (updateImage(config, updateKernelPath, bootPrefix, newKernelArgs,
			removeArgs, newMBKernelArgs, removeMBKernelArgs))
		return 1;
	if (updateKernelPath && newKernelInitrd) {
		if (newMBKernel) {
			if (addMBInitrd(config, newMBKernel, updateKernelPath,
					bootPrefix, newKernelInitrd,
					newKernelTitle))
				return 1;
		} else {
			if (updateInitrd(config, updateKernelPath, bootPrefix,
					 newKernelInitrd, newKernelTitle))
				return 1;
		}
	}
	if (addNewKernel(config, template, bootPrefix, newKernelPath,
			 newKernelTitle, newKernelArgs, newKernelInitrd,
			 (const char **)extraInitrds, extraInitrdCount,
			 newMBKernel, newMBKernelArgs, newDevTreePath,
			 newIndex))
		return 1;

	if (numEntries(config) == 0) {
		fprintf(stderr,
			_("grubby: doing this would leave no kernel entries. "
			  "Not writing out new config.\n"));
		return 1;
	}

	if (!outputFile)
		outputFile = (char *)grubConfig;

	return writeConfig(config, outputFile, bootPrefix);
}
