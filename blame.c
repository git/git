/*
 * Copyright (C) 2006, Fredrik Kuivinen <freku045@student.liu.se>
 */

#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "cache.h"
#include "refs.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "xdiff-interface.h"
#include "quote.h"

#ifndef DEBUG
#define DEBUG 0
#endif

static const char blame_usage[] =
"git-blame [-c] [-l] [-t] [-f] [-n] [-p] [-S <revs-file>] [--] file [commit]\n"
"  -c, --compatibility Use the same output mode as git-annotate (Default: off)\n"
"  -l, --long          Show long commit SHA1 (Default: off)\n"
"  -t, --time          Show raw timestamp (Default: off)\n"
"  -f, --show-name     Show original filename (Default: auto)\n"
"  -n, --show-number   Show original linenumber (Default: off)\n"
"  -p, --porcelain     Show in a format designed for machine consumption\n"
"  -S revs-file        Use revisions from revs-file instead of calling git-rev-list\n"
"  -h, --help          This message";

static struct commit **blame_lines;
static int num_blame_lines;
static char *blame_contents;
static int blame_len;

struct util_info {
	int *line_map;
	unsigned char sha1[20];	/* blob sha, not commit! */
	char *buf;
	unsigned long size;
	int num_lines;
	const char *pathname;
	unsigned meta_given:1;

	void *topo_data;
};

struct chunk {
	int off1, len1;	/* --- */
	int off2, len2;	/* +++ */
};

struct patch {
	struct chunk *chunks;
	int num;
};

static void get_blob(struct commit *commit);

/* Only used for statistics */
static int num_get_patch;
static int num_commits;
static int patch_time;
static int num_read_blob;

struct blame_diff_state {
	struct xdiff_emit_state xm;
	struct patch *ret;
};

static void process_u0_diff(void *state_, char *line, unsigned long len)
{
	struct blame_diff_state *state = state_;
	struct chunk *chunk;

	if (len < 4 || line[0] != '@' || line[1] != '@')
		return;

	if (DEBUG)
		printf("chunk line: %.*s", (int)len, line);
	state->ret->num++;
	state->ret->chunks = xrealloc(state->ret->chunks,
				      sizeof(struct chunk) * state->ret->num);
	chunk = &state->ret->chunks[state->ret->num - 1];

	assert(!strncmp(line, "@@ -", 4));

	if (parse_hunk_header(line, len,
			      &chunk->off1, &chunk->len1,
			      &chunk->off2, &chunk->len2)) {
		state->ret->num--;
		return;
	}

	if (chunk->len1 == 0)
		chunk->off1++;
	if (chunk->len2 == 0)
		chunk->off2++;

	if (chunk->off1 > 0)
		chunk->off1--;
	if (chunk->off2 > 0)
		chunk->off2--;

	assert(chunk->off1 >= 0);
	assert(chunk->off2 >= 0);
}

static struct patch *get_patch(struct commit *commit, struct commit *other)
{
	struct blame_diff_state state;
	xpparam_t xpp;
	xdemitconf_t xecfg;
	mmfile_t file_c, file_o;
	xdemitcb_t ecb;
	struct util_info *info_c = (struct util_info *)commit->util;
	struct util_info *info_o = (struct util_info *)other->util;
	struct timeval tv_start, tv_end;

	get_blob(commit);
	file_c.ptr = info_c->buf;
	file_c.size = info_c->size;

	get_blob(other);
	file_o.ptr = info_o->buf;
	file_o.size = info_o->size;

	gettimeofday(&tv_start, NULL);

	xpp.flags = XDF_NEED_MINIMAL;
	xecfg.ctxlen = 0;
	xecfg.flags = 0;
	ecb.outf = xdiff_outf;
	ecb.priv = &state;
	memset(&state, 0, sizeof(state));
	state.xm.consume = process_u0_diff;
	state.ret = xmalloc(sizeof(struct patch));
	state.ret->chunks = NULL;
	state.ret->num = 0;

	xdl_diff(&file_c, &file_o, &xpp, &xecfg, &ecb);

	gettimeofday(&tv_end, NULL);
	patch_time += 1000000 * (tv_end.tv_sec - tv_start.tv_sec) +
		tv_end.tv_usec - tv_start.tv_usec;

	num_get_patch++;
	return state.ret;
}

static void free_patch(struct patch *p)
{
	free(p->chunks);
	free(p);
}

static int get_blob_sha1_internal(const unsigned char *sha1, const char *base,
				  int baselen, const char *pathname,
				  unsigned mode, int stage);

static unsigned char blob_sha1[20];
static const char *blame_file;
static int get_blob_sha1(struct tree *t, const char *pathname,
			 unsigned char *sha1)
{
	const char *pathspec[2];
	blame_file = pathname;
	pathspec[0] = pathname;
	pathspec[1] = NULL;
	hashclr(blob_sha1);
	read_tree_recursive(t, "", 0, 0, pathspec, get_blob_sha1_internal);

	if (is_null_sha1(blob_sha1))
		return -1;

	hashcpy(sha1, blob_sha1);
	return 0;
}

static int get_blob_sha1_internal(const unsigned char *sha1, const char *base,
				  int baselen, const char *pathname,
				  unsigned mode, int stage)
{
	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	if (strncmp(blame_file, base, baselen) ||
	    strcmp(blame_file + baselen, pathname))
		return -1;

	hashcpy(blob_sha1, sha1);
	return -1;
}

static void get_blob(struct commit *commit)
{
	struct util_info *info = commit->util;
	char type[20];

	if (info->buf)
		return;

	info->buf = read_sha1_file(info->sha1, type, &info->size);
	num_read_blob++;

	assert(!strcmp(type, blob_type));
}

/* For debugging only */
static void print_patch(struct patch *p)
{
	int i;
	printf("Num chunks: %d\n", p->num);
	for (i = 0; i < p->num; i++) {
		printf("%d,%d %d,%d\n", p->chunks[i].off1, p->chunks[i].len1,
		       p->chunks[i].off2, p->chunks[i].len2);
	}
}

#if DEBUG
/* For debugging only */
static void print_map(struct commit *cmit, struct commit *other)
{
	struct util_info *util = cmit->util;
	struct util_info *util2 = other->util;

	int i;
	int max =
	    util->num_lines >
	    util2->num_lines ? util->num_lines : util2->num_lines;
	int num;

	if (print_map == NULL)
		; /* to avoid "unused function" warning */

	for (i = 0; i < max; i++) {
		printf("i: %d ", i);
		num = -1;

		if (i < util->num_lines) {
			num = util->line_map[i];
			printf("%d\t", num);
		}
		else
			printf("\t");

		if (i < util2->num_lines) {
			int num2 = util2->line_map[i];
			printf("%d\t", num2);
			if (num != -1 && num2 != num)
				printf("---");
		}
		else
			printf("\t");

		printf("\n");
	}
}
#endif

/* p is a patch from commit to other. */
static void fill_line_map(struct commit *commit, struct commit *other,
			  struct patch *p)
{
	struct util_info *util = commit->util;
	struct util_info *util2 = other->util;
	int *map = util->line_map;
	int *map2 = util2->line_map;
	int cur_chunk = 0;
	int i1, i2;

	if (DEBUG) {
		if (p->num)
			print_patch(p);
		printf("num lines 1: %d num lines 2: %d\n", util->num_lines,
		       util2->num_lines);
	}

	for (i1 = 0, i2 = 0; i1 < util->num_lines; i1++, i2++) {
		struct chunk *chunk = NULL;
		if (cur_chunk < p->num)
			chunk = &p->chunks[cur_chunk];

		if (chunk && chunk->off1 == i1) {
			if (DEBUG && i2 != chunk->off2)
				printf("i2: %d off2: %d\n", i2, chunk->off2);

			assert(i2 == chunk->off2);

			i1--;
			i2--;
			if (chunk->len1 > 0)
				i1 += chunk->len1;

			if (chunk->len2 > 0)
				i2 += chunk->len2;

			cur_chunk++;
		}
		else {
			if (i2 >= util2->num_lines)
				break;

			if (map[i1] != map2[i2] && map[i1] != -1) {
				if (DEBUG)
					printf("map: i1: %d %d %p i2: %d %d %p\n",
					       i1, map[i1],
					       (void *) (i1 != -1 ? blame_lines[map[i1]] : NULL),
					       i2, map2[i2],
					       (void *) (i2 != -1 ? blame_lines[map2[i2]] : NULL));
				if (map2[i2] != -1 &&
				    blame_lines[map[i1]] &&
				    !blame_lines[map2[i2]])
					map[i1] = map2[i2];
			}

			if (map[i1] == -1 && map2[i2] != -1)
				map[i1] = map2[i2];
		}

		if (DEBUG > 1)
			printf("l1: %d l2: %d i1: %d i2: %d\n",
			       map[i1], map2[i2], i1, i2);
	}
}

static int map_line(struct commit *commit, int line)
{
	struct util_info *info = commit->util;
	assert(line >= 0 && line < info->num_lines);
	return info->line_map[line];
}

static struct util_info *get_util(struct commit *commit)
{
	struct util_info *util = commit->util;

	if (util)
		return util;

	util = xcalloc(1, sizeof(struct util_info));
	util->num_lines = -1;
	commit->util = util;
	return util;
}

static int fill_util_info(struct commit *commit)
{
	struct util_info *util = commit->util;

	assert(util);
	assert(util->pathname);

	return !!get_blob_sha1(commit->tree, util->pathname, util->sha1);
}

static void alloc_line_map(struct commit *commit)
{
	struct util_info *util = commit->util;
	int i;

	if (util->line_map)
		return;

	get_blob(commit);

	util->num_lines = 0;
	for (i = 0; i < util->size; i++) {
		if (util->buf[i] == '\n')
			util->num_lines++;
	}
	if (util->buf[util->size - 1] != '\n')
		util->num_lines++;

	util->line_map = xmalloc(sizeof(int) * util->num_lines);

	for (i = 0; i < util->num_lines; i++)
		util->line_map[i] = -1;
}

static void init_first_commit(struct commit *commit, const char *filename)
{
	struct util_info *util = commit->util;
	int i;

	util->pathname = filename;
	if (fill_util_info(commit))
		die("fill_util_info failed");

	alloc_line_map(commit);

	util = commit->util;

	for (i = 0; i < util->num_lines; i++)
		util->line_map[i] = i;
}

static void process_commits(struct rev_info *rev, const char *path,
			    struct commit **initial)
{
	int i;
	struct util_info *util;
	int lines_left;
	int *blame_p;
	int *new_lines;
	int new_lines_len;

	struct commit *commit = get_revision(rev);
	assert(commit);
	init_first_commit(commit, path);

	util = commit->util;
	num_blame_lines = util->num_lines;
	blame_lines = xmalloc(sizeof(struct commit *) * num_blame_lines);
	blame_contents = util->buf;
	blame_len = util->size;

	for (i = 0; i < num_blame_lines; i++)
		blame_lines[i] = NULL;

	lines_left = num_blame_lines;
	blame_p = xmalloc(sizeof(int) * num_blame_lines);
	new_lines = xmalloc(sizeof(int) * num_blame_lines);
	do {
		struct commit_list *parents;
		int num_parents;
		struct util_info *util;

		if (DEBUG)
			printf("\nProcessing commit: %d %s\n", num_commits,
			       sha1_to_hex(commit->object.sha1));

		if (lines_left == 0)
			return;

		num_commits++;
		memset(blame_p, 0, sizeof(int) * num_blame_lines);
		new_lines_len = 0;
		num_parents = 0;
		for (parents = commit->parents;
		     parents != NULL; parents = parents->next)
			num_parents++;

		if (num_parents == 0)
			*initial = commit;

		if (fill_util_info(commit))
			continue;

		alloc_line_map(commit);
		util = commit->util;

		for (parents = commit->parents;
		     parents != NULL; parents = parents->next) {
			struct commit *parent = parents->item;
			struct patch *patch;

			if (parse_commit(parent) < 0)
				die("parse_commit error");

			if (DEBUG)
				printf("parent: %s\n",
				       sha1_to_hex(parent->object.sha1));

			if (fill_util_info(parent)) {
				num_parents--;
				continue;
			}

			patch = get_patch(parent, commit);
                        alloc_line_map(parent);
                        fill_line_map(parent, commit, patch);

                        for (i = 0; i < patch->num; i++) {
                            int l;
                            for (l = 0; l < patch->chunks[i].len2; l++) {
                                int mapped_line =
                                    map_line(commit, patch->chunks[i].off2 + l);
                                if (mapped_line != -1) {
                                    blame_p[mapped_line]++;
                                    if (blame_p[mapped_line] == num_parents)
                                        new_lines[new_lines_len++] = mapped_line;
                                }
                            }
			}
                        free_patch(patch);
		}

		if (DEBUG)
			printf("parents: %d\n", num_parents);

		for (i = 0; i < new_lines_len; i++) {
			int mapped_line = new_lines[i];
			if (blame_lines[mapped_line] == NULL) {
				blame_lines[mapped_line] = commit;
				lines_left--;
				if (DEBUG)
					printf("blame: mapped: %d i: %d\n",
					       mapped_line, i);
			}
		}
	} while ((commit = get_revision(rev)) != NULL);
}

static int compare_tree_path(struct rev_info *revs,
			     struct commit *c1, struct commit *c2)
{
	int ret;
	const char *paths[2];
	struct util_info *util = c2->util;
	paths[0] = util->pathname;
	paths[1] = NULL;

	diff_tree_setup_paths(get_pathspec(revs->prefix, paths),
			      &revs->pruning);
	ret = rev_compare_tree(revs, c1->tree, c2->tree);
	diff_tree_release_paths(&revs->pruning);
	return ret;
}

static int same_tree_as_empty_path(struct rev_info *revs, struct tree *t1,
				   const char *path)
{
	int ret;
	const char *paths[2];
	paths[0] = path;
	paths[1] = NULL;

	diff_tree_setup_paths(get_pathspec(revs->prefix, paths),
			      &revs->pruning);
	ret = rev_same_tree_as_empty(revs, t1);
	diff_tree_release_paths(&revs->pruning);
	return ret;
}

static const char *find_rename(struct commit *commit, struct commit *parent)
{
	struct util_info *cutil = commit->util;
	struct diff_options diff_opts;
	const char *paths[1];
	int i;

	if (DEBUG) {
		printf("find_rename commit: %s ",
		       sha1_to_hex(commit->object.sha1));
		puts(sha1_to_hex(parent->object.sha1));
	}

	diff_setup(&diff_opts);
	diff_opts.recursive = 1;
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
	if (diff_setup_done(&diff_opts) < 0)
		die("diff_setup_done failed");

	diff_tree_sha1(commit->tree->object.sha1, parent->tree->object.sha1,
		       "", &diff_opts);
	diffcore_std(&diff_opts);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];

		if (p->status == 'R' &&
		    !strcmp(p->one->path, cutil->pathname)) {
			if (DEBUG)
				printf("rename %s -> %s\n",
				       p->one->path, p->two->path);
			return p->two->path;
		}
	}

	return 0;
}

static void simplify_commit(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp, *parent;

	if (!commit->tree)
		return;

	if (!commit->parents) {
		struct util_info *util = commit->util;
		if (!same_tree_as_empty_path(revs, commit->tree,
					     util->pathname))
			commit->object.flags |= TREECHANGE;
		return;
	}

	pp = &commit->parents;
	while ((parent = *pp) != NULL) {
		struct commit *p = parent->item;

		if (p->object.flags & UNINTERESTING) {
			pp = &parent->next;
			continue;
		}

		parse_commit(p);
		switch (compare_tree_path(revs, p, commit)) {
		case REV_TREE_SAME:
			parent->next = NULL;
			commit->parents = parent;
			get_util(p)->pathname = get_util(commit)->pathname;
			return;

		case REV_TREE_NEW:
		{
			struct util_info *util = commit->util;
			if (revs->remove_empty_trees &&
			    same_tree_as_empty_path(revs, p->tree,
						    util->pathname)) {
				const char *new_name = find_rename(commit, p);
				if (new_name) {
					struct util_info *putil = get_util(p);
					if (!putil->pathname)
						putil->pathname = xstrdup(new_name);
				}
				else {
					*pp = parent->next;
					continue;
				}
			}
		}

		/* fallthrough */
		case REV_TREE_DIFFERENT:
			pp = &parent->next;
			if (!get_util(p)->pathname)
				get_util(p)->pathname =
					get_util(commit)->pathname;
			continue;
		}
		die("bad tree compare for commit %s",
		    sha1_to_hex(commit->object.sha1));
	}
	commit->object.flags |= TREECHANGE;
}

struct commit_info
{
	char *author;
	char *author_mail;
	unsigned long author_time;
	char *author_tz;

	/* filled only when asked for details */
	char *committer;
	char *committer_mail;
	unsigned long committer_time;
	char *committer_tz;

	char *summary;
};

static void get_ac_line(const char *inbuf, const char *what,
			int bufsz, char *person, char **mail,
			unsigned long *time, char **tz)
{
	int len;
	char *tmp, *endp;

	tmp = strstr(inbuf, what);
	if (!tmp)
		goto error_out;
	tmp += strlen(what);
	endp = strchr(tmp, '\n');
	if (!endp)
		len = strlen(tmp);
	else
		len = endp - tmp;
	if (bufsz <= len) {
	error_out:
		/* Ugh */
		person = *mail = *tz = "(unknown)";
		*time = 0;
		return;
	}
	memcpy(person, tmp, len);

	tmp = person;
	tmp += len;
	*tmp = 0;
	while (*tmp != ' ')
		tmp--;
	*tz = tmp+1;

	*tmp = 0;
	while (*tmp != ' ')
		tmp--;
	*time = strtoul(tmp, NULL, 10);

	*tmp = 0;
	while (*tmp != ' ')
		tmp--;
	*mail = tmp + 1;
	*tmp = 0;
}

static void get_commit_info(struct commit *commit, struct commit_info *ret, int detailed)
{
	int len;
	char *tmp, *endp;
	static char author_buf[1024];
	static char committer_buf[1024];
	static char summary_buf[1024];

	ret->author = author_buf;
	get_ac_line(commit->buffer, "\nauthor ",
		    sizeof(author_buf), author_buf, &ret->author_mail,
		    &ret->author_time, &ret->author_tz);

	if (!detailed)
		return;

	ret->committer = committer_buf;
	get_ac_line(commit->buffer, "\ncommitter ",
		    sizeof(committer_buf), committer_buf, &ret->committer_mail,
		    &ret->committer_time, &ret->committer_tz);

	ret->summary = summary_buf;
	tmp = strstr(commit->buffer, "\n\n");
	if (!tmp) {
	error_out:
		sprintf(summary_buf, "(%s)", sha1_to_hex(commit->object.sha1));
		return;
	}
	tmp += 2;
	endp = strchr(tmp, '\n');
	if (!endp)
		goto error_out;
	len = endp - tmp;
	if (len >= sizeof(summary_buf))
		goto error_out;
	memcpy(summary_buf, tmp, len);
	summary_buf[len] = 0;
}

static const char *format_time(unsigned long time, const char *tz_str,
			       int show_raw_time)
{
	static char time_buf[128];
	time_t t = time;
	int minutes, tz;
	struct tm *tm;

	if (show_raw_time) {
		sprintf(time_buf, "%lu %s", time, tz_str);
		return time_buf;
	}

	tz = atoi(tz_str);
	minutes = tz < 0 ? -tz : tz;
	minutes = (minutes / 100)*60 + (minutes % 100);
	minutes = tz < 0 ? -minutes : minutes;
	t = time + minutes * 60;
	tm = gmtime(&t);

	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S ", tm);
	strcat(time_buf, tz_str);
	return time_buf;
}

static void topo_setter(struct commit *c, void *data)
{
	struct util_info *util = c->util;
	util->topo_data = data;
}

static void *topo_getter(struct commit *c)
{
	struct util_info *util = c->util;
	return util->topo_data;
}

static int read_ancestry(const char *graft_file,
			 unsigned char **start_sha1)
{
	FILE *fp = fopen(graft_file, "r");
	char buf[1024];
	if (!fp)
		return -1;
	while (fgets(buf, sizeof(buf), fp)) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		int len = strlen(buf);
		struct commit_graft *graft = read_graft_line(buf, len);
		register_commit_graft(graft, 0);
		if (!*start_sha1)
			*start_sha1 = graft->sha1;
	}
	fclose(fp);
	return 0;
}

static int lineno_width(int lines)
{
	int i, width;

	for (width = 1, i = 10; i <= lines + 1; width++)
		i *= 10;
	return width;
}

static int find_orig_linenum(struct util_info *u, int lineno)
{
	int i;

	for (i = 0; i < u->num_lines; i++)
		if (lineno == u->line_map[i])
			return i + 1;
	return 0;
}

static void emit_meta(struct commit *c, int lno,
		      int sha1_len, int compatibility, int porcelain,
		      int show_name, int show_number, int show_raw_time,
		      int longest_file, int longest_author,
		      int max_digits, int max_orig_digits)
{
	struct util_info *u;
	int lineno;
	struct commit_info ci;

	u = c->util;
	lineno = find_orig_linenum(u, lno);

	if (porcelain) {
		int group_size = -1;
		struct commit *cc = (lno == 0) ? NULL : blame_lines[lno-1];
		if (cc != c) {
			/* This is the beginning of this group */
			int i;
			for (i = lno + 1; i < num_blame_lines; i++)
				if (blame_lines[i] != c)
					break;
			group_size = i - lno;
		}
		if (0 < group_size)
			printf("%s %d %d %d\n", sha1_to_hex(c->object.sha1),
			       lineno, lno + 1, group_size);
		else
			printf("%s %d %d\n", sha1_to_hex(c->object.sha1),
			       lineno, lno + 1);
		if (!u->meta_given) {
			get_commit_info(c, &ci, 1);
			printf("author %s\n", ci.author);
			printf("author-mail %s\n", ci.author_mail);
			printf("author-time %lu\n", ci.author_time);
			printf("author-tz %s\n", ci.author_tz);
			printf("committer %s\n", ci.committer);
			printf("committer-mail %s\n", ci.committer_mail);
			printf("committer-time %lu\n", ci.committer_time);
			printf("committer-tz %s\n", ci.committer_tz);
			printf("filename ");
			if (quote_c_style(u->pathname, NULL, NULL, 0))
				quote_c_style(u->pathname, NULL, stdout, 0);
			else
				fputs(u->pathname, stdout);
			printf("\nsummary %s\n", ci.summary);

			u->meta_given = 1;
		}
		putchar('\t');
		return;
	}

	get_commit_info(c, &ci, 0);
	fwrite(sha1_to_hex(c->object.sha1), sha1_len, 1, stdout);
	if (compatibility) {
		printf("\t(%10s\t%10s\t%d)", ci.author,
		       format_time(ci.author_time, ci.author_tz,
				   show_raw_time),
		       lno + 1);
	}
	else {
		if (show_name)
			printf(" %-*.*s", longest_file, longest_file,
			       u->pathname);
		if (show_number)
			printf(" %*d", max_orig_digits,
			       lineno);
		printf(" (%-*.*s %10s %*d) ",
		       longest_author, longest_author, ci.author,
		       format_time(ci.author_time, ci.author_tz,
				   show_raw_time),
		       max_digits, lno + 1);
	}
}

int main(int argc, const char **argv)
{
	int i;
	struct commit *initial = NULL;
	unsigned char sha1[20], *sha1_p = NULL;

	const char *filename = NULL, *commit = NULL;
	char filename_buf[256];
	int sha1_len = 8;
	int compatibility = 0;
	int show_raw_time = 0;
	int options = 1;
	struct commit *start_commit;

	const char *args[10];
	struct rev_info rev;

	struct commit_info ci;
	const char *buf;
	int max_digits, max_orig_digits;
	int longest_file, longest_author, longest_file_lines;
	int show_name = 0;
	int show_number = 0;
	int porcelain = 0;

	const char *prefix = setup_git_directory();
	git_config(git_default_config);

	for (i = 1; i < argc; i++) {
		if (options) {
			if (!strcmp(argv[i], "-h") ||
			   !strcmp(argv[i], "--help"))
				usage(blame_usage);
			if (!strcmp(argv[i], "-l") ||
			    !strcmp(argv[i], "--long")) {
				sha1_len = 40;
				continue;
			}
			if (!strcmp(argv[i], "-c") ||
			    !strcmp(argv[i], "--compatibility")) {
				compatibility = 1;
				continue;
			}
			if (!strcmp(argv[i], "-t") ||
			    !strcmp(argv[i], "--time")) {
				show_raw_time = 1;
				continue;
			}
			if (!strcmp(argv[i], "-S")) {
				if (i + 1 < argc &&
				    !read_ancestry(argv[i + 1], &sha1_p)) {
					compatibility = 1;
					i++;
					continue;
				}
				usage(blame_usage);
			}
			if (!strcmp(argv[i], "-f") ||
			    !strcmp(argv[i], "--show-name")) {
				show_name = 1;
				continue;
			}
			if (!strcmp(argv[i], "-n") ||
			    !strcmp(argv[i], "--show-number")) {
				show_number = 1;
				continue;
			}
			if (!strcmp(argv[i], "-p") ||
			    !strcmp(argv[i], "--porcelain")) {
				porcelain = 1;
				sha1_len = 40;
				show_raw_time = 1;
				continue;
			}
			if (!strcmp(argv[i], "--")) {
				options = 0;
				continue;
			}
			if (argv[i][0] == '-')
				usage(blame_usage);
			options = 0;
		}

		if (!options) {
			if (!filename)
				filename = argv[i];
			else if (!commit)
				commit = argv[i];
			else
				usage(blame_usage);
		}
	}

	if (!filename)
		usage(blame_usage);
	if (commit && sha1_p)
		usage(blame_usage);
	else if (!commit)
		commit = "HEAD";

	if (prefix)
		sprintf(filename_buf, "%s%s", prefix, filename);
	else
		strcpy(filename_buf, filename);
	filename = filename_buf;

	if (!sha1_p) {
		if (get_sha1(commit, sha1))
			die("get_sha1 failed, commit '%s' not found", commit);
		sha1_p = sha1;
	}
	start_commit = lookup_commit_reference(sha1_p);
	get_util(start_commit)->pathname = filename;
	if (fill_util_info(start_commit)) {
		printf("%s not found in %s\n", filename, commit);
		return 1;
	}

	init_revisions(&rev, setup_git_directory());
	rev.remove_empty_trees = 1;
	rev.topo_order = 1;
	rev.prune_fn = simplify_commit;
	rev.topo_setter = topo_setter;
	rev.topo_getter = topo_getter;
	rev.parents = 1;
	rev.limited = 1;

	commit_list_insert(start_commit, &rev.commits);

	args[0] = filename;
	args[1] = NULL;
	diff_tree_setup_paths(args, &rev.pruning);
	prepare_revision_walk(&rev);
	process_commits(&rev, filename, &initial);

	for (i = 0; i < num_blame_lines; i++)
		if (!blame_lines[i])
			blame_lines[i] = initial;

	buf = blame_contents;
	max_digits = lineno_width(num_blame_lines);

	longest_file = 0;
	longest_author = 0;
	longest_file_lines = 0;
	for (i = 0; i < num_blame_lines; i++) {
		struct commit *c = blame_lines[i];
		struct util_info *u;
		u = c->util;

		if (!show_name && strcmp(filename, u->pathname))
			show_name = 1;
		if (longest_file < strlen(u->pathname))
			longest_file = strlen(u->pathname);
		if (longest_file_lines < u->num_lines)
			longest_file_lines = u->num_lines;
		get_commit_info(c, &ci, 0);
		if (longest_author < strlen(ci.author))
			longest_author = strlen(ci.author);
	}

	max_orig_digits = lineno_width(longest_file_lines);

	for (i = 0; i < num_blame_lines; i++) {
		emit_meta(blame_lines[i], i,
			  sha1_len, compatibility, porcelain,
			  show_name, show_number, show_raw_time,
			  longest_file, longest_author,
			  max_digits, max_orig_digits);

		if (i == num_blame_lines - 1) {
			fwrite(buf, blame_len - (buf - blame_contents),
			       1, stdout);
			if (blame_contents[blame_len-1] != '\n')
				putc('\n', stdout);
		}
		else {
			char *next_buf = strchr(buf, '\n') + 1;
			fwrite(buf, next_buf - buf, 1, stdout);
			buf = next_buf;
		}
	}

	if (DEBUG) {
		printf("num read blob: %d\n", num_read_blob);
		printf("num get patch: %d\n", num_get_patch);
		printf("num commits: %d\n", num_commits);
		printf("patch time: %f\n", patch_time / 1000000.0);
		printf("initial: %s\n", sha1_to_hex(initial->object.sha1));
	}

	return 0;
}
