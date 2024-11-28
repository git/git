/*
 * Totally braindamaged mbox splitter program.
 *
 * It just splits a mbox into a list of files: "0001" "0002" ..
 * so you can process them further from there.
 */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "gettext.h"
#include "string-list.h"
#include "strbuf.h"

static const char git_mailsplit_usage[] =
"git mailsplit [-d<prec>] [-f<n>] [-b] [--keep-cr] -o<directory> [(<mbox>|<Maildir>)...]";

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

static struct strbuf buf = STRBUF_INIT;
static int keep_cr;
static int mboxrd;

static int is_gtfrom(const struct strbuf *buf)
{
	size_t min = strlen(">From ");
	size_t ngt;

	if (buf->len < min)
		return 0;

	ngt = strspn(buf->buf, ">");
	return ngt && starts_with(buf->buf + ngt, "From ");
}

/* Called with the first line (potentially partial)
 * already in buf[] -- normally that should begin with
 * the Unix "From " line.  Write it into the specified
 * file.
 */
static int split_one(FILE *mbox, const char *name, int allow_bare)
{
	FILE *output;
	int fd;
	int status = 0;
	int is_bare = !is_from_line(buf.buf, buf.len);

	if (is_bare && !allow_bare) {
		fprintf(stderr, "corrupt mailbox\n");
		exit(1);
	}
	fd = xopen(name, O_WRONLY | O_CREAT | O_EXCL, 0666);
	output = xfdopen(fd, "w");

	/* Copy it out, while searching for a line that begins with
	 * "From " and having something that looks like a date format.
	 */
	for (;;) {
		if (!keep_cr && buf.len > 1 && buf.buf[buf.len-1] == '\n' &&
			buf.buf[buf.len-2] == '\r') {
			strbuf_setlen(&buf, buf.len-2);
			strbuf_addch(&buf, '\n');
		}

		if (mboxrd && is_gtfrom(&buf))
			strbuf_remove(&buf, 0, 1);

		if (fwrite(buf.buf, 1, buf.len, output) != buf.len)
			die_errno("cannot write output");

		if (strbuf_getwholeline(&buf, mbox, '\n')) {
			if (feof(mbox)) {
				status = 1;
				break;
			}
			die_errno("cannot read mbox");
		}
		if (!is_bare && is_from_line(buf.buf, buf.len))
			break; /* done with one message */
	}
	fclose(output);
	return status;
}

static int populate_maildir_list(struct string_list *list, const char *path)
{
	DIR *dir;
	struct dirent *dent;
	char *name = NULL;
	const char *subs[] = { "cur", "new", NULL };
	const char **sub;
	int ret = -1;

	for (sub = subs; *sub; ++sub) {
		free(name);
		name = xstrfmt("%s/%s", path, *sub);
		if (!(dir = opendir(name))) {
			if (errno == ENOENT)
				continue;
			error_errno("cannot opendir %s", name);
			goto out;
		}

		while ((dent = readdir(dir)) != NULL) {
			if (dent->d_name[0] == '.')
				continue;
			free(name);
			name = xstrfmt("%s/%s", *sub, dent->d_name);
			string_list_insert(list, name);
		}

		closedir(dir);
	}

	ret = 0;

out:
	free(name);
	return ret;
}

static int maildir_filename_cmp(const char *a, const char *b)
{
	while (*a && *b) {
		if (isdigit(*a) && isdigit(*b)) {
			long int na, nb;
			na = strtol(a, (char **)&a, 10);
			nb = strtol(b, (char **)&b, 10);
			if (na != nb)
				return na - nb;
			/* strtol advanced our pointers */
		}
		else {
			if (*a != *b)
				return (unsigned char)*a - (unsigned char)*b;
			a++;
			b++;
		}
	}
	return (unsigned char)*a - (unsigned char)*b;
}

static int split_maildir(const char *maildir, const char *dir,
	int nr_prec, int skip)
{
	char *file = NULL;
	FILE *f = NULL;
	int ret = -1;
	struct string_list list = STRING_LIST_INIT_DUP;

	list.cmp = maildir_filename_cmp;

	if (populate_maildir_list(&list, maildir) < 0)
		goto out;

	for (size_t i = 0; i < list.nr; i++) {
		char *name;

		free(file);
		file = xstrfmt("%s/%s", maildir, list.items[i].string);

		f = fopen(file, "r");
		if (!f) {
			error_errno("cannot open mail %s", file);
			goto out;
		}

		if (strbuf_getwholeline(&buf, f, '\n')) {
			error_errno("cannot read mail %s", file);
			goto out;
		}

		name = xstrfmt("%s/%0*d", dir, nr_prec, ++skip);
		split_one(f, name, 1);
		free(name);

		fclose(f);
		f = NULL;
	}

	ret = skip;
out:
	if (f)
		fclose(f);
	free(file);
	string_list_clear(&list, 1);
	return ret;
}

static int split_mbox(const char *file, const char *dir, int allow_bare,
		      int nr_prec, int skip)
{
	int ret = -1;
	int peek;

	FILE *f = !strcmp(file, "-") ? stdin : fopen(file, "r");
	int file_done = 0;

	if (isatty(fileno(f)))
		warning(_("reading patches from stdin/tty..."));

	if (!f) {
		error_errno("cannot open mbox %s", file);
		goto out;
	}

	do {
		peek = fgetc(f);
		if (peek == EOF) {
			if (f == stdin)
				/* empty stdin is OK */
				ret = skip;
			else {
				fclose(f);
				error(_("empty mbox: '%s'"), file);
			}
			goto out;
		}
	} while (isspace(peek));
	ungetc(peek, f);

	if (strbuf_getwholeline(&buf, f, '\n')) {
		/* empty stdin is OK */
		if (f != stdin) {
			error("cannot read mbox %s", file);
			goto out;
		}
		file_done = 1;
	}

	while (!file_done) {
		char *name = xstrfmt("%s/%0*d", dir, nr_prec, ++skip);
		file_done = split_one(f, name, allow_bare);
		free(name);
	}

	if (f != stdin)
		fclose(f);

	ret = skip;
out:
	return ret;
}

int cmd_mailsplit(int argc,
		  const char **argv,
		  const char *prefix,
		  struct repository *repo UNUSED)
{
	int nr = 0, nr_prec = 4, num = 0;
	int allow_bare = 0;
	const char *dir = NULL;
	const char **argp;
	static const char *stdin_only[] = { "-", NULL };

	BUG_ON_NON_EMPTY_PREFIX(prefix);

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
		} else if ( arg[1] == 'h' ) {
			usage(git_mailsplit_usage);
		} else if ( arg[1] == 'b' && !arg[2] ) {
			allow_bare = 1;
		} else if (!strcmp(arg, "--keep-cr")) {
			keep_cr = 1;
		} else if ( arg[1] == 'o' && arg[2] ) {
			dir = arg+2;
		} else if (!strcmp(arg, "--mboxrd")) {
			mboxrd = 1;
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
			error_errno("cannot stat %s", arg);
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
