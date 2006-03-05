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
#include "revision.h"

#define DEBUG 0

static const char blame_usage[] = "[-c] [-l] [--] file [commit]\n"
	"  -c, --compability Use the same output mode as git-annotate (Default: off)\n"
	"  -l, --long        Show long commit SHA1 (Default: off)\n"
	"  -h, --help        This message";

static struct commit **blame_lines;
static int num_blame_lines;
static char* blame_contents;
static int blame_len;

struct util_info {
	int *line_map;
	unsigned char sha1[20];	/* blob sha, not commit! */
	char *buf;
	unsigned long size;
	int num_lines;
//    const char* path;
};

struct chunk {
	int off1, len1;	// ---
	int off2, len2;	// +++
};

struct patch {
	struct chunk *chunks;
	int num;
};

static void get_blob(struct commit *commit);

/* Only used for statistics */
static int num_get_patch = 0;
static int num_commits = 0;
static int patch_time = 0;

#define TEMPFILE_PATH_LEN 60
static struct patch *get_patch(struct commit *commit, struct commit *other)
{
	struct patch *ret;
	struct util_info *info_c = (struct util_info *)commit->object.util;
	struct util_info *info_o = (struct util_info *)other->object.util;
	char tmp_path1[TEMPFILE_PATH_LEN], tmp_path2[TEMPFILE_PATH_LEN];
	char diff_cmd[TEMPFILE_PATH_LEN*2 + 20];
	struct timeval tv_start, tv_end;
	int fd;
	FILE *fin;
	char buf[1024];

	ret = xmalloc(sizeof(struct patch));
	ret->chunks = NULL;
	ret->num = 0;

	get_blob(commit);
	get_blob(other);

	gettimeofday(&tv_start, NULL);

	fd = git_mkstemp(tmp_path1, TEMPFILE_PATH_LEN, "git-blame-XXXXXX");
	if (fd < 0)
		die("unable to create temp-file: %s", strerror(errno));

	if (xwrite(fd, info_c->buf, info_c->size) != info_c->size)
		die("write failed: %s", strerror(errno));
	close(fd);

	fd = git_mkstemp(tmp_path2, TEMPFILE_PATH_LEN, "git-blame-XXXXXX");
	if (fd < 0)
		die("unable to create temp-file: %s", strerror(errno));

	if (xwrite(fd, info_o->buf, info_o->size) != info_o->size)
		die("write failed: %s", strerror(errno));
	close(fd);

	sprintf(diff_cmd, "diff -u0 %s %s", tmp_path1, tmp_path2);
	fin = popen(diff_cmd, "r");
	if (!fin)
		die("popen failed: %s", strerror(errno));

	while (fgets(buf, sizeof(buf), fin)) {
		struct chunk *chunk;
		char *start, *sp;

		if (buf[0] != '@' || buf[1] != '@')
			continue;

		if (DEBUG)
			printf("chunk line: %s", buf);
		ret->num++;
		ret->chunks = xrealloc(ret->chunks,
				       sizeof(struct chunk) * ret->num);
		chunk = &ret->chunks[ret->num - 1];

		assert(!strncmp(buf, "@@ -", 4));

		start = buf + 4;
		sp = index(start, ' ');
		*sp = '\0';
		if (index(start, ',')) {
			int ret =
			    sscanf(start, "%d,%d", &chunk->off1, &chunk->len1);
			assert(ret == 2);
		} else {
			int ret = sscanf(start, "%d", &chunk->off1);
			assert(ret == 1);
			chunk->len1 = 1;
		}
		*sp = ' ';

		start = sp + 1;
		sp = index(start, ' ');
		*sp = '\0';
		if (index(start, ',')) {
			int ret =
			    sscanf(start, "%d,%d", &chunk->off2, &chunk->len2);
			assert(ret == 2);
		} else {
			int ret = sscanf(start, "%d", &chunk->off2);
			assert(ret == 1);
			chunk->len2 = 1;
		}
		*sp = ' ';

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
	pclose(fin);
	unlink(tmp_path1);
	unlink(tmp_path2);

	gettimeofday(&tv_end, NULL);
	patch_time += 1000000 * (tv_end.tv_sec - tv_start.tv_sec) +
		tv_end.tv_usec - tv_start.tv_usec;

	num_get_patch++;
	return ret;
}

static void free_patch(struct patch *p)
{
	free(p->chunks);
	free(p);
}

static int get_blob_sha1_internal(unsigned char *sha1, const char *base,
				  int baselen, const char *pathname,
				  unsigned mode, int stage);

static unsigned char blob_sha1[20];
static int get_blob_sha1(struct tree *t, const char *pathname,
			 unsigned char *sha1)
{
	int i;
	const char *pathspec[2];
	pathspec[0] = pathname;
	pathspec[1] = NULL;
	memset(blob_sha1, 0, sizeof(blob_sha1));
	read_tree_recursive(t, "", 0, 0, pathspec, get_blob_sha1_internal);

	for (i = 0; i < 20; i++) {
		if (blob_sha1[i] != 0)
			break;
	}

	if (i == 20)
		return -1;

	memcpy(sha1, blob_sha1, 20);
	return 0;
}

static int get_blob_sha1_internal(unsigned char *sha1, const char *base,
				  int baselen, const char *pathname,
				  unsigned mode, int stage)
{
	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	memcpy(blob_sha1, sha1, 20);
	return -1;
}

static void get_blob(struct commit *commit)
{
	struct util_info *info = commit->object.util;
	char type[20];

	if (info->buf)
		return;

	info->buf = read_sha1_file(info->sha1, type, &info->size);

	assert(!strcmp(type, "blob"));
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

#if 0
/* For debugging only */
static void print_map(struct commit *cmit, struct commit *other)
{
	struct util_info *util = cmit->object.util;
	struct util_info *util2 = other->object.util;

	int i;
	int max =
	    util->num_lines >
	    util2->num_lines ? util->num_lines : util2->num_lines;
	int num;

	for (i = 0; i < max; i++) {
		printf("i: %d ", i);
		num = -1;

		if (i < util->num_lines) {
			num = util->line_map[i];
			printf("%d\t", num);
		} else
			printf("\t");

		if (i < util2->num_lines) {
			int num2 = util2->line_map[i];
			printf("%d\t", num2);
			if (num != -1 && num2 != num)
				printf("---");
		} else
			printf("\t");

		printf("\n");
	}
}
#endif

// p is a patch from commit to other.
static void fill_line_map(struct commit *commit, struct commit *other,
			  struct patch *p)
{
	struct util_info *util = commit->object.util;
	struct util_info *util2 = other->object.util;
	int *map = util->line_map;
	int *map2 = util2->line_map;
	int cur_chunk = 0;
	int i1, i2;

	if (p->num && DEBUG)
		print_patch(p);

	if (DEBUG)
		printf("num lines 1: %d num lines 2: %d\n", util->num_lines,
		       util2->num_lines);

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
		} else {
			if (i2 >= util2->num_lines)
				break;

			if (map[i1] != map2[i2] && map[i1] != -1) {
				if (DEBUG)
					printf("map: i1: %d %d %p i2: %d %d %p\n",
					       i1, map[i1],
					       i1 != -1 ? blame_lines[map[i1]] : NULL,
					       i2, map2[i2],
					       i2 != -1 ? blame_lines[map2[i2]] : NULL);
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
	struct util_info *info = commit->object.util;
	assert(line >= 0 && line < info->num_lines);
	return info->line_map[line];
}

static int fill_util_info(struct commit *commit, const char *path)
{
	struct util_info *util;
	if (commit->object.util)
		return 0;

	util = xmalloc(sizeof(struct util_info));

	if (get_blob_sha1(commit->tree, path, util->sha1)) {
		free(util);
		return 1;
	} else {
		util->buf = NULL;
		util->size = 0;
		util->line_map = NULL;
		util->num_lines = -1;
		commit->object.util = util;
		return 0;
	}
}

static void alloc_line_map(struct commit *commit)
{
	struct util_info *util = commit->object.util;
	int i;

	if (util->line_map)
		return;

	get_blob(commit);

	util->num_lines = 0;
	for (i = 0; i < util->size; i++) {
		if (util->buf[i] == '\n')
			util->num_lines++;
	}
	if(util->buf[util->size - 1] != '\n')
		util->num_lines++;

	util->line_map = xmalloc(sizeof(int) * util->num_lines);

	for (i = 0; i < util->num_lines; i++)
		util->line_map[i] = -1;
}

static void init_first_commit(struct commit* commit, const char* filename)
{
	struct util_info* util;
	int i;

	if (fill_util_info(commit, filename))
		die("fill_util_info failed");

	alloc_line_map(commit);

	util = commit->object.util;

	for (i = 0; i < util->num_lines; i++)
		util->line_map[i] = i;
}


static void process_commits(struct rev_info *rev, const char *path,
			    struct commit** initial)
{
	int i;
	struct util_info* util;
	int lines_left;
	int *blame_p;
	int *new_lines;
	int new_lines_len;

	struct commit* commit = get_revision(rev);
	assert(commit);
	init_first_commit(commit, path);

	util = commit->object.util;
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

		if(num_parents == 0)
			*initial = commit;

		if(fill_util_info(commit, path))
			continue;

		alloc_line_map(commit);
		util = commit->object.util;

		for (parents = commit->parents;
		     parents != NULL; parents = parents->next) {
			struct commit *parent = parents->item;
			struct patch *patch;

			if (parse_commit(parent) < 0)
				die("parse_commit error");

			if (DEBUG)
				printf("parent: %s\n",
				       sha1_to_hex(parent->object.sha1));

			if(fill_util_info(parent, path)) {
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

struct commit_info
{
	char* author;
	char* author_mail;
	unsigned long author_time;
	char* author_tz;
};

static void get_commit_info(struct commit* commit, struct commit_info* ret)
{
	int len;
	char* tmp;
	static char author_buf[1024];

	tmp = strstr(commit->buffer, "\nauthor ") + 8;
	len = index(tmp, '\n') - tmp;
	ret->author = author_buf;
	memcpy(ret->author, tmp, len);

	tmp = ret->author;
	tmp += len;
	*tmp = 0;
	while(*tmp != ' ')
		tmp--;
	ret->author_tz = tmp+1;

	*tmp = 0;
	while(*tmp != ' ')
		tmp--;
	ret->author_time = strtoul(tmp, NULL, 10);

	*tmp = 0;
	while(*tmp != ' ')
		tmp--;
	ret->author_mail = tmp + 1;

	*tmp = 0;
}

char* format_time(unsigned long time, const char* tz)
{
	static char time_buf[128];
	time_t t = time;

	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S ", gmtime(&t));
	strcat(time_buf, tz);
	return time_buf;
}

int main(int argc, const char **argv)
{
	int i;
	struct commit *initial = NULL;
	unsigned char sha1[20];

	const char *filename = NULL, *commit = NULL;
	char filename_buf[256];
	int sha1_len = 8;
	int compability = 0;
	int options = 1;

	int num_args;
	const char* args[10];
	struct rev_info rev;

	struct commit_info ci;
	const char *buf;
	int max_digits;

	const char* prefix = setup_git_directory();

	for(i = 1; i < argc; i++) {
		if(options) {
			if(!strcmp(argv[i], "-h") ||
			   !strcmp(argv[i], "--help"))
				usage(blame_usage);
			else if(!strcmp(argv[i], "-l") ||
				!strcmp(argv[i], "--long")) {
				sha1_len = 20;
				continue;
			} else if(!strcmp(argv[i], "-c") ||
				  !strcmp(argv[i], "--compability")) {
				compability = 1;
				continue;
			} else if(!strcmp(argv[i], "--")) {
				options = 0;
				continue;
			} else if(argv[i][0] == '-')
				usage(blame_usage);
			else
				options = 0;
		}

		if(!options) {
			if(!filename)
				filename = argv[i];
			else if(!commit)
				commit = argv[i];
			else
				usage(blame_usage);
		}
	}

	if(!filename)
		usage(blame_usage);
	if(!commit)
		commit = "HEAD";

	if(prefix)
		sprintf(filename_buf, "%s%s", prefix, filename);
	else
		strcpy(filename_buf, filename);
	filename = filename_buf;

	{
		struct commit* c;
		if (get_sha1(commit, sha1))
			die("get_sha1 failed, commit '%s' not found", commit);
		c = lookup_commit_reference(sha1);

		if (fill_util_info(c, filename)) {
			printf("%s not found in %s\n", filename, commit);
			return 1;
		}
	}

	num_args = 0;
	args[num_args++] = NULL;
	args[num_args++] = "--topo-order";
	args[num_args++] = "--remove-empty";
	args[num_args++] = commit;
	args[num_args++] = "--";
	args[num_args++] = filename;
	args[num_args] = NULL;

	setup_revisions(num_args, args, &rev, "HEAD");
	prepare_revision_walk(&rev);
	process_commits(&rev, filename, &initial);

	buf = blame_contents;
	max_digits = 1 + log(num_blame_lines+1)/log(10);
	for (i = 0; i < num_blame_lines; i++) {
		struct commit *c = blame_lines[i];
		if (!c)
			c = initial;

		get_commit_info(c, &ci);
		fwrite(sha1_to_hex(c->object.sha1), sha1_len, 1, stdout);
		if(compability)
			printf("\t(%10s\t%10s\t%d)", ci.author,
			       format_time(ci.author_time, ci.author_tz), i+1);
		else
			printf(" (%-15.15s %10s %*d) ", ci.author,
			       format_time(ci.author_time, ci.author_tz),
			       max_digits, i+1);

		if(i == num_blame_lines - 1) {
			fwrite(buf, blame_len - (buf - blame_contents),
			       1, stdout);
			if(blame_contents[blame_len-1] != '\n')
				putc('\n', stdout);
		} else {
			char* next_buf = index(buf, '\n') + 1;
			fwrite(buf, next_buf - buf, 1, stdout);
			buf = next_buf;
		}
	}

	if (DEBUG) {
		printf("num get patch: %d\n", num_get_patch);
		printf("num commits: %d\n", num_commits);
		printf("patch time: %f\n", patch_time / 1000000.0);
		printf("initial: %s\n", sha1_to_hex(initial->object.sha1));
	}

	return 0;
}
