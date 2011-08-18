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
#include "string-list.h"
#include "run-command.h"
#include "userdiff.h"
#include "grep.h"
#include "quote.h"
#include "dir.h"
#include "thread-utils.h"

static char const * const grep_usage[] = {
	"git grep [options] [-e] <pattern> [<rev>...] [[--] <path>...]",
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
struct work_item {
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

static int print_hunk_marks_between_files;
static int printed_something;

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
		if (w->out.len) {
			if (print_hunk_marks_between_files && printed_something)
				write_or_die(1, "--\n", 3);
			write_or_die(1, w->out.buf, w->out.len);
			printed_something = 1;
		}
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
			die(_("grep: failed to create thread: %s"),
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
	char *color = NULL;

	switch (userdiff_config(var, value)) {
	case 0: break;
	case -1: return -1;
	default: return 0;
	}

	if (!strcmp(var, "grep.extendedregexp")) {
		if (git_config_bool(var, value))
			opt->regflags |= REG_EXTENDED;
		else
			opt->regflags &= ~REG_EXTENDED;
		return 0;
	}

	if (!strcmp(var, "grep.linenumber")) {
		opt->linenum = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "color.grep"))
		opt->color = git_config_colorbool(var, value);
	else if (!strcmp(var, "color.grep.context"))
		color = opt->color_context;
	else if (!strcmp(var, "color.grep.filename"))
		color = opt->color_filename;
	else if (!strcmp(var, "color.grep.function"))
		color = opt->color_function;
	else if (!strcmp(var, "color.grep.linenumber"))
		color = opt->color_lineno;
	else if (!strcmp(var, "color.grep.match"))
		color = opt->color_match;
	else if (!strcmp(var, "color.grep.selected"))
		color = opt->color_selected;
	else if (!strcmp(var, "color.grep.separator"))
		color = opt->color_sep;
	else
		return git_color_default_config(var, value, cb);
	if (color) {
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, color);
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
		error(_("'%s': unable to read %s"), name, sha1_to_hex(sha1));

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
			error(_("'%s': %s"), filename, strerror(errno));
		return NULL;
	}
	if (!S_ISREG(st.st_mode))
		return NULL;
	*sz = xsize_t(st.st_size);
	i = open(filename, O_RDONLY);
	if (i < 0)
		goto err_ret;
	data = xmalloc(*sz + 1);
	if (st.st_size != read_in_full(i, data, *sz)) {
		error(_("'%s': short read %s"), filename, strerror(errno));
		close(i);
		free(data);
		return NULL;
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

static void append_path(struct grep_opt *opt, const void *data, size_t len)
{
	struct string_list *path_list = opt->output_priv;

	if (len == 1 && *(const char *)data == '\0')
		return;
	string_list_append(path_list, xstrndup(data, len));
}

static void run_pager(struct grep_opt *opt, const char *prefix)
{
	struct string_list *path_list = opt->output_priv;
	const char **argv = xmalloc(sizeof(const char *) * (path_list->nr + 1));
	int i, status;

	for (i = 0; i < path_list->nr; i++)
		argv[i] = path_list->items[i].string;
	argv[path_list->nr] = NULL;

	if (prefix && chdir(prefix))
		die(_("Failed to chdir: %s"), prefix);
	status = run_command_v_opt(argv, RUN_USING_SHELL);
	if (status)
		exit(status);
	free(argv);
}

static int grep_cache(struct grep_opt *opt, const struct pathspec *pathspec, int cached)
{
	int hit = 0;
	int nr;
	read_cache();

	for (nr = 0; nr < active_nr; nr++) {
		struct cache_entry *ce = active_cache[nr];
		if (!S_ISREG(ce->ce_mode))
			continue;
		if (!match_pathspec_depth(pathspec, ce->name, ce_namelen(ce), 0, NULL))
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
	return hit;
}

static int grep_tree(struct grep_opt *opt, const struct pathspec *pathspec,
		     struct tree_desc *tree, struct strbuf *base, int tn_len)
{
	int hit = 0, match = 0;
	struct name_entry entry;
	int old_baselen = base->len;

	while (tree_entry(tree, &entry)) {
		int te_len = tree_entry_len(entry.path, entry.sha1);

		if (match != 2) {
			match = tree_entry_interesting(&entry, base, tn_len, pathspec);
			if (match < 0)
				break;
			if (match == 0)
				continue;
		}

		strbuf_add(base, entry.path, te_len);

		if (S_ISREG(entry.mode)) {
			hit |= grep_sha1(opt, entry.sha1, base->buf, tn_len);
		}
		else if (S_ISDIR(entry.mode)) {
			enum object_type type;
			struct tree_desc sub;
			void *data;
			unsigned long size;

			data = lock_and_read_sha1_file(entry.sha1, &type, &size);
			if (!data)
				die(_("unable to read tree (%s)"),
				    sha1_to_hex(entry.sha1));

			strbuf_addch(base, '/');
			init_tree_desc(&sub, data, size);
			hit |= grep_tree(opt, pathspec, &sub, base, tn_len);
			free(data);
		}
		strbuf_setlen(base, old_baselen);

		if (hit && opt->status_only)
			break;
	}
	return hit;
}

static int grep_object(struct grep_opt *opt, const struct pathspec *pathspec,
		       struct object *obj, const char *name)
{
	if (obj->type == OBJ_BLOB)
		return grep_sha1(opt, obj->sha1, name, 0);
	if (obj->type == OBJ_COMMIT || obj->type == OBJ_TREE) {
		struct tree_desc tree;
		void *data;
		unsigned long size;
		struct strbuf base;
		int hit, len;

		data = read_object_with_reference(obj->sha1, tree_type,
						  &size, NULL);
		if (!data)
			die(_("unable to read tree (%s)"), sha1_to_hex(obj->sha1));

		len = name ? strlen(name) : 0;
		strbuf_init(&base, PATH_MAX + len + 1);
		if (len) {
			strbuf_add(&base, name, len);
			strbuf_addch(&base, ':');
		}
		init_tree_desc(&tree, data, size);
		hit = grep_tree(opt, pathspec, &tree, &base, base.len);
		strbuf_release(&base);
		free(data);
		return hit;
	}
	die(_("unable to grep from object of type %s"), typename(obj->type));
}

static int grep_objects(struct grep_opt *opt, const struct pathspec *pathspec,
			const struct object_array *list)
{
	unsigned int i;
	int hit = 0;
	const unsigned int nr = list->nr;

	for (i = 0; i < nr; i++) {
		struct object *real_obj;
		real_obj = deref_tag(list->objects[i].item, NULL, 0);
		if (grep_object(opt, pathspec, real_obj, list->objects[i].name)) {
			hit = 1;
			if (opt->status_only)
				break;
		}
	}
	return hit;
}

static int grep_directory(struct grep_opt *opt, const struct pathspec *pathspec)
{
	struct dir_struct dir;
	int i, hit = 0;

	memset(&dir, 0, sizeof(dir));
	setup_standard_excludes(&dir);

	fill_directory(&dir, pathspec->raw);
	for (i = 0; i < dir.nr; i++) {
		const char *name = dir.entries[i]->name;
		int namelen = strlen(name);
		if (!match_pathspec_depth(pathspec, name, namelen, 0, NULL))
			continue;
		hit |= grep_file(opt, dir.entries[i]->name);
		if (hit && opt->status_only)
			break;
	}
	return hit;
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
		return error(_("switch `%c' expects a numerical value"),
			     opt->short_name);
	}
	grep_opt->pre_context = grep_opt->post_context = value;
	return 0;
}

static int file_callback(const struct option *opt, const char *arg, int unset)
{
	struct grep_opt *grep_opt = opt->value;
	int from_stdin = !strcmp(arg, "-");
	FILE *patterns;
	int lno = 0;
	struct strbuf sb = STRBUF_INIT;

	patterns = from_stdin ? stdin : fopen(arg, "r");
	if (!patterns)
		die_errno(_("cannot open '%s'"), arg);
	while (strbuf_getline(&sb, patterns, '\n') == 0) {
		char *s;
		size_t len;

		/* ignore empty line like grep does */
		if (sb.len == 0)
			continue;

		s = strbuf_detach(&sb, &len);
		append_grep_pat(grep_opt, s, len, arg, ++lno, GREP_PATTERN);
	}
	if (!from_stdin)
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
	const char *show_in_pager = NULL, *default_pager = "dummy";
	struct grep_opt opt;
	struct object_array list = OBJECT_ARRAY_INIT;
	const char **paths = NULL;
	struct pathspec pathspec;
	struct string_list path_list = STRING_LIST_INIT_NODUP;
	int i;
	int dummy;
	int use_index = 1;
	enum {
		pattern_type_unspecified = 0,
		pattern_type_bre,
		pattern_type_ere,
		pattern_type_fixed,
		pattern_type_pcre,
	};
	int pattern_type = pattern_type_unspecified;

	struct option options[] = {
		OPT_BOOLEAN(0, "cached", &cached,
			"search in index instead of in the work tree"),
		OPT_BOOLEAN(0, "index", &use_index,
			"--no-index finds in contents not managed by git"),
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
		OPT_SET_INT('E', "extended-regexp", &pattern_type,
			    "use extended POSIX regular expressions",
			    pattern_type_ere),
		OPT_SET_INT('G', "basic-regexp", &pattern_type,
			    "use basic POSIX regular expressions (default)",
			    pattern_type_bre),
		OPT_SET_INT('F', "fixed-strings", &pattern_type,
			    "interpret patterns as fixed strings",
			    pattern_type_fixed),
		OPT_SET_INT('P', "perl-regexp", &pattern_type,
			    "use Perl-compatible regular expressions",
			    pattern_type_pcre),
		OPT_GROUP(""),
		OPT_BOOLEAN('n', "line-number", &opt.linenum, "show line numbers"),
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
		OPT__COLOR(&opt.color, "highlight matches"),
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
		OPT__QUIET(&opt.status_only,
			   "indicate hit with exit status without output"),
		OPT_BOOLEAN(0, "all-match", &opt.all_match,
			"show only matches from files that match all patterns"),
		OPT_GROUP(""),
		{ OPTION_STRING, 'O', "open-files-in-pager", &show_in_pager,
			"pager", "show matching files in the pager",
			PARSE_OPT_OPTARG, NULL, (intptr_t)default_pager },
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

	strcpy(opt.color_context, "");
	strcpy(opt.color_filename, "");
	strcpy(opt.color_function, "");
	strcpy(opt.color_lineno, "");
	strcpy(opt.color_match, GIT_COLOR_BOLD_RED);
	strcpy(opt.color_selected, "");
	strcpy(opt.color_sep, GIT_COLOR_CYAN);
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
	switch (pattern_type) {
	case pattern_type_fixed:
		opt.fixed = 1;
		opt.pcre = 0;
		break;
	case pattern_type_bre:
		opt.fixed = 0;
		opt.pcre = 0;
		opt.regflags &= ~REG_EXTENDED;
		break;
	case pattern_type_ere:
		opt.fixed = 0;
		opt.pcre = 0;
		opt.regflags |= REG_EXTENDED;
		break;
	case pattern_type_pcre:
		opt.fixed = 0;
		opt.pcre = 1;
		break;
	default:
		break; /* nothing */
	}

	if (use_index && !startup_info->have_repository)
		/* die the same way as if we did it at the beginning */
		setup_git_directory();

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

	if (show_in_pager == default_pager)
		show_in_pager = git_pager(1);
	if (show_in_pager) {
		opt.color = 0;
		opt.name_only = 1;
		opt.null_following_name = 1;
		opt.output_priv = &path_list;
		opt.output = append_path;
		string_list_append(&path_list, show_in_pager);
		use_threads = 0;
	}

	if (!opt.pattern_list)
		die(_("no pattern given."));
	if (!opt.fixed && opt.ignore_case)
		opt.regflags |= REG_ICASE;

#ifndef NO_PTHREADS
	if (online_cpus() == 1 || !grep_threads_ok(&opt))
		use_threads = 0;

	if (use_threads) {
		if (opt.pre_context || opt.post_context)
			print_hunk_marks_between_files = 1;
		start_threads(&opt);
	}
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
				die(_("bad object %s"), arg);
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

	paths = get_pathspec(prefix, argv + i);
	init_pathspec(&pathspec, paths);
	pathspec.max_depth = opt.max_depth;
	pathspec.recursive = 1;

	if (show_in_pager && (cached || list.nr))
		die(_("--open-files-in-pager only works on the worktree"));

	if (show_in_pager && opt.pattern_list && !opt.pattern_list->next) {
		const char *pager = path_list.items[0].string;
		int len = strlen(pager);

		if (len > 4 && is_dir_sep(pager[len - 5]))
			pager += len - 4;

		if (!strcmp("less", pager) || !strcmp("vi", pager)) {
			struct strbuf buf = STRBUF_INIT;
			strbuf_addf(&buf, "+/%s%s",
					strcmp("less", pager) ? "" : "*",
					opt.pattern_list->pattern);
			string_list_append(&path_list, buf.buf);
			strbuf_detach(&buf, NULL);
		}
	}

	if (!show_in_pager)
		setup_pager();


	if (!use_index) {
		if (cached)
			die(_("--cached cannot be used with --no-index."));
		if (list.nr)
			die(_("--no-index cannot be used with revs."));
		hit = grep_directory(&opt, &pathspec);
	} else if (!list.nr) {
		if (!cached)
			setup_work_tree();

		hit = grep_cache(&opt, &pathspec, cached);
	} else {
		if (cached)
			die(_("both --cached and trees are given."));
		hit = grep_objects(&opt, &pathspec, &list);
	}

	if (use_threads)
		hit |= wait_all();
	if (hit && show_in_pager)
		run_pager(&opt, prefix);
	free_grep_patterns(&opt);
	return !hit;
}
