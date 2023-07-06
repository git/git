#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "repository.h"
#include "revision.h"
#include "reachable.h"
#include "wildmatch.h"
#include "worktree.h"
#include "reflog.h"
#include "parse-options.h"

#define BUILTIN_REFLOG_SHOW_USAGE \
	N_("git reflog [show] [<log-options>] [<ref>]")

#define BUILTIN_REFLOG_EXPIRE_USAGE \
	N_("git reflog expire [--expire=<time>] [--expire-unreachable=<time>]\n" \
	   "                  [--rewrite] [--updateref] [--stale-fix]\n" \
	   "                  [--dry-run | -n] [--verbose] [--all [--single-worktree] | <refs>...]")

#define BUILTIN_REFLOG_DELETE_USAGE \
	N_("git reflog delete [--rewrite] [--updateref]\n" \
	   "                  [--dry-run | -n] [--verbose] <ref>@{<specifier>}...")

#define BUILTIN_REFLOG_EXISTS_USAGE \
	N_("git reflog exists <ref>")

static const char *const reflog_show_usage[] = {
	BUILTIN_REFLOG_SHOW_USAGE,
	NULL,
};

static const char *const reflog_expire_usage[] = {
	BUILTIN_REFLOG_EXPIRE_USAGE,
	NULL
};

static const char *const reflog_delete_usage[] = {
	BUILTIN_REFLOG_DELETE_USAGE,
	NULL
};

static const char *const reflog_exists_usage[] = {
	BUILTIN_REFLOG_EXISTS_USAGE,
	NULL,
};

static const char *const reflog_usage[] = {
	BUILTIN_REFLOG_SHOW_USAGE,
	BUILTIN_REFLOG_EXPIRE_USAGE,
	BUILTIN_REFLOG_DELETE_USAGE,
	BUILTIN_REFLOG_EXISTS_USAGE,
	NULL
};

static timestamp_t default_reflog_expire;
static timestamp_t default_reflog_expire_unreachable;

struct worktree_reflogs {
	struct worktree *worktree;
	struct string_list reflogs;
};

static int collect_reflog(const char *ref, const struct object_id *oid UNUSED,
			  int flags UNUSED, void *cb_data)
{
	struct worktree_reflogs *cb = cb_data;
	struct worktree *worktree = cb->worktree;
	struct strbuf newref = STRBUF_INIT;

	/*
	 * Avoid collecting the same shared ref multiple times because
	 * they are available via all worktrees.
	 */
	if (!worktree->is_current &&
	    parse_worktree_ref(ref, NULL, NULL, NULL) == REF_WORKTREE_SHARED)
		return 0;

	strbuf_worktree_ref(worktree, &newref, ref);
	string_list_append_nodup(&cb->reflogs, strbuf_detach(&newref, NULL));

	return 0;
}

static struct reflog_expire_cfg {
	struct reflog_expire_cfg *next;
	timestamp_t expire_total;
	timestamp_t expire_unreachable;
	char pattern[FLEX_ARRAY];
} *reflog_expire_cfg, **reflog_expire_cfg_tail;

static struct reflog_expire_cfg *find_cfg_ent(const char *pattern, size_t len)
{
	struct reflog_expire_cfg *ent;

	if (!reflog_expire_cfg_tail)
		reflog_expire_cfg_tail = &reflog_expire_cfg;

	for (ent = reflog_expire_cfg; ent; ent = ent->next)
		if (!strncmp(ent->pattern, pattern, len) &&
		    ent->pattern[len] == '\0')
			return ent;

	FLEX_ALLOC_MEM(ent, pattern, pattern, len);
	*reflog_expire_cfg_tail = ent;
	reflog_expire_cfg_tail = &(ent->next);
	return ent;
}

/* expiry timer slot */
#define EXPIRE_TOTAL   01
#define EXPIRE_UNREACH 02

static int reflog_expire_config(const char *var, const char *value,
				const struct config_context *ctx, void *cb)
{
	const char *pattern, *key;
	size_t pattern_len;
	timestamp_t expire;
	int slot;
	struct reflog_expire_cfg *ent;

	if (parse_config_key(var, "gc", &pattern, &pattern_len, &key) < 0)
		return git_default_config(var, value, ctx, cb);

	if (!strcmp(key, "reflogexpire")) {
		slot = EXPIRE_TOTAL;
		if (git_config_expiry_date(&expire, var, value))
			return -1;
	} else if (!strcmp(key, "reflogexpireunreachable")) {
		slot = EXPIRE_UNREACH;
		if (git_config_expiry_date(&expire, var, value))
			return -1;
	} else
		return git_default_config(var, value, ctx, cb);

	if (!pattern) {
		switch (slot) {
		case EXPIRE_TOTAL:
			default_reflog_expire = expire;
			break;
		case EXPIRE_UNREACH:
			default_reflog_expire_unreachable = expire;
			break;
		}
		return 0;
	}

	ent = find_cfg_ent(pattern, pattern_len);
	if (!ent)
		return -1;
	switch (slot) {
	case EXPIRE_TOTAL:
		ent->expire_total = expire;
		break;
	case EXPIRE_UNREACH:
		ent->expire_unreachable = expire;
		break;
	}
	return 0;
}

static void set_reflog_expiry_param(struct cmd_reflog_expire_cb *cb, const char *ref)
{
	struct reflog_expire_cfg *ent;

	if (cb->explicit_expiry == (EXPIRE_TOTAL|EXPIRE_UNREACH))
		return; /* both given explicitly -- nothing to tweak */

	for (ent = reflog_expire_cfg; ent; ent = ent->next) {
		if (!wildmatch(ent->pattern, ref, 0)) {
			if (!(cb->explicit_expiry & EXPIRE_TOTAL))
				cb->expire_total = ent->expire_total;
			if (!(cb->explicit_expiry & EXPIRE_UNREACH))
				cb->expire_unreachable = ent->expire_unreachable;
			return;
		}
	}

	/*
	 * If unconfigured, make stash never expire
	 */
	if (!strcmp(ref, "refs/stash")) {
		if (!(cb->explicit_expiry & EXPIRE_TOTAL))
			cb->expire_total = 0;
		if (!(cb->explicit_expiry & EXPIRE_UNREACH))
			cb->expire_unreachable = 0;
		return;
	}

	/* Nothing matched -- use the default value */
	if (!(cb->explicit_expiry & EXPIRE_TOTAL))
		cb->expire_total = default_reflog_expire;
	if (!(cb->explicit_expiry & EXPIRE_UNREACH))
		cb->expire_unreachable = default_reflog_expire_unreachable;
}

static int expire_unreachable_callback(const struct option *opt,
				 const char *arg,
				 int unset)
{
	struct cmd_reflog_expire_cb *cmd = opt->value;

	BUG_ON_OPT_NEG(unset);

	if (parse_expiry_date(arg, &cmd->expire_unreachable))
		die(_("invalid timestamp '%s' given to '--%s'"),
		    arg, opt->long_name);

	cmd->explicit_expiry |= EXPIRE_UNREACH;
	return 0;
}

static int expire_total_callback(const struct option *opt,
				 const char *arg,
				 int unset)
{
	struct cmd_reflog_expire_cb *cmd = opt->value;

	BUG_ON_OPT_NEG(unset);

	if (parse_expiry_date(arg, &cmd->expire_total))
		die(_("invalid timestamp '%s' given to '--%s'"),
		    arg, opt->long_name);

	cmd->explicit_expiry |= EXPIRE_TOTAL;
	return 0;
}

static int cmd_reflog_show(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	parse_options(argc, argv, prefix, options, reflog_show_usage,
		      PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_ARGV0 |
		      PARSE_OPT_KEEP_UNKNOWN_OPT);

	return cmd_log_reflog(argc, argv, prefix);
}

static int cmd_reflog_expire(int argc, const char **argv, const char *prefix)
{
	struct cmd_reflog_expire_cb cmd = { 0 };
	timestamp_t now = time(NULL);
	int i, status, do_all, all_worktrees = 1;
	unsigned int flags = 0;
	int verbose = 0;
	reflog_expiry_should_prune_fn *should_prune_fn = should_expire_reflog_ent;
	const struct option options[] = {
		OPT_BIT(0, "dry-run", &flags, N_("do not actually prune any entries"),
			EXPIRE_REFLOGS_DRY_RUN),
		OPT_BIT(0, "rewrite", &flags,
			N_("rewrite the old SHA1 with the new SHA1 of the entry that now precedes it"),
			EXPIRE_REFLOGS_REWRITE),
		OPT_BIT(0, "updateref", &flags,
			N_("update the reference to the value of the top reflog entry"),
			EXPIRE_REFLOGS_UPDATE_REF),
		OPT_BOOL(0, "verbose", &verbose, N_("print extra information on screen")),
		OPT_CALLBACK_F(0, "expire", &cmd, N_("timestamp"),
			       N_("prune entries older than the specified time"),
			       PARSE_OPT_NONEG,
			       expire_total_callback),
		OPT_CALLBACK_F(0, "expire-unreachable", &cmd, N_("timestamp"),
			       N_("prune entries older than <time> that are not reachable from the current tip of the branch"),
			       PARSE_OPT_NONEG,
			       expire_unreachable_callback),
		OPT_BOOL(0, "stale-fix", &cmd.stalefix,
			 N_("prune any reflog entries that point to broken commits")),
		OPT_BOOL(0, "all", &do_all, N_("process the reflogs of all references")),
		OPT_BOOL(1, "single-worktree", &all_worktrees,
			 N_("limits processing to reflogs from the current worktree only")),
		OPT_END()
	};

	default_reflog_expire_unreachable = now - 30 * 24 * 3600;
	default_reflog_expire = now - 90 * 24 * 3600;
	git_config(reflog_expire_config, NULL);

	save_commit_buffer = 0;
	do_all = status = 0;

	cmd.explicit_expiry = 0;
	cmd.expire_total = default_reflog_expire;
	cmd.expire_unreachable = default_reflog_expire_unreachable;

	argc = parse_options(argc, argv, prefix, options, reflog_expire_usage, 0);

	if (verbose)
		should_prune_fn = should_expire_reflog_ent_verbose;

	/*
	 * We can trust the commits and objects reachable from refs
	 * even in older repository.  We cannot trust what's reachable
	 * from reflog if the repository was pruned with older git.
	 */
	if (cmd.stalefix) {
		struct rev_info revs;

		repo_init_revisions(the_repository, &revs, prefix);
		revs.do_not_die_on_missing_tree = 1;
		revs.ignore_missing = 1;
		revs.ignore_missing_links = 1;
		if (verbose)
			printf(_("Marking reachable objects..."));
		mark_reachable_objects(&revs, 0, 0, NULL);
		release_revisions(&revs);
		if (verbose)
			putchar('\n');
	}

	if (do_all) {
		struct worktree_reflogs collected = {
			.reflogs = STRING_LIST_INIT_DUP,
		};
		struct string_list_item *item;
		struct worktree **worktrees, **p;

		worktrees = get_worktrees();
		for (p = worktrees; *p; p++) {
			if (!all_worktrees && !(*p)->is_current)
				continue;
			collected.worktree = *p;
			refs_for_each_reflog(get_worktree_ref_store(*p),
					     collect_reflog, &collected);
		}
		free_worktrees(worktrees);

		for_each_string_list_item(item, &collected.reflogs) {
			struct expire_reflog_policy_cb cb = {
				.cmd = cmd,
				.dry_run = !!(flags & EXPIRE_REFLOGS_DRY_RUN),
			};

			set_reflog_expiry_param(&cb.cmd,  item->string);
			status |= reflog_expire(item->string, flags,
						reflog_expiry_prepare,
						should_prune_fn,
						reflog_expiry_cleanup,
						&cb);
		}
		string_list_clear(&collected.reflogs, 0);
	}

	for (i = 0; i < argc; i++) {
		char *ref;
		struct expire_reflog_policy_cb cb = { .cmd = cmd };

		if (!dwim_log(argv[i], strlen(argv[i]), NULL, &ref)) {
			status |= error(_("%s points nowhere!"), argv[i]);
			continue;
		}
		set_reflog_expiry_param(&cb.cmd, ref);
		status |= reflog_expire(ref, flags,
					reflog_expiry_prepare,
					should_prune_fn,
					reflog_expiry_cleanup,
					&cb);
		free(ref);
	}
	return status;
}

static int cmd_reflog_delete(int argc, const char **argv, const char *prefix)
{
	int i, status = 0;
	unsigned int flags = 0;
	int verbose = 0;

	const struct option options[] = {
		OPT_BIT(0, "dry-run", &flags, N_("do not actually prune any entries"),
			EXPIRE_REFLOGS_DRY_RUN),
		OPT_BIT(0, "rewrite", &flags,
			N_("rewrite the old SHA1 with the new SHA1 of the entry that now precedes it"),
			EXPIRE_REFLOGS_REWRITE),
		OPT_BIT(0, "updateref", &flags,
			N_("update the reference to the value of the top reflog entry"),
			EXPIRE_REFLOGS_UPDATE_REF),
		OPT_BOOL(0, "verbose", &verbose, N_("print extra information on screen")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, reflog_delete_usage, 0);

	if (argc < 1)
		return error(_("no reflog specified to delete"));

	for (i = 0; i < argc; i++)
		status |= reflog_delete(argv[i], flags, verbose);

	return status;
}

static int cmd_reflog_exists(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};
	const char *refname;

	argc = parse_options(argc, argv, prefix, options, reflog_exists_usage,
			     0);
	if (!argc)
		usage_with_options(reflog_exists_usage, options);

	refname = argv[0];
	if (check_refname_format(refname, REFNAME_ALLOW_ONELEVEL))
		die(_("invalid ref format: %s"), refname);
	return !reflog_exists(refname);
}

/*
 * main "reflog"
 */

int cmd_reflog(int argc, const char **argv, const char *prefix)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("show", &fn, cmd_reflog_show),
		OPT_SUBCOMMAND("expire", &fn, cmd_reflog_expire),
		OPT_SUBCOMMAND("delete", &fn, cmd_reflog_delete),
		OPT_SUBCOMMAND("exists", &fn, cmd_reflog_exists),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, reflog_usage,
			     PARSE_OPT_SUBCOMMAND_OPTIONAL |
			     PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_ARGV0 |
			     PARSE_OPT_KEEP_UNKNOWN_OPT);
	if (fn)
		return fn(argc - 1, argv + 1, prefix);
	else
		return cmd_log_reflog(argc, argv, prefix);
}
