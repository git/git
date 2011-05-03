#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "parse-options.h"
#include "string-list.h"
#include "rerere.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"

static const char * const rerere_usage[] = {
	"git rerere [clear | forget path... | status | remaining | diff | gc]",
	NULL,
};

/* these values are days */
static int cutoff_noresolve = 15;
static int cutoff_resolve = 60;

static time_t rerere_created_at(const char *name)
{
	struct stat st;
	return stat(rerere_path(name, "preimage"), &st) ? (time_t) 0 : st.st_mtime;
}

static time_t rerere_last_used_at(const char *name)
{
	struct stat st;
	return stat(rerere_path(name, "postimage"), &st) ? (time_t) 0 : st.st_mtime;
}

static void unlink_rr_item(const char *name)
{
	unlink(rerere_path(name, "thisimage"));
	unlink(rerere_path(name, "preimage"));
	unlink(rerere_path(name, "postimage"));
	rmdir(git_path("rr-cache/%s", name));
}

static int git_rerere_gc_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "gc.rerereresolved"))
		cutoff_resolve = git_config_int(var, value);
	else if (!strcmp(var, "gc.rerereunresolved"))
		cutoff_noresolve = git_config_int(var, value);
	else
		return git_default_config(var, value, cb);
	return 0;
}

static void garbage_collect(struct string_list *rr)
{
	struct string_list to_remove = STRING_LIST_INIT_DUP;
	DIR *dir;
	struct dirent *e;
	int i, cutoff;
	time_t now = time(NULL), then;

	git_config(git_rerere_gc_config, NULL);
	dir = opendir(git_path("rr-cache"));
	if (!dir)
		die_errno("unable to open rr-cache directory");
	while ((e = readdir(dir))) {
		if (is_dot_or_dotdot(e->d_name))
			continue;

		then = rerere_last_used_at(e->d_name);
		if (then) {
			cutoff = cutoff_resolve;
		} else {
			then = rerere_created_at(e->d_name);
			if (!then)
				continue;
			cutoff = cutoff_noresolve;
		}
		if (then < now - cutoff * 86400)
			string_list_append(&to_remove, e->d_name);
	}
	for (i = 0; i < to_remove.nr; i++)
		unlink_rr_item(to_remove.items[i].string);
	string_list_clear(&to_remove, 0);
}

static int outf(void *dummy, mmbuffer_t *ptr, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		if (write_in_full(1, ptr[i].ptr, ptr[i].size) != ptr[i].size)
			return -1;
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
	memset(&xpp, 0, sizeof(xpp));
	xpp.flags = 0;
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = 3;
	ecb.outf = outf;
	xdi_diff(&minus, &plus, &xpp, &xecfg, &ecb);

	free(minus.ptr);
	free(plus.ptr);
	return 0;
}

int cmd_rerere(int argc, const char **argv, const char *prefix)
{
	struct string_list merge_rr = STRING_LIST_INIT_DUP;
	int i, fd, autoupdate = -1, flags = 0;

	struct option options[] = {
		OPT_SET_INT(0, "rerere-autoupdate", &autoupdate,
			"register clean resolutions in index", 1),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);

	if (autoupdate == 1)
		flags = RERERE_AUTOUPDATE;
	if (autoupdate == 0)
		flags = RERERE_NOAUTOUPDATE;

	if (argc < 1)
		return rerere(flags);

	if (!strcmp(argv[0], "forget")) {
		const char **pathspec;
		if (argc < 2)
			warning("'git rerere forget' without paths is deprecated");
		pathspec = get_pathspec(prefix, argv + 1);
		return rerere_forget(pathspec);
	}

	fd = setup_rerere(&merge_rr, flags);
	if (fd < 0)
		return 0;

	if (!strcmp(argv[0], "clear")) {
		for (i = 0; i < merge_rr.nr; i++) {
			const char *name = (const char *)merge_rr.items[i].util;
			if (!has_rerere_resolution(name))
				unlink_rr_item(name);
		}
		unlink_or_warn(git_path("MERGE_RR"));
	} else if (!strcmp(argv[0], "gc"))
		garbage_collect(&merge_rr);
	else if (!strcmp(argv[0], "status"))
		for (i = 0; i < merge_rr.nr; i++)
			printf("%s\n", merge_rr.items[i].string);
	else if (!strcmp(argv[0], "remaining")) {
		rerere_remaining(&merge_rr);
		for (i = 0; i < merge_rr.nr; i++) {
			if (merge_rr.items[i].util != RERERE_RESOLVED)
				printf("%s\n", merge_rr.items[i].string);
			else
				/* prepare for later call to
				 * string_list_clear() */
				merge_rr.items[i].util = NULL;
		}
	} else if (!strcmp(argv[0], "diff"))
		for (i = 0; i < merge_rr.nr; i++) {
			const char *path = merge_rr.items[i].string;
			const char *name = (const char *)merge_rr.items[i].util;
			diff_two(rerere_path(name, "preimage"), path, path, path);
		}
	else
		usage_with_options(rerere_usage, options);

	string_list_clear(&merge_rr, 1);
	return 0;
}
