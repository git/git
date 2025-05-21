/*
 * "diff --no-index" support
 * Copyright (c) 2007 by Johannes Schindelin
 * Copyright (c) 2008 by Junio C Hamano
 */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "abspath.h"
#include "color.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "gettext.h"
#include "revision.h"
#include "parse-options.h"
#include "pathspec.h"
#include "string-list.h"
#include "dir.h"

static int read_directory_contents(const char *path, struct string_list *list,
				   const struct pathspec *pathspec,
				   int skip)
{
	struct strbuf match = STRBUF_INIT;
	int len;
	DIR *dir;
	struct dirent *e;

	if (!(dir = opendir(path)))
		return error("Could not open directory %s", path);

	if (pathspec) {
		strbuf_addstr(&match, path);
		strbuf_complete(&match, '/');
		strbuf_remove(&match, 0, skip);

		len = match.len;
	}

	while ((e = readdir_skip_dot_and_dotdot(dir))) {
		if (pathspec) {
			strbuf_setlen(&match, len);
			strbuf_addstr(&match, e->d_name);

			if (!match_leading_pathspec(NULL, pathspec,
						    match.buf, match.len,
						    0, NULL, e->d_type == DT_DIR ? 1 : 0))
				continue;
		}

		string_list_insert(list, e->d_name);
	}

	strbuf_release(&match);
	closedir(dir);
	return 0;
}

/*
 * This should be "(standard input)" or something, but it will
 * probably expose many more breakages in the way no-index code
 * is bolted onto the diff callchain.
 */
static const char file_from_standard_input[] = "-";

/*
 * For paths given on the command-line we treat "-" as stdin and named
 * pipes and symbolic links to named pipes specially.
 */
enum special {
	SPECIAL_NONE,
	SPECIAL_STDIN,
	SPECIAL_PIPE,
};

static int get_mode(const char *path, int *mode, enum special *special)
{
	struct stat st;

	if (!path || !strcmp(path, "/dev/null")) {
		*mode = 0;
#ifdef GIT_WINDOWS_NATIVE
	} else if (!strcasecmp(path, "nul")) {
		*mode = 0;
#endif
	} else if (path == file_from_standard_input) {
		*mode = create_ce_mode(0666);
		*special = SPECIAL_STDIN;
	} else if (lstat(path, &st)) {
		return error("Could not access '%s'", path);
	} else {
		*mode = st.st_mode;
	}
	/*
	 * For paths on the command-line treat named pipes and symbolic
	 * links that resolve to a named pipe specially.
	 */
	if (special &&
	    (S_ISFIFO(*mode) ||
	     (S_ISLNK(*mode) && !stat(path, &st) && S_ISFIFO(st.st_mode)))) {
		*mode = create_ce_mode(0666);
		*special = SPECIAL_PIPE;
	}

	return 0;
}

static void populate_common(struct diff_filespec *s, struct strbuf *buf)
{
	size_t size = 0;

	s->should_munmap = 0;
	s->data = strbuf_detach(buf, &size);
	s->size = size;
	s->should_free = 1;
	s->is_stdin = 1;
}

static void populate_from_pipe(struct diff_filespec *s)
{
	struct strbuf buf = STRBUF_INIT;
	int fd = xopen(s->path, O_RDONLY);

	if (strbuf_read(&buf, fd, 0) < 0)
		die_errno("error while reading from '%s'", s->path);
	close(fd);
	populate_common(s, &buf);
}

static void populate_from_stdin(struct diff_filespec *s)
{
	struct strbuf buf = STRBUF_INIT;

	if (strbuf_read(&buf, 0, 0) < 0)
		die_errno("error while reading from stdin");
	populate_common(s, &buf);
}

static struct diff_filespec *noindex_filespec(const struct git_hash_algo *algop,
					      const char *name, int mode,
					      enum special special)
{
	struct diff_filespec *s;

	if (!name)
		name = "/dev/null";
	s = alloc_filespec(name);
	fill_filespec(s, null_oid(algop), 0, mode);
	if (special == SPECIAL_STDIN)
		populate_from_stdin(s);
	else if (special == SPECIAL_PIPE)
		populate_from_pipe(s);
	return s;
}

static int queue_diff(struct diff_options *o, const struct git_hash_algo *algop,
		      const char *name1, const char *name2, int recursing,
		      const struct pathspec *ps, int skip1, int skip2)
{
	int mode1 = 0, mode2 = 0;
	enum special special1 = SPECIAL_NONE, special2 = SPECIAL_NONE;

	/* Paths can only be special if we're not recursing. */
	if (get_mode(name1, &mode1, recursing ? NULL : &special1) ||
	    get_mode(name2, &mode2, recursing ? NULL : &special2))
		return -1;

	if (mode1 && mode2 && S_ISDIR(mode1) != S_ISDIR(mode2)) {
		struct diff_filespec *d1, *d2;

		if (S_ISDIR(mode1)) {
			/* 2 is file that is created */
			d1 = noindex_filespec(algop, NULL, 0, SPECIAL_NONE);
			d2 = noindex_filespec(algop, name2, mode2, special2);
			name2 = NULL;
			mode2 = 0;
		} else {
			/* 1 is file that is deleted */
			d1 = noindex_filespec(algop, name1, mode1, special1);
			d2 = noindex_filespec(algop, NULL, 0, SPECIAL_NONE);
			name1 = NULL;
			mode1 = 0;
		}
		/* emit that file */
		diff_queue(&diff_queued_diff, d1, d2);

		/* and then let the entire directory be created or deleted */
	}

	if (S_ISDIR(mode1) || S_ISDIR(mode2)) {
		struct strbuf buffer1 = STRBUF_INIT;
		struct strbuf buffer2 = STRBUF_INIT;
		struct string_list p1 = STRING_LIST_INIT_DUP;
		struct string_list p2 = STRING_LIST_INIT_DUP;
		int i1, i2, ret = 0;
		size_t len1 = 0, len2 = 0;

		if (name1 && read_directory_contents(name1, &p1, ps, skip1))
			return -1;
		if (name2 && read_directory_contents(name2, &p2, ps, skip2)) {
			string_list_clear(&p1, 0);
			return -1;
		}

		if (name1) {
			strbuf_addstr(&buffer1, name1);
			strbuf_complete(&buffer1, '/');
			len1 = buffer1.len;
		}

		if (name2) {
			strbuf_addstr(&buffer2, name2);
			strbuf_complete(&buffer2, '/');
			len2 = buffer2.len;
		}

		for (i1 = i2 = 0; !ret && (i1 < p1.nr || i2 < p2.nr); ) {
			const char *n1, *n2;
			int comp;

			strbuf_setlen(&buffer1, len1);
			strbuf_setlen(&buffer2, len2);

			if (i1 == p1.nr)
				comp = 1;
			else if (i2 == p2.nr)
				comp = -1;
			else
				comp = strcmp(p1.items[i1].string, p2.items[i2].string);

			if (comp > 0)
				n1 = NULL;
			else {
				strbuf_addstr(&buffer1, p1.items[i1++].string);
				n1 = buffer1.buf;
			}

			if (comp < 0)
				n2 = NULL;
			else {
				strbuf_addstr(&buffer2, p2.items[i2++].string);
				n2 = buffer2.buf;
			}

			ret = queue_diff(o, algop, n1, n2, 1, ps, skip1, skip2);
		}
		string_list_clear(&p1, 0);
		string_list_clear(&p2, 0);
		strbuf_release(&buffer1);
		strbuf_release(&buffer2);

		return ret;
	} else {
		struct diff_filespec *d1, *d2;

		if (o->flags.reverse_diff) {
			SWAP(mode1, mode2);
			SWAP(name1, name2);
			SWAP(special1, special2);
		}

		d1 = noindex_filespec(algop, name1, mode1, special1);
		d2 = noindex_filespec(algop, name2, mode2, special2);
		diff_queue(&diff_queued_diff, d1, d2);
		return 0;
	}
}

/* append basename of F to D */
static void append_basename(struct strbuf *path, const char *dir, const char *file)
{
	const char *tail = strrchr(file, '/');

	strbuf_addstr(path, dir);
	while (path->len && path->buf[path->len - 1] == '/')
		path->len--;
	strbuf_addch(path, '/');
	strbuf_addstr(path, tail ? tail + 1 : file);
}

/*
 * DWIM "diff D F" into "diff D/F F" and "diff F D" into "diff F D/F"
 * Note that we append the basename of F to D/, so "diff a/b/file D"
 * becomes "diff a/b/file D/file", not "diff a/b/file D/a/b/file".
 *
 * Return 1 if both paths are directories, 0 otherwise.
 */
static int fixup_paths(const char **path, struct strbuf *replacement)
{
	struct stat st;
	unsigned int isdir0 = 0, isdir1 = 0;
	unsigned int ispipe0 = 0, ispipe1 = 0;

	if (path[0] != file_from_standard_input && !stat(path[0], &st)) {
		isdir0 = S_ISDIR(st.st_mode);
		ispipe0 = S_ISFIFO(st.st_mode);
	}

	if (path[1] != file_from_standard_input && !stat(path[1], &st)) {
		isdir1 = S_ISDIR(st.st_mode);
		ispipe1 = S_ISFIFO(st.st_mode);
	}

	if ((path[0] == file_from_standard_input && isdir1) ||
	    (isdir0 && path[1] == file_from_standard_input))
		die(_("cannot compare stdin to a directory"));

	if ((isdir0 && ispipe1) || (ispipe0 && isdir1))
		die(_("cannot compare a named pipe to a directory"));

	/* if both paths are directories, we will enable pathspecs */
	if (isdir0 && isdir1)
		return 1;

	if (isdir0) {
		append_basename(replacement, path[0], path[1]);
		path[0] = replacement->buf;
	} else if (isdir1) {
		append_basename(replacement, path[1], path[0]);
		path[1] = replacement->buf;
	}

	return 0;
}

static const char * const diff_no_index_usage[] = {
	N_("git diff --no-index [<options>] <path> <path> [<pathspec>...]"),
	NULL
};

int diff_no_index(struct rev_info *revs, const struct git_hash_algo *algop,
		  int implicit_no_index, int argc, const char **argv)
{
	struct pathspec pathspec, *ps = NULL;
	int i, no_index, skip1 = 0, skip2 = 0;
	int ret = 1;
	const char *paths[2];
	char *to_free[ARRAY_SIZE(paths)] = { 0 };
	struct strbuf replacement = STRBUF_INIT;
	const char *prefix = revs->prefix;
	struct option no_index_options[] = {
		OPT_BOOL_F(0, "no-index", &no_index, "",
			   PARSE_OPT_NONEG | PARSE_OPT_HIDDEN),
		OPT_END(),
	};
	struct option *options;

	options = add_diff_options(no_index_options, &revs->diffopt);
	argc = parse_options(argc, argv, revs->prefix, options,
			     diff_no_index_usage, 0);
	if (argc < 2) {
		if (implicit_no_index)
			warning(_("Not a git repository. Use --no-index to "
				  "compare two paths outside a working tree"));
		usage_with_options(diff_no_index_usage, options);
	}
	for (i = 0; i < 2; i++) {
		const char *p = argv[i];
		if (!strcmp(p, "-"))
			/*
			 * stdin should be spelled as "-"; if you have
			 * path that is "-", spell it as "./-".
			 */
			p = file_from_standard_input;
		else if (prefix)
			p = to_free[i] = prefix_filename(prefix, p);
		paths[i] = p;
	}

	if (fixup_paths(paths, &replacement)) {
		parse_pathspec(&pathspec, PATHSPEC_FROMTOP | PATHSPEC_ATTR,
			       PATHSPEC_PREFER_FULL | PATHSPEC_NO_REPOSITORY,
			       NULL, &argv[2]);
		if (pathspec.nr)
			ps = &pathspec;

		skip1 = strlen(paths[0]);
		skip1 += paths[0][skip1] == '/' ? 0 : 1;
		skip2 = strlen(paths[1]);
		skip2 += paths[1][skip2] == '/' ? 0 : 1;
	} else if (argc > 2) {
		warning(_("Limiting comparison with pathspecs is only "
			  "supported if both paths are directories."));
		usage_with_options(diff_no_index_usage, options);
	}
	FREE_AND_NULL(options);

	revs->diffopt.skip_stat_unmatch = 1;
	if (!revs->diffopt.output_format)
		revs->diffopt.output_format = DIFF_FORMAT_PATCH;

	revs->diffopt.flags.no_index = 1;

	revs->diffopt.flags.relative_name = 1;
	revs->diffopt.prefix = prefix;

	revs->max_count = -2;
	diff_setup_done(&revs->diffopt);

	setup_diff_pager(&revs->diffopt);
	revs->diffopt.flags.exit_with_status = 1;

	if (queue_diff(&revs->diffopt, algop, paths[0], paths[1], 0, ps,
		       skip1, skip2))
		goto out;
	diff_set_mnemonic_prefix(&revs->diffopt, "1/", "2/");
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);

	/*
	 * The return code for --no-index imitates diff(1):
	 * 0 = no changes, 1 = changes, else error
	 */
	ret = diff_result_code(revs);

out:
	for (i = 0; i < ARRAY_SIZE(to_free); i++)
		free(to_free[i]);
	strbuf_release(&replacement);
	if (ps)
		clear_pathspec(ps);
	return ret;
}
