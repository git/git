#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "parse-options.h"
#include "string-list.h"
#include "rerere.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"
#include "pathspec.h"

static const char * const rerere_usage[] = {
	N_("git rerere [clear | forget path... | status | remaining | diff | gc]"),
	NULL,
};

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
			N_("register clean resolutions in index"), 1),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);

	git_config(git_xmerge_config, NULL);

	if (autoupdate == 1)
		flags = RERERE_AUTOUPDATE;
	if (autoupdate == 0)
		flags = RERERE_NOAUTOUPDATE;

	if (argc < 1)
		return rerere(flags);

	if (!strcmp(argv[0], "forget")) {
		struct pathspec pathspec;
		if (argc < 2)
			warning("'git rerere forget' without paths is deprecated");
		parse_pathspec(&pathspec, 0, PATHSPEC_PREFER_CWD,
			       prefix, argv + 1);
		return rerere_forget(&pathspec);
	}

	fd = setup_rerere(&merge_rr, flags);
	if (fd < 0)
		return 0;

	if (!strcmp(argv[0], "clear")) {
		rerere_clear(&merge_rr);
	} else if (!strcmp(argv[0], "gc"))
		rerere_gc(&merge_rr);
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
