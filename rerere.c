#include "cache.h"
#include "string-list.h"
#include "rerere.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"

/* if rerere_enabled == -1, fall back to detection of .git/rr-cache */
static int rerere_enabled = -1;

/* automatically update cleanly resolved paths to the index */
static int rerere_autoupdate;

static char *merge_rr_path;

static const char *rr_path(const char *name, const char *file)
{
	return git_path("rr-cache/%s/%s", name, file);
}

static int has_resolution(const char *name)
{
	struct stat st;
	return !stat(rr_path(name, "postimage"), &st);
}

static void read_rr(struct string_list *rr)
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
		string_list_insert(buf, rr)->util = name;
	}
	fclose(in);
}

static struct lock_file write_lock;

static int write_rr(struct string_list *rr, int out_fd)
{
	int i;
	for (i = 0; i < rr->nr; i++) {
		const char *path;
		int length;
		if (!rr->items[i].util)
			continue;
		path = rr->items[i].string;
		length = strlen(path) + 1;
		if (write_in_full(out_fd, rr->items[i].util, 40) != 40 ||
		    write_in_full(out_fd, "\t", 1) != 1 ||
		    write_in_full(out_fd, path, length) != length)
			die("unable to write rerere record");
	}
	if (commit_lock_file(&write_lock) != 0)
		die("unable to write rerere record");
	return 0;
}

static int handle_file(const char *path,
	 unsigned char *sha1, const char *output)
{
	SHA_CTX ctx;
	char buf[1024];
	int hunk = 0, hunk_no = 0;
	struct strbuf one, two;
	FILE *f = fopen(path, "r");
	FILE *out = NULL;

	if (!f)
		return error("Could not open %s", path);

	if (output) {
		out = fopen(output, "w");
		if (!out) {
			fclose(f);
			return error("Could not write %s", output);
		}
	}

	if (sha1)
		SHA1_Init(&ctx);

	strbuf_init(&one, 0);
	strbuf_init(&two,  0);
	while (fgets(buf, sizeof(buf), f)) {
		if (!prefixcmp(buf, "<<<<<<< ")) {
			if (hunk)
				goto bad;
			hunk = 1;
		} else if (!prefixcmp(buf, "=======") && isspace(buf[7])) {
			if (hunk != 1)
				goto bad;
			hunk = 2;
		} else if (!prefixcmp(buf, ">>>>>>> ")) {
			if (hunk != 2)
				goto bad;
			if (strbuf_cmp(&one, &two) > 0)
				strbuf_swap(&one, &two);
			hunk_no++;
			hunk = 0;
			if (out) {
				fputs("<<<<<<<\n", out);
				fwrite(one.buf, one.len, 1, out);
				fputs("=======\n", out);
				fwrite(two.buf, two.len, 1, out);
				fputs(">>>>>>>\n", out);
			}
			if (sha1) {
				SHA1_Update(&ctx, one.buf ? one.buf : "",
					    one.len + 1);
				SHA1_Update(&ctx, two.buf ? two.buf : "",
					    two.len + 1);
			}
			strbuf_reset(&one);
			strbuf_reset(&two);
		} else if (hunk == 1)
			strbuf_addstr(&one, buf);
		else if (hunk == 2)
			strbuf_addstr(&two, buf);
		else if (out)
			fputs(buf, out);
		continue;
	bad:
		hunk = 99; /* force error exit */
		break;
	}
	strbuf_release(&one);
	strbuf_release(&two);

	fclose(f);
	if (out)
		fclose(out);
	if (sha1)
		SHA1_Final(sha1, &ctx);
	if (hunk) {
		if (output)
			unlink(output);
		return error("Could not parse conflict hunks in %s", path);
	}
	return hunk_no;
}

static int find_conflict(struct string_list *conflict)
{
	int i;
	if (read_cache() < 0)
		return error("Could not read index");
	for (i = 0; i+1 < active_nr; i++) {
		struct cache_entry *e2 = active_cache[i];
		struct cache_entry *e3 = active_cache[i+1];
		if (ce_stage(e2) == 2 &&
		    ce_stage(e3) == 3 &&
		    ce_same_name(e2, e3) &&
		    S_ISREG(e2->ce_mode) &&
		    S_ISREG(e3->ce_mode)) {
			string_list_insert((const char *)e2->name, conflict);
			i++; /* skip over both #2 and #3 */
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

static struct lock_file index_lock;

static int update_paths(struct string_list *update)
{
	int i;
	int fd = hold_locked_index(&index_lock, 0);
	int status = 0;

	if (fd < 0)
		return -1;

	for (i = 0; i < update->nr; i++) {
		struct string_list_item *item = &update->items[i];
		if (add_file_to_cache(item->string, ADD_CACHE_IGNORE_ERRORS))
			status = -1;
	}

	if (!status && active_cache_changed) {
		if (write_cache(fd, active_cache, active_nr) ||
		    commit_locked_index(&index_lock))
			die("Unable to write new index file");
	} else if (fd >= 0)
		rollback_lock_file(&index_lock);
	return status;
}

static int do_plain_rerere(struct string_list *rr, int fd)
{
	struct string_list conflict = { NULL, 0, 0, 1 };
	struct string_list update = { NULL, 0, 0, 1 };
	int i;

	find_conflict(&conflict);

	/*
	 * MERGE_RR records paths with conflicts immediately after merge
	 * failed.  Some of the conflicted paths might have been hand resolved
	 * in the working tree since then, but the initial run would catch all
	 * and register their preimages.
	 */

	for (i = 0; i < conflict.nr; i++) {
		const char *path = conflict.items[i].string;
		if (!string_list_has_string(rr, path)) {
			unsigned char sha1[20];
			char *hex;
			int ret;
			ret = handle_file(path, sha1, NULL);
			if (ret < 1)
				continue;
			hex = xstrdup(sha1_to_hex(sha1));
			string_list_insert(path, rr)->util = hex;
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
		int ret;
		const char *path = rr->items[i].string;
		const char *name = (const char *)rr->items[i].util;

		if (has_resolution(name)) {
			if (!merge(name, path)) {
				if (rerere_autoupdate)
					string_list_insert(path, &update);
				fprintf(stderr,
					"%s '%s' using previous resolution.\n",
					rerere_autoupdate
					? "Staged" : "Resolved",
					path);
				goto mark_resolved;
			}
		}

		/* Let's see if we have resolved it. */
		ret = handle_file(path, NULL, NULL);
		if (ret)
			continue;

		fprintf(stderr, "Recorded resolution for '%s'.\n", path);
		copy_file(rr_path(name, "postimage"), path, 0666);
	mark_resolved:
		rr->items[i].util = NULL;
	}

	if (update.nr)
		update_paths(&update);

	return write_rr(rr, fd);
}

static int git_rerere_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "rerere.enabled"))
		rerere_enabled = git_config_bool(var, value);
	else if (!strcmp(var, "rerere.autoupdate"))
		rerere_autoupdate = git_config_bool(var, value);
	else
		return git_default_config(var, value, cb);
	return 0;
}

static int is_rerere_enabled(void)
{
	struct stat st;
	const char *rr_cache;
	int rr_cache_exists;

	if (!rerere_enabled)
		return 0;

	rr_cache = git_path("rr-cache");
	rr_cache_exists = !stat(rr_cache, &st) && S_ISDIR(st.st_mode);
	if (rerere_enabled < 0)
		return rr_cache_exists;

	if (!rr_cache_exists &&
	    (mkdir(rr_cache, 0777) || adjust_shared_perm(rr_cache)))
		die("Could not create directory %s", rr_cache);
	return 1;
}

int setup_rerere(struct string_list *merge_rr)
{
	int fd;

	git_config(git_rerere_config, NULL);
	if (!is_rerere_enabled())
		return -1;

	merge_rr_path = xstrdup(git_path("MERGE_RR"));
	fd = hold_lock_file_for_update(&write_lock, merge_rr_path, 1);
	read_rr(merge_rr);
	return fd;
}

int rerere(void)
{
	struct string_list merge_rr = { NULL, 0, 0, 1 };
	int fd;

	fd = setup_rerere(&merge_rr);
	if (fd < 0)
		return 0;
	return do_plain_rerere(&merge_rr, fd);
}
