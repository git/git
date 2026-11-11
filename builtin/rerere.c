#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"
#include "rerere.h"
#include "strbuf.h"
#include "string-list.h"
#include "strvec.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"
#include "pathspec.h"

static const char * const rerere_usage[] = {
	N_("git rerere [clear | forget <pathspec>... | diff | status | remaining | gc]"),
	NULL,
};

static int outf(void *dummy UNUSED, mmfile_t *ptr, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		if (write_in_full(1, ptr[i].ptr, ptr[i].size) < 0)
			return -1;
	return 0;
}

static int diff_two(const char *file1, const char *label1,
		const char *file2, const char *label2)
{
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb = { .out_line = outf };
	mmfile_t minus, plus;
	int ret;

	if (read_mmfile(&minus, file1))
		return -1;
	if (read_mmfile(&plus, file2)) {
		free(minus.ptr);
		return -1;
	}

	printf("--- a/%s\n+++ b/%s\n", label1, label2);
	fflush(stdout);
	memset(&xpp, 0, sizeof(xpp));
	xpp.flags = 0;
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = 3;
	ret = xdi_diff(&minus, &plus, &xpp, &xecfg, &ecb);

	free(minus.ptr);
	free(plus.ptr);
	return ret;
}

static int cmd_rerere_clear(int argc,
			    const char **argv,
			    const char *prefix,
			    struct repository *repo)
{
	struct option options[] = {
		OPT_END(),
	};
	struct string_list merge_rr = STRING_LIST_INIT_DUP;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	rerere_clear(repo, &merge_rr);

	string_list_clear(&merge_rr, 1);
	return 0;
}

static int cmd_rerere_diff(int argc,
			   const char **argv,
			   const char *prefix,
			   struct repository *repo)
{
	struct option options[] = {
		OPT_END(),
	};
	struct string_list merge_rr = STRING_LIST_INIT_DUP;
	struct strbuf buf = STRBUF_INIT;
	struct rerere_cache cache;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	if (rerere_cache_setup(&cache, repo, &merge_rr,
			       RERERE_READONLY) < 0)
		return 0;

	for (size_t i = 0; i < merge_rr.nr; i++) {
		const char *path = merge_rr.items[i].string;
		const struct rerere_id *id = merge_rr.items[i].util;
		if (diff_two(rerere_path(repo, &buf, id, "preimage"), path, path, path))
			die(_("unable to generate diff for '%s'"),
			    rerere_path(repo, &buf, id, NULL));
	}

	string_list_clear(&merge_rr, 1);
	rerere_cache_release(&cache);
	strbuf_release(&buf);
	return 0;
}

static int cmd_rerere_forget(int argc,
			     const char **argv,
			     const char *prefix,
			     struct repository *repo)
{
	struct option options[] = {
		OPT_END(),
	};
	struct pathspec pathspec;
	int ret;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (!argc)
		warning(_("'git rerere forget' without paths is deprecated"));
	parse_pathspec(&pathspec, 0, PATHSPEC_PREFER_CWD, prefix, argv);

	ret = rerere_forget(repo, &pathspec);

	clear_pathspec(&pathspec);
	return ret;
}

static int cmd_rerere_gc(int argc,
			 const char **argv,
			 const char *prefix,
			 struct repository *repo)
{
	int skip_locked = 0;
	struct option options[] = {
		OPT_HIDDEN_BOOL(0, "skip-locked", &skip_locked,
				N_("skip gc while another process holds the lock")),
		OPT_END(),
	};
	struct string_list merge_rr = STRING_LIST_INIT_DUP;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	rerere_gc(repo, &merge_rr,
		  skip_locked ? RERERE_NOWAIT : 0);

	string_list_clear(&merge_rr, 1);
	return 0;
}

static int cmd_rerere_perform(int argc,
			      const char **argv,
			      const char *prefix,
			      struct repository *repo)
{
	int autoupdate = -1;
	struct option options[] = {
		OPT_SET_INT(0, "rerere-autoupdate", &autoupdate,
			N_("register clean resolutions in index"), 1),
		OPT_END(),
	};
	enum rerere_flags flags = 0;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	if (autoupdate == 1)
		flags = RERERE_AUTOUPDATE;
	if (autoupdate == 0)
		flags = RERERE_NOAUTOUPDATE;

	return rerere(repo, flags);
}

static int cmd_rerere_remaining(int argc,
				const char **argv,
				const char *prefix,
				struct repository *repo)
{
	struct option options[] = {
		OPT_END(),
	};
	struct string_list merge_rr = STRING_LIST_INIT_DUP;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	rerere_remaining(repo, &merge_rr);
	for (size_t i = 0; i < merge_rr.nr; i++) {
		if (merge_rr.items[i].util != RERERE_RESOLVED)
			printf("%s\n", merge_rr.items[i].string);
		else
			/* prepare for later call to
			 * string_list_clear() */
			merge_rr.items[i].util = NULL;
	}

	string_list_clear(&merge_rr, 1);
	return 0;
}

static int cmd_rerere_status(int argc,
			     const char **argv,
			     const char *prefix,
			     struct repository *repo)
{
	struct option options[] = {
		OPT_END(),
	};
	struct string_list merge_rr = STRING_LIST_INIT_DUP;
	struct rerere_cache cache;

	argc = parse_options(argc, argv, prefix, options, rerere_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	if (rerere_cache_setup(&cache, repo, &merge_rr,
			       RERERE_READONLY) < 0)
		return 0;
	for (size_t i = 0; i < merge_rr.nr; i++)
		printf("%s\n", merge_rr.items[i].string);

	string_list_clear(&merge_rr, 1);
	rerere_cache_release(&cache);
	return 0;
}

int cmd_rerere(int argc,
	       const char **argv,
	       const char *prefix,
	       struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("clear", &fn, cmd_rerere_clear),
		OPT_SUBCOMMAND("diff", &fn, cmd_rerere_diff),
		OPT_SUBCOMMAND("forget", &fn, cmd_rerere_forget),
		OPT_SUBCOMMAND("gc", &fn, cmd_rerere_gc),
		OPT_SUBCOMMAND("remaining", &fn, cmd_rerere_remaining),
		OPT_SUBCOMMAND("status", &fn, cmd_rerere_status),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, rerere_usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT | PARSE_OPT_SUBCOMMAND_OPTIONAL);
	repo_config(repo, git_xmerge_config, NULL);

	if (!fn) {
		struct strvec args = STRVEC_INIT;
		const char **args_copy;
		int ret;

		strvec_push(&args, "perform");
		strvec_pushv(&args, argv);

		/*
		 * We need to copy the array as it will be modified by
		 * `parse_options()`, and if so we leak the strings
		 * referenced by `args`.
		 */
		CALLOC_ARRAY(args_copy, args.nr);
		COPY_ARRAY(args_copy, args.v, args.nr);

		ret = cmd_rerere_perform(args.nr, args_copy, prefix, repo);

		strvec_clear(&args);
		free(args_copy);
		return ret;
	}

	return fn(argc, argv, prefix, repo);
}
