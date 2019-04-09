/*
 * "diff --no-index" support
 * Copyright (c) 2007 by Johannes Schindelin
 * Copyright (c) 2008 by Junio C Hamano
 */

#include "cache.h"
#include "color.h"
#include "commit.h"
#include "blob.h"
#include "tag.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"
#include "parse-options.h"
#include "string-list.h"
#include "dir.h"

static int read_directory_contents(const char *path, struct string_list *list)
{
	DIR *dir;
	struct dirent *e;

	if (!(dir = opendir(path)))
		return error("Could not open directory %s", path);

	while ((e = readdir(dir)))
		if (!is_dot_or_dotdot(e->d_name))
			string_list_insert(list, e->d_name);

	closedir(dir);
	return 0;
}

/*
 * This should be "(standard input)" or something, but it will
 * probably expose many more breakages in the way no-index code
 * is bolted onto the diff callchain.
 */
static const char file_from_standard_input[] = "-";

static int get_mode(const char *path, int *mode)
{
	struct stat st;

	if (!path || !strcmp(path, "/dev/null"))
		*mode = 0;
#ifdef GIT_WINDOWS_NATIVE
	else if (!strcasecmp(path, "nul"))
		*mode = 0;
#endif
	else if (path == file_from_standard_input)
		*mode = create_ce_mode(0666);
	else if (lstat(path, &st))
		return error("Could not access '%s'", path);
	else
		*mode = st.st_mode;
	return 0;
}

static int populate_from_stdin(struct diff_filespec *s)
{
	struct strbuf buf = STRBUF_INIT;
	size_t size = 0;

	if (strbuf_read(&buf, 0, 0) < 0)
		return error_errno("error while reading from stdin");

	s->should_munmap = 0;
	s->data = strbuf_detach(&buf, &size);
	s->size = size;
	s->should_free = 1;
	s->is_stdin = 1;
	return 0;
}

static struct diff_filespec *noindex_filespec(const char *name, int mode)
{
	struct diff_filespec *s;

	if (!name)
		name = "/dev/null";
	s = alloc_filespec(name);
	fill_filespec(s, &null_oid, 0, mode);
	if (name == file_from_standard_input)
		populate_from_stdin(s);
	return s;
}

static int queue_diff(struct diff_options *o,
		      const char *name1, const char *name2)
{
	int mode1 = 0, mode2 = 0;

	if (get_mode(name1, &mode1) || get_mode(name2, &mode2))
		return -1;

	if (mode1 && mode2 && S_ISDIR(mode1) != S_ISDIR(mode2)) {
		struct diff_filespec *d1, *d2;

		if (S_ISDIR(mode1)) {
			/* 2 is file that is created */
			d1 = noindex_filespec(NULL, 0);
			d2 = noindex_filespec(name2, mode2);
			name2 = NULL;
			mode2 = 0;
		} else {
			/* 1 is file that is deleted */
			d1 = noindex_filespec(name1, mode1);
			d2 = noindex_filespec(NULL, 0);
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

		if (name1 && read_directory_contents(name1, &p1))
			return -1;
		if (name2 && read_directory_contents(name2, &p2)) {
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

			ret = queue_diff(o, n1, n2);
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
		}

		d1 = noindex_filespec(name1, mode1);
		d2 = noindex_filespec(name2, mode2);
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
 */
static void fixup_paths(const char **path, struct strbuf *replacement)
{
	unsigned int isdir0, isdir1;

	if (path[0] == file_from_standard_input ||
	    path[1] == file_from_standard_input)
		return;
	isdir0 = is_directory(path[0]);
	isdir1 = is_directory(path[1]);
	if (isdir0 == isdir1)
		return;
	if (isdir0) {
		append_basename(replacement, path[0], path[1]);
		path[0] = replacement->buf;
	} else {
		append_basename(replacement, path[1], path[0]);
		path[1] = replacement->buf;
	}
}

static const char * const diff_no_index_usage[] = {
	N_("git diff --no-index [<options>] <path> <path>"),
	NULL
};

int diff_no_index(struct rev_info *revs,
		  int implicit_no_index,
		  int argc, const char **argv)
{
	int i, no_index;
	const char *paths[2];
	struct strbuf replacement = STRBUF_INIT;
	const char *prefix = revs->prefix;
	struct option no_index_options[] = {
		OPT_BOOL_F(0, "no-index", &no_index, "",
			   PARSE_OPT_NONEG | PARSE_OPT_HIDDEN),
		OPT_END(),
	};
	struct option *options;

	options = parse_options_concat(no_index_options,
				       revs->diffopt.parseopts);
	argc = parse_options(argc, argv, revs->prefix, options,
			     diff_no_index_usage, 0);
	if (argc != 2) {
		if (implicit_no_index)
			warning(_("Not a git repository. Use --no-index to "
				  "compare two paths outside a working tree"));
		usage_with_options(diff_no_index_usage, options);
	}
	FREE_AND_NULL(options);
	for (i = 0; i < 2; i++) {
		const char *p = argv[argc - 2 + i];
		if (!strcmp(p, "-"))
			/*
			 * stdin should be spelled as "-"; if you have
			 * path that is "-", spell it as "./-".
			 */
			p = file_from_standard_input;
		else if (prefix)
			p = prefix_filename(prefix, p);
		paths[i] = p;
	}

	fixup_paths(paths, &replacement);

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

	if (queue_diff(&revs->diffopt, paths[0], paths[1]))
		return 1;
	diff_set_mnemonic_prefix(&revs->diffopt, "1/", "2/");
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);

	strbuf_release(&replacement);

	/*
	 * The return code for --no-index imitates diff(1):
	 * 0 = no changes, 1 = changes, else error
	 */
	return diff_result_code(&revs->diffopt, 0);
}
