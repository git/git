/*
 * Totally braindamaged mbox splitter program.
 *
 * It just splits a mbox into a list of files: "0001" "0002" ..
 * so you can process them further from there.
 */
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "cache.h"

static const char git_mailsplit_usage[] =
"git-mailsplit [-d<prec>] [<mbox>] <directory>";

static int is_from_line(const char *line, int len)
{
	const char *colon;

	if (len < 20 || memcmp("From ", line, 5))
		return 0;

	colon = line + len - 2;
	line += 5;
	for (;;) {
		if (colon < line)
			return 0;
		if (*--colon == ':')
			break;
	}

	if (!isdigit(colon[-4]) ||
	    !isdigit(colon[-2]) ||
	    !isdigit(colon[-1]) ||
	    !isdigit(colon[ 1]) ||
	    !isdigit(colon[ 2]))
		return 0;

	/* year */
	if (strtol(colon+3, NULL, 10) <= 90)
		return 0;

	/* Ok, close enough */
	return 1;
}

/* Could be as small as 64, enough to hold a Unix "From " line. */
static char buf[4096];

/* Called with the first line (potentially partial)
 * already in buf[] -- normally that should begin with
 * the Unix "From " line.  Write it into the specified
 * file.
 */
static int split_one(FILE *mbox, const char *name)
{
	FILE *output = NULL;
	int len = strlen(buf);
	int fd;
	int status = 0;

	if (!is_from_line(buf, len))
		goto corrupt;

	fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0)
		die("cannot open output file %s", name);
	output = fdopen(fd, "w");

	/* Copy it out, while searching for a line that begins with
	 * "From " and having something that looks like a date format.
	 */
	for (;;) {
		int is_partial = (buf[len-1] != '\n');

		if (fputs(buf, output) == EOF)
			die("cannot write output");

		if (fgets(buf, sizeof(buf), mbox) == NULL) {
			if (feof(mbox)) {
				status = 1;
				break;
			}
			die("cannot read mbox");
		}
		len = strlen(buf);
		if (!is_partial && is_from_line(buf, len))
			break; /* done with one message */
	}
	fclose(output);
	return status;

 corrupt:
	if (output)
		fclose(output);
	unlink(name);
	fprintf(stderr, "corrupt mailbox\n");
	exit(1);
}

int main(int argc, const char **argv)
{
	int i, nr, nr_prec = 4;
	FILE *mbox = NULL;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-')
			break;
		/* do flags here */
		if (!strncmp(arg, "-d", 2)) {
			nr_prec = strtol(arg + 2, NULL, 10);
			if (nr_prec < 3 || 10 <= nr_prec)
				usage(git_mailsplit_usage);
			continue;
		}
	}

	/* Either one remaining arg (dir), or two (mbox and dir) */
	switch (argc - i) {
	case 1:
		mbox = stdin;
		break;
	case 2:
		if ((mbox = fopen(argv[i], "r")) == NULL)
			die("cannot open mbox %s for reading", argv[i]);
		break;
	default:
		usage(git_mailsplit_usage);
	}
	if (chdir(argv[argc - 1]) < 0)
		usage(git_mailsplit_usage);

	nr = 0;
	if (fgets(buf, sizeof(buf), mbox) == NULL)
		die("cannot read mbox");

	for (;;) {
		char name[10];

		sprintf(name, "%0*d", nr_prec, ++nr);
		switch (split_one(mbox, name)) {
		case 0:
			break;
		case 1:
			printf("%d\n", nr);
			return 0;
		default:
			exit(1);
		}
	}
}
