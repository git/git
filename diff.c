/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include "cache.h"
#include "diff.h"
#include "diffcore.h"

static const char *diff_opts = "-pu";
static unsigned char null_sha1[20] = { 0, };

static int reverse_diff;
static int diff_raw_output = -1;
static const char **pathspec;
static int speccnt;

static const char *external_diff(void)
{
	static const char *external_diff_cmd = NULL;
	static int done_preparing = 0;

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
	if (gitenv("GIT_EXTERNAL_DIFF"))
		external_diff_cmd = gitenv("GIT_EXTERNAL_DIFF");

	/* In case external diff fails... */
	diff_opts = gitenv("GIT_DIFF_OPTS") ? : diff_opts;

	done_preparing = 1;
	return external_diff_cmd;
}

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', and the caller is
 * expected to enclose the result within a single quote pair.
 *
 * E.g.
 *  original     sq_expand     result
 *  name     ==> name      ==> 'name'
 *  a b      ==> a b       ==> 'a b'
 *  a'b      ==> a'\''b    ==> 'a'\''b'
 */
static char *sq_expand(const char *src)
{
	static char *buf = NULL;
	int cnt, c;
	const char *cp;
	char *bp;

	/* count bytes needed to store the quoted string. */
	for (cnt = 1, cp = src; *cp; cnt++, cp++)
		if (*cp == '\'')
			cnt += 3;

	buf = xmalloc(cnt);
	bp = buf;
	while ((c = *src++)) {
		if (c != '\'')
			*bp++ = c;
		else {
			bp = strcpy(bp, "'\\''");
			bp += 4;
		}
	}
	*bp = 0;
	return buf;
}

static struct diff_tempfile {
	const char *name; /* filename external diff should read from */
	char hex[41];
	char mode[10];
	char tmp_path[50];
} diff_temp[2];

static void builtin_diff(const char *name_a,
			 const char *name_b,
			 struct diff_tempfile *temp,
			 const char *xfrm_msg)
{
	int i, next_at, cmd_size;
	const char *diff_cmd = "diff -L'%s%s' -L'%s%s'";
	const char *diff_arg  = "'%s' '%s'||:"; /* "||:" is to return 0 */
	const char *input_name_sq[2];
	const char *path0[2];
	const char *path1[2];
	const char *name_sq[2];
	char *cmd;

	name_sq[0] = sq_expand(name_a);
	name_sq[1] = sq_expand(name_b);

	/* diff_cmd and diff_arg have 6 %s in total which makes
	 * the sum of these strings 12 bytes larger than required.
	 * we use 2 spaces around diff-opts, and we need to count
	 * terminating NUL, so we subtract 9 here.
	 */
	cmd_size = (strlen(diff_cmd) + strlen(diff_opts) +
			strlen(diff_arg) - 9);
	for (i = 0; i < 2; i++) {
		input_name_sq[i] = sq_expand(temp[i].name);
		if (!strcmp(temp[i].name, "/dev/null")) {
			path0[i] = "/dev/null";
			path1[i] = "";
		} else {
			path0[i] = i ? "b/" : "a/";
			path1[i] = name_sq[i];
		}
		cmd_size += (strlen(path0[i]) + strlen(path1[i]) +
			     strlen(input_name_sq[i]));
	}

	cmd = xmalloc(cmd_size);

	next_at = 0;
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    diff_cmd,
			    path0[0], path1[0], path0[1], path1[1]);
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    " %s ", diff_opts);
	next_at += snprintf(cmd+next_at, cmd_size-next_at,
			    diff_arg, input_name_sq[0], input_name_sq[1]);

	printf("diff --git a/%s b/%s\n", name_a, name_b);
	if (!path1[0][0])
		printf("new file mode %s\n", temp[1].mode);
	else if (!path1[1][0])
		printf("deleted file mode %s\n", temp[0].mode);
	else {
		if (strcmp(temp[0].mode, temp[1].mode)) {
			printf("old mode %s\n", temp[0].mode);
			printf("new mode %s\n", temp[1].mode);
		}
		if (xfrm_msg && xfrm_msg[0])
			fputs(xfrm_msg, stdout);

		if (strncmp(temp[0].mode, temp[1].mode, 3))
			/* we do not run diff between different kind
			 * of objects.
			 */
			exit(0);
	}
	fflush(NULL);
	execlp("/bin/sh","sh", "-c", cmd, NULL);
}

struct diff_filespec *alloc_filespec(const char *path)
{
	int namelen = strlen(path);
	struct diff_filespec *spec = xmalloc(sizeof(*spec) + namelen + 1);
	spec->path = (char *)(spec + 1);
	strcpy(spec->path, path);
	spec->should_free = spec->should_munmap = spec->file_valid = 0;
	spec->xfrm_flags = 0;
	spec->size = 0;
	spec->data = 0;
	return spec;
}

void fill_filespec(struct diff_filespec *spec, const unsigned char *sha1,
		   unsigned short mode)
{
	spec->mode = mode;
	memcpy(spec->sha1, sha1, 20);
	spec->sha1_valid = !!memcmp(sha1, null_sha1, 20);
	spec->file_valid = 1;
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

/*
 * While doing rename detection and pickaxe operation, we may need to
 * grab the data for the blob (or file) for our own in-core comparison.
 * diff_filespec has data and size fields for this purpose.
 */
int diff_populate_filespec(struct diff_filespec *s)
{
	int err = 0;
	if (!s->file_valid)
		die("internal error: asking to populate invalid file.");
	if (S_ISDIR(s->mode))
		return -1;

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
		s->should_munmap = 1;
		close(fd);
	}
	else {
		char type[20];
		s->data = read_sha1_file(s->sha1, type, &s->size);
		s->should_free = 1;
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
	s->data = 0;
}

static void prep_temp_blob(struct diff_tempfile *temp,
			   void *blob,
			   unsigned long size,
			   unsigned char *sha1,
			   int mode)
{
	int fd;

	strcpy(temp->tmp_path, ".diff_XXXXXX");
	fd = mkstemp(temp->tmp_path);
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
	if (!one->file_valid) {
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
			char *buf, buf_[1024];
			buf = ((sizeof(buf_) < st.st_size) ?
			       xmalloc(st.st_size) : buf_);
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
			sprintf(temp->mode, "%06o",
				S_IFREG |ce_permissions(st.st_mode));
		}
		return;
	}
	else {
		if (diff_populate_filespec(one))
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

static int matches_pathspec(const char *name)
{
	int i;
	int namelen;

	if (speccnt == 0)
		return 1;

	namelen = strlen(name);
	for (i = 0; i < speccnt; i++) {
		int speclen = strlen(pathspec[i]);
		if (! strncmp(pathspec[i], name, speclen) &&
		    speclen <= namelen &&
		    (name[speclen] == 0 || name[speclen] == '/'))
			return 1;
	}
	return 0;
}

/* An external diff command takes:
 *
 * diff-cmd name infile1 infile1-sha1 infile1-mode \
 *               infile2 infile2-sha1 infile2-mode [ rename-to ]
 *
 */
static void run_external_diff(const char *name,
			      const char *other,
			      struct diff_filespec *one,
			      struct diff_filespec *two,
			      const char *xfrm_msg)
{
	struct diff_tempfile *temp = diff_temp;
	pid_t pid;
	int status;
	static int atexit_asked = 0;

	if (!matches_pathspec(name) && (!other || !matches_pathspec(other)))
		return;

	if (one && two) {
		prepare_temp_file(name, &temp[0], one);
		prepare_temp_file(other ? : name, &temp[1], two);
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
		const char *pgm = external_diff();
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
				*arg = 0;
				execvp(pgm, (char *const*) exec_arg);
			}
			else
				execlp(pgm, pgm, name, NULL);
		}
		/*
		 * otherwise we use the built-in one.
		 */
		if (one && two)
			builtin_diff(name, other ? : name, temp, xfrm_msg);
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

int diff_scoreopt_parse(const char *opt)
{
	int diglen, num, scale, i;
	if (opt[0] != '-' || (opt[1] != 'M' && opt[1] != 'C'))
		return -1; /* that is not a -M nor -C option */
	diglen = strspn(opt+2, "0123456789");
	if (diglen == 0 || strlen(opt+2) != diglen)
		return 0; /* use default */
	sscanf(opt+2, "%d", &num);
	for (i = 0, scale = 1; i < diglen; i++)
		scale *= 10;

	/* user says num divided by scale and we say internally that
	 * is MAX_SCORE * num / scale.
	 */
	return MAX_SCORE * num / scale;
}

void diff_setup(int reverse_diff_, int diff_raw_output_)
{
	reverse_diff = reverse_diff_;
	diff_raw_output = diff_raw_output_;
}

struct diff_queue_struct diff_queued_diff;

struct diff_filepair *diff_queue(struct diff_queue_struct *queue,
				  struct diff_filespec *one,
				  struct diff_filespec *two)
{
	struct diff_filepair *dp = xmalloc(sizeof(*dp));
	dp->one = one;
	dp->two = two;
	dp->xfrm_msg = 0;
	dp->orig_order = queue->nr;
	dp->xfrm_work = 0;
	if (queue->alloc <= queue->nr) {
		queue->alloc = alloc_nr(queue->alloc);
		queue->queue = xrealloc(queue->queue,
				       sizeof(dp) * queue->alloc);
	}
	queue->queue[queue->nr++] = dp;
	return dp;
}

static const char *git_object_type(unsigned mode)
{
	return S_ISDIR(mode) ? "tree" : "blob";
}

static void diff_flush_raw(struct diff_filepair *p)
{
	struct diff_filespec *it;
	int addremove;

	/* raw output does not have a way to express rename nor copy */
	if (strcmp(p->one->path, p->two->path))
		return;

	if (p->one->file_valid && p->two->file_valid) {
		char hex[41];
		strcpy(hex, sha1_to_hex(p->one->sha1));
		printf("*%06o->%06o %s %s->%s %s%c",
		       p->one->mode, p->two->mode,
		       git_object_type(p->one->mode),
		       hex, sha1_to_hex(p->two->sha1),
		       p->one->path, diff_raw_output);
		return;
	}

	if (p->one->file_valid) {
		it = p->one;
		addremove = '-';
	} else {
		it = p->two;
		addremove = '+';
	}

	printf("%c%06o %s %s %s%c",
	       addremove,
	       it->mode, git_object_type(it->mode),
	       sha1_to_hex(it->sha1), it->path, diff_raw_output);
}

static void diff_flush_patch(struct diff_filepair *p)
{
	const char *name, *other;

	name = p->one->path;
	other = (strcmp(name, p->two->path) ? p->two->path : NULL);
	if ((p->one->file_valid && S_ISDIR(p->one->mode)) ||
	    (p->two->file_valid && S_ISDIR(p->two->mode)))
		return; /* no tree diffs in patch format */ 

	run_external_diff(name, other, p->one, p->two, p->xfrm_msg);
}

static int identical(struct diff_filespec *one, struct diff_filespec *two)
{
	/* This function is written stricter than necessary to support
	 * the currently implemented transformers, but the idea is to
	 * let transformers to produce diff_filepairs any way they want,
	 * and filter and clean them up here before producing the output.
	 */

	if (!one->file_valid && !two->file_valid)
		return 1; /* not interesting */

	/* deletion, addition, mode change and renames are all interesting. */
	if ((one->file_valid != two->file_valid) || (one->mode != two->mode) ||
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

static void diff_flush_one(struct diff_filepair *p)
{
	if (identical(p->one, p->two))
		return;
	if (0 <= diff_raw_output)
		diff_flush_raw(p);
	else
		diff_flush_patch(p);
}

int diff_queue_is_empty(void)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!identical(p->one, p->two))
			return 0;
	}
	return 1;
}

void diff_flush(const char **pathspec_, int speccnt_)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	int i;

	pathspec = pathspec_;
	speccnt = speccnt_;

	for (i = 0; i < q->nr; i++)
		diff_flush_one(q->queue[i]);

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		diff_free_filespec_data(p->one);
		diff_free_filespec_data(p->two);
		free(p->xfrm_msg);
		free(p);
	}
	free(q->queue);
	q->queue = NULL;
	q->nr = q->alloc = 0;
}

void diff_addremove(int addremove, unsigned mode,
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
	 * Feeding the same new and old to diff_change() should
	 * also have the same effect.  diff_flush() should
	 * filter the identical ones out at the final output
	 * stage.
	 */
	if (reverse_diff)
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

void diff_change(unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path) {
	char concatpath[PATH_MAX];
	struct diff_filespec *one, *two;

	if (reverse_diff) {
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

void diff_unmerge(const char *path)
{
	if (0 <= diff_raw_output) {
		printf("U %s%c", path, diff_raw_output);
		return;
	}
	run_external_diff(path, NULL, NULL, NULL, NULL);
}
