#include "cache.h"
#include "path-list.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"

#include <time.h>

static const char git_rerere_usage[] =
"git-rerere [clear | status | diff | gc]";

/* these values are days */
static int cutoff_noresolve = 15;
static int cutoff_resolve = 60;

static char *merge_rr_path;

static const char *rr_path(const char *name, const char *file)
{
	return git_path("rr-cache/%s/%s", name, file);
}

static void read_rr(struct path_list *rr)
{
	unsigned char sha1[20];
	char buf[PATH_MAX];
	FILE *in = fopen(merge_rr_path, "r");
	if (!in)
		return;
	while (fread(buf, 40, 1, in) == 1) {
		int i;
		char *name;
		if (get_sha1_hex(buf, sha1))
			die("corrupt MERGE_RR");
		buf[40] = '\0';
		name = xstrdup(buf);
		if (fgetc(in) != '\t')
			die("corrupt MERGE_RR");
		for (i = 0; i < sizeof(buf) && (buf[i] = fgetc(in)); i++)
			; /* do nothing */
		if (i == sizeof(buf))
			die("filename too long");
		path_list_insert(buf, rr)->util = xstrdup(name);
	}
	fclose(in);
}

static struct lock_file write_lock;

static int write_rr(struct path_list *rr, int out_fd)
{
	int i;
	for (i = 0; i < rr->nr; i++) {
		const char *path = rr->items[i].path;
		write(out_fd, rr->items[i].util, 40);
		write(out_fd, "\t", 1);
		write(out_fd, path, strlen(path) + 1);
	}
	close(out_fd);
	return commit_lock_file(&write_lock);
}

struct buffer {
	char *ptr;
	int nr, alloc;
};

static void append_line(struct buffer *buffer, const char *line)
{
	int len = strlen(line);

	if (buffer->nr + len > buffer->alloc) {
		buffer->alloc = alloc_nr(buffer->nr + len);
		buffer->ptr = xrealloc(buffer->ptr, buffer->alloc);
	}
	memcpy(buffer->ptr + buffer->nr, line, len);
	buffer->nr += len;
}

static int handle_file(const char *path,
	 unsigned char *sha1, const char *output)
{
	SHA_CTX ctx;
	char buf[1024];
	int hunk = 0, hunk_no = 0;
	struct buffer minus = { NULL, 0, 0 }, plus = { NULL, 0, 0 };
	struct buffer *one = &minus, *two = &plus;
	FILE *f = fopen(path, "r");
	FILE *out;

	if (!f)
		return error("Could not open %s", path);

	if (output) {
		out = fopen(output, "w");
		if (!out) {
			fclose(f);
			return error("Could not write %s", output);
		}
	} else
		out = NULL;

	if (sha1)
		SHA1_Init(&ctx);

	while (fgets(buf, sizeof(buf), f)) {
		if (!strncmp("<<<<<<< ", buf, 8))
			hunk = 1;
		else if (!strncmp("=======", buf, 7))
			hunk = 2;
		else if (!strncmp(">>>>>>> ", buf, 8)) {
			hunk_no++;
			hunk = 0;
			if (memcmp(one->ptr, two->ptr, one->nr < two->nr ?
						one->nr : two->nr) > 0) {
				struct buffer *swap = one;
				one = two;
				two = swap;
			}
			if (out) {
				fputs("<<<<<<<\n", out);
				fwrite(one->ptr, one->nr, 1, out);
				fputs("=======\n", out);
				fwrite(two->ptr, two->nr, 1, out);
				fputs(">>>>>>>\n", out);
			}
			if (sha1) {
				SHA1_Update(&ctx, one->ptr, one->nr);
				SHA1_Update(&ctx, "\0", 1);
				SHA1_Update(&ctx, two->ptr, two->nr);
				SHA1_Update(&ctx, "\0", 1);
			}
		} else if (hunk == 1)
			append_line(one, buf);
		else if (hunk == 2)
			append_line(two, buf);
		else if (out)
			fputs(buf, out);
	}

	fclose(f);
	if (out)
		fclose(out);
	if (sha1)
		SHA1_Final(sha1, &ctx);
	return hunk_no;
}

static int find_conflict(struct path_list *conflict)
{
	int i;
	if (read_cache() < 0)
		return error("Could not read index");
	for (i = 0; i + 2 < active_nr; i++) {
		struct cache_entry *e1 = active_cache[i];
		struct cache_entry *e2 = active_cache[i + 1];
		struct cache_entry *e3 = active_cache[i + 2];
		if (ce_stage(e1) == 1 && ce_stage(e2) == 2 &&
				ce_stage(e3) == 3 && ce_same_name(e1, e2) &&
				ce_same_name(e1, e3)) {
			path_list_insert((const char *)e1->name, conflict);
			i += 3;
		}
	}
	return 0;
}

static int merge(const char *name, const char *path)
{
	int ret;
	mmfile_t cur, base, other;
	mmbuffer_t result = {NULL, 0};
	xpparam_t xpp = {XDF_NEED_MINIMAL};

	if (handle_file(path, NULL, rr_path(name, "thisimage")) < 0)
		return 1;

	if (read_mmfile(&cur, rr_path(name, "thisimage")) ||
			read_mmfile(&base, rr_path(name, "preimage")) ||
			read_mmfile(&other, rr_path(name, "postimage")))
		return 1;
	ret = xdl_merge(&base, &cur, "", &other, "",
			&xpp, XDL_MERGE_ZEALOUS, &result);
	if (!ret) {
		FILE *f = fopen(path, "w");
		if (!f)
			return error("Could not write to %s", path);
		fwrite(result.ptr, result.size, 1, f);
		fclose(f);
	}

	free(cur.ptr);
	free(base.ptr);
	free(other.ptr);
	free(result.ptr);

	return ret;
}

static void unlink_rr_item(const char *name)
{
	unlink(rr_path(name, "thisimage"));
	unlink(rr_path(name, "preimage"));
	unlink(rr_path(name, "postimage"));
	rmdir(git_path("rr-cache/%s", name));
}

static void garbage_collect(struct path_list *rr)
{
	struct path_list to_remove = { NULL, 0, 0, 1 };
	char buf[1024];
	DIR *dir;
	struct dirent *e;
	int len, i, cutoff;
	time_t now = time(NULL), then;

	strlcpy(buf, git_path("rr-cache"), sizeof(buf));
	len = strlen(buf);
	dir = opendir(buf);
	strcpy(buf + len++, "/");
	while ((e = readdir(dir))) {
		const char *name = e->d_name;
		struct stat st;
		if (name[0] == '.' && (name[1] == '\0' ||
					(name[1] == '.' && name[2] == '\0')))
			continue;
		i = snprintf(buf + len, sizeof(buf) - len, "%s", name);
		strlcpy(buf + len + i, "/preimage", sizeof(buf) - len - i);
		if (stat(buf, &st))
			continue;
		then = st.st_mtime;
		strlcpy(buf + len + i, "/postimage", sizeof(buf) - len - i);
		cutoff = stat(buf, &st) ? cutoff_noresolve : cutoff_resolve;
		if (then < now - cutoff * 86400) {
			buf[len + i] = '\0';
			path_list_insert(xstrdup(name), &to_remove);
		}
	}
	for (i = 0; i < to_remove.nr; i++)
		unlink_rr_item(to_remove.items[i].path);
	path_list_clear(&to_remove, 0);
}

static int outf(void *dummy, mmbuffer_t *ptr, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		write(1, ptr[i].ptr, ptr[i].size);
	return 0;
}

static int diff_two(const char *file1, const char *label1,
		const char *file2, const char *label2)
{
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;
	mmfile_t minus, plus;

	if (read_mmfile(&minus, file1) || read_mmfile(&plus, file2))
		return 1;

	printf("--- a/%s\n+++ b/%s\n", label1, label2);
	fflush(stdout);
	xpp.flags = XDF_NEED_MINIMAL;
	xecfg.ctxlen = 3;
	xecfg.flags = 0;
	ecb.outf = outf;
	xdl_diff(&minus, &plus, &xpp, &xecfg, &ecb);

	free(minus.ptr);
	free(plus.ptr);
	return 0;
}

static int copy_file(const char *src, const char *dest)
{
	FILE *in, *out;
	char buffer[32768];
	int count;

	if (!(in = fopen(src, "r")))
		return error("Could not open %s", src);
	if (!(out = fopen(dest, "w")))
		return error("Could not open %s", dest);
	while ((count = fread(buffer, 1, sizeof(buffer), in)))
		fwrite(buffer, 1, count, out);
	fclose(in);
	fclose(out);
	return 0;
}

static int do_plain_rerere(struct path_list *rr, int fd)
{
	struct path_list conflict = { NULL, 0, 0, 1 };
	int i;

	find_conflict(&conflict);

	/*
	 * MERGE_RR records paths with conflicts immediately after merge
	 * failed.  Some of the conflicted paths might have been hand resolved
	 * in the working tree since then, but the initial run would catch all
	 * and register their preimages.
	 */

	for (i = 0; i < conflict.nr; i++) {
		const char *path = conflict.items[i].path;
		if (!path_list_has_path(rr, path)) {
			unsigned char sha1[20];
			char *hex;
			int ret;
			ret = handle_file(path, sha1, NULL);
			if (ret < 1)
				continue;
			hex = xstrdup(sha1_to_hex(sha1));
			path_list_insert(path, rr)->util = hex;
			if (mkdir(git_path("rr-cache/%s", hex), 0755))
				continue;;
			handle_file(path, NULL, rr_path(hex, "preimage"));
			fprintf(stderr, "Recorded preimage for '%s'\n", path);
		}
	}

	/*
	 * Now some of the paths that had conflicts earlier might have been
	 * hand resolved.  Others may be similar to a conflict already that
	 * was resolved before.
	 */

	for (i = 0; i < rr->nr; i++) {
		struct stat st;
		int ret;
		const char *path = rr->items[i].path;
		const char *name = (const char *)rr->items[i].util;

		if (!stat(rr_path(name, "preimage"), &st) &&
				!stat(rr_path(name, "postimage"), &st)) {
			if (!merge(name, path)) {
				fprintf(stderr, "Resolved '%s' using "
						"previous resolution.\n", path);
				goto tail_optimization;
			}
		}

		/* Let's see if we have resolved it. */
		ret = handle_file(path, NULL, NULL);
		if (ret)
			continue;

		fprintf(stderr, "Recorded resolution for '%s'.\n", path);
		copy_file(path, rr_path(name, "postimage"));
tail_optimization:
		if (i < rr->nr - 1) {
			memmove(rr->items + i,
					rr->items + i + 1,
					rr->nr - i - 1);
		}
		rr->nr--;
		i--;
	}

	return write_rr(rr, fd);
}

static int git_rerere_config(const char *var, const char *value)
{
	if (!strcmp(var, "gc.rerereresolved"))
		cutoff_resolve = git_config_int(var, value);
	else if (!strcmp(var, "gc.rerereunresolved"))
		cutoff_noresolve = git_config_int(var, value);
	else
		return git_default_config(var, value);
	return 0;
}

int cmd_rerere(int argc, const char **argv, const char *prefix)
{
	struct path_list merge_rr = { NULL, 0, 0, 1 };
	int i, fd = -1;
	struct stat st;

	if (stat(git_path("rr-cache"), &st) || !S_ISDIR(st.st_mode))
		return 0;

	git_config(git_rerere_config);

	merge_rr_path = xstrdup(git_path("rr-cache/MERGE_RR"));
	fd = hold_lock_file_for_update(&write_lock, merge_rr_path, 1);
	read_rr(&merge_rr);

	if (argc < 2)
		return do_plain_rerere(&merge_rr, fd);
	else if (!strcmp(argv[1], "clear")) {
		for (i = 0; i < merge_rr.nr; i++) {
			const char *name = (const char *)merge_rr.items[i].util;
			if (!stat(git_path("rr-cache/%s", name), &st) &&
					S_ISDIR(st.st_mode) &&
					stat(rr_path(name, "postimage"), &st))
				unlink_rr_item(name);
		}
		unlink(merge_rr_path);
	} else if (!strcmp(argv[1], "gc"))
		garbage_collect(&merge_rr);
	else if (!strcmp(argv[1], "status"))
		for (i = 0; i < merge_rr.nr; i++)
			printf("%s\n", merge_rr.items[i].path);
	else if (!strcmp(argv[1], "diff"))
		for (i = 0; i < merge_rr.nr; i++) {
			const char *path = merge_rr.items[i].path;
			const char *name = (const char *)merge_rr.items[i].util;
			diff_two(rr_path(name, "preimage"), path, path, path);
		}
	else
		usage(git_rerere_usage);

	path_list_clear(&merge_rr, 1);
	return 0;
}

