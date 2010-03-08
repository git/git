/*
 * Builtin "git grep"
 *
 * Copyright (c) 2006 Junio C Hamano
 */
#include "cache.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"
#include "tree-walk.h"
#include "builtin.h"
#include "parse-options.h"
#include "userdiff.h"
#include "grep.h"
#include "quote.h"

#ifndef NO_PTHREADS
#include "thread-utils.h"
#include <pthread.h>
#endif

static char const * const grep_usage[] = {
	"git grep [options] [-e] <pattern> [<rev>...] [[--] path...]",
	NULL
};

static int use_threads = 1;

#ifndef NO_PTHREADS
#define THREADS 8
static pthread_t threads[THREADS];

static void *load_sha1(const unsigned char *sha1, unsigned long *size,
		       const char *name);
static void *load_file(const char *filename, size_t *sz);

enum work_type {WORK_SHA1, WORK_FILE};

/* We use one producer thread and THREADS consumer
 * threads. The producer adds struct work_items to 'todo' and the
 * consumers pick work items from the same array.
 */
struct work_item
{
	enum work_type type;
	char *name;

	/* if type == WORK_SHA1, then 'identifier' is a SHA1,
	 * otherwise type == WORK_FILE, and 'identifier' is a NUL
	 * terminated filename.
	 */
	void *identifier;
	char done;
	struct strbuf out;
};

/* In the range [todo_done, todo_start) in 'todo' we have work_items
 * that have been or are processed by a consumer thread. We haven't
 * written the result for these to stdout yet.
 *
 * The work_items in [todo_start, todo_end) are waiting to be picked
 * up by a consumer thread.
 *
 * The ranges are modulo TODO_SIZE.
 */
#define TODO_SIZE 128
static struct work_item todo[TODO_SIZE];
static int todo_start;
static int todo_end;
static int todo_done;

/* Has all work items been added? */
static int all_work_added;

/* This lock protects all the variables above. */
static pthread_mutex_t grep_mutex;

/* Used to serialize calls to read_sha1_file. */
static pthread_mutex_t read_sha1_mutex;

#define grep_lock() pthread_mutex_lock(&grep_mutex)
#define grep_unlock() pthread_mutex_unlock(&grep_mutex)
#define read_sha1_lock() pthread_mutex_lock(&read_sha1_mutex)
#define read_sha1_unlock() pthread_mutex_unlock(&read_sha1_mutex)

/* Signalled when a new work_item is added to todo. */
static pthread_cond_t cond_add;

/* Signalled when the result from one work_item is written to
 * stdout.
 */
static pthread_cond_t cond_write;

/* Signalled when we are finished with everything. */
static pthread_cond_t cond_result;

static void add_work(enum work_type type, char *name, void *id)
{
	grep_lock();

	while ((todo_end+1) % ARRAY_SIZE(todo) == todo_done) {
		pthread_cond_wait(&cond_write, &grep_mutex);
	}

	todo[todo_end].type = type;
	todo[todo_end].name = name;
	todo[todo_end].identifier = id;
	todo[todo_end].done = 0;
	strbuf_reset(&todo[todo_end].out);
	todo_end = (todo_end + 1) % ARRAY_SIZE(todo);

	pthread_cond_signal(&cond_add);
	grep_unlock();
}

static struct work_item *get_work(void)
{
	struct work_item *ret;

	grep_lock();
	while (todo_start == todo_end && !all_work_added) {
		pthread_cond_wait(&cond_add, &grep_mutex);
	}

	if (todo_start == todo_end && all_work_added) {
		ret = NULL;
	} else {
		ret = &todo[todo_start];
		todo_start = (todo_start + 1) % ARRAY_SIZE(todo);
	}
	grep_unlock();
	return ret;
}

static void grep_sha1_async(struct grep_opt *opt, char *name,
			    const unsigned char *sha1)
{
	unsigned char *s;
	s = xmalloc(20);
	memcpy(s, sha1, 20);
	add_work(WORK_SHA1, name, s);
}

static void grep_file_async(struct grep_opt *opt, char *name,
			    const char *filename)
{
	add_work(WORK_FILE, name, xstrdup(filename));
}

static void work_done(struct work_item *w)
{
	int old_done;

	grep_lock();
	w->done = 1;
	old_done = todo_done;
	for(; todo[todo_done].done && todo_done != todo_start;
	    todo_done = (todo_done+1) % ARRAY_SIZE(todo)) {
		w = &todo[todo_done];
		write_or_die(1, w->out.buf, w->out.len);
		free(w->name);
		free(w->identifier);
	}

	if (old_done != todo_done)
		pthread_cond_signal(&cond_write);

	if (all_work_added && todo_done == todo_end)
		pthread_cond_signal(&cond_result);

	grep_unlock();
}

static void *run(void *arg)
{
	int hit = 0;
	struct grep_opt *opt = arg;

	while (1) {
		struct work_item *w = get_work();
		if (!w)
			break;

		opt->output_priv = w;
		if (w->type == WORK_SHA1) {
			unsigned long sz;
			void* data = load_sha1(w->identifier, &sz, w->name);

			if (data) {
				hit |= grep_buffer(opt, w->name, data, sz);
				free(data);
			}
		} else if (w->type == WORK_FILE) {
			size_t sz;
			void* data = load_file(w->identifier, &sz);
			if (data) {
				hit |= grep_buffer(opt, w->name, data, sz);
				free(data);
			}
		} else {
			assert(0);
		}

		work_done(w);
	}
	free_grep_patterns(arg);
	free(arg);

	return (void*) (intptr_t) hit;
}

static void strbuf_out(struct grep_opt *opt, const void *buf, size_t size)
{
	struct work_item *w = opt->output_priv;
	strbuf_add(&w->out, buf, size);
}

static void start_threads(struct grep_opt *opt)
{
	int i;

	pthread_mutex_init(&grep_mutex, NULL);
	pthread_mutex_init(&read_sha1_mutex, NULL);
	pthread_cond_init(&cond_add, NULL);
	pthread_cond_init(&cond_write, NULL);
	pthread_cond_init(&cond_result, NULL);

	for (i = 0; i < ARRAY_SIZE(todo); i++) {
		strbuf_init(&todo[i].out, 0);
	}

	for (i = 0; i < ARRAY_SIZE(threads); i++) {
		int err;
		struct grep_opt *o = grep_opt_dup(opt);
		o->output = strbuf_out;
		compile_grep_patterns(o);
		err = pthread_create(&threads[i], NULL, run, o);

		if (err)
			die("grep: failed to create thread: %s",
			    strerror(err));
	}
}

static int wait_all(void)
{
	int hit = 0;
	int i;

	grep_lock();
	all_work_added = 1;

	/* Wait until all work is done. */
	while (todo_done != todo_end)
		pthread_cond_wait(&cond_result, &grep_mutex);

	/* Wake up all the consumer threads so they can see that there
	 * is no more work to do.
	 */
	pthread_cond_broadcast(&cond_add);
	grep_unlock();

	for (i = 0; i < ARRAY_SIZE(threads); i++) {
		void *h;
		pthread_join(threads[i], &h);
		hit |= (int) (intptr_t) h;
	}

	pthread_mutex_destroy(&grep_mutex);
	pthread_mutex_destroy(&read_sha1_mutex);
	pthread_cond_destroy(&cond_add);
	pthread_cond_destroy(&cond_write);
	pthread_cond_destroy(&cond_result);

	return hit;
}
#else /* !NO_PTHREADS */
#define read_sha1_lock()
#define read_sha1_unlock()

static int wait_all(void)
{
	return 0;
}
#endif

static int grep_config(const char *var, const char *value, void *cb)
{
	struct grep_opt *opt = cb;

	switch (userdiff_config(var, value)) {
	case 0: break;
	case -1: return -1;
	default: return 0;
	}

	if (!strcmp(var, "color.grep")) {
		opt->color = git_config_colorbool(var, value, -1);
		return 0;
	}
	if (!strcmp(var, "color.grep.match")) {
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, opt->color_match);
		return 0;
	}
	return git_color_default_config(var, value, cb);
}

/*
 * Return non-zero if max_depth is negative or path has no more then max_depth
 * slashes.
 */
static int accept_subdir(const char *path, int max_depth)
{
	if (max_depth < 0)
		return 1;

	while ((path = strchr(path, '/')) != NULL) {
		max_depth--;
		if (max_depth < 0)
			return 0;
		path++;
	}
	return 1;
}

/*
 * Return non-zero if name is a subdirectory of match and is not too deep.
 */
static int is_subdir(const char *name, int namelen,
		const char *match, int matchlen, int max_depth)
{
	if (matchlen > namelen || strncmp(name, match, matchlen))
		return 0;

	if (name[matchlen] == '\0') /* exact match */
		return 1;

	if (!matchlen || match[matchlen-1] == '/' || name[matchlen] == '/')
		return accept_subdir(name + matchlen + 1, max_depth);

	return 0;
}

/*
 * git grep pathspecs are somewhat different from diff-tree pathspecs;
 * pathname wildcards are allowed.
 */
static int pathspec_matches(const char **paths, const char *name, int max_depth)
{
	int namelen, i;
	if (!paths || !*paths)
		return accept_subdir(name, max_depth);
	namelen = strlen(name);
	for (i = 0; paths[i]; i++) {
		const char *match = paths[i];
		int matchlen = strlen(match);
		const char *cp, *meta;

		if (is_subdir(name, namelen, match, matchlen, max_depth))
			return 1;
		if (!fnmatch(match, name, 0))
			return 1;
		if (name[namelen-1] != '/')
			continue;

		/* We are being asked if the directory ("name") is worth
		 * descending into.
		 *
		 * Find the longest leading directory name that does
		 * not have metacharacter in the pathspec; the name
		 * we are looking at must overlap with that directory.
		 */
		for (cp = match, meta = NULL; cp - match < matchlen; cp++) {
			char ch = *cp;
			if (ch == '*' || ch == '[' || ch == '?') {
				meta = cp;
				break;
			}
		}
		if (!meta)
			meta = cp; /* fully literal */

		if (namelen <= meta - match) {
			/* Looking at "Documentation/" and
			 * the pattern says "Documentation/howto/", or
			 * "Documentation/diff*.txt".  The name we
			 * have should match prefix.
			 */
			if (!memcmp(match, name, namelen))
				return 1;
			continue;
		}

		if (meta - match < namelen) {
			/* Looking at "Documentation/howto/" and
			 * the pattern says "Documentation/h*";
			 * match up to "Do.../h"; this avoids descending
			 * into "Documentation/technical/".
			 */
			if (!memcmp(match, name, meta - match))
				return 1;
			continue;
		}
	}
	return 0;
}

static void *lock_and_read_sha1_file(const unsigned char *sha1, enum object_type *type, unsigned long *size)
{
	void *data;

	if (use_threads) {
		read_sha1_lock();
		data = read_sha1_file(sha1, type, size);
		read_sha1_unlock();
	} else {
		data = read_sha1_file(sha1, type, size);
	}
	return data;
}

static void *load_sha1(const unsigned char *sha1, unsigned long *size,
		       const char *name)
{
	enum object_type type;
	void *data = lock_and_read_sha1_file(sha1, &type, size);

	if (!data)
		error("'%s': unable to read %s", name, sha1_to_hex(sha1));

	return data;
}

static int grep_sha1(struct grep_opt *opt, const unsigned char *sha1,
		     const char *filename, int tree_name_len)
{
	struct strbuf pathbuf = STRBUF_INIT;
	char *name;

	if (opt->relative && opt->prefix_length) {
		quote_path_relative(filename + tree_name_len, -1, &pathbuf,
				    opt->prefix);
		strbuf_insert(&pathbuf, 0, filename, tree_name_len);
	} else {
		strbuf_addstr(&pathbuf, filename);
	}

	name = strbuf_detach(&pathbuf, NULL);

#ifndef NO_PTHREADS
	if (use_threads) {
		grep_sha1_async(opt, name, sha1);
		return 0;
	} else
#endif
	{
		int hit;
		unsigned long sz;
		void *data = load_sha1(sha1, &sz, name);
		if (!data)
			hit = 0;
		else
			hit = grep_buffer(opt, name, data, sz);

		free(data);
		free(name);
		return hit;
	}
}

static void *load_file(const char *filename, size_t *sz)
{
	struct stat st;
	char *data;
	int i;

	if (lstat(filename, &st) < 0) {
	err_ret:
		if (errno != ENOENT)
			error("'%s': %s", filename, strerror(errno));
		return 0;
	}
	if (!S_ISREG(st.st_mode))
		return 0;
	*sz = xsize_t(st.st_size);
	i = open(filename, O_RDONLY);
	if (i < 0)
		goto err_ret;
	data = xmalloc(*sz + 1);
	if (st.st_size != read_in_full(i, data, *sz)) {
		error("'%s': short read %s", filename, strerror(errno));
		close(i);
		free(data);
		return 0;
	}
	close(i);
	data[*sz] = 0;
	return data;
}

static int grep_file(struct grep_opt *opt, const char *filename)
{
	struct strbuf buf = STRBUF_INIT;
	char *name;

	if (opt->relative && opt->prefix_length)
		quote_path_relative(filename, -1, &buf, opt->prefix);
	else
		strbuf_addstr(&buf, filename);
	name = strbuf_detach(&buf, NULL);

#ifndef NO_PTHREADS
	if (use_threads) {
		grep_file_async(opt, name, filename);
		return 0;
	} else
#endif
	{
		int hit;
		size_t sz;
		void *data = load_file(filename, &sz);
		if (!data)
			hit = 0;
		else
			hit = grep_buffer(opt, name, data, sz);

		free(data);
		free(name);
		return hit;
	}
}

static int grep_cache(struct grep_opt *opt, const char **paths, int cached)
{
	int hit = 0;
	int nr;
	read_cache();

	for (nr = 0; nr < active_nr; nr++) {
		struct cache_entry *ce = active_cache[nr];
		if (!S_ISREG(ce->ce_mode))
			continue;
		if (!pathspec_matches(paths, ce->name, opt->max_depth))
			continue;
		/*
		 * If CE_VALID is on, we assume worktree file and its cache entry
		 * are identical, even if worktree file has been modified, so use
		 * cache version instead
		 */
		if (cached || (ce->ce_flags & CE_VALID) || ce_skip_worktree(ce)) {
			if (ce_stage(ce))
				continue;
			hit |= grep_sha1(opt, ce->sha1, ce->name, 0);
		}
		else
			hit |= grep_file(opt, ce->name);
		if (ce_stage(ce)) {
			do {
				nr++;
			} while (nr < active_nr &&
				 !strcmp(ce->name, active_cache[nr]->name));
			nr--; /* compensate for loop control */
		}
		if (hit && opt->status_only)
			break;
	}
	free_grep_patterns(opt);
	return hit;
}

static int grep_tree(struct grep_opt *opt, const char **paths,
		     struct tree_desc *tree,
		     const char *tree_name, const char *base)
{
	int len;
	int hit = 0;
	struct name_entry entry;
	char *down;
	int tn_len = strlen(tree_name);
	struct strbuf pathbuf;

	strbuf_init(&pathbuf, PATH_MAX + tn_len);

	if (tn_len) {
		strbuf_add(&pathbuf, tree_name, tn_len);
		strbuf_addch(&pathbuf, ':');
		tn_len = pathbuf.len;
	}
	strbuf_addstr(&pathbuf, base);
	len = pathbuf.len;

	while (tree_entry(tree, &entry)) {
		int te_len = tree_entry_len(entry.path, entry.sha1);
		pathbuf.len = len;
		strbuf_add(&pathbuf, entry.path, te_len);

		if (S_ISDIR(entry.mode))
			/* Match "abc/" against pathspec to
			 * decide if we want to descend into "abc"
			 * directory.
			 */
			strbuf_addch(&pathbuf, '/');

		down = pathbuf.buf + tn_len;
		if (!pathspec_matches(paths, down, opt->max_depth))
			;
		else if (S_ISREG(entry.mode))
			hit |= grep_sha1(opt, entry.sha1, pathbuf.buf, tn_len);
		else if (S_ISDIR(entry.mode)) {
			enum object_type type;
			struct tree_desc sub;
			void *data;
			unsigned long size;

			data = lock_and_read_sha1_file(entry.sha1, &type, &size);
			if (!data)
				die("unable to read tree (%s)",
				    sha1_to_hex(entry.sha1));
			init_tree_desc(&sub, data, size);
			hit |= grep_tree(opt, paths, &sub, tree_name, down);
			free(data);
		}
		if (hit && opt->status_only)
			break;
	}
	strbuf_release(&pathbuf);
	return hit;
}

static int grep_object(struct grep_opt *opt, const char **paths,
		       struct object *obj, const char *name)
{
	if (obj->type == OBJ_BLOB)
		return grep_sha1(opt, obj->sha1, name, 0);
	if (obj->type == OBJ_COMMIT || obj->type == OBJ_TREE) {
		struct tree_desc tree;
		void *data;
		unsigned long size;
		int hit;
		data = read_object_with_reference(obj->sha1, tree_type,
						  &size, NULL);
		if (!data)
			die("unable to read tree (%s)", sha1_to_hex(obj->sha1));
		init_tree_desc(&tree, data, size);
		hit = grep_tree(opt, paths, &tree, name, "");
		free(data);
		return hit;
	}
	die("unable to grep from object of type %s", typename(obj->type));
}

static int context_callback(const struct option *opt, const char *arg,
			    int unset)
{
	struct grep_opt *grep_opt = opt->value;
	int value;
	const char *endp;

	if (unset) {
		grep_opt->pre_context = grep_opt->post_context = 0;
		return 0;
	}
	value = strtol(arg, (char **)&endp, 10);
	if (*endp) {
		return error("switch `%c' expects a numerical value",
			     opt->short_name);
	}
	grep_opt->pre_context = grep_opt->post_context = value;
	return 0;
}

static int file_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	FILE *patterns;
	int lno = 0;
	struct strbuf sb = STRBUF_INIT;

	patterns = fopen(arg, "r");
	if (!patterns)
		die_errno("cannot open '%s'", arg);
	while (strbuf_getline(&sb, patterns, '\n') == 0) {
		/* ignore empty line like grep does */
		if (sb.len == 0)
			continue;
		append_grep_pattern(grep_opt, strbuf_detach(&sb, NULL), arg,
				    ++lno, GREP_PATTERN);
	}
	fclose(patterns);
	strbuf_release(&sb);
	return 0;
}

static int not_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, "--not", "command line", 0, GREP_NOT);
	return 0;
}

static int and_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, "--and", "command line", 0, GREP_AND);
	return 0;
}

static int open_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, "(", "command line", 0, GREP_OPEN_PAREN);
	return 0;
}

static int close_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, ")", "command line", 0, GREP_CLOSE_PAREN);
	return 0;
}

static int pattern_callback(const struct option *opt, const char *arg,
			    int unset)
{
	struct grep_opt *grep_opt = opt->value;
	append_grep_pattern(grep_opt, arg, "-e option", 0, GREP_PATTERN);
	return 0;
}

static int help_callback(const struct option *opt, const char *arg, int unset)
{
	return -1;
}

int cmd_grep(int argc, const char **argv, const char *prefix)
{
	int hit = 0;
	int cached = 0;
	int seen_dashdash = 0;
	int external_grep_allowed__ignored;
	struct grep_opt opt;
	struct object_array list = { 0, 0, NULL };
	const char **paths = NULL;
	int i;
	int dummy;
	struct option options[] = {
		OPT_BOOLEAN(0, "cached", &cached,
			"search in index instead of in the work tree"),
		OPT_GROUP(""),
		OPT_BOOLEAN('v', "invert-match", &opt.invert,
			"show non-matching lines"),
		OPT_BOOLEAN('i', "ignore-case", &opt.ignore_case,
			"case insensitive matching"),
		OPT_BOOLEAN('w', "word-regexp", &opt.word_regexp,
			"match patterns only at word boundaries"),
		OPT_SET_INT('a', "text", &opt.binary,
			"process binary files as text", GREP_BINARY_TEXT),
		OPT_SET_INT('I', NULL, &opt.binary,
			"don't match patterns in binary files",
			GREP_BINARY_NOMATCH),
		{ OPTION_INTEGER, 0, "max-depth", &opt.max_depth, "depth",
			"descend at most <depth> levels", PARSE_OPT_NONEG,
			NULL, 1 },
		OPT_GROUP(""),
		OPT_BIT('E', "extended-regexp", &opt.regflags,
			"use extended POSIX regular expressions", REG_EXTENDED),
		OPT_NEGBIT('G', "basic-regexp", &opt.regflags,
			"use basic POSIX regular expressions (default)",
			REG_EXTENDED),
		OPT_BOOLEAN('F', "fixed-strings", &opt.fixed,
			"interpret patterns as fixed strings"),
		OPT_GROUP(""),
		OPT_BOOLEAN('n', NULL, &opt.linenum, "show line numbers"),
		OPT_NEGBIT('h', NULL, &opt.pathname, "don't show filenames", 1),
		OPT_BIT('H', NULL, &opt.pathname, "show filenames", 1),
		OPT_NEGBIT(0, "full-name", &opt.relative,
			"show filenames relative to top directory", 1),
		OPT_BOOLEAN('l', "files-with-matches", &opt.name_only,
			"show only filenames instead of matching lines"),
		OPT_BOOLEAN(0, "name-only", &opt.name_only,
			"synonym for --files-with-matches"),
		OPT_BOOLEAN('L', "files-without-match",
			&opt.unmatch_name_only,
			"show only the names of files without match"),
		OPT_BOOLEAN('z', "null", &opt.null_following_name,
			"print NUL after filenames"),
		OPT_BOOLEAN('c', "count", &opt.count,
			"show the number of matches instead of matching lines"),
		OPT_SET_INT(0, "color", &opt.color, "highlight matches", 1),
		OPT_GROUP(""),
		OPT_CALLBACK('C', NULL, &opt, "n",
			"show <n> context lines before and after matches",
			context_callback),
		OPT_INTEGER('B', NULL, &opt.pre_context,
			"show <n> context lines before matches"),
		OPT_INTEGER('A', NULL, &opt.post_context,
			"show <n> context lines after matches"),
		OPT_NUMBER_CALLBACK(&opt, "shortcut for -C NUM",
			context_callback),
		OPT_BOOLEAN('p', "show-function", &opt.funcname,
			"show a line with the function name before matches"),
		OPT_GROUP(""),
		OPT_CALLBACK('f', NULL, &opt, "file",
			"read patterns from file", file_callback),
		{ OPTION_CALLBACK, 'e', NULL, &opt, "pattern",
			"match <pattern>", PARSE_OPT_NONEG, pattern_callback },
		{ OPTION_CALLBACK, 0, "and", &opt, NULL,
		  "combine patterns specified with -e",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG, and_callback },
		OPT_BOOLEAN(0, "or", &dummy, ""),
		{ OPTION_CALLBACK, 0, "not", &opt, NULL, "",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG, not_callback },
		{ OPTION_CALLBACK, '(', NULL, &opt, NULL, "",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_NODASH,
		  open_callback },
		{ OPTION_CALLBACK, ')', NULL, &opt, NULL, "",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_NODASH,
		  close_callback },
		OPT_BOOLEAN('q', "quiet", &opt.status_only,
			    "indicate hit with exit status without output"),
		OPT_BOOLEAN(0, "all-match", &opt.all_match,
			"show only matches from files that match all patterns"),
		OPT_GROUP(""),
		OPT_BOOLEAN(0, "ext-grep", &external_grep_allowed__ignored,
			    "allow calling of grep(1) (ignored by this build)"),
		{ OPTION_CALLBACK, 0, "help-all", &options, NULL, "show usage",
		  PARSE_OPT_HIDDEN | PARSE_OPT_NOARG, help_callback },
		OPT_END()
	};

	/*
	 * 'git grep -h', unlike 'git grep -h <pattern>', is a request
	 * to show usage information and exit.
	 */
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(grep_usage, options);

	memset(&opt, 0, sizeof(opt));
	opt.prefix = prefix;
	opt.prefix_length = (prefix && *prefix) ? strlen(prefix) : 0;
	opt.relative = 1;
	opt.pathname = 1;
	opt.pattern_tail = &opt.pattern_list;
	opt.header_tail = &opt.header_list;
	opt.regflags = REG_NEWLINE;
	opt.max_depth = -1;

	strcpy(opt.color_match, GIT_COLOR_RED GIT_COLOR_BOLD);
	opt.color = -1;
	git_config(grep_config, &opt);
	if (opt.color == -1)
		opt.color = git_use_color_default;

	/*
	 * If there is no -- then the paths must exist in the working
	 * tree.  If there is no explicit pattern specified with -e or
	 * -f, we take the first unrecognized non option to be the
	 * pattern, but then what follows it must be zero or more
	 * valid refs up to the -- (if exists), and then existing
	 * paths.  If there is an explicit pattern, then the first
	 * unrecognized non option is the beginning of the refs list
	 * that continues up to the -- (if exists), and then paths.
	 */
	argc = parse_options(argc, argv, prefix, options, grep_usage,
			     PARSE_OPT_KEEP_DASHDASH |
			     PARSE_OPT_STOP_AT_NON_OPTION |
			     PARSE_OPT_NO_INTERNAL_HELP);

	/*
	 * skip a -- separator; we know it cannot be
	 * separating revisions from pathnames if
	 * we haven't even had any patterns yet
	 */
	if (argc > 0 && !opt.pattern_list && !strcmp(argv[0], "--")) {
		argv++;
		argc--;
	}

	/* First unrecognized non-option token */
	if (argc > 0 && !opt.pattern_list) {
		append_grep_pattern(&opt, argv[0], "command line", 0,
				    GREP_PATTERN);
		argv++;
		argc--;
	}

	if (!opt.pattern_list)
		die("no pattern given.");
	if (!opt.fixed && opt.ignore_case)
		opt.regflags |= REG_ICASE;
	if ((opt.regflags != REG_NEWLINE) && opt.fixed)
		die("cannot mix --fixed-strings and regexp");

#ifndef NO_PTHREADS
	if (online_cpus() == 1 || !grep_threads_ok(&opt))
		use_threads = 0;

	if (use_threads)
		start_threads(&opt);
#else
	use_threads = 0;
#endif

	compile_grep_patterns(&opt);

	/* Check revs and then paths */
	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		unsigned char sha1[20];
		/* Is it a rev? */
		if (!get_sha1(arg, sha1)) {
			struct object *object = parse_object(sha1);
			if (!object)
				die("bad object %s", arg);
			add_object_array(object, arg, &list);
			continue;
		}
		if (!strcmp(arg, "--")) {
			i++;
			seen_dashdash = 1;
		}
		break;
	}

	/* The rest are paths */
	if (!seen_dashdash) {
		int j;
		for (j = i; j < argc; j++)
			verify_filename(prefix, argv[j]);
	}

	if (i < argc)
		paths = get_pathspec(prefix, argv + i);
	else if (prefix) {
		paths = xcalloc(2, sizeof(const char *));
		paths[0] = prefix;
		paths[1] = NULL;
	}

	if (!list.nr) {
		int hit;
		if (!cached)
			setup_work_tree();

		hit = grep_cache(&opt, paths, cached);
		if (use_threads)
			hit |= wait_all();
		return !hit;
	}

	if (cached)
		die("both --cached and trees are given.");

	for (i = 0; i < list.nr; i++) {
		struct object *real_obj;
		real_obj = deref_tag(list.objects[i].item, NULL, 0);
		if (grep_object(&opt, paths, real_obj, list.objects[i].name)) {
			hit = 1;
			if (opt.status_only)
				break;
		}
	}

	if (use_threads)
		hit |= wait_all();
	free_grep_patterns(&opt);
	return !hit;
}
