#include "cache.h"
#include "commit.h"
#include "tree-walk.h"
#include "attr.h"
#include "archive.h"
#include "parse-options.h"
#include "unpack-trees.h"

static char const * const archive_usage[] = {
	"git archive [options] <tree-ish> [<path>...]",
	"git archive --list",
	"git archive --remote <repo> [--exec <cmd>] [options] <tree-ish> [<path>...]",
	"git archive --remote <repo> [--exec <cmd>] --list",
	NULL
};

static const struct archiver **archivers;
static int nr_archivers;
static int alloc_archivers;

void register_archiver(struct archiver *ar)
{
	ALLOC_GROW(archivers, nr_archivers + 1, alloc_archivers);
	archivers[nr_archivers++] = ar;
}

static void format_subst(const struct commit *commit,
                         const char *src, size_t len,
                         struct strbuf *buf)
{
	char *to_free = NULL;
	struct strbuf fmt = STRBUF_INIT;
	struct pretty_print_context ctx = {0};
	ctx.date_mode = DATE_NORMAL;
	ctx.abbrev = DEFAULT_ABBREV;

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
		format_commit_message(commit, fmt.buf, buf, &ctx);
		len -= c + 1 - src;
		src  = c + 1;
	}
	strbuf_add(buf, src, len);
	strbuf_release(&fmt);
	free(to_free);
}

static void *sha1_file_to_archive(const char *path, const unsigned char *sha1,
		unsigned int mode, enum object_type *type,
		unsigned long *sizep, const struct commit *commit)
{
	void *buffer;

	buffer = read_sha1_file(sha1, type, sizep);
	if (buffer && S_ISREG(mode)) {
		struct strbuf buf = STRBUF_INIT;
		size_t size = 0;

		strbuf_attach(&buf, buffer, *sizep, *sizep + 1);
		convert_to_working_tree(path, buf.buf, buf.len, &buf);
		if (commit)
			format_subst(commit, buf.buf, buf.len, &buf);
		buffer = strbuf_detach(&buf, &size);
		*sizep = size;
	}

	return buffer;
}

static void setup_archive_check(struct git_attr_check *check)
{
	static struct git_attr *attr_export_ignore;
	static struct git_attr *attr_export_subst;

	if (!attr_export_ignore) {
		attr_export_ignore = git_attr("export-ignore");
		attr_export_subst = git_attr("export-subst");
	}
	check[0].attr = attr_export_ignore;
	check[1].attr = attr_export_subst;
}

struct archiver_context {
	struct archiver_args *args;
	write_archive_entry_fn_t write_entry;
};

static int write_archive_entry(const unsigned char *sha1, const char *base,
		int baselen, const char *filename, unsigned mode, int stage,
		void *context)
{
	static struct strbuf path = STRBUF_INIT;
	struct archiver_context *c = context;
	struct archiver_args *args = c->args;
	write_archive_entry_fn_t write_entry = c->write_entry;
	struct git_attr_check check[2];
	const char *path_without_prefix;
	int convert = 0;
	int err;
	enum object_type type;
	unsigned long size;
	void *buffer;

	strbuf_reset(&path);
	strbuf_grow(&path, PATH_MAX);
	strbuf_add(&path, args->base, args->baselen);
	strbuf_add(&path, base, baselen);
	strbuf_addstr(&path, filename);
	path_without_prefix = path.buf + args->baselen;

	setup_archive_check(check);
	if (!git_checkattr(path_without_prefix, ARRAY_SIZE(check), check)) {
		if (ATTR_TRUE(check[0].value))
			return 0;
		convert = ATTR_TRUE(check[1].value);
	}

	if (S_ISDIR(mode) || S_ISGITLINK(mode)) {
		strbuf_addch(&path, '/');
		if (args->verbose)
			fprintf(stderr, "%.*s\n", (int)path.len, path.buf);
		err = write_entry(args, sha1, path.buf, path.len, mode, NULL, 0);
		if (err)
			return err;
		return (S_ISDIR(mode) ? READ_TREE_RECURSIVE : 0);
	}

	buffer = sha1_file_to_archive(path_without_prefix, sha1, mode,
			&type, &size, convert ? args->commit : NULL);
	if (!buffer)
		return error("cannot read %s", sha1_to_hex(sha1));
	if (args->verbose)
		fprintf(stderr, "%.*s\n", (int)path.len, path.buf);
	err = write_entry(args, sha1, path.buf, path.len, mode, buffer, size);
	free(buffer);
	return err;
}

int write_archive_entries(struct archiver_args *args,
		write_archive_entry_fn_t write_entry)
{
	struct archiver_context context;
	struct unpack_trees_options opts;
	struct tree_desc t;
	struct pathspec pathspec;
	int err;

	if (args->baselen > 0 && args->base[args->baselen - 1] == '/') {
		size_t len = args->baselen;

		while (len > 1 && args->base[len - 2] == '/')
			len--;
		if (args->verbose)
			fprintf(stderr, "%.*s\n", (int)len, args->base);
		err = write_entry(args, args->tree->object.sha1, args->base,
				len, 040777, NULL, 0);
		if (err)
			return err;
	}

	context.args = args;
	context.write_entry = write_entry;

	/*
	 * Setup index and instruct attr to read index only
	 */
	if (!args->worktree_attributes) {
		memset(&opts, 0, sizeof(opts));
		opts.index_only = 1;
		opts.head_idx = -1;
		opts.src_index = &the_index;
		opts.dst_index = &the_index;
		opts.fn = oneway_merge;
		init_tree_desc(&t, args->tree->buffer, args->tree->size);
		if (unpack_trees(1, &t, &opts))
			return -1;
		git_attr_set_direction(GIT_ATTR_INDEX, &the_index);
	}

	init_pathspec(&pathspec, args->pathspec);
	err = read_tree_recursive(args->tree, "", 0, 0, &pathspec,
				  write_archive_entry, &context);
	free_pathspec(&pathspec);
	if (err == READ_TREE_RECURSIVE)
		err = 0;
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

static int reject_entry(const unsigned char *sha1, const char *base,
			int baselen, const char *filename, unsigned mode,
			int stage, void *context)
{
	return -1;
}

static int path_exists(struct tree *tree, const char *path)
{
	const char *paths[] = { path, NULL };
	struct pathspec pathspec;
	int ret;

	init_pathspec(&pathspec, paths);
	ret = read_tree_recursive(tree, "", 0, 0, &pathspec, reject_entry, NULL);
	free_pathspec(&pathspec);
	return ret != 0;
}

static void parse_pathspec_arg(const char **pathspec,
		struct archiver_args *ar_args)
{
	ar_args->pathspec = pathspec = get_pathspec("", pathspec);
	if (pathspec) {
		while (*pathspec) {
			if (!path_exists(ar_args->tree, *pathspec))
				die("path not found: %s", *pathspec);
			pathspec++;
		}
	}
}

static void parse_treeish_arg(const char **argv,
		struct archiver_args *ar_args, const char *prefix)
{
	const char *name = argv[0];
	const unsigned char *commit_sha1;
	time_t archive_time;
	struct tree *tree;
	const struct commit *commit;
	unsigned char sha1[20];

	if (get_sha1(name, sha1))
		die("Not a valid object name");

	commit = lookup_commit_reference_gently(sha1, 1);
	if (commit) {
		commit_sha1 = commit->object.sha1;
		archive_time = commit->date;
	} else {
		commit_sha1 = NULL;
		archive_time = time(NULL);
	}

	tree = parse_tree_indirect(sha1);
	if (tree == NULL)
		die("not a tree object");

	if (prefix) {
		unsigned char tree_sha1[20];
		unsigned int mode;
		int err;

		err = get_tree_entry(tree->object.sha1, prefix,
				     tree_sha1, &mode);
		if (err || !S_ISDIR(mode))
			die("current working directory is untracked");

		tree = parse_tree_indirect(tree_sha1);
	}
	ar_args->tree = tree;
	ar_args->commit_sha1 = commit_sha1;
	ar_args->commit = commit;
	ar_args->time = archive_time;
}

#define OPT__COMPR(s, v, h, p) \
	{ OPTION_SET_INT, (s), NULL, (v), NULL, (h), \
	  PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, (p) }
#define OPT__COMPR_HIDDEN(s, v, p) \
	{ OPTION_SET_INT, (s), NULL, (v), NULL, "", \
	  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_HIDDEN, NULL, (p) }

static int parse_archive_args(int argc, const char **argv,
		const struct archiver **ar, struct archiver_args *args,
		const char *name_hint, int is_remote)
{
	const char *format = NULL;
	const char *base = NULL;
	const char *remote = NULL;
	const char *exec = NULL;
	const char *output = NULL;
	int compression_level = -1;
	int verbose = 0;
	int i;
	int list = 0;
	int worktree_attributes = 0;
	struct option opts[] = {
		OPT_GROUP(""),
		OPT_STRING(0, "format", &format, "fmt", "archive format"),
		OPT_STRING(0, "prefix", &base, "prefix",
			"prepend prefix to each pathname in the archive"),
		OPT_STRING('o', "output", &output, "file",
			"write the archive to this file"),
		OPT_BOOLEAN(0, "worktree-attributes", &worktree_attributes,
			"read .gitattributes in working directory"),
		OPT__VERBOSE(&verbose, "report archived files on stderr"),
		OPT__COMPR('0', &compression_level, "store only", 0),
		OPT__COMPR('1', &compression_level, "compress faster", 1),
		OPT__COMPR_HIDDEN('2', &compression_level, 2),
		OPT__COMPR_HIDDEN('3', &compression_level, 3),
		OPT__COMPR_HIDDEN('4', &compression_level, 4),
		OPT__COMPR_HIDDEN('5', &compression_level, 5),
		OPT__COMPR_HIDDEN('6', &compression_level, 6),
		OPT__COMPR_HIDDEN('7', &compression_level, 7),
		OPT__COMPR_HIDDEN('8', &compression_level, 8),
		OPT__COMPR('9', &compression_level, "compress better", 9),
		OPT_GROUP(""),
		OPT_BOOLEAN('l', "list", &list,
			"list supported archive formats"),
		OPT_GROUP(""),
		OPT_STRING(0, "remote", &remote, "repo",
			"retrieve the archive from remote repository <repo>"),
		OPT_STRING(0, "exec", &exec, "cmd",
			"path to the remote git-upload-archive command"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, opts, archive_usage, 0);

	if (remote)
		die("Unexpected option --remote");
	if (exec)
		die("Option --exec can only be used together with --remote");
	if (output)
		die("Unexpected option --output");

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
		die("Unknown archive format '%s'", format);

	args->compression_level = Z_DEFAULT_COMPRESSION;
	if (compression_level != -1) {
		if ((*ar)->flags & ARCHIVER_WANT_COMPRESSION_LEVELS)
			args->compression_level = compression_level;
		else {
			die("Argument not supported for format '%s': -%d",
					format, compression_level);
		}
	}
	args->verbose = verbose;
	args->base = base;
	args->baselen = strlen(base);
	args->worktree_attributes = worktree_attributes;

	return argc;
}

int write_archive(int argc, const char **argv, const char *prefix,
		  int setup_prefix, const char *name_hint, int remote)
{
	int nongit = 0;
	const struct archiver *ar = NULL;
	struct archiver_args args;

	if (setup_prefix && prefix == NULL)
		prefix = setup_git_directory_gently(&nongit);

	git_config(git_default_config, NULL);
	init_tar_archiver();
	init_zip_archiver();

	argc = parse_archive_args(argc, argv, &ar, &args, name_hint, remote);
	if (nongit) {
		/*
		 * We know this will die() with an error, so we could just
		 * die ourselves; but its error message will be more specific
		 * than what we could write here.
		 */
		setup_git_directory();
	}

	parse_treeish_arg(argv, &args, prefix);
	parse_pathspec_arg(argv + 1, &args);

	return ar->write_archive(ar, &args);
}

static int match_extension(const char *filename, const char *ext)
{
	int prefixlen = strlen(filename) - strlen(ext);

	/*
	 * We need 1 character for the '.', and 1 character to ensure that the
	 * prefix is non-empty (k.e., we don't match .tar.gz with no actual
	 * filename).
	 */
	if (prefixlen < 2 || filename[prefixlen-1] != '.')
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
