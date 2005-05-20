/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include "cache.h"
#include "diff.h"
#include "delta.h"

static const char *diff_opts = "-pu";
static unsigned char null_sha1[20] = { 0, };
#define MAX_SCORE 10000
#define DEFAULT_MINIMUM_SCORE 5000

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
	const char *name;
	char hex[41];
	char mode[10];
	char tmp_path[50];
} diff_temp[2];

struct diff_spec {
	unsigned char blob_sha1[20];
	unsigned short mode;	 /* file mode */
	unsigned sha1_valid : 1; /* if true, use blob_sha1 and trust mode;
				  * if false, use the name and read from
				  * the filesystem.
				  */
	unsigned file_valid : 1; /* if false the file does not exist */
};

static void builtin_diff(const char *name_a,
			 const char *name_b,
			 struct diff_tempfile *temp,
			 int rename_score)
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
		if (strcmp(name_a, name_b)) {
			if (0 < rename_score)
				printf("rename similarity index %d%%\n",
				       (int)(0.5+
					     rename_score*100.0/MAX_SCORE));
			printf("rename old %s\n", name_a);
			printf("rename new %s\n", name_b);
		}
		if (strncmp(temp[0].mode, temp[1].mode, 3))
			/* we do not run diff between different kind
			 * of objects.
			 */
			exit(0);
	}
	fflush(NULL);
	execlp("/bin/sh","sh", "-c", cmd, NULL);
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
	    !S_ISREG(st.st_mode) ||
	    ce_match_stat(ce, &st) ||
	    memcmp(sha1, ce->sha1, 20))
		return 0;
	return 1;
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
			      struct diff_spec *one)
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
	    work_tree_matches(name, one->blob_sha1)) {
		struct stat st;
		temp->name = name;
		if (lstat(temp->name, &st) < 0) {
			if (errno == ENOENT)
				goto not_a_valid_file;
			die("stat(%s): %s", temp->name, strerror(errno));
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
					one->blob_sha1 : null_sha1),
				       (one->sha1_valid ?
					one->mode : S_IFLNK));
		}
		else {
			if (!one->sha1_valid)
				strcpy(temp->hex, sha1_to_hex(null_sha1));
			else
				strcpy(temp->hex, sha1_to_hex(one->blob_sha1));
			sprintf(temp->mode, "%06o",
				S_IFREG |ce_permissions(st.st_mode));
		}
		return;
	}
	else {
		void *blob;
		char type[20];
		unsigned long size;

		blob = read_sha1_file(one->blob_sha1, type, &size);
		if (!blob || strcmp(type, "blob"))
			die("unable to read blob object for %s (%s)",
			    name, sha1_to_hex(one->blob_sha1));
		prep_temp_blob(temp, blob, size, one->blob_sha1, one->mode);
		free(blob);
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

static int detect_rename;
static int reverse_diff;
static int diff_raw_output = -1;
static const char **pathspec;
static int speccnt;
static int minimum_score;

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
			      struct diff_spec *one,
			      struct diff_spec *two,
			      int rename_score)
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
				const char *exec_arg[9];
				const char **arg = &exec_arg[0];
				*arg++ = pgm;
				*arg++ = name;
				*arg++ = temp[0].name;
				*arg++ = temp[0].hex;
				*arg++ = temp[0].mode;
				*arg++ = temp[1].name;
				*arg++ = temp[1].hex;
				*arg++ = temp[1].mode;
				if (other)
					*arg++ = other;
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
			builtin_diff(name, other ? : name, temp, rename_score);
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

/*
 * We do not detect circular renames.  Just hold created and deleted
 * entries and later attempt to match them up.  If they do not match,
 * then spit them out as deletes or creates as original.
 */

static struct diff_spec_hold {
	struct diff_spec_hold *next;
	struct diff_spec it;
	unsigned long size;
	int flags;
#define MATCHED 1
#define SHOULD_FREE 2
#define SHOULD_MUNMAP 4
	void *data;
	char path[1];
} *createdfile, *deletedfile;

static void hold_diff(const char *name,
		      struct diff_spec *one,
		      struct diff_spec *two)
{
	struct diff_spec_hold **list, *elem;

	if (one->file_valid && two->file_valid)
		die("internal error");

	if (!detect_rename) {
		run_external_diff(name, NULL, one, two, -1);
		return;
	}
	elem = xmalloc(sizeof(*elem) + strlen(name));
	strcpy(elem->path, name);
	elem->size = 0;
	elem->data = NULL;
	elem->flags = 0;
	if (one->file_valid) {
		list = &deletedfile;
		elem->it = *one;
	}
	else {
		list = &createdfile;
		elem->it = *two;
	}
	elem->next = *list;
	*list = elem;
}

static int populate_data(struct diff_spec_hold *s)
{
	char type[20];

	if (s->data)
		return 0;
	if (s->it.sha1_valid) {
		s->data = read_sha1_file(s->it.blob_sha1, type, &s->size);
		s->flags |= SHOULD_FREE;
	}
	else {
		struct stat st;
		int fd;
		fd = open(s->path, O_RDONLY);
		if (fd < 0)
			return -1;
		if (fstat(fd, &st)) {
			close(fd);
			return -1;
		}
		s->size = st.st_size;
		s->data = mmap(NULL, s->size, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (!s->size)
			s->data = "";
		else
			s->flags |= SHOULD_MUNMAP;
	}
	return 0;
}

static void free_data(struct diff_spec_hold *s)
{
	if (s->flags & SHOULD_FREE)
		free(s->data);
	else if (s->flags & SHOULD_MUNMAP)
		munmap(s->data, s->size);
	s->flags &= ~(SHOULD_FREE|SHOULD_MUNMAP);
	s->data = NULL;
}

static void flush_remaining_diff(struct diff_spec_hold *elem,
				 int on_created_list)
{
	static struct diff_spec null_file_spec;

	null_file_spec.file_valid = 0;
	for ( ; elem ; elem = elem->next) {
		free_data(elem);
		if (elem->flags & MATCHED)
			continue;
		if (on_created_list)
			run_external_diff(elem->path, NULL,
					  &null_file_spec, &elem->it, -1);
		else
			run_external_diff(elem->path, NULL,
					  &elem->it, &null_file_spec, -1);
	}
}

static int is_exact_match(struct diff_spec_hold *src,
			  struct diff_spec_hold *dst)
{
	if (src->it.sha1_valid && dst->it.sha1_valid &&
	    !memcmp(src->it.blob_sha1, dst->it.blob_sha1, 20))
		return 1;
	if (populate_data(src) || populate_data(dst))
		/* this is an error but will be caught downstream */
		return 0;
	if (src->size == dst->size &&
	    !memcmp(src->data, dst->data, src->size))
		return 1;
	return 0;
}

static int estimate_similarity(struct diff_spec_hold *src, struct diff_spec_hold *dst)
{
	/* src points at a deleted file and dst points at a created
	 * file.  They may be quite similar, in which case we want to
	 * say src is renamed to dst.
	 *
	 * Compare them and return how similar they are, representing
	 * the score as an integer between 0 and 10000, except
	 * where they match exactly it is considered better than anything
	 * else.
	 */
	void *delta;
	unsigned long delta_size;
	int score;

	delta_size = ((src->size < dst->size) ?
		      (dst->size - src->size) : (src->size - dst->size));

	/* We would not consider rename followed by more than
	 * minimum_score/MAX_SCORE edits; that is, delta_size must be smaller
	 * than (src->size + dst->size)/2 * minimum_score/MAX_SCORE,
	 * which means...
	 */

	if ((src->size+dst->size)*minimum_score < delta_size*MAX_SCORE*2)
		return 0;

	delta = diff_delta(src->data, src->size,
			   dst->data, dst->size,
			   &delta_size);
	free(delta);

	/* This "delta" is really xdiff with adler32 and all the
	 * overheads but it is a quick and dirty approximation.
	 *
	 * Now we will give some score to it.  100% edit gets
	 * 0 points and 0% edit gets MAX_SCORE points.  That is, every
	 * 1/MAX_SCORE edit gets 1 point penalty.  The amount of penalty is:
	 *
	 * (delta_size * 2 / (src->size + dst->size)) * MAX_SCORE
	 *
	 */
	score = MAX_SCORE-(MAX_SCORE*2*delta_size/(src->size+dst->size));
	if (score < 0) return 0;
	if (MAX_SCORE < score) return MAX_SCORE;
	return score;
}

struct diff_score {
	struct diff_spec_hold *src;
	struct diff_spec_hold *dst;
	int score;
};

static int score_compare(const void *a_, const void *b_)
{
	const struct diff_score *a = a_, *b = b_;
	return b->score - a->score;
}

static void flush_rename_pair(struct diff_spec_hold *src,
			      struct diff_spec_hold *dst,
			      int rename_score)
{
	src->flags |= MATCHED;
	dst->flags |= MATCHED;
	free_data(src);
	free_data(dst);
	run_external_diff(src->path, dst->path,
			  &src->it, &dst->it, rename_score);
}

static void free_held_diff(struct diff_spec_hold *list)
{
	struct diff_spec_hold *h;
	for (h = list; list; list = h) {
		h = list->next;
		free_data(list);
		free(list);
	}
}

void diff_flush(void)
{
	int num_create, num_delete, c, d;
	struct diff_spec_hold *elem, *src, *dst;
	struct diff_score *mx;

	/* We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 *
	 * With the current callers, we should not have already
	 * matched entries at this point, but it is nonetheless
	 * checked for sanity.
	 */
	for (dst = createdfile; dst; dst = dst->next) {
		if (dst->flags & MATCHED)
			continue;
		for (src = deletedfile; src; src = src->next) {
			if (src->flags & MATCHED)
				continue;
			if (! is_exact_match(src, dst))
				continue;
			flush_rename_pair(src, dst, MAX_SCORE);
			break;
		}
	}

	/* Count surviving candidates */
	for (num_create = 0, elem = createdfile; elem; elem = elem->next)
		if (!(elem->flags & MATCHED))
			num_create++;

	for (num_delete = 0, elem = deletedfile; elem; elem = elem->next)
		if (!(elem->flags & MATCHED))
			num_delete++;

	if (num_create == 0 ||  num_delete == 0)
		goto exit_path;

	mx = xmalloc(sizeof(*mx) * num_create * num_delete);
	for (c = 0, dst = createdfile; dst; dst = dst->next) {
		int base = c * num_delete;
		if (dst->flags & MATCHED)
			continue;
		for (d = 0, src = deletedfile; src; src = src->next) {
			struct diff_score *m = &mx[base+d];
			if (src->flags & MATCHED)
				continue;
			m->src = src;
			m->dst = dst;
			m->score = estimate_similarity(src, dst);
			d++;
		}
		c++;
	}
	qsort(mx, num_create*num_delete, sizeof(*mx), score_compare);

#if 0
 	for (c = 0; c < num_create * num_delete; c++) {
		src = mx[c].src;
		dst = mx[c].dst;
		if ((src->flags & MATCHED) || (dst->flags & MATCHED))
			continue;
		fprintf(stderr,
			"**score ** %d %s %s\n",
			mx[c].score, src->path, dst->path);
	}
#endif

 	for (c = 0; c < num_create * num_delete; c++) {
		src = mx[c].src;
		dst = mx[c].dst;
		if ((src->flags & MATCHED) || (dst->flags & MATCHED))
			continue;
		if (mx[c].score < minimum_score)
			break;
		flush_rename_pair(src, dst, mx[c].score);
	}
	free(mx);

 exit_path:
	flush_remaining_diff(createdfile, 1);
	flush_remaining_diff(deletedfile, 0);
	free_held_diff(createdfile);
	free_held_diff(deletedfile);
	createdfile = deletedfile = NULL;
}

int diff_scoreopt_parse(const char *opt)
{
	int diglen, num, scale, i;
	if (opt[0] != '-' || opt[1] != 'M')
		return -1; /* that is not -M option */
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

void diff_setup(int detect_rename_, int minimum_score_, int reverse_diff_,
		int diff_raw_output_,
		const char **pathspec_, int speccnt_)
{
	free_held_diff(createdfile);
	free_held_diff(deletedfile);
	createdfile = deletedfile = NULL;

	detect_rename = detect_rename_;
	reverse_diff = reverse_diff_;
	pathspec = pathspec_;
	diff_raw_output = diff_raw_output_;
	speccnt = speccnt_;
	minimum_score = minimum_score_ ? : DEFAULT_MINIMUM_SCORE;
}

static const char *git_object_type(unsigned mode)
{
	return S_ISDIR(mode) ? "tree" : "blob";
}

void diff_addremove(int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path)
{
	char concatpath[PATH_MAX];
	struct diff_spec spec[2], *one, *two;

	if (reverse_diff)
		addremove = (addremove == '+' ? '-' : '+');

	if (0 <= diff_raw_output) {
		if (!path)
			path = "";
		printf("%c%06o %s %s %s%s%c",
		       addremove,
		       mode,
		       git_object_type(mode), sha1_to_hex(sha1),
		       base, path, diff_raw_output);
		return;
	}
	if (S_ISDIR(mode))
		return;

	memcpy(spec[0].blob_sha1, sha1, 20);
	spec[0].mode = mode;
	spec[0].sha1_valid = !!memcmp(sha1, null_sha1, 20);
	spec[0].file_valid = 1;
	spec[1].file_valid = 0;

	if (addremove == '+') {
		one = spec + 1; two = spec;
	} else {
		one = spec; two = one + 1;
	}

	if (path) {
		strcpy(concatpath, base);
		strcat(concatpath, path);
	}
	hold_diff(path ? concatpath : base, one, two);
}

void diff_change(unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path) {
	char concatpath[PATH_MAX];
	struct diff_spec spec[2];

	if (reverse_diff) {
		unsigned tmp;
		const unsigned char *tmp_c;
		tmp = old_mode; old_mode = new_mode; new_mode = tmp;
		tmp_c = old_sha1; old_sha1 = new_sha1; new_sha1 = tmp_c;
	}

	if (0 <= diff_raw_output) {
		char old_hex[41];
		strcpy(old_hex, sha1_to_hex(old_sha1));

		if (!path)
			path = "";
		printf("*%06o->%06o %s %s->%s %s%s%c",
		       old_mode, new_mode,
		       git_object_type(new_mode),
		       old_hex, sha1_to_hex(new_sha1),
		       base, path, diff_raw_output);
		return;
	}
	if (S_ISDIR(new_mode))
		return;

	if (path) {
		strcpy(concatpath, base);
		strcat(concatpath, path);
	}

	memcpy(spec[0].blob_sha1, old_sha1, 20);
	spec[0].mode = old_mode;
	memcpy(spec[1].blob_sha1, new_sha1, 20);
	spec[1].mode = new_mode;
	spec[0].sha1_valid = !!memcmp(old_sha1, null_sha1, 20);
	spec[1].sha1_valid = !!memcmp(new_sha1, null_sha1, 20);
	spec[1].file_valid = spec[0].file_valid = 1;

	/* We do not look at changed files as candidate for
	 * rename detection ever.
	 */
	run_external_diff(path ? concatpath : base, NULL,
			  &spec[0], &spec[1], -1);
}

void diff_unmerge(const char *path)
{
	if (0 <= diff_raw_output) {
		printf("U %s%c", path, diff_raw_output);
		return;
	}
	run_external_diff(path, NULL, NULL, NULL, -1);
}
