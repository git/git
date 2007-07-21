/*
 * Totally braindamaged mbox splitter program.
 *
 * It just splits a mbox into a list of files: "0001" "0002" ..
 * so you can process them further from there.
 */
#include "cache.h"
#include "builtin.h"
#include "path-list.h"

static const char git_mailsplit_usage[] =
"git-mailsplit [-d<prec>] [-f<n>] [-b] -o<directory> <mbox>|<Maildir>...";

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
static int split_one(FILE *mbox, const char *name, int allow_bare)
{
	FILE *output = NULL;
	int len = strlen(buf);
	int fd;
	int status = 0;
	int is_bare = !is_from_line(buf, len);

	if (is_bare && !allow_bare)
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
		if (!is_partial && !is_bare && is_from_line(buf, len))
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

static int populate_maildir_list(struct path_list *list, const char *path)
{
	DIR *dir;
	struct dirent *dent;

	if ((dir = opendir(path)) == NULL) {
		error("cannot opendir %s (%s)", path, strerror(errno));
		return -1;
	}

	while ((dent = readdir(dir)) != NULL) {
		if (dent->d_name[0] == '.')
			continue;
		path_list_insert(dent->d_name, list);
	}

	closedir(dir);

	return 0;
}

static int split_maildir(const char *maildir, const char *dir,
	int nr_prec, int skip)
{
	char file[PATH_MAX];
	char curdir[PATH_MAX];
	char name[PATH_MAX];
	int ret = -1;
	int i;
	struct path_list list = {NULL, 0, 0, 1};

	snprintf(curdir, sizeof(curdir), "%s/cur", maildir);
	if (populate_maildir_list(&list, curdir) < 0)
		goto out;

	for (i = 0; i < list.nr; i++) {
		FILE *f;
		snprintf(file, sizeof(file), "%s/%s", curdir, list.items[i].path);
		f = fopen(file, "r");
		if (!f) {
			error("cannot open mail %s (%s)", file, strerror(errno));
			goto out;
		}

		if (fgets(buf, sizeof(buf), f) == NULL) {
			error("cannot read mail %s (%s)", file, strerror(errno));
			goto out;
		}

		sprintf(name, "%s/%0*d", dir, nr_prec, ++skip);
		split_one(f, name, 1);

		fclose(f);
	}

	path_list_clear(&list, 1);

	ret = skip;
out:
	return ret;
}

static int split_mbox(const char *file, const char *dir, int allow_bare,
		      int nr_prec, int skip)
{
	char name[PATH_MAX];
	int ret = -1;

	FILE *f = !strcmp(file, "-") ? stdin : fopen(file, "r");
	int file_done = 0;

	if (!f) {
		error("cannot open mbox %s", file);
		goto out;
	}

	if (fgets(buf, sizeof(buf), f) == NULL) {
		/* empty stdin is OK */
		if (f != stdin) {
			error("cannot read mbox %s", file);
			goto out;
		}
		file_done = 1;
	}

	while (!file_done) {
		sprintf(name, "%s/%0*d", dir, nr_prec, ++skip);
		file_done = split_one(f, name, allow_bare);
	}

	if (f != stdin)
		fclose(f);

	ret = skip;
out:
	return ret;
}

int cmd_mailsplit(int argc, const char **argv, const char *prefix)
{
	int nr = 0, nr_prec = 4, num = 0;
	int allow_bare = 0;
	const char *dir = NULL;
	const char **argp;
	static const char *stdin_only[] = { "-", NULL };

	for (argp = argv+1; *argp; argp++) {
		const char *arg = *argp;

		if (arg[0] != '-')
			break;
		/* do flags here */
		if ( arg[1] == 'd' ) {
			nr_prec = strtol(arg+2, NULL, 10);
			if (nr_prec < 3 || 10 <= nr_prec)
				usage(git_mailsplit_usage);
			continue;
		} else if ( arg[1] == 'f' ) {
			nr = strtol(arg+2, NULL, 10);
		} else if ( arg[1] == 'b' && !arg[2] ) {
			allow_bare = 1;
		} else if ( arg[1] == 'o' && arg[2] ) {
			dir = arg+2;
		} else if ( arg[1] == '-' && !arg[2] ) {
			argp++;	/* -- marks end of options */
			break;
		} else {
			die("unknown option: %s", arg);
		}
	}

	if ( !dir ) {
		/* Backwards compatibility: if no -o specified, accept
		   <mbox> <dir> or just <dir> */
		switch (argc - (argp-argv)) {
		case 1:
			dir = argp[0];
			argp = stdin_only;
			break;
		case 2:
			stdin_only[0] = argp[0];
			dir = argp[1];
			argp = stdin_only;
			break;
		default:
			usage(git_mailsplit_usage);
		}
	} else {
		/* New usage: if no more argument, parse stdin */
		if ( !*argp )
			argp = stdin_only;
	}

	while (*argp) {
		const char *arg = *argp++;
		struct stat argstat;
		int ret = 0;

		if (arg[0] == '-' && arg[1] == 0) {
			ret = split_mbox(arg, dir, allow_bare, nr_prec, nr);
			if (ret < 0) {
				error("cannot split patches from stdin");
				return 1;
			}
			num += (ret - nr);
			nr = ret;
			continue;
		}

		if (stat(arg, &argstat) == -1) {
			error("cannot stat %s (%s)", arg, strerror(errno));
			return 1;
		}

		if (S_ISDIR(argstat.st_mode))
			ret = split_maildir(arg, dir, nr_prec, nr);
		else
			ret = split_mbox(arg, dir, allow_bare, nr_prec, nr);

		if (ret < 0) {
			error("cannot split patches from %s", arg);
			return 1;
		}
		num += (ret - nr);
		nr = ret;
	}

	printf("%d\n", num);

	return 0;
}
