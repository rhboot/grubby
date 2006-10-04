/*
 * blkent_r.c - Utilities for reading/writing blktab 
 *
 * Peter Jones <pjones@redhat.com>
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define _GNU_SOURCE 1

#include <alloca.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <argz.h>

#include "blkent.h"

#if 0
struct blkent {
    char *blk_name;     /* symbolic block device name */
    char *blk_type;     /* rule type */
    char *blk_opts;     /* opts */
};
#endif

#undef __setmntent
#undef __endmntent
#undef __getmntent_r

/* Prepare to begin reading and/or writing block table entries from the
   beginning of FILE.  MODE is as for `fopen'.  */
FILE *
__setblkent(const char *file, const char *mode)
{
    /* Extend the mode parameter with "c" to disable cancellation in the
       I/O functions.  */
    size_t modelen = strlen(mode);
    char newmode[modelen + 2];
    FILE *result = NULL;
    memcpy(mempcpy(newmode, mode, modelen), "c", 2);
    result = fopen(file, newmode);

    if (result != NULL)
        __fsetlocking(result, FSETLOCKING_BYCALLER);

    return result;
}
FILE *setblkent(const char *fule, const char *mode)
    __attribute__((weak, alias("__setblkent")));

/* Close a stream opened with `setmntent'.  */
int __endblkent(FILE *stream)
{
    if (stream)
        fclose(stream);
    return 1;
}
int endblkent(FILE *stream)
    __attribute__((weak, alias("__endblkent")));

/* Since the values in a line are separated by spaces, a name cannot
   contain a space.  Therefore some programs encode spaces in names
   by the strings "\040".  We undo the encoding when reading an entry.
   The decoding happens in place.  */
static inline char *
decode_name(char *buf)
{
    char *rp = buf;
    char *wp = buf;
    do {
        if (rp[0] == '\\' && rp[1] == '0' && rp[2] == '4' && rp[3] == '0') {
            /* \040 is a SPACE. */
            *wp++ = ' ';
            rp += 3;
        } else if (rp[0] == '\\' && rp[1] == '0' && rp[2] == '1' &&
                   rp[3] == '1') {
            /* \011 is a TAB. */
            *wp++ = '\t';
            rp += 3;
        } else if (rp[0] == '\\' && rp[1] == 't') {
            *wp++ = '\t';
            rp += 1;
        } else if (rp[0] == '\\' && rp[1] == '0' && rp[2] == '1' &&
                   rp[3] == '2') {
            /* \012 is a NEWLINE. */
            *wp++ = '\n';
            rp += 3;
        } else if (rp[0] == '\\' && rp[1] == 'n') {
            /* \n is a NEWLINE. */
            *wp++ = '\n';
            rp += 1;
        } else if (rp[0] == '\\' && rp[1] == '\\') {
            /* We have to escape \\ to be able to represent all characters. */
            *wp++ = '\\';
            rp += 1;
        } else if (rp[0] == '\\' && rp[1] == '1' && rp[2] == '3' &&
                   rp[3] == '4') {
            /* \134 is also \\. */
            *wp++ = '\\';
            rp += 3;
        } else
            *wp++ = *rp;

    } while (*rp++ != '\0');

    return buf;
}

/* Read one block table entry from STREAM.  Returns a pointer to storage
   reused on the next call, or null for EOF or error (use feof/ferror to
   check).  */
struct blkent *
__getblkent_r(FILE *stream, struct blkent *bp, char *buffer, int bufsiz)
{
    char *cp;
    char *head;

    flockfile(stream);
    do {
        char *end_ptr;

        if (fgets_unlocked(buffer, bufsiz, stream) == NULL) {
            funlockfile(stream);
            return NULL;
        }

        end_ptr = strchr(buffer, '\n');
        if (end_ptr != NULL) /* chop newline */
            *end_ptr = '\0';
        else {
            /* Not the whole line was read.  Do it now but forget it.  */
            char tmp[1024];
            while (fgets_unlocked(tmp, sizeof tmp, stream) != NULL)
                if (strchr(tmp, '\n') != NULL)
                    break;
        }

        head = buffer + strspn(buffer, " \t");
    } while (head[0] == '\0' || head[0] == '#');

    cp = strsep(&head, " \t");
    bp->blk_name = cp != NULL ? decode_name(cp) : (char *) "";
    if (head)
        head += strspn(head, " \t");
    cp = strsep(&head, " \t");
    bp->blk_type = cp != NULL ? decode_name(cp) : (char *) "";
    if (head)
        head += strspn(head, " \t");
    cp = strsep(&head, " \t");
    bp->blk_opts = cp != NULL ? decode_name(cp) : (char *) "";

    funlockfile(stream);
    return bp;
}
struct blkent *
getblkent_r(FILE *stream, struct blkent *bp, char *buffer, int bufsiz)
    __attribute__((weak, alias("__getblkent_r")));

/* We have to use an encoding for names if they contain spaces or tabs.
   To be able to represent all characters we also have to escape the
   backslash itself.  This "function" must be a macro since we use
   `alloca'.  */
#define encode_name(name) \
  do {									      \
    const char *rp = name;						      \
									      \
    while (*rp != '\0')							      \
      if (*rp == ' ' || *rp == '\t' || *rp == '\\' || *rp == '\n')	      \
	break;								      \
      else								      \
	++rp;								      \
									      \
    if (*rp != '\0')							      \
      {									      \
	/* In the worst case the length of the string can increase to	      \
	   four times the current length.  */				      \
	char *wp;							      \
									      \
	rp = name;							      \
	name = wp = (char *) alloca (strlen (name) * 4 + 1);		      \
									      \
	do {								      \
	  if (*rp == ' ')						      \
	    {								      \
	      *wp++ = '\\';						      \
	      *wp++ = '0';						      \
	      *wp++ = '4';						      \
	      *wp++ = '0';						      \
	    }								      \
	  else if (*rp == '\t')						      \
	    {								      \
	      *wp++ = '\\';						      \
	      *wp++ = 't';						      \
	    }								      \
	  else if (*rp == '\n')						      \
	    {								      \
	      *wp++ = '\\';						      \
	      *wp++ = 'n';						      \
	    }								      \
	  else if (*rp == '\\')						      \
	    {								      \
	      *wp++ = '\\';						      \
	      *wp++ = '\\';						      \
	    }								      \
	  else								      \
	    *wp++ = *rp;						      \
        } while (*rp++ != '\0');					      \
      }									      \
  } while (0)

/* Write the block table entry described by BLK to STREAM.
   Return zero on success, nonzero on failure.  */
int __addblkent(FILE *stream, const struct blkent *blk)
{
    struct blkent blkcopy = *blk;
    if (fseek(stream, 0, SEEK_END))
        return 1;

    encode_name(blkcopy.blk_name);
    encode_name(blkcopy.blk_type);
    encode_name(blkcopy.blk_opts);

    return (fprintf(stream, "%s %s %s\n",
                blkcopy.blk_name,
                blkcopy.blk_type,
                blkcopy.blk_opts)
            < 0 ? 1 : 0);
}
int addblkent(FILE *stream, const struct blkent *blk)
    __attribute__((weak, alias("__addblkent")));

/* We don't want to allocate the static buffer all the time since it
   is not always used (in fact, rather infrequently).  Accept the
   extra cost of a `malloc'.  */
static char *getblkent_buffer;

/* This is the size of the buffer.  This is really big.  */
#define BUFFER_SIZE     4096

static inline void
allocate(void)
{
    getblkent_buffer = (char *) malloc(BUFFER_SIZE);
}

struct blkent *
getblkent(FILE *stream)
{
    static struct blkent b;
    static int once = 0;
    if (once == 0) {
        allocate();
        once = 1;
    }

    if (getblkent_buffer == NULL)
        /* If no core is available we don't have a chance to run the
           program successfully and so returning NULL is an acceptable
           result.  */
        return NULL;

    return __getblkent_r(stream, &b, getblkent_buffer, BUFFER_SIZE);
}

char *
__hasblkopt (const struct blkent *blk, const char *opt)
{
  const size_t optlen = strlen (opt);
  char *rest = blk->blk_opts, *p;

  while ((p = strstr (rest, opt)) != NULL)
    {
      if (p == rest
          || (p[-1] == ';'
              && (p[optlen] == '\0' ||
                  p[optlen] == '='  ||
                  p[optlen] == ';')))
        return p;

      rest = strchr (rest, ';');
      if (rest == NULL)
        break;
      ++rest;
    }

  return NULL;
}
char *hasblkopt (const struct blkent *blk, const char *opt)
    __attribute__((weak, alias("__hasblkopt")));

/*
 * vim:ts=8:sw=4:sts=4:et
 */
