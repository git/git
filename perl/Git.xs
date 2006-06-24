/* By carefully stacking #includes here (even if WE don't really need them)
 * we strive to make the thing actually compile. Git header files aren't very
 * nice. Perl headers are one of the signs of the coming apocalypse. */
#include <ctype.h>
/* Ok, it hasn't been so bad so far. */

/* libgit interface */
#include "../cache.h"
#include "../exec_cmd.h"

/* XS and Perl interface */
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"


MODULE = Git		PACKAGE = Git

PROTOTYPES: DISABLE

# /* TODO: xs_call_gate(). See Git.pm. */


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
xs_hash_object(file, type = "blob")
	SV *file;
	char *type;
CODE:
{
	unsigned char sha1[20];

	if (SvTYPE(file) == SVt_RV)
		file = SvRV(file);

	if (SvTYPE(file) == SVt_PVGV) {
		/* Filehandle */
		PerlIO *pio;

		pio = IoIFP(sv_2io(file));
		if (!pio)
			croak("You passed me something weird - a dir glob?");
		/* XXX: I just hope PerlIO didn't read anything from it yet.
		 * --pasky */
		if (index_pipe(sha1, PerlIO_fileno(pio), type, 0))
			croak("Unable to hash given filehandle");
		/* Avoid any nasty surprises. */
		PerlIO_close(pio);

	} else {
		/* String */
		char *path = SvPV_nolen(file);
		int fd = open(path, O_RDONLY);
		struct stat st;

		if (fd < 0 ||
		    fstat(fd, &st) < 0 ||
		    index_fd(sha1, fd, &st, 0, type))
			croak("Unable to hash %s", path);
		close(fd);
	}
	RETVAL = sha1_to_hex(sha1);
}
OUTPUT:
	RETVAL
