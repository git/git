#include "git-compat-util.h"
#include "abspath.h"
#include "alloc.h"
#include "config.h"
#include "convert.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "path.h"
#include "pretty.h"
#include "setup.h"
#include "refs.h"
#include "object-store-ll.h"
#include "commit.h"
#include "tree.h"
#include "tree-walk.h"
#include "attr.h"
#include "archive.h"
#include "parse-options.h"
#include "unpack-trees.h"
#include "dir.h"
#include "quote.h"

static char const * const archive_usage[] = {
	N_("git archive [<options>] <tree-ish> [<path>...]"),
	"git archive --list",
	N_("git archive --remote <repo> [--exec <cmd>] [<options>] <tree-ish> [<path>...]"),
	N_("git archive --remote <repo> [--exec <cmd>] --list"),
	NULL
};

static const struct archiver **archivers;
static int nr_archivers;
static int alloc_archivers;
static int remote_allow_unreachable;

void register_archiver(struct archiver *ar)
{
	ALLOC_GROW(archivers, nr_archivers + 1, alloc_archivers);
	archivers[nr_archivers++] = ar;
}

void init_archivers(void)
{
	init_tar_archiver();
	init_zip_archiver();
}

static void format_subst(const struct commit *commit,
			 const char *src, size_t len,
			 struct strbuf *buf, struct pretty_print_context *ctx)
{
	char *to_free = NULL;
	struct strbuf fmt = STRBUF_INIT;

	if (src == buf->buf)
		to_free = strbuf_detach(buf, NULL);
	for (;;) {
		const char *b, *c;

		b = memmem(src, len, "$Format:", 8);
		if (!b)
			break;
		c = memchr(b + 8, '$', (src + len) - b - 8);
		if (!c)
			break;

		strbuf_reset(&fmt);
		strbuf_add(&fmt, b + 8, c - b - 8);

		strbuf_add(buf, src, b - src);
		repo_format_commit_message(the_repository, commit, fmt.buf,
					   buf, ctx);
		len -= c + 1 - src;
		src  = c + 1;
	}
	strbuf_add(buf, src, len);
	strbuf_release(&fmt);
	free(to_free);
}

static void *object_file_to_archive(const struct archiver_args *args,
				    const char *path,
				    const struct object_id *oid,
				    unsigned int mode,
				    enum object_type *type,
				    unsigned long *sizep)
{
	void *buffer;
	const struct commit *commit = args->convert ? args->commit : NULL;
	struct checkout_metadata meta;

	init_checkout_metadata(&meta, args->refname,
			       args->commit_oid ? args->commit_oid :
			       (args->tree ? &args->tree->object.oid : NULL), oid);

	path += args->baselen;
	buffer = repo_read_object_file(the_repository, oid, type, sizep);
	if (buffer && S_ISREG(mode)) {
		struct strbuf buf = STRBUF_INIT;
		size_t size = 0;

		strbuf_attach(&buf, buffer, *sizep, *sizep + 1);
		convert_to_working_tree(args->repo->index, path, buf.buf, buf.len, &buf, &meta);
		if (commit)
			format_subst(commit, buf.buf, buf.len, &buf, args->pretty_ctx);
		buffer = strbuf_detach(&buf, &size);
		*sizep = size;
	}

	return buffer;
}

struct directory {
	struct directory *up;
	struct object_id oid;
	int baselen, len;
	unsigned mode;
	char path[FLEX_ARRAY];
};

struct archiver_context {
	struct archiver_args *args;
	write_archive_entry_fn_t write_entry;
	struct directory *bottom;
};

static const struct attr_check *get_archive_attrs(struct index_state *istate,
						  const char *path)
{
	static struct attr_check *check;
	if (!check)
		check = attr_check_initl("export-ignore", "export-subst", NULL);
	git_check_attr(istate, path, check);
	return check;
}

static int check_attr_export_ignore(const struct attr_check *check)
{
	return check && ATTR_TRUE(check->items[0].value);
}

static int check_attr_export_subst(const struct attr_check *check)
{
	return check && ATTR_TRUE(check->items[1].value);
}

static int write_archive_entry(const struct object_id *oid, const char *base,
		int baselen, const char *filename, unsigned mode,
		void *context)
{
	static struct strbuf path = STRBUF_INIT;
	struct archiver_context *c = context;
	struct archiver_args *args = c->args;
	write_archive_entry_fn_t write_entry = c->write_entry;
	int err;
	const char *path_without_prefix;
	unsigned long size;
	void *buffer;
	enum object_type type;

	args->convert = 0;
	strbuf_reset(&path);
	strbuf_grow(&path, PATH_MAX);
	strbuf_add(&path, args->base, args->baselen);
	strbuf_add(&path, base, baselen);
	strbuf_addstr(&path, filename);
	if (S_ISDIR(mode) || S_ISGITLINK(mode))
		strbuf_addch(&path, '/');
	path_without_prefix = path.buf + args->baselen;

	if (!S_ISDIR(mode)) {
		const struct attr_check *check;
		check = get_archive_attrs(args->repo->index, path_without_prefix);
		if (check_attr_export_ignore(check))
			return 0;
		args->convert = check_attr_export_subst(check);
	}

	if (args->prefix) {
		static struct strbuf new_path = STRBUF_INIT;
		static struct strbuf buf = STRBUF_INIT;
		const char *rel;

		rel = relative_path(path_without_prefix, args->prefix, &buf);

		/*
		 * We don't add an entry for the current working
		 * directory when we are at the root; skip it also when
		 * we're in a subdirectory or submodule.  Skip entries
		 * higher up as well.
		 */
		if (!strcmp(rel, "./") || starts_with(rel, "../"))
			return S_ISDIR(mode) ? READ_TREE_RECURSIVE : 0;

		/* rel can refer to path, so don't edit it in place */
		strbuf_reset(&new_path);
		strbuf_add(&new_path, args->base, args->baselen);
		strbuf_addstr(&new_path, rel);
		strbuf_swap(&path, &new_path);
	}

	if (args->verbose)
		fprintf(stderr, "%.*s\n", (int)path.len, path.buf);

	if (S_ISDIR(mode) || S_ISGITLINK(mode)) {
		err = write_entry(args, oid, path.buf, path.len, mode, NULL, 0);
		if (err)
			return err;
		return (S_ISDIR(mode) ? READ_TREE_RECURSIVE : 0);
	}

	/* Stream it? */
	if (S_ISREG(mode) && !args->convert &&
	    oid_object_info(args->repo, oid, &size) == OBJ_BLOB &&
	    size > big_file_threshold)
		return write_entry(args, oid, path.buf, path.len, mode, NULL, size);

	buffer = object_file_to_archive(args, path.buf, oid, mode, &type, &size);
	if (!buffer)
		return error(_("cannot read '%s'"), oid_to_hex(oid));
	err = write_entry(args, oid, path.buf, path.len, mode, buffer, size);
	free(buffer);
	return err;
}

static void queue_directory(const struct object_id *oid,
		struct strbuf *base, const char *filename,
		unsigned mode, struct archiver_context *c)
{
	struct directory *d;
	size_t len = st_add4(base->len, 1, strlen(filename), 1);
	d = xmalloc(st_add(sizeof(*d), len));
	d->up	   = c->bottom;
	d->baselen = base->len;
	d->mode	   = mode;
	c->bottom  = d;
	d->len = xsnprintf(d->path, len, "%.*s%s/", (int)base->len, base->buf, filename);
	oidcpy(&d->oid, oid);
}

static int write_directory(struct archiver_context *c)
{
	struct directory *d = c->bottom;
	int ret;

	if (!d)
		return 0;
	c->bottom = d->up;
	d->path[d->len - 1] = '\0'; /* no trailing slash */
	ret =
		write_directory(c) ||
		write_archive_entry(&d->oid, d->path, d->baselen,
				    d->path + d->baselen, d->mode,
				    c) != READ_TREE_RECURSIVE;
	free(d);
	return ret ? -1 : 0;
}

static int queue_or_write_archive_entry(const struct object_id *oid,
		struct strbuf *base, const char *filename,
		unsigned mode, void *context)
{
	struct archiver_context *c = context;

	while (c->bottom &&
	       !(base->len >= c->bottom->len &&
		 !strncmp(base->buf, c->bottom->path, c->bottom->len))) {
		struct directory *next = c->bottom->up;
		free(c->bottom);
		c->bottom = next;
	}

	if (S_ISDIR(mode)) {
		size_t baselen = base->len;
		const struct attr_check *check;

		/* Borrow base, but restore its original value when done. */
		strbuf_addstr(base, filename);
		strbuf_addch(base, '/');
		check = get_archive_attrs(c->args->repo->index, base->buf);
		strbuf_setlen(base, baselen);

		if (check_attr_export_ignore(check))
			return 0;
		queue_directory(oid, base, filename, mode, c);
		return READ_TREE_RECURSIVE;
	}

	if (write_directory(c))
		return -1;
	return write_archive_entry(oid, base->buf, base->len, filename, mode,
				   context);
}

struct extra_file_info {
	char *base;
	struct stat stat;
	void *content;
};

int write_archive_entries(struct archiver_args *args,
		write_archive_entry_fn_t write_entry)
{
	struct archiver_context context;
	struct unpack_trees_options opts;
	struct tree_desc t;
	int err;
	struct strbuf path_in_archive = STRBUF_INIT;
	struct strbuf content = STRBUF_INIT;
	struct object_id fake_oid;
	int i;

	oidcpy(&fake_oid, null_oid());

	if (args->baselen > 0 && args->base[args->baselen - 1] == '/') {
		size_t len = args->baselen;

		while (len > 1 && args->base[len - 2] == '/')
			len--;
		if (args->verbose)
			fprintf(stderr, "%.*s\n", (int)len, args->base);
		err = write_entry(args, &args->tree->object.oid, args->base,
				  len, 040777, NULL, 0);
		if (err)
			return err;
	}

	memset(&context, 0, sizeof(context));
	context.args = args;
	context.write_entry = write_entry;

	/*
	 * Setup index and instruct attr to read index only
	 */
	if (!args->worktree_attributes) {
		memset(&opts, 0, sizeof(opts));
		opts.index_only = 1;
		opts.head_idx = -1;
		opts.src_index = args->repo->index;
		opts.dst_index = args->repo->index;
		opts.fn = oneway_merge;
		init_tree_desc(&t, args->tree->buffer, args->tree->size);
		if (unpack_trees(1, &t, &opts))
			return -1;
		git_attr_set_direction(GIT_ATTR_INDEX);
	}

	err = read_tree(args->repo, args->tree,
			&args->pathspec,
			queue_or_write_archive_entry,
			&context);
	if (err == READ_TREE_RECURSIVE)
		err = 0;
	while (context.bottom) {
		struct directory *next = context.bottom->up;
		free(context.bottom);
		context.bottom = next;
	}

	for (i = 0; i < args->extra_files.nr; i++) {
		struct string_list_item *item = args->extra_files.items + i;
		char *path = item->string;
		struct extra_file_info *info = item->util;

		put_be64(fake_oid.hash, i + 1);

		if (!info->content) {
			strbuf_reset(&path_in_archive);
			if (info->base)
				strbuf_addstr(&path_in_archive, info->base);
			strbuf_addstr(&path_in_archive, basename(path));

			strbuf_reset(&content);
			if (strbuf_read_file(&content, path, info->stat.st_size) < 0)
				err = error_errno(_("cannot read '%s'"), path);
			else
				err = write_entry(args, &fake_oid, path_in_archive.buf,
						  path_in_archive.len,
						  canon_mode(info->stat.st_mode),
						  content.buf, content.len);
		} else {
			err = write_entry(args, &fake_oid,
					  path, strlen(path),
					  canon_mode(info->stat.st_mode),
					  info->content, info->stat.st_size);
		}

		if (err)
			break;
	}
	strbuf_release(&path_in_archive);
	strbuf_release(&content);

	return err;
}

static const struct archiver *lookup_archiver(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; i < nr_archivers; i++) {
		if (!strcmp(name, archivers[i]->name))
			return archivers[i];
	}
	return NULL;
}

struct path_exists_context {
	struct pathspec pathspec;
	struct archiver_args *args;
};

static int reject_entry(const struct object_id *oid UNUSED,
			struct strbuf *base,
			const char *filename, unsigned mode,
			void *context)
{
	int ret = -1;
	struct path_exists_context *ctx = context;

	if (S_ISDIR(mode)) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addbuf(&sb, base);
		strbuf_addstr(&sb, filename);
		if (!match_pathspec(ctx->args->repo->index,
				    &ctx->pathspec,
				    sb.buf, sb.len, 0, NULL, 1))
			ret = READ_TREE_RECURSIVE;
		strbuf_release(&sb);
	}
	return ret;
}

static int reject_outside(const struct object_id *oid UNUSED,
			  struct strbuf *base, const char *filename,
			  unsigned mode, void *context)
{
	struct archiver_args *args = context;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	int ret = 0;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	strbuf_addbuf(&path, base);
	strbuf_addstr(&path, filename);
	if (starts_with(relative_path(path.buf, args->prefix, &buf), "../"))
		ret = -1;
	strbuf_release(&buf);
	strbuf_release(&path);
	return ret;
}

static int path_exists(struct archiver_args *args, const char *path)
{
	const char *paths[] = { path, NULL };
	struct path_exists_context ctx;
	int ret;

	ctx.args = args;
	parse_pathspec(&ctx.pathspec, 0, PATHSPEC_PREFER_CWD,
		       args->prefix, paths);
	ctx.pathspec.recursive = 1;
	if (args->prefix && read_tree(args->repo, args->tree, &ctx.pathspec,
				      reject_outside, args))
		die(_("pathspec '%s' matches files outside the "
		      "current directory"), path);
	ret = read_tree(args->repo, args->tree,
			&ctx.pathspec,
			reject_entry, &ctx);
	clear_pathspec(&ctx.pathspec);
	return ret != 0;
}

static void parse_pathspec_arg(const char **pathspec,
		struct archiver_args *ar_args)
{
	/*
	 * must be consistent with parse_pathspec in path_exists()
	 * Also if pathspec patterns are dependent, we're in big
	 * trouble as we test each one separately
	 */
	parse_pathspec(&ar_args->pathspec, 0, PATHSPEC_PREFER_CWD,
		       ar_args->prefix, pathspec);
	ar_args->pathspec.recursive = 1;
	if (pathspec) {
		while (*pathspec) {
			if (**pathspec && !path_exists(ar_args, *pathspec))
				die(_("pathspec '%s' did not match any files"), *pathspec);
			pathspec++;
		}
	}
}

static void parse_treeish_arg(const char **argv,
			      struct archiver_args *ar_args, int remote)
{
	const char *name = argv[0];
	const struct object_id *commit_oid;
	time_t archive_time;
	struct tree *tree;
	const struct commit *commit;
	struct object_id oid;
	char *ref = NULL;

	/* Remotes are only allowed to fetch actual refs */
	if (remote && !remote_allow_unreachable) {
		const char *colon = strchrnul(name, ':');
		int refnamelen = colon - name;

		if (!repo_dwim_ref(the_repository, name, refnamelen, &oid, &ref, 0))
			die(_("no such ref: %.*s"), refnamelen, name);
	} else {
		repo_dwim_ref(the_repository, name, strlen(name), &oid, &ref,
			      0);
	}

	if (repo_get_oid(the_repository, name, &oid))
		die(_("not a valid object name: %s"), name);

	commit = lookup_commit_reference_gently(ar_args->repo, &oid, 1);
	if (commit) {
		commit_oid = &commit->object.oid;
		archive_time = commit->date;
	} else {
		commit_oid = NULL;
		archive_time = time(NULL);
	}
	if (ar_args->mtime_option)
		archive_time = approxidate(ar_args->mtime_option);

	tree = parse_tree_indirect(&oid);
	if (!tree)
		die(_("not a tree object: %s"), oid_to_hex(&oid));

	ar_args->refname = ref;
	ar_args->tree = tree;
	ar_args->commit_oid = commit_oid;
	ar_args->commit = commit;
	ar_args->time = archive_time;
}

static void extra_file_info_clear(void *util, const char *str UNUSED)
{
	struct extra_file_info *info = util;
	free(info->base);
	free(info->content);
	free(info);
}

static int add_file_cb(const struct option *opt, const char *arg, int unset)
{
	struct archiver_args *args = opt->value;
	const char **basep = (const char **)opt->defval;
	const char *base = *basep;
	char *path;
	struct string_list_item *item;
	struct extra_file_info *info;

	if (unset) {
		string_list_clear_func(&args->extra_files,
				       extra_file_info_clear);
		return 0;
	}

	if (!arg)
		return -1;

	info = xmalloc(sizeof(*info));
	info->base = xstrdup_or_null(base);

	if (!strcmp(opt->long_name, "add-file")) {
		path = prefix_filename(args->prefix, arg);
		if (stat(path, &info->stat))
			die(_("File not found: %s"), path);
		if (!S_ISREG(info->stat.st_mode))
			die(_("Not a regular file: %s"), path);
		info->content = NULL; /* read the file later */
	} else if (!strcmp(opt->long_name, "add-virtual-file")) {
		struct strbuf buf = STRBUF_INIT;
		const char *p = arg;

		if (*p != '"')
			p = strchr(p, ':');
		else if (unquote_c_style(&buf, p, &p) < 0)
			die(_("unclosed quote: '%s'"), arg);

		if (!p || *p != ':')
			die(_("missing colon: '%s'"), arg);

		if (p == arg)
			die(_("empty file name: '%s'"), arg);

		path = buf.len ?
			strbuf_detach(&buf, NULL) : xstrndup(arg, p - arg);

		if (args->prefix) {
			char *save = path;
			path = prefix_filename(args->prefix, path);
			free(save);
		}
		memset(&info->stat, 0, sizeof(info->stat));
		info->stat.st_mode = S_IFREG | 0644;
		info->content = xstrdup(p + 1);
		info->stat.st_size = strlen(info->content);
	} else {
		BUG("add_file_cb() called for %s", opt->long_name);
	}
	item = string_list_append_nodup(&args->extra_files, path);
	item->util = info;

	return 0;
}

static int number_callback(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	*(int *)opt->value = strtol(arg, NULL, 10);
	return 0;
}

static int parse_archive_args(int argc, const char **argv,
		const struct archiver **ar, struct archiver_args *args,
		const char *name_hint, int is_remote)
{
	const char *format = NULL;
	const char *base = NULL;
	const char *remote = NULL;
	const char *exec = NULL;
	const char *output = NULL;
	const char *mtime_option = NULL;
	int compression_level = -1;
	int verbose = 0;
	int i;
	int list = 0;
	int worktree_attributes = 0;
	struct option opts[] = {
		OPT_GROUP(""),
		OPT_STRING(0, "format", &format, N_("fmt"), N_("archive format")),
		OPT_STRING(0, "prefix", &base, N_("prefix"),
			N_("prepend prefix to each pathname in the archive")),
		{ OPTION_CALLBACK, 0, "add-file", args, N_("file"),
		  N_("add untracked file to archive"), 0, add_file_cb,
		  (intptr_t)&base },
		{ OPTION_CALLBACK, 0, "add-virtual-file", args,
		  N_("path:content"), N_("add untracked file to archive"), 0,
		  add_file_cb, (intptr_t)&base },
		OPT_STRING('o', "output", &output, N_("file"),
			N_("write the archive to this file")),
		OPT_BOOL(0, "worktree-attributes", &worktree_attributes,
			N_("read .gitattributes in working directory")),
		OPT__VERBOSE(&verbose, N_("report archived files on stderr")),
		{ OPTION_STRING, 0, "mtime", &mtime_option, N_("time"),
		  N_("set modification time of archive entries"),
		  PARSE_OPT_NONEG },
		OPT_NUMBER_CALLBACK(&compression_level,
			N_("set compression level"), number_callback),
		OPT_GROUP(""),
		OPT_BOOL('l', "list", &list,
			N_("list supported archive formats")),
		OPT_GROUP(""),
		OPT_STRING(0, "remote", &remote, N_("repo"),
			N_("retrieve the archive from remote repository <repo>")),
		OPT_STRING(0, "exec", &exec, N_("command"),
			N_("path to the remote git-upload-archive command")),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, opts, archive_usage, 0);

	if (remote)
		die(_("Unexpected option --remote"));
	if (exec)
		die(_("the option '%s' requires '%s'"), "--exec", "--remote");
	if (output)
		die(_("Unexpected option --output"));
	if (is_remote && args->extra_files.nr)
		die(_("options '%s' and '%s' cannot be used together"), "--add-file", "--remote");

	if (!base)
		base = "";

	if (list) {
		for (i = 0; i < nr_archivers; i++)
			if (!is_remote || archivers[i]->flags & ARCHIVER_REMOTE)
				printf("%s\n", archivers[i]->name);
		exit(0);
	}

	if (!format && name_hint)
		format = archive_format_from_filename(name_hint);
	if (!format)
		format = "tar";

	/* We need at least one parameter -- tree-ish */
	if (argc < 1)
		usage_with_options(archive_usage, opts);
	*ar = lookup_archiver(format);
	if (!*ar || (is_remote && !((*ar)->flags & ARCHIVER_REMOTE)))
		die(_("Unknown archive format '%s'"), format);

	args->compression_level = Z_DEFAULT_COMPRESSION;
	if (compression_level != -1) {
		int levels_ok = (*ar)->flags & ARCHIVER_WANT_COMPRESSION_LEVELS;
		int high_ok = (*ar)->flags & ARCHIVER_HIGH_COMPRESSION_LEVELS;
		if (levels_ok && (compression_level <= 9 || high_ok))
			args->compression_level = compression_level;
		else {
			die(_("Argument not supported for format '%s': -%d"),
					format, compression_level);
		}
	}
	args->verbose = verbose;
	args->base = base;
	args->baselen = strlen(base);
	args->worktree_attributes = worktree_attributes;
	args->mtime_option = mtime_option;

	return argc;
}

int write_archive(int argc, const char **argv, const char *prefix,
		  struct repository *repo,
		  const char *name_hint, int remote)
{
	const struct archiver *ar = NULL;
	struct pretty_print_describe_status describe_status = {0};
	struct pretty_print_context ctx = {0};
	struct archiver_args args;
	int rc;

	git_config_get_bool("uploadarchive.allowunreachable", &remote_allow_unreachable);
	git_config(git_default_config, NULL);

	describe_status.max_invocations = 1;
	ctx.date_mode.type = DATE_NORMAL;
	ctx.abbrev = DEFAULT_ABBREV;
	ctx.describe_status = &describe_status;
	args.pretty_ctx = &ctx;
	args.repo = repo;
	args.prefix = prefix;
	string_list_init_dup(&args.extra_files);
	argc = parse_archive_args(argc, argv, &ar, &args, name_hint, remote);
	if (!startup_info->have_repository) {
		/*
		 * We know this will die() with an error, so we could just
		 * die ourselves; but its error message will be more specific
		 * than what we could write here.
		 */
		setup_git_directory();
	}

	parse_treeish_arg(argv, &args, remote);
	parse_pathspec_arg(argv + 1, &args);

	rc = ar->write_archive(ar, &args);

	string_list_clear_func(&args.extra_files, extra_file_info_clear);
	free(args.refname);
	clear_pathspec(&args.pathspec);

	return rc;
}

static int match_extension(const char *filename, const char *ext)
{
	int prefixlen = strlen(filename) - strlen(ext);

	/*
	 * We need 1 character for the '.', and 1 character to ensure that the
	 * prefix is non-empty (k.e., we don't match .tar.gz with no actual
	 * filename).
	 */
	if (prefixlen < 2 || filename[prefixlen - 1] != '.')
		return 0;
	return !strcmp(filename + prefixlen, ext);
}

const char *archive_format_from_filename(const char *filename)
{
	int i;

	for (i = 0; i < nr_archivers; i++)
		if (match_extension(filename, archivers[i]->name))
			return archivers[i]->name;
	return NULL;
}
