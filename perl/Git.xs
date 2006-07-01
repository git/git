/* By carefully stacking #includes here (even if WE don't really need them)
 * we strive to make the thing actually compile. Git header files aren't very
 * nice. Perl headers are one of the signs of the coming apocalypse. */
#include <ctype.h>
/* Ok, it hasn't been so bad so far. */

/* libgit interface */
#include "../cache.h"
#include "../exec_cmd.h"

#define die perlyshadow_die__

/* XS and Perl interface */
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#undef die


static char *
report_xs(const char *prefix, const char *err, va_list params)
{
	static char buf[4096];
	strcpy(buf, prefix);
	vsnprintf(buf + strlen(prefix), 4096 - strlen(prefix), err, params);
	return buf;
}

static void NORETURN
die_xs(const char *err, va_list params)
{
	char *str;
	str = report_xs("fatal: ", err, params);
	croak(str);
}

static void
error_xs(const char *err, va_list params)
{
	char *str;
	str = report_xs("error: ", err, params);
	warn(str);
}


MODULE = Git		PACKAGE = Git

PROTOTYPES: DISABLE


BOOT:
{
	set_error_routine(error_xs);
	set_die_routine(die_xs);
}


# /* TODO: xs_call_gate(). See Git.pm. */


const char *
xs_version()
CODE:
{
	RETVAL = GIT_VERSION;
}
OUTPUT:
	RETVAL


const char *
xs_exec_path()
CODE:
{
	RETVAL = git_exec_path();
}
OUTPUT:
	RETVAL


void
xs__execv_git_cmd(...)
CODE:
{
	const char **argv;
	int i;

	argv = malloc(sizeof(const char *) * (items + 1));
	if (!argv)
		croak("malloc failed");
	for (i = 0; i < items; i++)
		argv[i] = strdup(SvPV_nolen(ST(i)));
	argv[i] = NULL;

	execv_git_cmd(argv);

	for (i = 0; i < items; i++)
		if (argv[i])
			free((char *) argv[i]);
	free((char **) argv);
}

char *
xs_hash_object_pipe(type, fd)
	char *type;
	int fd;
CODE:
{
	unsigned char sha1[20];

	if (index_pipe(sha1, fd, type, 0))
		croak("Unable to hash given filehandle");
	RETVAL = sha1_to_hex(sha1);
}
OUTPUT:
	RETVAL

char *
xs_hash_object_file(type, path)
	char *type;
	char *path;
CODE:
{
	unsigned char sha1[20];
	int fd = open(path, O_RDONLY);
	struct stat st;

	if (fd < 0 ||
	    fstat(fd, &st) < 0 ||
	    index_fd(sha1, fd, &st, 0, type))
		croak("Unable to hash %s", path);
	close(fd);

	RETVAL = sha1_to_hex(sha1);
}
OUTPUT:
	RETVAL
