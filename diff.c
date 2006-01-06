/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "cache.h"
#include "quote.h"
#include "diff.h"
#include "diffcore.h"

static const char *diff_opts = "-pu";

static int use_size_cache;

int diff_rename_limit_default = -1;

int git_diff_config(const char *var, const char *value)
{
	if (!strcmp(var, "diff.renamelimit")) {
		diff_rename_limit_default = git_config_int(var, value);
		return 0;
	}

	return git_default_config(var, value);
}

static char *quote_one(const char *str)
{
	int needlen;
	char *xp;

	if (!str)
		return NULL;
	needlen = quote_c_style(str, NULL, NULL, 0);
	if (!needlen)
		return strdup(str);
	xp = xmalloc(needlen + 1);
	quote_c_style(str, xp, NULL, 0);
	return xp;
}

static char *quote_two(const char *one, const char *two)
{
	int need_one = quote_c_style(one, NULL, NULL, 1);
	int need_two = quote_c_style(two, NULL, NULL, 1);
	char *xp;

	if (need_one + need_two) {
		if (!need_one) need_one = strlen(one);
		if (!need_two) need_one = strlen(two);

		xp = xmalloc(need_one + need_two + 3);
		xp[0] = '"';
		quote_c_style(one, xp + 1, NULL, 1);
		quote_c_style(two, xp + need_one + 1, NULL, 1);
		strcpy(xp + need_one + need_two + 1, "\"");
		return xp;
	}
	need_one = strlen(one);
	need_two = strlen(two);
	xp = xmalloc(need_one + need_two + 1);
	strcpy(xp, one);
	strcpy(xp + need_one, two);
	return xp;
}

static const char *external_diff(void)
{
	static const char *external_diff_cmd = NULL;
	static int done_preparing = 0;
	const char *env_diff_opts;

	if (done_preparing)
		return external_diff_cmd;

	/*
	 * Default values above are meant to match the
	 * Linux kernel development style.  Examples of
	 * alternative styles you can specify via environment
	 * variables are:
	 *
	 * GIT_DIFF_OPTS="-c";
	 */
	external_diff_cmd = getenv("GIT_EXTERNAL_DIFF");

	/* In case external diff fails... */
	env_diff_opts = getenv("GIT_DIFF_OPTS");
	if (env_diff_opts) diff_opts = env_diff_opts;

	done_preparing = 1;
	return external_diff_cmd;
}

#define TEMPFILE_PATH_LEN		50

static struct diff_tempfile {
	const char *name; /* filename external diff should read from */
	char hex[41];
	char mode[10];
	char tmp_path[TEMPFILE_PATH_LEN];
} diff_temp[2];

static int count_lines(const char *filename)
{
	FILE *in;
	int count, ch, completely_empty = 1, nl_just_seen = 0;
	in = fopen(filename, "r");
	count = 0;
	while ((ch = fgetc(in)) != EOF)
		if (ch == '\n') {
			count++;
			nl_just_seen = 1;
			completely_empty = 0;
		}
		else {
			nl_just_seen = 0;
			completely_empty = 0;
		}
	fclose(in);
	if (completely_empty)
		return 0;
	if (!nl_just_seen)
		count++; /* no trailing newline */
	return count;
}

static void print_line_count(int count)
{
	switch (count) {
	case 0:
		printf("0,0");
		break;
	case 1:
		printf("1");
		break;
	default:
		printf("1,%d", count);
		break;
	}
}

static void copy_file(int prefix, const char *filename)
{
	FILE *in;
	int ch, nl_just_seen = 1;
	in = fopen(filename, "r");
	while ((ch = fgetc(in)) != EOF) {
		if (nl_just_seen)
			putchar(prefix);
		putchar(ch);
		if (ch == '\n')
			nl_just_seen = 1;
		else
			nl_just_seen = 0;
	}
	fclose(in);
	if (!nl_just_seen)
		printf("\n\\ No newline at end of file\n");
}

static void emit_rewrite_diff(const char *name_a,
			      const char *name_b,
			      struct diff_tempfile *temp)
{
	/* Use temp[i].name as input, name_a and name_b as labels */
	int lc_a, lc_b;
	lc_a = count_lines(temp[0].name);
	lc_b = count_lines(temp[1].name);
	printf("--- %s\n+++ %s\n@@ -", name_a, name_b);
	print_line_count(lc_a);
	printf(" +");
	print_line_count(lc_b);
	printf(" @@\n");
	if (lc_a)
		copy_file('-', temp[0].name);
	if (lc_b)
		copy_file('+', temp[1].name);
}

static void builtin_diff(const char *name_a,
			 const char *name_b,
			 struct diff_tempfile *temp,
			 const char *xfrm_msg,
			 int complete_rewrite)
{
	int i, next_at, cmd_size;
	const char *const diff_cmd = "diff -L%s -L%s";
	const char *const diff_arg  = "-- %s %s||:"; /* "||:" is to return 0 */
	const char *input_name_sq[2];
	const char *label_path[2];
	char *cmd;

	/* diff_cmd and diff_arg have 4 %s in total which makes
	 * the sum of these strings 8 bytes larger than required.
	 * we use 2 spaces around diff-opts, and we need to count
	 * terminating NUL; we used to subtract 5 here, but we do not
	 * care about small leaks in this subprocess that is about
	 * to exec "diff" anymore.
	 */
	cmd_size = (strlen(diff_cmd) + strlen(diff_opts) + strlen(diff_arg)
		    + 128);

	for (i = 0; i < 2; i++) {
		input_name_sq[i] = sq_quote(temp[i].name);
		if (!strcmp(temp[i].name, "/dev/null"))
			label_path[i] = "/dev/null";
		else if (!i)
			label_path[i] = sq_quote(quote_two("a/", name_a));
		else
			label_path[i] = sq_quote(quote_two("b/", name_b));
		cmd_size += (strlen(label_path[i]) + strlen(input_name_sq[i]));
	}

	cmd = xmalloc(cmd_size);

	next_at = 0;
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    diff_cmd, label_path[0], label_path[1]);
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    " %s ", diff_opts);
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    diff_arg, input_name_sq[0], input_name_sq[1]);

	printf("diff --git %s %s\n",
	       quote_two("a/", name_a), quote_two("b/", name_b));
	if (label_path[0][0] == '/') {
		/* dev/null */
		printf("new file mode %s\n", temp[1].mode);
		if (xfrm_msg && xfrm_msg[0])
			puts(xfrm_msg);
	}
	else if (label_path[1][0] == '/') {
		printf("deleted file mode %s\n", temp[0].mode);
		if (xfrm_msg && xfrm_msg[0])
			puts(xfrm_msg);
	}
	else {
		if (strcmp(temp[0].mode, temp[1].mode)) {
			printf("old mode %s\n", temp[0].mode);
			printf("new mode %s\n", temp[1].mode);
		}
		if (xfrm_msg && xfrm_msg[0])
			puts(xfrm_msg);
		if (strncmp(temp[0].mode, temp[1].mode, 3))
			/* we do not run diff between different kind
			 * of objects.
			 */
			exit(0);
		if (complete_rewrite) {
			fflush(NULL);
			emit_rewrite_diff(name_a, name_b, temp);
			exit(0);
		}
	}
	fflush(NULL);
	execlp("/bin/sh","sh", "-c", cmd, NULL);
}

struct diff_filespec *alloc_filespec(const char *path)
{
	int namelen = strlen(path);
	struct diff_filespec *spec = xmalloc(sizeof(*spec) + namelen + 1);

	memset(spec, 0, sizeof(*spec));
	spec->path = (char *)(spec + 1);
	memcpy(spec->path, path, namelen+1);
	return spec;
}

void fill_filespec(struct diff_filespec *spec, const unsigned char *sha1,
		   unsigned short mode)
{
	if (mode) {
		spec->mode = DIFF_FILE_CANON_MODE(mode);
		memcpy(spec->sha1, sha1, 20);
		spec->sha1_valid = !!memcmp(sha1, null_sha1, 20);
	}
}

/*
 * Given a name and sha1 pair, if the dircache tells us the file in
 * the work tree has that object contents, return true, so that
 * prepare_temp_file() does not have to inflate and extract.
 */
static int work_tree_matches(const char *name, const unsigned char *sha1)
{
	struct cache_entry *ce;
	struct stat st;
	int pos, len;

	/* We do not read the cache ourselves here, because the
	 * benchmark with my previous version that always reads cache
	 * shows that it makes things worse for diff-tree comparing
	 * two linux-2.6 kernel trees in an already checked out work
	 * tree.  This is because most diff-tree comparisons deal with
	 * only a small number of files, while reading the cache is
	 * expensive for a large project, and its cost outweighs the
	 * savings we get by not inflating the object to a temporary
	 * file.  Practically, this code only helps when we are used
	 * by diff-cache --cached, which does read the cache before
	 * calling us.
	 */
	if (!active_cache)
		return 0;

	len = strlen(name);
	pos = cache_name_pos(name, len);
	if (pos < 0)
		return 0;
	ce = active_cache[pos];
	if ((lstat(name, &st) < 0) ||
	    !S_ISREG(st.st_mode) || /* careful! */
	    ce_match_stat(ce, &st) ||
	    memcmp(sha1, ce->sha1, 20))
		return 0;
	/* we return 1 only when we can stat, it is a regular file,
	 * stat information matches, and sha1 recorded in the cache
	 * matches.  I.e. we know the file in the work tree really is
	 * the same as the <name, sha1> pair.
	 */
	return 1;
}

static struct sha1_size_cache {
	unsigned char sha1[20];
	unsigned long size;
} **sha1_size_cache;
static int sha1_size_cache_nr, sha1_size_cache_alloc;

static struct sha1_size_cache *locate_size_cache(unsigned char *sha1,
						 int find_only,
						 unsigned long size)
{
	int first, last;
	struct sha1_size_cache *e;

	first = 0;
	last = sha1_size_cache_nr;
	while (last > first) {
		int cmp, next = (last + first) >> 1;
		e = sha1_size_cache[next];
		cmp = memcmp(e->sha1, sha1, 20);
		if (!cmp)
			return e;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	/* not found */
	if (find_only)
		return NULL;
	/* insert to make it at "first" */
	if (sha1_size_cache_alloc <= sha1_size_cache_nr) {
		sha1_size_cache_alloc = alloc_nr(sha1_size_cache_alloc);
		sha1_size_cache = xrealloc(sha1_size_cache,
					   sha1_size_cache_alloc *
					   sizeof(*sha1_size_cache));
	}
	sha1_size_cache_nr++;
	if (first < sha1_size_cache_nr)
		memmove(sha1_size_cache + first + 1, sha1_size_cache + first,
			(sha1_size_cache_nr - first - 1) *
			sizeof(*sha1_size_cache));
	e = xmalloc(sizeof(struct sha1_size_cache));
	sha1_size_cache[first] = e;
	memcpy(e->sha1, sha1, 20);
	e->size = size;
	return e;
}

/*
 * While doing rename detection and pickaxe operation, we may need to
 * grab the data for the blob (or file) for our own in-core comparison.
 * diff_filespec has data and size fields for this purpose.
 */
int diff_populate_filespec(struct diff_filespec *s, int size_only)
{
	int err = 0;
	if (!DIFF_FILE_VALID(s))
		die("internal error: asking to populate invalid file.");
	if (S_ISDIR(s->mode))
		return -1;

	if (!use_size_cache)
		size_only = 0;

	if (s->data)
		return err;
	if (!s->sha1_valid ||
	    work_tree_matches(s->path, s->sha1)) {
		struct stat st;
		int fd;
		if (lstat(s->path, &st) < 0) {
			if (errno == ENOENT) {
			err_empty:
				err = -1;
			empty:
				s->data = "";
				s->size = 0;
				return err;
			}
		}
		s->size = st.st_size;
		if (!s->size)
			goto empty;
		if (size_only)
			return 0;
		if (S_ISLNK(st.st_mode)) {
			int ret;
			s->data = xmalloc(s->size);
			s->should_free = 1;
			ret = readlink(s->path, s->data, s->size);
			if (ret < 0) {
				free(s->data);
				goto err_empty;
			}
			return 0;
		}
		fd = open(s->path, O_RDONLY);
		if (fd < 0)
			goto err_empty;
		s->data = mmap(NULL, s->size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (s->data == MAP_FAILED)
			goto err_empty;
		s->should_munmap = 1;
	}
	else {
		char type[20];
		struct sha1_size_cache *e;

		if (size_only) {
			e = locate_size_cache(s->sha1, 1, 0);
			if (e) {
				s->size = e->size;
				return 0;
			}
			if (!sha1_object_info(s->sha1, type, &s->size))
				locate_size_cache(s->sha1, 0, s->size);
		}
		else {
			s->data = read_sha1_file(s->sha1, type, &s->size);
			s->should_free = 1;
		}
	}
	return 0;
}

void diff_free_filespec_data(struct diff_filespec *s)
{
	if (s->should_free)
		free(s->data);
	else if (s->should_munmap)
		munmap(s->data, s->size);
	s->should_free = s->should_munmap = 0;
	s->data = NULL;
}

static void prep_temp_blob(struct diff_tempfile *temp,
			   void *blob,
			   unsigned long size,
			   const unsigned char *sha1,
			   int mode)
{
	int fd;

	fd = git_mkstemp(temp->tmp_path, TEMPFILE_PATH_LEN, ".diff_XXXXXX");
	if (fd < 0)
		die("unable to create temp-file");
	if (write(fd, blob, size) != size)
		die("unable to write temp-file");
	close(fd);
	temp->name = temp->tmp_path;
	strcpy(temp->hex, sha1_to_hex(sha1));
	temp->hex[40] = 0;
	sprintf(temp->mode, "%06o", mode);
}

static void prepare_temp_file(const char *name,
			      struct diff_tempfile *temp,
			      struct diff_filespec *one)
{
	if (!DIFF_FILE_VALID(one)) {
	not_a_valid_file:
		/* A '-' entry produces this for file-2, and
		 * a '+' entry produces this for file-1.
		 */
		temp->name = "/dev/null";
		strcpy(temp->hex, ".");
		strcpy(temp->mode, ".");
		return;
	}

	if (!one->sha1_valid ||
	    work_tree_matches(name, one->sha1)) {
		struct stat st;
		if (lstat(name, &st) < 0) {
			if (errno == ENOENT)
				goto not_a_valid_file;
			die("stat(%s): %s", name, strerror(errno));
		}
		if (S_ISLNK(st.st_mode)) {
			int ret;
			char buf[PATH_MAX + 1]; /* ought to be SYMLINK_MAX */
			if (sizeof(buf) <= st.st_size)
				die("symlink too long: %s", name);
			ret = readlink(name, buf, st.st_size);
			if (ret < 0)
				die("readlink(%s)", name);
			prep_temp_blob(temp, buf, st.st_size,
				       (one->sha1_valid ?
					one->sha1 : null_sha1),
				       (one->sha1_valid ?
					one->mode : S_IFLNK));
		}
		else {
			/* we can borrow from the file in the work tree */
			temp->name = name;
			if (!one->sha1_valid)
				strcpy(temp->hex, sha1_to_hex(null_sha1));
			else
				strcpy(temp->hex, sha1_to_hex(one->sha1));
			/* Even though we may sometimes borrow the
			 * contents from the work tree, we always want
			 * one->mode.  mode is trustworthy even when
			 * !(one->sha1_valid), as long as
			 * DIFF_FILE_VALID(one).
			 */
			sprintf(temp->mode, "%06o", one->mode);
		}
		return;
	}
	else {
		if (diff_populate_filespec(one, 0))
			die("cannot read data blob for %s", one->path);
		prep_temp_blob(temp, one->data, one->size,
			       one->sha1, one->mode);
	}
}

static void remove_tempfile(void)
{
	int i;

	for (i = 0; i < 2; i++)
		if (diff_temp[i].name == diff_temp[i].tmp_path) {
			unlink(diff_temp[i].name);
			diff_temp[i].name = NULL;
		}
}

static void remove_tempfile_on_signal(int signo)
{
	remove_tempfile();
}

/* An external diff command takes:
 *
 * diff-cmd name infile1 infile1-sha1 infile1-mode \
 *               infile2 infile2-sha1 infile2-mode [ rename-to ]
 *
 */
static void run_external_diff(const char *pgm,
			      const char *name,
			      const char *other,
			      struct diff_filespec *one,
			      struct diff_filespec *two,
			      const char *xfrm_msg,
			      int complete_rewrite)
{
	struct diff_tempfile *temp = diff_temp;
	pid_t pid;
	int status;
	static int atexit_asked = 0;
	const char *othername;

	othername = (other? other : name);
	if (one && two) {
		prepare_temp_file(name, &temp[0], one);
		prepare_temp_file(othername, &temp[1], two);
		if (! atexit_asked &&
		    (temp[0].name == temp[0].tmp_path ||
		     temp[1].name == temp[1].tmp_path)) {
			atexit_asked = 1;
			atexit(remove_tempfile);
		}
		signal(SIGINT, remove_tempfile_on_signal);
	}

	fflush(NULL);
	pid = fork();
	if (pid < 0)
		die("unable to fork");
	if (!pid) {
		if (pgm) {
			if (one && two) {
				const char *exec_arg[10];
				const char **arg = &exec_arg[0];
				*arg++ = pgm;
				*arg++ = name;
				*arg++ = temp[0].name;
				*arg++ = temp[0].hex;
				*arg++ = temp[0].mode;
				*arg++ = temp[1].name;
				*arg++ = temp[1].hex;
				*arg++ = temp[1].mode;
				if (other) {
					*arg++ = other;
					*arg++ = xfrm_msg;
				}
				*arg = NULL;
				execvp(pgm, (char *const*) exec_arg);
			}
			else
				execlp(pgm, pgm, name, NULL);
		}
		/*
		 * otherwise we use the built-in one.
		 */
		if (one && two)
			builtin_diff(name, othername, temp, xfrm_msg,
				     complete_rewrite);
		else
			printf("* Unmerged path %s\n", name);
		exit(0);
	}
	if (waitpid(pid, &status, 0) < 0 ||
	    !WIFEXITED(status) || WEXITSTATUS(status)) {
		/* Earlier we did not check the exit status because
		 * diff exits non-zero if files are different, and
		 * we are not interested in knowing that.  It was a
		 * mistake which made it harder to quit a diff-*
		 * session that uses the git-apply-patch-script as
		 * the GIT_EXTERNAL_DIFF.  A custom GIT_EXTERNAL_DIFF
		 * should also exit non-zero only when it wants to
		 * abort the entire diff-* session.
		 */
		remove_tempfile();
		fprintf(stderr, "external diff died, stopping at %s.\n", name);
		exit(1);
	}
	remove_tempfile();
}

static void diff_fill_sha1_info(struct diff_filespec *one)
{
	if (DIFF_FILE_VALID(one)) {
		if (!one->sha1_valid) {
			struct stat st;
			if (lstat(one->path, &st) < 0)
				die("stat %s", one->path);
			if (index_path(one->sha1, one->path, &st, 0))
				die("cannot hash %s\n", one->path);
		}
	}
	else
		memset(one->sha1, 0, 20);
}

static void run_diff(struct diff_filepair *p, struct diff_options *o)
{
	const char *pgm = external_diff();
	char msg[PATH_MAX*2+300], *xfrm_msg;
	struct diff_filespec *one;
	struct diff_filespec *two;
	const char *name;
	const char *other;
	char *name_munged, *other_munged;
	int complete_rewrite = 0;
	int len;

	if (DIFF_PAIR_UNMERGED(p)) {
		/* unmerged */
		run_external_diff(pgm, p->one->path, NULL, NULL, NULL, NULL,
				  0);
		return;
	}

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);
	name_munged = quote_one(name);
	other_munged = quote_one(other);
	one = p->one; two = p->two;

	diff_fill_sha1_info(one);
	diff_fill_sha1_info(two);

	len = 0;
	switch (p->status) {
	case DIFF_STATUS_COPIED:
		len += snprintf(msg + len, sizeof(msg) - len,
				"similarity index %d%%\n"
				"copy from %s\n"
				"copy to %s\n",
				(int)(0.5 + p->score * 100.0/MAX_SCORE),
				name_munged, other_munged);
		break;
	case DIFF_STATUS_RENAMED:
		len += snprintf(msg + len, sizeof(msg) - len,
				"similarity index %d%%\n"
				"rename from %s\n"
				"rename to %s\n",
				(int)(0.5 + p->score * 100.0/MAX_SCORE),
				name_munged, other_munged);
		break;
	case DIFF_STATUS_MODIFIED:
		if (p->score) {
			len += snprintf(msg + len, sizeof(msg) - len,
					"dissimilarity index %d%%\n",
					(int)(0.5 + p->score *
					      100.0/MAX_SCORE));
			complete_rewrite = 1;
			break;
		}
		/* fallthru */
	default:
		/* nothing */
		;
	}

	if (memcmp(one->sha1, two->sha1, 20)) {
		char one_sha1[41];
		int abbrev = o->full_index ? 40 : DIFF_DEFAULT_INDEX_ABBREV;
		memcpy(one_sha1, sha1_to_hex(one->sha1), 41);

		len += snprintf(msg + len, sizeof(msg) - len,
				"index %.*s..%.*s",
				abbrev, one_sha1, abbrev,
				sha1_to_hex(two->sha1));
		if (one->mode == two->mode)
			len += snprintf(msg + len, sizeof(msg) - len,
					" %06o", one->mode);
		len += snprintf(msg + len, sizeof(msg) - len, "\n");
	}

	if (len)
		msg[--len] = 0;
	xfrm_msg = len ? msg : NULL;

	if (!pgm &&
	    DIFF_FILE_VALID(one) && DIFF_FILE_VALID(two) &&
	    (S_IFMT & one->mode) != (S_IFMT & two->mode)) {
		/* a filepair that changes between file and symlink
		 * needs to be split into deletion and creation.
		 */
		struct diff_filespec *null = alloc_filespec(two->path);
		run_external_diff(NULL, name, other, one, null, xfrm_msg, 0);
		free(null);
		null = alloc_filespec(one->path);
		run_external_diff(NULL, name, other, null, two, xfrm_msg, 0);
		free(null);
	}
	else
		run_external_diff(pgm, name, other, one, two, xfrm_msg,
				  complete_rewrite);

	free(name_munged);
	free(other_munged);
}

void diff_setup(struct diff_options *options)
{
	memset(options, 0, sizeof(*options));
	options->output_format = DIFF_FORMAT_RAW;
	options->line_termination = '\n';
	options->break_opt = -1;
	options->rename_limit = -1;

	options->change = diff_change;
	options->add_remove = diff_addremove;
}

int diff_setup_done(struct diff_options *options)
{
	if ((options->find_copies_harder &&
	     options->detect_rename != DIFF_DETECT_COPY) ||
	    (0 <= options->rename_limit && !options->detect_rename))
		return -1;
	if (options->detect_rename && options->rename_limit < 0)
		options->rename_limit = diff_rename_limit_default;
	if (options->setup & DIFF_SETUP_USE_CACHE) {
		if (!active_cache)
			/* read-cache does not die even when it fails
			 * so it is safe for us to do this here.  Also
			 * it does not smudge active_cache or active_nr
			 * when it fails, so we do not have to worry about
			 * cleaning it up ourselves either.
			 */
			read_cache();
	}
	if (options->setup & DIFF_SETUP_USE_SIZE_CACHE)
		use_size_cache = 1;
	if (options->abbrev <= 0 || 40 < options->abbrev)
		options->abbrev = 40; /* full */

	return 0;
}

int diff_opt_parse(struct diff_options *options, const char **av, int ac)
{
	const char *arg = av[0];
	if (!strcmp(arg, "-p") || !strcmp(arg, "-u"))
		options->output_format = DIFF_FORMAT_PATCH;
	else if (!strcmp(arg, "-z"))
		options->line_termination = 0;
	else if (!strncmp(arg, "-l", 2))
		options->rename_limit = strtoul(arg+2, NULL, 10);
	else if (!strcmp(arg, "--full-index"))
		options->full_index = 1;
	else if (!strcmp(arg, "--name-only"))
		options->output_format = DIFF_FORMAT_NAME;
	else if (!strcmp(arg, "--name-status"))
		options->output_format = DIFF_FORMAT_NAME_STATUS;
	else if (!strcmp(arg, "-R"))
		options->reverse_diff = 1;
	else if (!strncmp(arg, "-S", 2))
		options->pickaxe = arg + 2;
	else if (!strcmp(arg, "-s"))
		options->output_format = DIFF_FORMAT_NO_OUTPUT;
	else if (!strncmp(arg, "-O", 2))
		options->orderfile = arg + 2;
	else if (!strncmp(arg, "--diff-filter=", 14))
		options->filter = arg + 14;
	else if (!strcmp(arg, "--pickaxe-all"))
		options->pickaxe_opts = DIFF_PICKAXE_ALL;
	else if (!strncmp(arg, "-B", 2)) {
		if ((options->break_opt =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
	}
	else if (!strncmp(arg, "-M", 2)) {
		if ((options->rename_score =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
		options->detect_rename = DIFF_DETECT_RENAME;
	}
	else if (!strncmp(arg, "-C", 2)) {
		if ((options->rename_score =
		     diff_scoreopt_parse(arg)) == -1)
			return -1;
		options->detect_rename = DIFF_DETECT_COPY;
	}
	else if (!strcmp(arg, "--find-copies-harder"))
		options->find_copies_harder = 1;
	else if (!strcmp(arg, "--abbrev"))
		options->abbrev = DIFF_DEFAULT_ABBREV;
	else if (!strncmp(arg, "--abbrev=", 9))
		options->abbrev = strtoul(arg + 9, NULL, 10);
	else
		return 0;
	return 1;
}

static int parse_num(const char **cp_p)
{
	unsigned long num, scale;
	int ch, dot;
	const char *cp = *cp_p;

	num = 0;
	scale = 1;
	dot = 0;
	for(;;) {
		ch = *cp;
		if ( !dot && ch == '.' ) {
			scale = 1;
			dot = 1;
		} else if ( ch == '%' ) {
			scale = dot ? scale*100 : 100;
			cp++;	/* % is always at the end */
			break;
		} else if ( ch >= '0' && ch <= '9' ) {
			if ( scale < 100000 ) {
				scale *= 10;
				num = (num*10) + (ch-'0');
			}
		} else {
			break;
		}
		cp++;
	}
	*cp_p = cp;

	/* user says num divided by scale and we say internally that
	 * is MAX_SCORE * num / scale.
	 */
	return (num >= scale) ? MAX_SCORE : (MAX_SCORE * num / scale);
}

int diff_scoreopt_parse(const char *opt)
{
	int opt1, opt2, cmd;

	if (*opt++ != '-')
		return -1;
	cmd = *opt++;
	if (cmd != 'M' && cmd != 'C' && cmd != 'B')
		return -1; /* that is not a -M, -C nor -B option */

	opt1 = parse_num(&opt);
	if (cmd != 'B')
		opt2 = 0;
	else {
		if (*opt == 0)
			opt2 = 0;
		else if (*opt != '/')
			return -1; /* we expect -B80/99 or -B80 */
		else {
			opt++;
			opt2 = parse_num(&opt);
		}
	}
	if (*opt != 0)
		return -1;
	return opt1 | (opt2 << 16);
}

struct diff_queue_struct diff_queued_diff;

void diff_q(struct diff_queue_struct *queue, struct diff_filepair *dp)
{
	if (queue->alloc <= queue->nr) {
		queue->alloc = alloc_nr(queue->alloc);
		queue->queue = xrealloc(queue->queue,
					sizeof(dp) * queue->alloc);
	}
	queue->queue[queue->nr++] = dp;
}

struct diff_filepair *diff_queue(struct diff_queue_struct *queue,
				 struct diff_filespec *one,
				 struct diff_filespec *two)
{
	struct diff_filepair *dp = xmalloc(sizeof(*dp));
	dp->one = one;
	dp->two = two;
	dp->score = 0;
	dp->status = 0;
	dp->source_stays = 0;
	dp->broken_pair = 0;
	if (queue)
		diff_q(queue, dp);
	return dp;
}

void diff_free_filepair(struct diff_filepair *p)
{
	diff_free_filespec_data(p->one);
	diff_free_filespec_data(p->two);
	free(p->one);
	free(p->two);
	free(p);
}

/* This is different from find_unique_abbrev() in that
 * it needs to deal with 0{40} SHA1.
 */
const char *diff_unique_abbrev(const unsigned char *sha1, int len)
{
	int abblen;
	const char *abbrev;
	if (len == 40)
		return sha1_to_hex(sha1);

	abbrev = find_unique_abbrev(sha1, len);
	if (!abbrev) {
		if (!memcmp(sha1, null_sha1, 20)) {
			char *buf = sha1_to_hex(null_sha1);
			if (len < 37)
				strcpy(buf + len, "...");
			return buf;
		}
		else 
			return sha1_to_hex(sha1);
	}
	abblen = strlen(abbrev);
	if (abblen < 37) {
		static char hex[41];
		if (len < abblen && abblen <= len + 2)
			sprintf(hex, "%s%.*s", abbrev, len+3-abblen, "..");
		else
			sprintf(hex, "%s...", abbrev);
		return hex;
	}
	return sha1_to_hex(sha1);
}

static void diff_flush_raw(struct diff_filepair *p,
			   int line_termination,
			   int inter_name_termination,
			   struct diff_options *options)
{
	int two_paths;
	char status[10];
	int abbrev = options->abbrev;
	const char *path_one, *path_two;
	int output_format = options->output_format;

	path_one = p->one->path;
	path_two = p->two->path;
	if (line_termination) {
		path_one = quote_one(path_one);
		path_two = quote_one(path_two);
	}

	if (p->score)
		sprintf(status, "%c%03d", p->status,
			(int)(0.5 + p->score * 100.0/MAX_SCORE));
	else {
		status[0] = p->status;
		status[1] = 0;
	}
	switch (p->status) {
	case DIFF_STATUS_COPIED:
	case DIFF_STATUS_RENAMED:
		two_paths = 1;
		break;
	case DIFF_STATUS_ADDED:
	case DIFF_STATUS_DELETED:
		two_paths = 0;
		break;
	default:
		two_paths = 0;
		break;
	}
	if (output_format != DIFF_FORMAT_NAME_STATUS) {
		printf(":%06o %06o %s ",
		       p->one->mode, p->two->mode,
		       diff_unique_abbrev(p->one->sha1, abbrev));
		printf("%s ",
		       diff_unique_abbrev(p->two->sha1, abbrev));
	}
	printf("%s%c%s", status, inter_name_termination, path_one);
	if (two_paths)
		printf("%c%s", inter_name_termination, path_two);
	putchar(line_termination);
	if (path_one != p->one->path)
		free((void*)path_one);
	if (path_two != p->two->path)
		free((void*)path_two);
}

static void diff_flush_name(struct diff_filepair *p,
			    int inter_name_termination,
			    int line_termination)
{
	char *path = p->two->path;

	if (line_termination)
		path = quote_one(p->two->path);
	else
		path = p->two->path;
	printf("%s%c", path, line_termination);
	if (p->two->path != path)
		free(path);
}

int diff_unmodified_pair(struct diff_filepair *p)
{
	/* This function is written stricter than necessary to support
	 * the currently implemented transformers, but the idea is to
	 * let transformers to produce diff_filepairs any way they want,
	 * and filter and clean them up here before producing the output.
	 */
	struct diff_filespec *one, *two;

	if (DIFF_PAIR_UNMERGED(p))
		return 0; /* unmerged is interesting */

	one = p->one;
	two = p->two;

	/* deletion, addition, mode or type change
	 * and rename are all interesting.
	 */
	if (DIFF_FILE_VALID(one) != DIFF_FILE_VALID(two) ||
	    DIFF_PAIR_MODE_CHANGED(p) ||
	    strcmp(one->path, two->path))
		return 0;

	/* both are valid and point at the same path.  that is, we are
	 * dealing with a change.
	 */
	if (one->sha1_valid && two->sha1_valid &&
	    !memcmp(one->sha1, two->sha1, sizeof(one->sha1)))
		return 1; /* no change */
	if (!one->sha1_valid && !two->sha1_valid)
		return 1; /* both look at the same file on the filesystem. */
	return 0;
}

static void diff_flush_patch(struct diff_filepair *p, struct diff_options *o)
{
	if (diff_unmodified_pair(p))
		return;

	if ((DIFF_FILE_VALID(p->one) && S_ISDIR(p->one->mode)) ||
	    (DIFF_FILE_VALID(p->two) && S_ISDIR(p->two->mode)))
		return; /* no tree diffs in patch format */ 

	run_diff(p, o);
}

int diff_queue_is_empty(void)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	for (i = 0; i < q->nr; i++)
		if (!diff_unmodified_pair(q->queue[i]))
			return 0;
	return 1;
}

#if DIFF_DEBUG
void diff_debug_filespec(struct diff_filespec *s, int x, const char *one)
{
	fprintf(stderr, "queue[%d] %s (%s) %s %06o %s\n",
		x, one ? one : "",
		s->path,
		DIFF_FILE_VALID(s) ? "valid" : "invalid",
		s->mode,
		s->sha1_valid ? sha1_to_hex(s->sha1) : "");
	fprintf(stderr, "queue[%d] %s size %lu flags %d\n",
		x, one ? one : "",
		s->size, s->xfrm_flags);
}

void diff_debug_filepair(const struct diff_filepair *p, int i)
{
	diff_debug_filespec(p->one, i, "one");
	diff_debug_filespec(p->two, i, "two");
	fprintf(stderr, "score %d, status %c stays %d broken %d\n",
		p->score, p->status ? p->status : '?',
		p->source_stays, p->broken_pair);
}

void diff_debug_queue(const char *msg, struct diff_queue_struct *q)
{
	int i;
	if (msg)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "q->nr = %d\n", q->nr);
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		diff_debug_filepair(p, i);
	}
}
#endif

static void diff_resolve_rename_copy(void)
{
	int i, j;
	struct diff_filepair *p, *pp;
	struct diff_queue_struct *q = &diff_queued_diff;

	diff_debug_queue("resolve-rename-copy", q);

	for (i = 0; i < q->nr; i++) {
		p = q->queue[i];
		p->status = 0; /* undecided */
		if (DIFF_PAIR_UNMERGED(p))
			p->status = DIFF_STATUS_UNMERGED;
		else if (!DIFF_FILE_VALID(p->one))
			p->status = DIFF_STATUS_ADDED;
		else if (!DIFF_FILE_VALID(p->two))
			p->status = DIFF_STATUS_DELETED;
		else if (DIFF_PAIR_TYPE_CHANGED(p))
			p->status = DIFF_STATUS_TYPE_CHANGED;

		/* from this point on, we are dealing with a pair
		 * whose both sides are valid and of the same type, i.e.
		 * either in-place edit or rename/copy edit.
		 */
		else if (DIFF_PAIR_RENAME(p)) {
			if (p->source_stays) {
				p->status = DIFF_STATUS_COPIED;
				continue;
			}
			/* See if there is some other filepair that
			 * copies from the same source as us.  If so
			 * we are a copy.  Otherwise we are either a
			 * copy if the path stays, or a rename if it
			 * does not, but we already handled "stays" case.
			 */
			for (j = i + 1; j < q->nr; j++) {
				pp = q->queue[j];
				if (strcmp(pp->one->path, p->one->path))
					continue; /* not us */
				if (!DIFF_PAIR_RENAME(pp))
					continue; /* not a rename/copy */
				/* pp is a rename/copy from the same source */
				p->status = DIFF_STATUS_COPIED;
				break;
			}
			if (!p->status)
				p->status = DIFF_STATUS_RENAMED;
		}
		else if (memcmp(p->one->sha1, p->two->sha1, 20) ||
			 p->one->mode != p->two->mode)
			p->status = DIFF_STATUS_MODIFIED;
		else {
			/* This is a "no-change" entry and should not
			 * happen anymore, but prepare for broken callers.
			 */
			error("feeding unmodified %s to diffcore",
			      p->one->path);
			p->status = DIFF_STATUS_UNKNOWN;
		}
	}
	diff_debug_queue("resolve-rename-copy done", q);
}

void diff_flush(struct diff_options *options)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;
	int inter_name_termination = '\t';
	int diff_output_format = options->output_format;
	int line_termination = options->line_termination;

	if (!line_termination)
		inter_name_termination = 0;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if ((diff_output_format == DIFF_FORMAT_NO_OUTPUT) ||
		    (p->status == DIFF_STATUS_UNKNOWN))
			continue;
		if (p->status == 0)
			die("internal error in diff-resolve-rename-copy");
		switch (diff_output_format) {
		case DIFF_FORMAT_PATCH:
			diff_flush_patch(p, options);
			break;
		case DIFF_FORMAT_RAW:
		case DIFF_FORMAT_NAME_STATUS:
			diff_flush_raw(p, line_termination,
				       inter_name_termination,
				       options);
			break;
		case DIFF_FORMAT_NAME:
			diff_flush_name(p,
					inter_name_termination,
					line_termination);
			break;
		}
		diff_free_filepair(q->queue[i]);
	}
	free(q->queue);
	q->queue = NULL;
	q->nr = q->alloc = 0;
}

static void diffcore_apply_filter(const char *filter)
{
	int i;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	if (!filter)
		return;

	if (strchr(filter, DIFF_STATUS_FILTER_AON)) {
		int found;
		for (i = found = 0; !found && i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];
			if (((p->status == DIFF_STATUS_MODIFIED) &&
			     ((p->score &&
			       strchr(filter, DIFF_STATUS_FILTER_BROKEN)) ||
			      (!p->score &&
			       strchr(filter, DIFF_STATUS_MODIFIED)))) ||
			    ((p->status != DIFF_STATUS_MODIFIED) &&
			     strchr(filter, p->status)))
				found++;
		}
		if (found)
			return;

		/* otherwise we will clear the whole queue
		 * by copying the empty outq at the end of this
		 * function, but first clear the current entries
		 * in the queue.
		 */
		for (i = 0; i < q->nr; i++)
			diff_free_filepair(q->queue[i]);
	}
	else {
		/* Only the matching ones */
		for (i = 0; i < q->nr; i++) {
			struct diff_filepair *p = q->queue[i];

			if (((p->status == DIFF_STATUS_MODIFIED) &&
			     ((p->score &&
			       strchr(filter, DIFF_STATUS_FILTER_BROKEN)) ||
			      (!p->score &&
			       strchr(filter, DIFF_STATUS_MODIFIED)))) ||
			    ((p->status != DIFF_STATUS_MODIFIED) &&
			     strchr(filter, p->status)))
				diff_q(&outq, p);
			else
				diff_free_filepair(p);
		}
	}
	free(q->queue);
	*q = outq;
}

void diffcore_std(struct diff_options *options)
{
	if (options->paths && options->paths[0])
		diffcore_pathspec(options->paths);
	if (options->break_opt != -1)
		diffcore_break(options->break_opt);
	if (options->detect_rename)
		diffcore_rename(options);
	if (options->break_opt != -1)
		diffcore_merge_broken();
	if (options->pickaxe)
		diffcore_pickaxe(options->pickaxe, options->pickaxe_opts);
	if (options->orderfile)
		diffcore_order(options->orderfile);
	diff_resolve_rename_copy();
	diffcore_apply_filter(options->filter);
}


void diffcore_std_no_resolve(struct diff_options *options)
{
	if (options->pickaxe)
		diffcore_pickaxe(options->pickaxe, options->pickaxe_opts);
	if (options->orderfile)
		diffcore_order(options->orderfile);
	diffcore_apply_filter(options->filter);
}

void diff_addremove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path)
{
	char concatpath[PATH_MAX];
	struct diff_filespec *one, *two;

	/* This may look odd, but it is a preparation for
	 * feeding "there are unchanged files which should
	 * not produce diffs, but when you are doing copy
	 * detection you would need them, so here they are"
	 * entries to the diff-core.  They will be prefixed
	 * with something like '=' or '*' (I haven't decided
	 * which but should not make any difference).
	 * Feeding the same new and old to diff_change() 
	 * also has the same effect.
	 * Before the final output happens, they are pruned after
	 * merged into rename/copy pairs as appropriate.
	 */
	if (options->reverse_diff)
		addremove = (addremove == '+' ? '-' :
			     addremove == '-' ? '+' : addremove);

	if (!path) path = "";
	sprintf(concatpath, "%s%s", base, path);
	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);

	if (addremove != '+')
		fill_filespec(one, sha1, mode);
	if (addremove != '-')
		fill_filespec(two, sha1, mode);

	diff_queue(&diff_queued_diff, one, two);
}

void diff_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path) 
{
	char concatpath[PATH_MAX];
	struct diff_filespec *one, *two;

	if (options->reverse_diff) {
		unsigned tmp;
		const unsigned char *tmp_c;
		tmp = old_mode; old_mode = new_mode; new_mode = tmp;
		tmp_c = old_sha1; old_sha1 = new_sha1; new_sha1 = tmp_c;
	}
	if (!path) path = "";
	sprintf(concatpath, "%s%s", base, path);
	one = alloc_filespec(concatpath);
	two = alloc_filespec(concatpath);
	fill_filespec(one, old_sha1, old_mode);
	fill_filespec(two, new_sha1, new_mode);

	diff_queue(&diff_queued_diff, one, two);
}

void diff_unmerge(struct diff_options *options,
		  const char *path)
{
	struct diff_filespec *one, *two;
	one = alloc_filespec(path);
	two = alloc_filespec(path);
	diff_queue(&diff_queued_diff, one, two);
}
