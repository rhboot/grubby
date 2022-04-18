#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <argp.h>
#include <rpm/rpmlib.h>
#include <err.h>

typedef enum {
        RPMNVRCMP,
        VERSNVRCMP,
        RPMVERCMP,
        STRVERSCMP,
} comparitors;

static comparitors comparitor = RPMNVRCMP;

static inline void *xmalloc(size_t sz)
{
        void *ret = malloc(sz);

        assert(sz == 0 || ret != NULL);
        return ret;
}

static inline void *xrealloc(void *p, size_t sz)
{
        void *ret = realloc(p, sz);

        assert(sz == 0 || ret != NULL);
        return ret;
}

static inline char *xstrdup(const char * const s)
{
        void *ret = strdup(s);

        assert(s == NULL || ret != NULL);
        return ret;
}

static size_t
read_file (const char *input, char **ret)
{
  FILE *in;
  size_t s;
  size_t sz = 2048;
  size_t offset = 0;
  char *text;

  if (!strcmp(input, "-"))
    in = stdin;
  else
    in = fopen(input, "r");

  text = xmalloc (sz);

  if (!in)
    err(1, "cannot open `%s'", input);

  while ((s = fread (text + offset, 1, sz - offset, in)) != 0)
    {
      offset += s;
      if (sz - offset == 0)
	{
	  sz += 2048;
	  text = xrealloc (text, sz);
	}
    }

  text[offset] = '\0';
  *ret = text;

  if (in != stdin)
    fclose(in);

  return offset + 1;
}

/* returns name/version/release */
/* NULL string pointer returned if nothing found */
static void
split_package_string (char *package_string, char **name,
                     char **version, char **release)
{
  char *package_version, *package_release;

  /* Release */
  package_release = strrchr (package_string, '-');

  if (package_release != NULL)
      *package_release++ = '\0';

  *release = package_release;

  /* Version */
  package_version = strrchr(package_string, '-');

  if (package_version != NULL)
      *package_version++ = '\0';

  *version = package_version;
  /* Name */
  *name = package_string;

  /* Bubble up non-null values from release to name */
  if (*name == NULL)
    {
      *name = (*version == NULL ? *release : *version);
      *version = *release;
      *release = NULL;
    }
  if (*version == NULL)
    {
      *version = *release;
      *release = NULL;
    }
}

static int
cmprpmversp(const void *p1, const void *p2)
{
        return rpmvercmp(*(char * const *)p1, *(char * const *)p2);
}

static int
cmpstrversp(const void *p1, const void *p2)
{
        return strverscmp(*(char * const *)p1, *(char * const *)p2);
}

/*
 * package name-version-release comparator for qsort
 * expects p, q which are pointers to character strings (char *)
 * which will not be altered in this function
 */
static int
package_version_compare (const void *p, const void *q)
{
  char *local_p, *local_q;
  char *lhs_name, *lhs_version, *lhs_release;
  char *rhs_name, *rhs_version, *rhs_release;
  int vercmpflag = 0;
  int (*cmp)(const char *s1, const char *s2);

  switch(comparitor)
    {
    default: /* just to shut up -Werror=maybe-uninitialized */
    case RPMNVRCMP:
      cmp = rpmvercmp;
      break;
    case VERSNVRCMP:
      cmp = strverscmp;
      break;
    case RPMVERCMP:
      return cmprpmversp(p, q);
      break;
    case STRVERSCMP:
      return cmpstrversp(p, q);
      break;
    }

  local_p = alloca (strlen (*(char * const *)p) + 1);
  local_q = alloca (strlen (*(char * const *)q) + 1);

  /* make sure these allocated */
  assert (local_p);
  assert (local_q);

  strcpy (local_p, *(char * const *)p);
  strcpy (local_q, *(char * const *)q);

  split_package_string (local_p, &lhs_name, &lhs_version, &lhs_release);
  split_package_string (local_q, &rhs_name, &rhs_version, &rhs_release);

  /* Check Name and return if unequal */
  vercmpflag = cmp ((lhs_name == NULL ? "" : lhs_name),
                    (rhs_name == NULL ? "" : rhs_name));
  if (vercmpflag != 0)
    return vercmpflag;

  /* Check version and return if unequal */
  vercmpflag = cmp ((lhs_version == NULL ? "" : lhs_version),
                    (rhs_version == NULL ? "" : rhs_version));
  if (vercmpflag != 0)
    return vercmpflag;

  /* Check release and return the version compare value */
  vercmpflag = cmp ((lhs_release == NULL ? "" : lhs_release),
                    (rhs_release == NULL ? "" : rhs_release));

  return vercmpflag;
}

static void
add_input (const char *filename, char ***package_names, size_t *n_package_names)
{
  char *orig_input_buffer = NULL;
  char *input_buffer;
  char *position_of_newline;
  char **names = *package_names;
  char **new_names = NULL;
  size_t n_names = *n_package_names;

  if (!*package_names)
    new_names = names = xmalloc (sizeof (char *) * 2);

  if (read_file (filename, &orig_input_buffer) < 2)
    {
      if (new_names)
	free (new_names);
      if (orig_input_buffer)
	free (orig_input_buffer);
      return;
    }

  input_buffer = orig_input_buffer;
  while (input_buffer && *input_buffer &&
	 (position_of_newline = strchrnul (input_buffer, '\n')))
    {
      size_t sz = position_of_newline - input_buffer;
      char *new;

      if (sz == 0)
	{
	  input_buffer = position_of_newline + 1;
	  continue;
	}

      new = xmalloc (sz+1);
      strncpy (new, input_buffer, sz);
      new[sz] = '\0';

      names = xrealloc (names, sizeof (char *) * (n_names + 1));
      names[n_names] = new;
      n_names++;

      /* move buffer ahead to next line */
      input_buffer = position_of_newline + 1;
      if (*position_of_newline == '\0')
	input_buffer = NULL;
    }

  free (orig_input_buffer);

  *package_names = names;
  *n_package_names = n_names;
}

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  return (char *)text;
}

static struct argp_option options[] = {
  { "comparitor", 'c', "COMPARITOR", 0, "[rpm-nvr-cmp|vers-nvr-cmp|rpmvercmp|strverscmp]", 0},
  { 0, }
};

struct arguments
{
  size_t ninputs;
  size_t input_max;
  char **inputs;
};

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;
  switch (key)
    {
    case 'c':
      if (!strcmp(arg, "rpm-nvr-cmp") || !strcmp(arg, "rpmnvrcmp"))
        comparitor = RPMNVRCMP;
      else if (!strcmp(arg, "vers-nvr-cmp") || !strcmp(arg, "versnvrcmp"))
        comparitor = VERSNVRCMP;
      else if (!strcmp(arg, "rpmvercmp"))
        comparitor = RPMVERCMP;
      else if (!strcmp(arg, "strverscmp"))
        comparitor = STRVERSCMP;
      else
        err(1, "Invalid comparitor \"%s\"", arg);
      break;
    case ARGP_KEY_ARG:
      assert (arguments->ninputs < arguments->input_max);
      arguments->inputs[arguments->ninputs++] = xstrdup (arg);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp = {
  options, argp_parser, "[INPUT_FILES]",
  "Sort a list of strings in RPM version sort order.",
  NULL, help_filter, NULL
};

int
main (int argc, char *argv[])
{
  struct arguments arguments;
  char **package_names = NULL;
  size_t n_package_names = 0;
  int i;

  memset (&arguments, 0, sizeof (struct arguments));
  arguments.input_max = argc+1;
  arguments.inputs = xmalloc ((arguments.input_max + 1)
			      * sizeof (arguments.inputs[0]));
  memset (arguments.inputs, 0, (arguments.input_max + 1)
	  * sizeof (arguments.inputs[0]));

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, 0, &arguments) != 0)
    errx(1, "%s", "Error in parsing command line arguments\n");

  /* If there's no inputs in argv, add one for stdin */
  if (!arguments.ninputs)
    {
      arguments.ninputs = 1;
      arguments.inputs[0] = xmalloc (2);
      strcpy(arguments.inputs[0], "-");
    }

  for (i = 0; i < arguments.ninputs; i++)
    add_input(arguments.inputs[i], &package_names, &n_package_names);

  if (package_names == NULL || n_package_names < 1)
    errx(1, "Invalid input");

  qsort (package_names, n_package_names, sizeof (char *),
	 package_version_compare);

  /* send sorted list to stdout */
  for (i = 0; i < n_package_names; i++)
    {
      fprintf (stdout, "%s\n", package_names[i]);
      free (package_names[i]);
    }

  free (package_names);
  for (i = 0; i < arguments.ninputs; i++)
    free (arguments.inputs[i]);

  free (arguments.inputs);

  return 0;
}
