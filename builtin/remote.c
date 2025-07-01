#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"
#include "path.h"
#include "transport.h"
#include "remote.h"
#include "string-list.h"
#include "strbuf.h"
#include "run-command.h"
#include "rebase.h"
#include "refs.h"
#include "refspec.h"
#include "odb.h"
#include "strvec.h"
#include "commit-reach.h"
#include "progress.h"

static const char * const builtin_remote_usage[] = {
	"git remote [-v | --verbose]",
	N_("git remote add [-t <branch>] [-m <master>] [-f] [--tags | --no-tags] [--mirror=<fetch|push>] <name> <url>"),
	N_("git remote rename [--[no-]progress] <old> <new>"),
	N_("git remote remove <name>"),
	N_("git remote set-head <name> (-a | --auto | -d | --delete | <branch>)"),
	N_("git remote [-v | --verbose] show [-n] <name>"),
	N_("git remote prune [-n | --dry-run] <name>"),
	N_("git remote [-v | --verbose] update [-p | --prune] [(<group> | <remote>)...]"),
	N_("git remote set-branches [--add] <name> <branch>..."),
	N_("git remote get-url [--push] [--all] <name>"),
	N_("git remote set-url [--push] <name> <newurl> [<oldurl>]"),
	N_("git remote set-url --add <name> <newurl>"),
	N_("git remote set-url --delete <name> <url>"),
	NULL
};

static const char * const builtin_remote_add_usage[] = {
	N_("git remote add [<options>] <name> <url>"),
	NULL
};

static const char * const builtin_remote_rename_usage[] = {
	N_("git remote rename [--[no-]progress] <old> <new>"),
	NULL
};

static const char * const builtin_remote_rm_usage[] = {
	N_("git remote remove <name>"),
	NULL
};

static const char * const builtin_remote_sethead_usage[] = {
	N_("git remote set-head <name> (-a | --auto | -d | --delete | <branch>)"),
	NULL
};

static const char * const builtin_remote_setbranches_usage[] = {
	N_("git remote set-branches <name> <branch>..."),
	N_("git remote set-branches --add <name> <branch>..."),
	NULL
};

static const char * const builtin_remote_show_usage[] = {
	N_("git remote show [<options>] <name>"),
	NULL
};

static const char * const builtin_remote_prune_usage[] = {
	N_("git remote prune [<options>] <name>"),
	NULL
};

static const char * const builtin_remote_update_usage[] = {
	N_("git remote update [<options>] [<group> | <remote>]..."),
	NULL
};

static const char * const builtin_remote_geturl_usage[] = {
	N_("git remote get-url [--push] [--all] <name>"),
	NULL
};

static const char * const builtin_remote_seturl_usage[] = {
	N_("git remote set-url [--push] <name> <newurl> [<oldurl>]"),
	N_("git remote set-url --add <name> <newurl>"),
	N_("git remote set-url --delete <name> <url>"),
	NULL
};

#define GET_REF_STATES (1<<0)
#define GET_HEAD_NAMES (1<<1)
#define GET_PUSH_REF_STATES (1<<2)

static int verbose;

static int fetch_remote(const char *name)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	strvec_push(&cmd.args, "fetch");
	if (verbose)
		strvec_push(&cmd.args, "-v");
	strvec_push(&cmd.args, name);
	cmd.git_cmd = 1;
	printf_ln(_("Updating %s"), name);
	if (run_command(&cmd))
		return error(_("Could not fetch %s"), name);
	return 0;
}

enum {
	TAGS_UNSET = 0,
	TAGS_DEFAULT = 1,
	TAGS_SET = 2
};

#define MIRROR_NONE 0
#define MIRROR_FETCH 1
#define MIRROR_PUSH 2
#define MIRROR_BOTH (MIRROR_FETCH|MIRROR_PUSH)

static void add_branch(const char *key, const char *branchname,
		       const char *remotename, int mirror, struct strbuf *tmp)
{
	strbuf_reset(tmp);
	strbuf_addch(tmp, '+');
	if (mirror)
		strbuf_addf(tmp, "refs/%s:refs/%s",
				branchname, branchname);
	else
		strbuf_addf(tmp, "refs/heads/%s:refs/remotes/%s/%s",
				branchname, remotename, branchname);
	git_config_set_multivar(key, tmp->buf, "^$", 0);
}

static const char mirror_advice[] =
N_("--mirror is dangerous and deprecated; please\n"
   "\t use --mirror=fetch or --mirror=push instead");

static int parse_mirror_opt(const struct option *opt, const char *arg, int not)
{
	unsigned *mirror = opt->value;
	if (not)
		*mirror = MIRROR_NONE;
	else if (!arg) {
		warning("%s", _(mirror_advice));
		*mirror = MIRROR_BOTH;
	}
	else if (!strcmp(arg, "fetch"))
		*mirror = MIRROR_FETCH;
	else if (!strcmp(arg, "push"))
		*mirror = MIRROR_PUSH;
	else
		return error(_("unknown --mirror argument: %s"), arg);
	return 0;
}

static int add(int argc, const char **argv, const char *prefix,
	       struct repository *repo UNUSED)
{
	int fetch = 0, fetch_tags = TAGS_DEFAULT;
	unsigned mirror = MIRROR_NONE;
	struct string_list track = STRING_LIST_INIT_NODUP;
	const char *master = NULL;
	struct remote *remote;
	struct strbuf buf = STRBUF_INIT, buf2 = STRBUF_INIT;
	const char *name, *url;
	int i;
	int result = 0;

	struct option options[] = {
		OPT_BOOL('f', "fetch", &fetch, N_("fetch the remote branches")),
		OPT_SET_INT(0, "tags", &fetch_tags,
			    N_("import all tags and associated objects when fetching\n"
			       "or do not fetch any tag at all (--no-tags)"),
			    TAGS_SET),
		OPT_STRING_LIST('t', "track", &track, N_("branch"),
				N_("branch(es) to track")),
		OPT_STRING('m', "master", &master, N_("branch"), N_("master branch")),
		OPT_CALLBACK_F(0, "mirror", &mirror, "(push|fetch)",
			N_("set up remote as a mirror to push to or fetch from"),
			PARSE_OPT_OPTARG | PARSE_OPT_COMP_ARG, parse_mirror_opt),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_add_usage, 0);

	if (argc != 2)
		usage_with_options(builtin_remote_add_usage, options);

	if (mirror && master)
		die(_("specifying a master branch makes no sense with --mirror"));
	if (mirror && !(mirror & MIRROR_FETCH) && track.nr)
		die(_("specifying branches to track makes sense only with fetch mirrors"));

	name = argv[0];
	url = argv[1];

	remote = remote_get(name);
	if (remote_is_configured(remote, 1)) {
		error(_("remote %s already exists."), name);
		exit(3);
	}

	if (!valid_remote_name(name))
		die(_("'%s' is not a valid remote name"), name);

	strbuf_addf(&buf, "remote.%s.url", name);
	git_config_set(buf.buf, url);

	if (!mirror || mirror & MIRROR_FETCH) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "remote.%s.fetch", name);
		if (track.nr == 0)
			string_list_append(&track, "*");
		for (i = 0; i < track.nr; i++) {
			add_branch(buf.buf, track.items[i].string,
				   name, mirror, &buf2);
		}
	}

	if (mirror & MIRROR_PUSH) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "remote.%s.mirror", name);
		git_config_set(buf.buf, "true");
	}

	if (fetch_tags != TAGS_DEFAULT) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "remote.%s.tagOpt", name);
		git_config_set(buf.buf,
			       fetch_tags == TAGS_SET ? "--tags" : "--no-tags");
	}

	if (fetch && fetch_remote(name)) {
		result = 1;
		goto out;
	}

	if (master) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/remotes/%s/HEAD", name);

		strbuf_reset(&buf2);
		strbuf_addf(&buf2, "refs/remotes/%s/%s", name, master);

		if (refs_update_symref(get_main_ref_store(the_repository), buf.buf, buf2.buf, "remote add"))
			result = error(_("Could not setup master '%s'"), master);
	}

out:
	strbuf_release(&buf);
	strbuf_release(&buf2);
	string_list_clear(&track, 0);

	return result;
}

struct branch_info {
	char *remote_name;
	struct string_list merge;
	enum rebase_type rebase;
	char *push_remote_name;
};

static struct string_list branch_list = STRING_LIST_INIT_DUP;

static const char *abbrev_ref(const char *name, const char *prefix)
{
	skip_prefix(name, prefix, &name);
	return name;
}
#define abbrev_branch(name) abbrev_ref((name), "refs/heads/")

static int config_read_branches(const char *key, const char *value,
				const struct config_context *ctx UNUSED,
				void *data UNUSED)
{
	const char *orig_key = key;
	char *name;
	struct string_list_item *item;
	struct branch_info *info;
	enum { REMOTE, MERGE, REBASE, PUSH_REMOTE } type;
	size_t key_len;

	if (!starts_with(key, "branch."))
		return 0;

	key += strlen("branch.");
	if (strip_suffix(key, ".remote", &key_len))
		type = REMOTE;
	else if (strip_suffix(key, ".merge", &key_len))
		type = MERGE;
	else if (strip_suffix(key, ".rebase", &key_len))
		type = REBASE;
	else if (strip_suffix(key, ".pushremote", &key_len))
		type = PUSH_REMOTE;
	else
		return 0;

	name = xmemdupz(key, key_len);
	item = string_list_insert(&branch_list, name);

	if (!item->util)
		item->util = xcalloc(1, sizeof(struct branch_info));
	info = item->util;
	switch (type) {
	case REMOTE:
		if (info->remote_name)
			warning(_("more than one %s"), orig_key);
		info->remote_name = xstrdup(value);
		break;
	case MERGE: {
		char *space = strchr(value, ' ');
		value = abbrev_branch(value);
		while (space) {
			char *merge;
			merge = xstrndup(value, space - value);
			string_list_append(&info->merge, merge);
			value = abbrev_branch(space + 1);
			space = strchr(value, ' ');
		}
		string_list_append(&info->merge, xstrdup(value));
		break;
	}
	case REBASE:
		/*
		 * Consider invalid values as false and check the
		 * truth value with >= REBASE_TRUE.
		 */
		info->rebase = rebase_parse_value(value);
		if (info->rebase == REBASE_INVALID)
			warning(_("unhandled branch.%s.rebase=%s; assuming "
				  "'true'"), name, value);
		break;
	case PUSH_REMOTE:
		if (info->push_remote_name)
			warning(_("more than one %s"), orig_key);
		info->push_remote_name = xstrdup(value);
		break;
	default:
		BUG("unexpected type=%d", type);
	}

	free(name);
	return 0;
}

static void read_branches(void)
{
	if (branch_list.nr)
		return;
	git_config(config_read_branches, NULL);
}

struct ref_states {
	struct remote *remote;
	struct string_list new_refs, skipped, stale, tracked, heads, push;
	int queried;
};

#define REF_STATES_INIT { \
	.new_refs = STRING_LIST_INIT_DUP, \
	.skipped = STRING_LIST_INIT_DUP, \
	.stale = STRING_LIST_INIT_DUP, \
	.tracked = STRING_LIST_INIT_DUP, \
	.heads = STRING_LIST_INIT_DUP, \
	.push = STRING_LIST_INIT_DUP, \
}

static int get_ref_states(const struct ref *remote_refs, struct ref_states *states)
{
	struct ref *fetch_map = NULL, **tail = &fetch_map;
	struct ref *ref, *stale_refs;
	int i;

	for (i = 0; i < states->remote->fetch.nr; i++)
		if (get_fetch_map(remote_refs, &states->remote->fetch.items[i], &tail, 1))
			die(_("Could not get fetch map for refspec %s"),
				states->remote->fetch.items[i].raw);

	for (ref = fetch_map; ref; ref = ref->next) {
		if (refname_matches_negative_refspec_item(ref->name, &states->remote->fetch))
			string_list_append(&states->skipped, abbrev_branch(ref->name));
		else if (!ref->peer_ref || !refs_ref_exists(get_main_ref_store(the_repository), ref->peer_ref->name))
			string_list_append(&states->new_refs, abbrev_branch(ref->name));
		else
			string_list_append(&states->tracked, abbrev_branch(ref->name));
	}
	stale_refs = get_stale_heads(&states->remote->fetch, fetch_map);
	for (ref = stale_refs; ref; ref = ref->next) {
		struct string_list_item *item =
			string_list_append(&states->stale, abbrev_branch(ref->name));
		item->util = xstrdup(ref->name);
	}
	free_refs(stale_refs);
	free_refs(fetch_map);

	string_list_sort(&states->new_refs);
	string_list_sort(&states->skipped);
	string_list_sort(&states->tracked);
	string_list_sort(&states->stale);

	return 0;
}

struct push_info {
	char *dest;
	int forced;
	enum {
		PUSH_STATUS_CREATE = 0,
		PUSH_STATUS_DELETE,
		PUSH_STATUS_UPTODATE,
		PUSH_STATUS_FASTFORWARD,
		PUSH_STATUS_OUTOFDATE,
		PUSH_STATUS_NOTQUERIED
	} status;
};

static int get_push_ref_states(const struct ref *remote_refs,
	struct ref_states *states)
{
	struct remote *remote = states->remote;
	struct ref *ref, *local_refs, *push_map;
	if (remote->mirror)
		return 0;

	local_refs = get_local_heads();
	push_map = copy_ref_list(remote_refs);

	match_push_refs(local_refs, &push_map, &remote->push, MATCH_REFS_NONE);

	for (ref = push_map; ref; ref = ref->next) {
		struct string_list_item *item;
		struct push_info *info;

		if (!ref->peer_ref)
			continue;
		oidcpy(&ref->new_oid, &ref->peer_ref->new_oid);

		item = string_list_append(&states->push,
					  abbrev_branch(ref->peer_ref->name));
		item->util = xcalloc(1, sizeof(struct push_info));
		info = item->util;
		info->forced = ref->force;
		info->dest = xstrdup(abbrev_branch(ref->name));

		if (is_null_oid(&ref->new_oid)) {
			info->status = PUSH_STATUS_DELETE;
		} else if (oideq(&ref->old_oid, &ref->new_oid))
			info->status = PUSH_STATUS_UPTODATE;
		else if (is_null_oid(&ref->old_oid))
			info->status = PUSH_STATUS_CREATE;
		else if (odb_has_object(the_repository->objects, &ref->old_oid,
					HAS_OBJECT_RECHECK_PACKED | HAS_OBJECT_FETCH_PROMISOR) &&
			 ref_newer(&ref->new_oid, &ref->old_oid))
			info->status = PUSH_STATUS_FASTFORWARD;
		else
			info->status = PUSH_STATUS_OUTOFDATE;
	}
	free_refs(local_refs);
	free_refs(push_map);
	return 0;
}

static int get_push_ref_states_noquery(struct ref_states *states)
{
	int i;
	struct remote *remote = states->remote;
	struct string_list_item *item;
	struct push_info *info;

	if (remote->mirror)
		return 0;

	if (!remote->push.nr) {
		item = string_list_append(&states->push, _("(matching)"));
		info = item->util = xcalloc(1, sizeof(struct push_info));
		info->status = PUSH_STATUS_NOTQUERIED;
		info->dest = xstrdup(item->string);
	}
	for (i = 0; i < remote->push.nr; i++) {
		const struct refspec_item *spec = &remote->push.items[i];
		if (spec->matching)
			item = string_list_append(&states->push, _("(matching)"));
		else if (strlen(spec->src))
			item = string_list_append(&states->push, spec->src);
		else
			item = string_list_append(&states->push, _("(delete)"));

		info = item->util = xcalloc(1, sizeof(struct push_info));
		info->forced = spec->force;
		info->status = PUSH_STATUS_NOTQUERIED;
		info->dest = xstrdup(spec->dst ? spec->dst : item->string);
	}
	return 0;
}

static int get_head_names(const struct ref *remote_refs, struct ref_states *states)
{
	struct ref *ref, *matches;
	struct ref *fetch_map = NULL, **fetch_map_tail = &fetch_map;
	struct refspec_item refspec = {
		.force = 0,
		.pattern = 1,
		.src = (char *) "refs/heads/*",
		.dst = (char *) "refs/heads/*",
	};

	get_fetch_map(remote_refs, &refspec, &fetch_map_tail, 0);
	matches = guess_remote_head(find_ref_by_name(remote_refs, "HEAD"),
				    fetch_map, REMOTE_GUESS_HEAD_ALL);
	for (ref = matches; ref; ref = ref->next)
		string_list_append(&states->heads, abbrev_branch(ref->name));

	free_refs(fetch_map);
	free_refs(matches);
	return 0;
}

struct known_remote {
	struct known_remote *next;
	struct remote *remote;
};

struct known_remotes {
	struct remote *to_delete;
	struct known_remote *list;
};

static int add_known_remote(struct remote *remote, void *cb_data)
{
	struct known_remotes *all = cb_data;
	struct known_remote *r;

	if (!strcmp(all->to_delete->name, remote->name))
		return 0;

	r = xmalloc(sizeof(*r));
	r->remote = remote;
	r->next = all->list;
	all->list = r;
	return 0;
}

struct branches_for_remote {
	struct remote *remote;
	struct string_list *branches, *skipped;
	struct known_remotes *keep;
};

static int add_branch_for_removal(const char *refname,
				  const char *referent UNUSED,
				  const struct object_id *oid UNUSED,
				  int flags UNUSED, void *cb_data)
{
	struct branches_for_remote *branches = cb_data;
	struct refspec_item refspec;
	struct known_remote *kr;

	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (remote_find_tracking(branches->remote, &refspec))
		return 0;
	free(refspec.src);

	/* don't delete a branch if another remote also uses it */
	for (kr = branches->keep->list; kr; kr = kr->next) {
		memset(&refspec, 0, sizeof(refspec));
		refspec.dst = (char *)refname;
		if (!remote_find_tracking(kr->remote, &refspec)) {
			free(refspec.src);
			return 0;
		}
	}

	/* don't delete non-remote-tracking refs */
	if (!starts_with(refname, "refs/remotes/")) {
		/* advise user how to delete local branches */
		if (starts_with(refname, "refs/heads/"))
			string_list_append(branches->skipped,
					   abbrev_branch(refname));
		/* silently skip over other non-remote refs */
		return 0;
	}

	string_list_append(branches->branches, refname);

	return 0;
}

struct rename_info {
	const char *old_name;
	const char *new_name;
	struct string_list *remote_branches;
	uint32_t symrefs_nr;
};

static int read_remote_branches(const char *refname, const char *referent UNUSED,
				const struct object_id *oid UNUSED,
				int flags UNUSED, void *cb_data)
{
	struct rename_info *rename = cb_data;
	struct strbuf buf = STRBUF_INIT;
	struct string_list_item *item;
	int flag;
	const char *symref;

	strbuf_addf(&buf, "refs/remotes/%s/", rename->old_name);
	if (starts_with(refname, buf.buf)) {
		item = string_list_append(rename->remote_branches, refname);
		symref = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
						 refname, RESOLVE_REF_READING,
						 NULL, &flag);
		if (symref && (flag & REF_ISSYMREF)) {
			item->util = xstrdup(symref);
			rename->symrefs_nr++;
		} else {
			item->util = NULL;
		}
	}
	strbuf_release(&buf);

	return 0;
}

static int migrate_file(struct remote *remote)
{
	struct strbuf buf = STRBUF_INIT;
	int i;

	strbuf_addf(&buf, "remote.%s.url", remote->name);
	for (i = 0; i < remote->url.nr; i++)
		git_config_set_multivar(buf.buf, remote->url.v[i], "^$", 0);
	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.push", remote->name);
	for (i = 0; i < remote->push.nr; i++)
		git_config_set_multivar(buf.buf, remote->push.items[i].raw, "^$", 0);
	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.fetch", remote->name);
	for (i = 0; i < remote->fetch.nr; i++)
		git_config_set_multivar(buf.buf, remote->fetch.items[i].raw, "^$", 0);
#ifndef WITH_BREAKING_CHANGES
	if (remote->origin == REMOTE_REMOTES)
		unlink_or_warn(repo_git_path_replace(the_repository, &buf,
						     "remotes/%s", remote->name));
	else if (remote->origin == REMOTE_BRANCHES)
		unlink_or_warn(repo_git_path_replace(the_repository, &buf,
						     "branches/%s", remote->name));
#endif /* WITH_BREAKING_CHANGES */
	strbuf_release(&buf);

	return 0;
}

struct push_default_info
{
	const char *old_name;
	enum config_scope scope;
	struct strbuf origin;
	int linenr;
};

static int config_read_push_default(const char *key, const char *value,
	const struct config_context *ctx, void *cb)
{
	const struct key_value_info *kvi = ctx->kvi;

	struct push_default_info* info = cb;
	if (strcmp(key, "remote.pushdefault") ||
	    !value || strcmp(value, info->old_name))
		return 0;

	info->scope = kvi->scope;
	strbuf_reset(&info->origin);
	strbuf_addstr(&info->origin, config_origin_type_name(kvi->origin_type));
	info->linenr = kvi->linenr;

	return 0;
}

static void handle_push_default(const char* old_name, const char* new_name)
{
	struct push_default_info push_default = {
		.old_name = old_name,
		.scope = CONFIG_SCOPE_UNKNOWN,
		.origin = STRBUF_INIT,
		.linenr = -1,
	};
	git_config(config_read_push_default, &push_default);
	if (push_default.scope >= CONFIG_SCOPE_COMMAND)
		; /* pass */
	else if (push_default.scope >= CONFIG_SCOPE_LOCAL) {
		int result = git_config_set_gently("remote.pushDefault",
						   new_name);
		if (new_name && result && result != CONFIG_NOTHING_SET)
			die(_("could not set '%s'"), "remote.pushDefault");
		else if (!new_name && result && result != CONFIG_NOTHING_SET)
			die(_("could not unset '%s'"), "remote.pushDefault");
	} else if (push_default.scope >= CONFIG_SCOPE_SYSTEM) {
		/* warn */
		warning(_("The %s configuration remote.pushDefault in:\n"
			  "\t%s:%d\n"
			  "now names the non-existent remote '%s'"),
			config_scope_name(push_default.scope),
			push_default.origin.buf, push_default.linenr,
			old_name);
	}

	strbuf_release(&push_default.origin);
}


static int mv(int argc, const char **argv, const char *prefix,
	      struct repository *repo UNUSED)
{
	int show_progress = isatty(2);
	struct option options[] = {
		OPT_BOOL(0, "progress", &show_progress, N_("force progress reporting")),
		OPT_END()
	};
	struct remote *oldremote, *newremote;
	struct strbuf buf = STRBUF_INIT, buf2 = STRBUF_INIT, buf3 = STRBUF_INIT,
		old_remote_context = STRBUF_INIT;
	struct string_list remote_branches = STRING_LIST_INIT_DUP;
	struct rename_info rename;
	int i, refs_renamed_nr = 0, refspec_updated = 0;
	struct progress *progress = NULL;
	int result = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_rename_usage, 0);

	if (argc != 2)
		usage_with_options(builtin_remote_rename_usage, options);

	rename.old_name = argv[0];
	rename.new_name = argv[1];
	rename.remote_branches = &remote_branches;
	rename.symrefs_nr = 0;

	oldremote = remote_get(rename.old_name);
	if (!remote_is_configured(oldremote, 1)) {
		error(_("No such remote: '%s'"), rename.old_name);
		exit(2);
	}

	if (!strcmp(rename.old_name, rename.new_name) && oldremote->origin != REMOTE_CONFIG)
		return migrate_file(oldremote);

	newremote = remote_get(rename.new_name);
	if (remote_is_configured(newremote, 1)) {
		error(_("remote %s already exists."), rename.new_name);
		exit(3);
	}

	if (!valid_remote_name(rename.new_name))
		die(_("'%s' is not a valid remote name"), rename.new_name);

	strbuf_addf(&buf, "remote.%s", rename.old_name);
	strbuf_addf(&buf2, "remote.%s", rename.new_name);
	if (repo_config_rename_section(the_repository, buf.buf, buf2.buf) < 1) {
		result = error(_("Could not rename config section '%s' to '%s'"),
			       buf.buf, buf2.buf);
		goto out;
	}

	if (oldremote->fetch.nr) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "remote.%s.fetch", rename.new_name);
		git_config_set_multivar(buf.buf, NULL, NULL, CONFIG_FLAGS_MULTI_REPLACE);
		strbuf_addf(&old_remote_context, ":refs/remotes/%s/", rename.old_name);
		for (i = 0; i < oldremote->fetch.nr; i++) {
			char *ptr;

			strbuf_reset(&buf2);
			strbuf_addstr(&buf2, oldremote->fetch.items[i].raw);
			ptr = strstr(buf2.buf, old_remote_context.buf);
			if (ptr) {
				refspec_updated = 1;
				strbuf_splice(&buf2,
					      ptr-buf2.buf + strlen(":refs/remotes/"),
					      strlen(rename.old_name), rename.new_name,
					      strlen(rename.new_name));
			} else
				warning(_("Not updating non-default fetch refspec\n"
					  "\t%s\n"
					  "\tPlease update the configuration manually if necessary."),
					buf2.buf);

			git_config_set_multivar(buf.buf, buf2.buf, "^$", 0);
		}
	}

	read_branches();
	for (i = 0; i < branch_list.nr; i++) {
		struct string_list_item *item = branch_list.items + i;
		struct branch_info *info = item->util;
		if (info->remote_name && !strcmp(info->remote_name, rename.old_name)) {
			strbuf_reset(&buf);
			strbuf_addf(&buf, "branch.%s.remote", item->string);
			git_config_set(buf.buf, rename.new_name);
		}
		if (info->push_remote_name && !strcmp(info->push_remote_name, rename.old_name)) {
			strbuf_reset(&buf);
			strbuf_addf(&buf, "branch.%s.pushRemote", item->string);
			git_config_set(buf.buf, rename.new_name);
		}
	}

	if (!refspec_updated)
		goto out;

	/*
	 * First remove symrefs, then rename the rest, finally create
	 * the new symrefs.
	 */
	refs_for_each_ref(get_main_ref_store(the_repository),
			  read_remote_branches, &rename);
	if (show_progress) {
		/*
		 * Count symrefs twice, since "renaming" them is done by
		 * deleting and recreating them in two separate passes.
		 */
		progress = start_progress(the_repository,
					  _("Renaming remote references"),
					  rename.remote_branches->nr + rename.symrefs_nr);
	}
	for (i = 0; i < remote_branches.nr; i++) {
		struct string_list_item *item = remote_branches.items + i;
		struct strbuf referent = STRBUF_INIT;

		if (refs_read_symbolic_ref(get_main_ref_store(the_repository), item->string,
					   &referent))
			continue;
		if (refs_delete_ref(get_main_ref_store(the_repository), NULL, item->string, NULL, REF_NO_DEREF))
			die(_("deleting '%s' failed"), item->string);

		strbuf_release(&referent);
		display_progress(progress, ++refs_renamed_nr);
	}
	for (i = 0; i < remote_branches.nr; i++) {
		struct string_list_item *item = remote_branches.items + i;

		if (item->util)
			continue;
		strbuf_reset(&buf);
		strbuf_addstr(&buf, item->string);
		strbuf_splice(&buf, strlen("refs/remotes/"), strlen(rename.old_name),
				rename.new_name, strlen(rename.new_name));
		strbuf_reset(&buf2);
		strbuf_addf(&buf2, "remote: renamed %s to %s",
				item->string, buf.buf);
		if (refs_rename_ref(get_main_ref_store(the_repository), item->string, buf.buf, buf2.buf))
			die(_("renaming '%s' failed"), item->string);
		display_progress(progress, ++refs_renamed_nr);
	}
	for (i = 0; i < remote_branches.nr; i++) {
		struct string_list_item *item = remote_branches.items + i;

		if (!item->util)
			continue;
		strbuf_reset(&buf);
		strbuf_addstr(&buf, item->string);
		strbuf_splice(&buf, strlen("refs/remotes/"), strlen(rename.old_name),
				rename.new_name, strlen(rename.new_name));
		strbuf_reset(&buf2);
		strbuf_addstr(&buf2, item->util);
		strbuf_splice(&buf2, strlen("refs/remotes/"), strlen(rename.old_name),
				rename.new_name, strlen(rename.new_name));
		strbuf_reset(&buf3);
		strbuf_addf(&buf3, "remote: renamed %s to %s",
				item->string, buf.buf);
		if (refs_update_symref(get_main_ref_store(the_repository), buf.buf, buf2.buf, buf3.buf))
			die(_("creating '%s' failed"), buf.buf);
		display_progress(progress, ++refs_renamed_nr);
	}
	stop_progress(&progress);

	handle_push_default(rename.old_name, rename.new_name);

out:
	string_list_clear(&remote_branches, 1);
	strbuf_release(&old_remote_context);
	strbuf_release(&buf);
	strbuf_release(&buf2);
	strbuf_release(&buf3);
	return result;
}

static int rm(int argc, const char **argv, const char *prefix,
	      struct repository *repo UNUSED)
{
	struct option options[] = {
		OPT_END()
	};
	struct remote *remote;
	struct strbuf buf = STRBUF_INIT;
	struct known_remotes known_remotes = { NULL, NULL };
	struct string_list branches = STRING_LIST_INIT_DUP;
	struct string_list skipped = STRING_LIST_INIT_DUP;
	struct branches_for_remote cb_data;
	int i, result;

	memset(&cb_data, 0, sizeof(cb_data));
	cb_data.branches = &branches;
	cb_data.skipped = &skipped;
	cb_data.keep = &known_remotes;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_rm_usage, 0);
	if (argc != 1)
		usage_with_options(builtin_remote_rm_usage, options);

	remote = remote_get(argv[0]);
	if (!remote_is_configured(remote, 1)) {
		error(_("No such remote: '%s'"), argv[0]);
		exit(2);
	}

	known_remotes.to_delete = remote;
	for_each_remote(add_known_remote, &known_remotes);

	read_branches();
	for (i = 0; i < branch_list.nr; i++) {
		struct string_list_item *item = branch_list.items + i;
		struct branch_info *info = item->util;
		if (info->remote_name && !strcmp(info->remote_name, remote->name)) {
			const char *keys[] = { "remote", "merge", NULL }, **k;
			for (k = keys; *k; k++) {
				strbuf_reset(&buf);
				strbuf_addf(&buf, "branch.%s.%s",
						item->string, *k);
				result = git_config_set_gently(buf.buf, NULL);
				if (result && result != CONFIG_NOTHING_SET)
					die(_("could not unset '%s'"), buf.buf);
			}
		}
		if (info->push_remote_name && !strcmp(info->push_remote_name, remote->name)) {
			strbuf_reset(&buf);
			strbuf_addf(&buf, "branch.%s.pushremote", item->string);
			result = git_config_set_gently(buf.buf, NULL);
			if (result && result != CONFIG_NOTHING_SET)
				die(_("could not unset '%s'"), buf.buf);
		}
	}

	/*
	 * We cannot just pass a function to for_each_ref() which deletes
	 * the branches one by one, since for_each_ref() relies on cached
	 * refs, which are invalidated when deleting a branch.
	 */
	cb_data.remote = remote;
	result = refs_for_each_ref(get_main_ref_store(the_repository),
				   add_branch_for_removal, &cb_data);
	strbuf_release(&buf);

	if (!result)
		result = refs_delete_refs(get_main_ref_store(the_repository),
					  "remote: remove", &branches,
					  REF_NO_DEREF);
	string_list_clear(&branches, 0);

	if (skipped.nr) {
		fprintf_ln(stderr,
			   Q_("Note: A branch outside the refs/remotes/ hierarchy was not removed;\n"
			      "to delete it, use:",
			      "Note: Some branches outside the refs/remotes/ hierarchy were not removed;\n"
			      "to delete them, use:",
			      skipped.nr));
		for (i = 0; i < skipped.nr; i++)
			fprintf(stderr, "  git branch -d %s\n",
				skipped.items[i].string);
	}
	string_list_clear(&skipped, 0);

	if (!result) {
		strbuf_addf(&buf, "remote.%s", remote->name);
		if (repo_config_rename_section(the_repository, buf.buf, NULL) < 1) {
			result = error(_("Could not remove config section '%s'"), buf.buf);
			goto out;
		}

		handle_push_default(remote->name, NULL);
	}

out:
	for (struct known_remote *r = known_remotes.list; r;) {
		struct known_remote *next = r->next;
		free(r);
		r = next;
	}
	strbuf_release(&buf);
	return result;
}

static void clear_push_info(void *util, const char *string UNUSED)
{
	struct push_info *info = util;
	free(info->dest);
	free(info);
}

static void free_remote_ref_states(struct ref_states *states)
{
	string_list_clear(&states->new_refs, 0);
	string_list_clear(&states->skipped, 0);
	string_list_clear(&states->stale, 1);
	string_list_clear(&states->tracked, 0);
	string_list_clear(&states->heads, 0);
	string_list_clear_func(&states->push, clear_push_info);
}

static int append_ref_to_tracked_list(const char *refname,
				      const char *referent UNUSED,
				      const struct object_id *oid UNUSED,
				      int flags, void *cb_data)
{
	struct ref_states *states = cb_data;
	struct refspec_item refspec;

	if (flags & REF_ISSYMREF)
		return 0;

	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (!remote_find_tracking(states->remote, &refspec)) {
		string_list_append(&states->tracked, abbrev_branch(refspec.src));
		free(refspec.src);
	}

	return 0;
}

static int get_remote_ref_states(const char *name,
				 struct ref_states *states,
				 int query)
{
	states->remote = remote_get(name);
	if (!states->remote)
		return error(_("No such remote: '%s'"), name);

	read_branches();

	if (query) {
		struct transport *transport;
		const struct ref *remote_refs;

		transport = transport_get(states->remote, states->remote->url.v[0]);
		remote_refs = transport_get_remote_refs(transport, NULL);

		states->queried = 1;
		if (query & GET_REF_STATES)
			get_ref_states(remote_refs, states);
		if (query & GET_HEAD_NAMES)
			get_head_names(remote_refs, states);
		if (query & GET_PUSH_REF_STATES)
			get_push_ref_states(remote_refs, states);
		transport_disconnect(transport);
	} else {
		refs_for_each_ref(get_main_ref_store(the_repository),
				  append_ref_to_tracked_list, states);
		string_list_sort(&states->tracked);
		get_push_ref_states_noquery(states);
	}

	return 0;
}

struct show_info {
	struct string_list list;
	struct ref_states states;
	int width, width2;
	int any_rebase;
};

#define SHOW_INFO_INIT { \
	.list = STRING_LIST_INIT_DUP, \
	.states = REF_STATES_INIT, \
}

static int add_remote_to_show_info(struct string_list_item *item, void *cb_data)
{
	struct show_info *info = cb_data;
	int n = strlen(item->string);
	if (n > info->width)
		info->width = n;
	string_list_insert(&info->list, item->string);
	return 0;
}

static int show_remote_info_item(struct string_list_item *item, void *cb_data)
{
	struct show_info *info = cb_data;
	struct ref_states *states = &info->states;
	const char *name = item->string;

	if (states->queried) {
		const char *fmt = "%s";
		const char *arg = "";
		if (string_list_has_string(&states->new_refs, name)) {
			fmt = _(" new (next fetch will store in remotes/%s)");
			arg = states->remote->name;
		} else if (string_list_has_string(&states->tracked, name))
			arg = _(" tracked");
		else if (string_list_has_string(&states->skipped, name))
			arg = _(" skipped");
		else if (string_list_has_string(&states->stale, name))
			arg = _(" stale (use 'git remote prune' to remove)");
		else
			arg = _(" ???");
		printf("    %-*s", info->width, name);
		printf(fmt, arg);
		printf("\n");
	} else
		printf("    %s\n", name);

	return 0;
}

static int add_local_to_show_info(struct string_list_item *branch_item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct ref_states *states = &show_info->states;
	struct branch_info *branch_info = branch_item->util;
	struct string_list_item *item;
	int n;

	if (!branch_info->merge.nr || !branch_info->remote_name ||
	    strcmp(states->remote->name, branch_info->remote_name))
		return 0;
	if ((n = strlen(branch_item->string)) > show_info->width)
		show_info->width = n;
	if (branch_info->rebase >= REBASE_TRUE)
		show_info->any_rebase = 1;

	item = string_list_insert(&show_info->list, branch_item->string);
	item->util = branch_info;

	return 0;
}

static int show_local_info_item(struct string_list_item *item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct branch_info *branch_info = item->util;
	struct string_list *merge = &branch_info->merge;
	int width = show_info->width + 4;
	int i;

	if (branch_info->rebase >= REBASE_TRUE && branch_info->merge.nr > 1) {
		error(_("invalid branch.%s.merge; cannot rebase onto > 1 branch"),
			item->string);
		return 0;
	}

	printf("    %-*s ", show_info->width, item->string);
	if (branch_info->rebase >= REBASE_TRUE) {
		const char *msg;
		if (branch_info->rebase == REBASE_INTERACTIVE)
			msg = _("rebases interactively onto remote %s");
		else if (branch_info->rebase == REBASE_MERGES)
			msg = _("rebases interactively (with merges) onto "
				"remote %s");
		else
			msg = _("rebases onto remote %s");
		printf_ln(msg, merge->items[0].string);
		return 0;
	} else if (show_info->any_rebase) {
		printf_ln(_(" merges with remote %s"), merge->items[0].string);
		width++;
	} else {
		printf_ln(_("merges with remote %s"), merge->items[0].string);
	}
	for (i = 1; i < merge->nr; i++)
		printf(_("%-*s    and with remote %s\n"), width, "",
		       merge->items[i].string);

	return 0;
}

static int add_push_to_show_info(struct string_list_item *push_item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct push_info *push_info = push_item->util;
	struct string_list_item *item;
	int n;
	if ((n = strlen(push_item->string)) > show_info->width)
		show_info->width = n;
	if ((n = strlen(push_info->dest)) > show_info->width2)
		show_info->width2 = n;
	item = string_list_append(&show_info->list, push_item->string);
	item->util = push_item->util;
	return 0;
}

/*
 * Sorting comparison for a string list that has push_info
 * structs in its util field
 */
static int cmp_string_with_push(const void *va, const void *vb)
{
	const struct string_list_item *a = va;
	const struct string_list_item *b = vb;
	const struct push_info *a_push = a->util;
	const struct push_info *b_push = b->util;
	int cmp = strcmp(a->string, b->string);
	return cmp ? cmp : strcmp(a_push->dest, b_push->dest);
}

static int show_push_info_item(struct string_list_item *item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct push_info *push_info = item->util;
	const char *src = item->string, *status = NULL;

	switch (push_info->status) {
	case PUSH_STATUS_CREATE:
		status = _("create");
		break;
	case PUSH_STATUS_DELETE:
		status = _("delete");
		src = _("(none)");
		break;
	case PUSH_STATUS_UPTODATE:
		status = _("up to date");
		break;
	case PUSH_STATUS_FASTFORWARD:
		status = _("fast-forwardable");
		break;
	case PUSH_STATUS_OUTOFDATE:
		status = _("local out of date");
		break;
	case PUSH_STATUS_NOTQUERIED:
		break;
	}
	if (status) {
		if (push_info->forced)
			printf_ln(_("    %-*s forces to %-*s (%s)"), show_info->width, src,
			       show_info->width2, push_info->dest, status);
		else
			printf_ln(_("    %-*s pushes to %-*s (%s)"), show_info->width, src,
			       show_info->width2, push_info->dest, status);
	} else {
		if (push_info->forced)
			printf_ln(_("    %-*s forces to %s"), show_info->width, src,
			       push_info->dest);
		else
			printf_ln(_("    %-*s pushes to %s"), show_info->width, src,
			       push_info->dest);
	}
	return 0;
}

static int get_one_entry(struct remote *remote, void *priv)
{
	struct string_list *list = priv;
	struct strbuf remote_info_buf = STRBUF_INIT;
	struct strvec *url;
	int i;

	if (remote->url.nr > 0) {
		struct strbuf promisor_config = STRBUF_INIT;
		const char *partial_clone_filter = NULL;

		strbuf_addf(&promisor_config, "remote.%s.partialclonefilter", remote->name);
		strbuf_addf(&remote_info_buf, "%s (fetch)", remote->url.v[0]);
		if (!git_config_get_string_tmp(promisor_config.buf, &partial_clone_filter))
			strbuf_addf(&remote_info_buf, " [%s]", partial_clone_filter);

		strbuf_release(&promisor_config);
		string_list_append(list, remote->name)->util =
				strbuf_detach(&remote_info_buf, NULL);
	} else
		string_list_append(list, remote->name)->util = NULL;
	url = push_url_of_remote(remote);
	for (i = 0; i < url->nr; i++)
	{
		strbuf_addf(&remote_info_buf, "%s (push)", url->v[i]);
		string_list_append(list, remote->name)->util =
				strbuf_detach(&remote_info_buf, NULL);
	}

	return 0;
}

static int show_all(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;
	int result;

	result = for_each_remote(get_one_entry, &list);

	if (!result) {
		int i;

		string_list_sort(&list);
		for (i = 0; i < list.nr; i++) {
			struct string_list_item *item = list.items + i;
			if (verbose)
				printf("%s\t%s\n", item->string,
					item->util ? (const char *)item->util : "");
			else {
				if (i && !strcmp((item - 1)->string, item->string))
					continue;
				printf("%s\n", item->string);
			}
		}
	}
	string_list_clear(&list, 1);
	return result;
}

static int show(int argc, const char **argv, const char *prefix,
		struct repository *repo UNUSED)
{
	int no_query = 0, result = 0, query_flag = 0;
	struct option options[] = {
		OPT_BOOL('n', NULL, &no_query, N_("do not query remotes")),
		OPT_END()
	};
	struct show_info info = SHOW_INFO_INIT;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_show_usage,
			     0);

	if (argc < 1)
		return show_all();

	if (!no_query)
		query_flag = (GET_REF_STATES | GET_HEAD_NAMES | GET_PUSH_REF_STATES);

	for (; argc; argc--, argv++) {
		int i;
		struct strvec *url;

		get_remote_ref_states(*argv, &info.states, query_flag);

		printf_ln(_("* remote %s"), *argv);
		printf_ln(_("  Fetch URL: %s"), info.states.remote->url.v[0]);
		url = push_url_of_remote(info.states.remote);
		for (i = 0; i < url->nr; i++)
			/*
			 * TRANSLATORS: the colon ':' should align
			 * with the one in " Fetch URL: %s"
			 * translation.
			 */
			printf_ln(_("  Push  URL: %s"), url->v[i]);
		if (!i)
			printf_ln(_("  Push  URL: %s"), _("(no URL)"));
		if (no_query)
			printf_ln(_("  HEAD branch: %s"), _("(not queried)"));
		else if (!info.states.heads.nr)
			printf_ln(_("  HEAD branch: %s"), _("(unknown)"));
		else if (info.states.heads.nr == 1)
			printf_ln(_("  HEAD branch: %s"), info.states.heads.items[0].string);
		else {
			printf(_("  HEAD branch (remote HEAD is ambiguous,"
				 " may be one of the following):\n"));
			for (i = 0; i < info.states.heads.nr; i++)
				printf("    %s\n", info.states.heads.items[i].string);
		}

		/* remote branch info */
		info.width = 0;
		for_each_string_list(&info.states.new_refs, add_remote_to_show_info, &info);
		for_each_string_list(&info.states.skipped, add_remote_to_show_info, &info);
		for_each_string_list(&info.states.tracked, add_remote_to_show_info, &info);
		for_each_string_list(&info.states.stale, add_remote_to_show_info, &info);
		if (info.list.nr)
			printf_ln(Q_("  Remote branch:%s",
				     "  Remote branches:%s",
				     info.list.nr),
				  no_query ? _(" (status not queried)") : "");
		for_each_string_list(&info.list, show_remote_info_item, &info);
		string_list_clear(&info.list, 0);

		/* git pull info */
		info.width = 0;
		info.any_rebase = 0;
		for_each_string_list(&branch_list, add_local_to_show_info, &info);
		if (info.list.nr)
			printf_ln(Q_("  Local branch configured for 'git pull':",
				     "  Local branches configured for 'git pull':",
				     info.list.nr));
		for_each_string_list(&info.list, show_local_info_item, &info);
		string_list_clear(&info.list, 0);

		/* git push info */
		if (info.states.remote->mirror)
			printf_ln(_("  Local refs will be mirrored by 'git push'"));

		info.width = info.width2 = 0;
		for_each_string_list(&info.states.push, add_push_to_show_info, &info);
		QSORT(info.list.items, info.list.nr, cmp_string_with_push);
		if (info.list.nr)
			printf_ln(Q_("  Local ref configured for 'git push'%s:",
				     "  Local refs configured for 'git push'%s:",
				     info.list.nr),
				  no_query ? _(" (status not queried)") : "");
		for_each_string_list(&info.list, show_push_info_item, &info);
		string_list_clear(&info.list, 0);

		free_remote_ref_states(&info.states);
	}

	return result;
}

static void report_set_head_auto(const char *remote, const char *head_name,
			struct strbuf *b_local_head, int was_detached) {
	struct strbuf buf_prefix = STRBUF_INIT;
	const char *prev_head = NULL;

	strbuf_addf(&buf_prefix, "refs/remotes/%s/", remote);
	skip_prefix(b_local_head->buf, buf_prefix.buf, &prev_head);

	if (prev_head && !strcmp(prev_head, head_name))
		printf(_("'%s/HEAD' is unchanged and points to '%s'\n"),
			remote, head_name);
	else if (prev_head)
		printf(_("'%s/HEAD' has changed from '%s' and now points to '%s'\n"),
			remote, prev_head, head_name);
	else if (!b_local_head->len)
		printf(_("'%s/HEAD' is now created and points to '%s'\n"),
			remote, head_name);
	else if (was_detached && b_local_head->len)
		printf(_("'%s/HEAD' was detached at '%s' and now points to '%s'\n"),
			remote, b_local_head->buf, head_name);
	else
		printf(_("'%s/HEAD' used to point to '%s' "
			"(which is not a remote branch), but now points to '%s'\n"),
			remote, b_local_head->buf, head_name);
	strbuf_release(&buf_prefix);
}

static int set_head(int argc, const char **argv, const char *prefix,
		    struct repository *repo UNUSED)
{
	int i, opt_a = 0, opt_d = 0, result = 0, was_detached;
	struct strbuf b_head = STRBUF_INIT, b_remote_head = STRBUF_INIT,
		b_local_head = STRBUF_INIT;
	char *head_name = NULL;
	struct ref_store *refs = get_main_ref_store(the_repository);
	struct remote *remote;

	struct option options[] = {
		OPT_BOOL('a', "auto", &opt_a,
			 N_("set refs/remotes/<name>/HEAD according to remote")),
		OPT_BOOL('d', "delete", &opt_d,
			 N_("delete refs/remotes/<name>/HEAD")),
		OPT_END()
	};
	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_sethead_usage, 0);
	if (argc) {
		strbuf_addf(&b_head, "refs/remotes/%s/HEAD", argv[0]);
		remote = remote_get(argv[0]);
	}

	if (!opt_a && !opt_d && argc == 2) {
		head_name = xstrdup(argv[1]);
	} else if (opt_a && !opt_d && argc == 1) {
		struct ref_states states = REF_STATES_INIT;
		get_remote_ref_states(argv[0], &states, GET_HEAD_NAMES);
		if (!states.heads.nr)
			result |= error(_("Cannot determine remote HEAD"));
		else if (states.heads.nr > 1) {
			result |= error(_("Multiple remote HEAD branches. "
					  "Please choose one explicitly with:"));
			for (i = 0; i < states.heads.nr; i++)
				fprintf(stderr, "  git remote set-head %s %s\n",
					argv[0], states.heads.items[i].string);
		} else
			head_name = xstrdup(states.heads.items[0].string);
		free_remote_ref_states(&states);
	} else if (opt_d && !opt_a && argc == 1) {
		if (refs_delete_ref(refs, NULL, b_head.buf, NULL, REF_NO_DEREF))
			result |= error(_("Could not delete %s"), b_head.buf);
	} else
		usage_with_options(builtin_remote_sethead_usage, options);

	if (!head_name)
		goto cleanup;
	strbuf_addf(&b_remote_head, "refs/remotes/%s/%s", argv[0], head_name);
	if (!refs_ref_exists(refs, b_remote_head.buf)) {
		result |= error(_("Not a valid ref: %s"), b_remote_head.buf);
		goto cleanup;
	}
	was_detached = refs_update_symref_extended(refs, b_head.buf, b_remote_head.buf,
			"remote set-head", &b_local_head, 0);
	if (was_detached == -1) {
		result |= error(_("Could not set up %s"), b_head.buf);
		goto cleanup;
	}
	if (opt_a)
		report_set_head_auto(argv[0], head_name, &b_local_head, was_detached);
	if (remote->follow_remote_head == FOLLOW_REMOTE_ALWAYS) {
		struct strbuf config_name = STRBUF_INIT;
		strbuf_addf(&config_name,
			"remote.%s.followremotehead", remote->name);
		git_config_set(config_name.buf, "warn");
		strbuf_release(&config_name);
	}

cleanup:
	free(head_name);
	strbuf_release(&b_head);
	strbuf_release(&b_remote_head);
	strbuf_release(&b_local_head);
	return result;
}

static int prune_remote(const char *remote, int dry_run)
{
	int result = 0;
	struct ref_states states = REF_STATES_INIT;
	struct string_list refs_to_prune = STRING_LIST_INIT_NODUP;
	struct string_list_item *item;
	const char *dangling_msg = dry_run
		? _(" %s will become dangling!")
		: _(" %s has become dangling!");

	get_remote_ref_states(remote, &states, GET_REF_STATES);

	if (!states.stale.nr) {
		free_remote_ref_states(&states);
		return 0;
	}

	printf_ln(_("Pruning %s"), remote);
	printf_ln(_("URL: %s"), states.remote->url.v[0]);

	for_each_string_list_item(item, &states.stale)
		string_list_append(&refs_to_prune, item->util);
	string_list_sort(&refs_to_prune);

	if (!dry_run)
		result |= refs_delete_refs(get_main_ref_store(the_repository),
					   "remote: prune", &refs_to_prune, 0);

	for_each_string_list_item(item, &states.stale) {
		const char *refname = item->util;

		if (dry_run)
			printf_ln(_(" * [would prune] %s"),
			       abbrev_ref(refname, "refs/remotes/"));
		else
			printf_ln(_(" * [pruned] %s"),
			       abbrev_ref(refname, "refs/remotes/"));
	}

	refs_warn_dangling_symrefs(get_main_ref_store(the_repository),
				   stdout, dangling_msg, &refs_to_prune);

	string_list_clear(&refs_to_prune, 0);
	free_remote_ref_states(&states);
	return result;
}

static int prune(int argc, const char **argv, const char *prefix,
		 struct repository *repo UNUSED)
{
	int dry_run = 0, result = 0;
	struct option options[] = {
		OPT__DRY_RUN(&dry_run, N_("dry run")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_prune_usage, 0);

	if (argc < 1)
		usage_with_options(builtin_remote_prune_usage, options);

	for (; argc; argc--, argv++)
		result |= prune_remote(*argv, dry_run);

	return result;
}

static int get_remote_default(const char *key, const char *value UNUSED,
			      const struct config_context *ctx UNUSED,
			      void *priv)
{
	if (strcmp(key, "remotes.default") == 0) {
		int *found = priv;
		*found = 1;
	}
	return 0;
}

static int update(int argc, const char **argv, const char *prefix,
		  struct repository *repo UNUSED)
{
	int i, prune = -1;
	struct option options[] = {
		OPT_BOOL('p', "prune", &prune,
			 N_("prune remotes after fetching")),
		OPT_END()
	};
	struct child_process cmd = CHILD_PROCESS_INIT;
	int default_defined = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_update_usage,
			     PARSE_OPT_KEEP_ARGV0);

	strvec_push(&cmd.args, "fetch");

	if (prune != -1)
		strvec_push(&cmd.args, prune ? "--prune" : "--no-prune");
	if (verbose)
		strvec_push(&cmd.args, "-v");
	strvec_push(&cmd.args, "--multiple");
	if (argc < 2)
		strvec_push(&cmd.args, "default");
	for (i = 1; i < argc; i++)
		strvec_push(&cmd.args, argv[i]);

	if (strcmp(cmd.args.v[cmd.args.nr-1], "default") == 0) {
		git_config(get_remote_default, &default_defined);
		if (!default_defined) {
			strvec_pop(&cmd.args);
			strvec_push(&cmd.args, "--all");
		}
	}

	cmd.git_cmd = 1;
	return run_command(&cmd);
}

static int remove_all_fetch_refspecs(const char *key)
{
	return git_config_set_multivar_gently(key, NULL, NULL,
					      CONFIG_FLAGS_MULTI_REPLACE);
}

static void add_branches(struct remote *remote, const char **branches,
			 const char *key)
{
	const char *remotename = remote->name;
	int mirror = remote->mirror;
	struct strbuf refspec = STRBUF_INIT;

	for (; *branches; branches++)
		add_branch(key, *branches, remotename, mirror, &refspec);

	strbuf_release(&refspec);
}

static int set_remote_branches(const char *remotename, const char **branches,
				int add_mode)
{
	struct strbuf key = STRBUF_INIT;
	struct remote *remote;

	strbuf_addf(&key, "remote.%s.fetch", remotename);

	remote = remote_get(remotename);
	if (!remote_is_configured(remote, 1)) {
		error(_("No such remote '%s'"), remotename);
		exit(2);
	}

	if (!add_mode && remove_all_fetch_refspecs(key.buf)) {
		strbuf_release(&key);
		return 1;
	}
	add_branches(remote, branches, key.buf);

	strbuf_release(&key);
	return 0;
}

static int set_branches(int argc, const char **argv, const char *prefix,
			struct repository *repo UNUSED)
{
	int add_mode = 0;
	struct option options[] = {
		OPT_BOOL('\0', "add", &add_mode, N_("add branch")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_setbranches_usage, 0);
	if (argc == 0) {
		error(_("no remote specified"));
		usage_with_options(builtin_remote_setbranches_usage, options);
	}
	argv[argc] = NULL;

	return set_remote_branches(argv[0], argv + 1, add_mode);
}

static int get_url(int argc, const char **argv, const char *prefix,
		   struct repository *repo UNUSED)
{
	int i, push_mode = 0, all_mode = 0;
	const char *remotename = NULL;
	struct remote *remote;
	struct strvec *url;
	struct option options[] = {
		OPT_BOOL('\0', "push", &push_mode,
			 N_("query push URLs rather than fetch URLs")),
		OPT_BOOL('\0', "all", &all_mode,
			 N_("return all URLs")),
		OPT_END()
	};
	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_geturl_usage, 0);

	if (argc != 1)
		usage_with_options(builtin_remote_geturl_usage, options);

	remotename = argv[0];

	remote = remote_get(remotename);
	if (!remote_is_configured(remote, 1)) {
		error(_("No such remote '%s'"), remotename);
		exit(2);
	}

	url = push_mode ? push_url_of_remote(remote) : &remote->url;

	if (all_mode) {
		for (i = 0; i < url->nr; i++)
			printf_ln("%s", url->v[i]);
	} else {
		printf_ln("%s", url->v[0]);
	}

	return 0;
}

static int set_url(int argc, const char **argv, const char *prefix,
		   struct repository *repo UNUSED)
{
	int i, push_mode = 0, add_mode = 0, delete_mode = 0;
	int matches = 0, negative_matches = 0;
	const char *remotename = NULL;
	const char *newurl = NULL;
	const char *oldurl = NULL;
	struct remote *remote;
	regex_t old_regex;
	struct strvec *urlset;
	struct strbuf name_buf = STRBUF_INIT;
	struct option options[] = {
		OPT_BOOL('\0', "push", &push_mode,
			 N_("manipulate push URLs")),
		OPT_BOOL('\0', "add", &add_mode,
			 N_("add URL")),
		OPT_BOOL('\0', "delete", &delete_mode,
			    N_("delete URLs")),
		OPT_END()
	};
	argc = parse_options(argc, argv, prefix, options,
			     builtin_remote_seturl_usage,
			     PARSE_OPT_KEEP_ARGV0);

	if (add_mode && delete_mode)
		die(_("--add --delete doesn't make sense"));

	if (argc < 3 || argc > 4 || ((add_mode || delete_mode) && argc != 3))
		usage_with_options(builtin_remote_seturl_usage, options);

	remotename = argv[1];
	newurl = argv[2];
	if (argc > 3)
		oldurl = argv[3];

	if (delete_mode)
		oldurl = newurl;

	remote = remote_get(remotename);
	if (!remote_is_configured(remote, 1)) {
		error(_("No such remote '%s'"), remotename);
		exit(2);
	}

	if (push_mode) {
		strbuf_addf(&name_buf, "remote.%s.pushurl", remotename);
		urlset = &remote->pushurl;
	} else {
		strbuf_addf(&name_buf, "remote.%s.url", remotename);
		urlset = &remote->url;
	}

	/* Special cases that add new entry. */
	if ((!oldurl && !delete_mode) || add_mode) {
		if (add_mode)
			git_config_set_multivar(name_buf.buf, newurl,
						       "^$", 0);
		else
			git_config_set(name_buf.buf, newurl);
		goto out;
	}

	/* Old URL specified. Demand that one matches. */
	if (regcomp(&old_regex, oldurl, REG_EXTENDED))
		die(_("Invalid old URL pattern: %s"), oldurl);

	for (i = 0; i < urlset->nr; i++)
		if (!regexec(&old_regex, urlset->v[i], 0, NULL, 0))
			matches++;
		else
			negative_matches++;
	if (!delete_mode && !matches)
		die(_("No such URL found: %s"), oldurl);
	if (delete_mode && !negative_matches && !push_mode)
		die(_("Will not delete all non-push URLs"));

	regfree(&old_regex);

	if (!delete_mode)
		git_config_set_multivar(name_buf.buf, newurl, oldurl, 0);
	else
		git_config_set_multivar(name_buf.buf, NULL, oldurl,
					CONFIG_FLAGS_MULTI_REPLACE);
out:
	strbuf_release(&name_buf);
	return 0;
}

int cmd_remote(int argc,
	       const char **argv,
	       const char *prefix,
	       struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT__VERBOSE(&verbose, N_("be verbose; must be placed before a subcommand")),
		OPT_SUBCOMMAND("add", &fn, add),
		OPT_SUBCOMMAND("rename", &fn, mv),
		OPT_SUBCOMMAND_F("rm", &fn, rm, PARSE_OPT_NOCOMPLETE),
		OPT_SUBCOMMAND("remove", &fn, rm),
		OPT_SUBCOMMAND("set-head", &fn, set_head),
		OPT_SUBCOMMAND("set-branches", &fn, set_branches),
		OPT_SUBCOMMAND("get-url", &fn, get_url),
		OPT_SUBCOMMAND("set-url", &fn, set_url),
		OPT_SUBCOMMAND("show", &fn, show),
		OPT_SUBCOMMAND("prune", &fn, prune),
		OPT_SUBCOMMAND("update", &fn, update),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, builtin_remote_usage,
			     PARSE_OPT_SUBCOMMAND_OPTIONAL);

	if (fn) {
		return !!fn(argc, argv, prefix, repo);
	} else {
		if (argc) {
			error(_("unknown subcommand: `%s'"), argv[0]);
			usage_with_options(builtin_remote_usage, options);
		}
		return !!show_all();
	}
}
