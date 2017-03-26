/*
 * "git fetch"
 */
#include "cache.h"
#include "refs.h"
#include "commit.h"
#include "builtin.h"
#include "string-list.h"
#include "remote.h"
#include "transport.h"
#include "run-command.h"
#include "parse-options.h"
#include "sigchain.h"
#include "submodule-config.h"
#include "submodule.h"
#include "connected.h"
#include "argv-array.h"
#include "utf8.h"

static const char * const builtin_fetch_usage[] = {
	N_("git fetch [<options>] [<repository> [<refspec>...]]"),
	N_("git fetch [<options>] <group>"),
	N_("git fetch --multiple [<options>] [(<repository> | <group>)...]"),
	N_("git fetch --all [<options>]"),
	NULL
};

enum {
	TAGS_UNSET = 0,
	TAGS_DEFAULT = 1,
	TAGS_SET = 2
};

static int fetch_prune_config = -1; /* unspecified */
static int prune = -1; /* unspecified */
#define PRUNE_BY_DEFAULT 0 /* do we prune by default? */

static int all, append, dry_run, force, keep, multiple, update_head_ok, verbosity, deepen_relative;
static int progress = -1, recurse_submodules = RECURSE_SUBMODULES_DEFAULT;
static int tags = TAGS_DEFAULT, unshallow, update_shallow, deepen;
static int max_children = -1;
static enum transport_family family;
static const char *depth;
static const char *deepen_since;
static const char *upload_pack;
static struct string_list deepen_not = STRING_LIST_INIT_NODUP;
static struct strbuf default_rla = STRBUF_INIT;
static struct transport *gtransport;
static struct transport *gsecondary;
static const char *submodule_prefix = "";
static const char *recurse_submodules_default;
static int shown_url = 0;
static int refmap_alloc, refmap_nr;
static const char **refmap_array;

static int option_parse_recurse_submodules(const struct option *opt,
				   const char *arg, int unset)
{
	if (unset) {
		recurse_submodules = RECURSE_SUBMODULES_OFF;
	} else {
		if (arg)
			recurse_submodules = parse_fetch_recurse_submodules_arg(opt->long_name, arg);
		else
			recurse_submodules = RECURSE_SUBMODULES_ON;
	}
	return 0;
}

static int git_fetch_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "fetch.prune")) {
		fetch_prune_config = git_config_bool(k, v);
		return 0;
	}
	return git_default_config(k, v, cb);
}

static int parse_refmap_arg(const struct option *opt, const char *arg, int unset)
{
	ALLOC_GROW(refmap_array, refmap_nr + 1, refmap_alloc);

	/*
	 * "git fetch --refmap='' origin foo"
	 * can be used to tell the command not to store anywhere
	 */
	if (*arg)
		refmap_array[refmap_nr++] = arg;
	return 0;
}

static struct option builtin_fetch_options[] = {
	OPT__VERBOSITY(&verbosity),
	OPT_BOOL(0, "all", &all,
		 N_("fetch from all remotes")),
	OPT_BOOL('a', "append", &append,
		 N_("append to .git/FETCH_HEAD instead of overwriting")),
	OPT_STRING(0, "upload-pack", &upload_pack, N_("path"),
		   N_("path to upload pack on remote end")),
	OPT__FORCE(&force, N_("force overwrite of local branch")),
	OPT_BOOL('m', "multiple", &multiple,
		 N_("fetch from multiple remotes")),
	OPT_SET_INT('t', "tags", &tags,
		    N_("fetch all tags and associated objects"), TAGS_SET),
	OPT_SET_INT('n', NULL, &tags,
		    N_("do not fetch all tags (--no-tags)"), TAGS_UNSET),
	OPT_INTEGER('j', "jobs", &max_children,
		    N_("number of submodules fetched in parallel")),
	OPT_BOOL('p', "prune", &prune,
		 N_("prune remote-tracking branches no longer on remote")),
	{ OPTION_CALLBACK, 0, "recurse-submodules", NULL, N_("on-demand"),
		    N_("control recursive fetching of submodules"),
		    PARSE_OPT_OPTARG, option_parse_recurse_submodules },
	OPT_BOOL(0, "dry-run", &dry_run,
		 N_("dry run")),
	OPT_BOOL('k', "keep", &keep, N_("keep downloaded pack")),
	OPT_BOOL('u', "update-head-ok", &update_head_ok,
		    N_("allow updating of HEAD ref")),
	OPT_BOOL(0, "progress", &progress, N_("force progress reporting")),
	OPT_STRING(0, "depth", &depth, N_("depth"),
		   N_("deepen history of shallow clone")),
	OPT_STRING(0, "shallow-since", &deepen_since, N_("time"),
		   N_("deepen history of shallow repository based on time")),
	OPT_STRING_LIST(0, "shallow-exclude", &deepen_not, N_("revision"),
			N_("deepen history of shallow clone, excluding rev")),
	OPT_INTEGER(0, "deepen", &deepen_relative,
		    N_("deepen history of shallow clone")),
	{ OPTION_SET_INT, 0, "unshallow", &unshallow, NULL,
		   N_("convert to a complete repository"),
		   PARSE_OPT_NONEG | PARSE_OPT_NOARG, NULL, 1 },
	{ OPTION_STRING, 0, "submodule-prefix", &submodule_prefix, N_("dir"),
		   N_("prepend this to submodule path output"), PARSE_OPT_HIDDEN },
	{ OPTION_STRING, 0, "recurse-submodules-default",
		   &recurse_submodules_default, NULL,
		   N_("default mode for recursion"), PARSE_OPT_HIDDEN },
	OPT_BOOL(0, "update-shallow", &update_shallow,
		 N_("accept refs that update .git/shallow")),
	{ OPTION_CALLBACK, 0, "refmap", NULL, N_("refmap"),
	  N_("specify fetch refmap"), PARSE_OPT_NONEG, parse_refmap_arg },
	OPT_SET_INT('4', "ipv4", &family, N_("use IPv4 addresses only"),
			TRANSPORT_FAMILY_IPV4),
	OPT_SET_INT('6', "ipv6", &family, N_("use IPv6 addresses only"),
			TRANSPORT_FAMILY_IPV6),
	OPT_END()
};

static void unlock_pack(void)
{
	if (gtransport)
		transport_unlock_pack(gtransport);
	if (gsecondary)
		transport_unlock_pack(gsecondary);
}

static void unlock_pack_on_signal(int signo)
{
	unlock_pack();
	sigchain_pop(signo);
	raise(signo);
}

static void add_merge_config(struct ref **head,
			   const struct ref *remote_refs,
		           struct branch *branch,
		           struct ref ***tail)
{
	int i;

	for (i = 0; i < branch->merge_nr; i++) {
		struct ref *rm, **old_tail = *tail;
		struct refspec refspec;

		for (rm = *head; rm; rm = rm->next) {
			if (branch_merge_matches(branch, i, rm->name)) {
				rm->fetch_head_status = FETCH_HEAD_MERGE;
				break;
			}
		}
		if (rm)
			continue;

		/*
		 * Not fetched to a remote-tracking branch?  We need to fetch
		 * it anyway to allow this branch's "branch.$name.merge"
		 * to be honored by 'git pull', but we do not have to
		 * fail if branch.$name.merge is misconfigured to point
		 * at a nonexisting branch.  If we were indeed called by
		 * 'git pull', it will notice the misconfiguration because
		 * there is no entry in the resulting FETCH_HEAD marked
		 * for merging.
		 */
		memset(&refspec, 0, sizeof(refspec));
		refspec.src = branch->merge[i]->src;
		get_fetch_map(remote_refs, &refspec, tail, 1);
		for (rm = *old_tail; rm; rm = rm->next)
			rm->fetch_head_status = FETCH_HEAD_MERGE;
	}
}

static int add_existing(const char *refname, const struct object_id *oid,
			int flag, void *cbdata)
{
	struct string_list *list = (struct string_list *)cbdata;
	struct string_list_item *item = string_list_insert(list, refname);
	struct object_id *old_oid = xmalloc(sizeof(*old_oid));

	oidcpy(old_oid, oid);
	item->util = old_oid;
	return 0;
}

static int will_fetch(struct ref **head, const unsigned char *sha1)
{
	struct ref *rm = *head;
	while (rm) {
		if (!hashcmp(rm->old_oid.hash, sha1))
			return 1;
		rm = rm->next;
	}
	return 0;
}

static void find_non_local_tags(struct transport *transport,
			struct ref **head,
			struct ref ***tail)
{
	struct string_list existing_refs = STRING_LIST_INIT_DUP;
	struct string_list remote_refs = STRING_LIST_INIT_NODUP;
	const struct ref *ref;
	struct string_list_item *item = NULL;

	for_each_ref(add_existing, &existing_refs);
	for (ref = transport_get_remote_refs(transport); ref; ref = ref->next) {
		if (!starts_with(ref->name, "refs/tags/"))
			continue;

		/*
		 * The peeled ref always follows the matching base
		 * ref, so if we see a peeled ref that we don't want
		 * to fetch then we can mark the ref entry in the list
		 * as one to ignore by setting util to NULL.
		 */
		if (ends_with(ref->name, "^{}")) {
			if (item &&
			    !has_object_file_with_flags(&ref->old_oid, HAS_SHA1_QUICK) &&
			    !will_fetch(head, ref->old_oid.hash) &&
			    !has_sha1_file_with_flags(item->util, HAS_SHA1_QUICK) &&
			    !will_fetch(head, item->util))
				item->util = NULL;
			item = NULL;
			continue;
		}

		/*
		 * If item is non-NULL here, then we previously saw a
		 * ref not followed by a peeled reference, so we need
		 * to check if it is a lightweight tag that we want to
		 * fetch.
		 */
		if (item &&
		    !has_sha1_file_with_flags(item->util, HAS_SHA1_QUICK) &&
		    !will_fetch(head, item->util))
			item->util = NULL;

		item = NULL;

		/* skip duplicates and refs that we already have */
		if (string_list_has_string(&remote_refs, ref->name) ||
		    string_list_has_string(&existing_refs, ref->name))
			continue;

		item = string_list_insert(&remote_refs, ref->name);
		item->util = (void *)&ref->old_oid;
	}
	string_list_clear(&existing_refs, 1);

	/*
	 * We may have a final lightweight tag that needs to be
	 * checked to see if it needs fetching.
	 */
	if (item &&
	    !has_sha1_file_with_flags(item->util, HAS_SHA1_QUICK) &&
	    !will_fetch(head, item->util))
		item->util = NULL;

	/*
	 * For all the tags in the remote_refs string list,
	 * add them to the list of refs to be fetched
	 */
	for_each_string_list_item(item, &remote_refs) {
		/* Unless we have already decided to ignore this item... */
		if (item->util)
		{
			struct ref *rm = alloc_ref(item->string);
			rm->peer_ref = alloc_ref(item->string);
			oidcpy(&rm->old_oid, item->util);
			**tail = rm;
			*tail = &rm->next;
		}
	}

	string_list_clear(&remote_refs, 0);
}

static struct ref *get_ref_map(struct transport *transport,
			       struct refspec *refspecs, int refspec_count,
			       int tags, int *autotags)
{
	int i;
	struct ref *rm;
	struct ref *ref_map = NULL;
	struct ref **tail = &ref_map;

	/* opportunistically-updated references: */
	struct ref *orefs = NULL, **oref_tail = &orefs;

	const struct ref *remote_refs = transport_get_remote_refs(transport);

	if (refspec_count) {
		struct refspec *fetch_refspec;
		int fetch_refspec_nr;

		for (i = 0; i < refspec_count; i++) {
			get_fetch_map(remote_refs, &refspecs[i], &tail, 0);
			if (refspecs[i].dst && refspecs[i].dst[0])
				*autotags = 1;
		}
		/* Merge everything on the command line (but not --tags) */
		for (rm = ref_map; rm; rm = rm->next)
			rm->fetch_head_status = FETCH_HEAD_MERGE;

		/*
		 * For any refs that we happen to be fetching via
		 * command-line arguments, the destination ref might
		 * have been missing or have been different than the
		 * remote-tracking ref that would be derived from the
		 * configured refspec.  In these cases, we want to
		 * take the opportunity to update their configured
		 * remote-tracking reference.  However, we do not want
		 * to mention these entries in FETCH_HEAD at all, as
		 * they would simply be duplicates of existing
		 * entries, so we set them FETCH_HEAD_IGNORE below.
		 *
		 * We compute these entries now, based only on the
		 * refspecs specified on the command line.  But we add
		 * them to the list following the refspecs resulting
		 * from the tags option so that one of the latter,
		 * which has FETCH_HEAD_NOT_FOR_MERGE, is not removed
		 * by ref_remove_duplicates() in favor of one of these
		 * opportunistic entries with FETCH_HEAD_IGNORE.
		 */
		if (refmap_array) {
			fetch_refspec = parse_fetch_refspec(refmap_nr, refmap_array);
			fetch_refspec_nr = refmap_nr;
		} else {
			fetch_refspec = transport->remote->fetch;
			fetch_refspec_nr = transport->remote->fetch_refspec_nr;
		}

		for (i = 0; i < fetch_refspec_nr; i++)
			get_fetch_map(ref_map, &fetch_refspec[i], &oref_tail, 1);
	} else if (refmap_array) {
		die("--refmap option is only meaningful with command-line refspec(s).");
	} else {
		/* Use the defaults */
		struct remote *remote = transport->remote;
		struct branch *branch = branch_get(NULL);
		int has_merge = branch_has_merge_config(branch);
		if (remote &&
		    (remote->fetch_refspec_nr ||
		     /* Note: has_merge implies non-NULL branch->remote_name */
		     (has_merge && !strcmp(branch->remote_name, remote->name)))) {
			for (i = 0; i < remote->fetch_refspec_nr; i++) {
				get_fetch_map(remote_refs, &remote->fetch[i], &tail, 0);
				if (remote->fetch[i].dst &&
				    remote->fetch[i].dst[0])
					*autotags = 1;
				if (!i && !has_merge && ref_map &&
				    !remote->fetch[0].pattern)
					ref_map->fetch_head_status = FETCH_HEAD_MERGE;
			}
			/*
			 * if the remote we're fetching from is the same
			 * as given in branch.<name>.remote, we add the
			 * ref given in branch.<name>.merge, too.
			 *
			 * Note: has_merge implies non-NULL branch->remote_name
			 */
			if (has_merge &&
			    !strcmp(branch->remote_name, remote->name))
				add_merge_config(&ref_map, remote_refs, branch, &tail);
		} else {
			ref_map = get_remote_ref(remote_refs, "HEAD");
			if (!ref_map)
				die(_("Couldn't find remote ref HEAD"));
			ref_map->fetch_head_status = FETCH_HEAD_MERGE;
			tail = &ref_map->next;
		}
	}

	if (tags == TAGS_SET)
		/* also fetch all tags */
		get_fetch_map(remote_refs, tag_refspec, &tail, 0);
	else if (tags == TAGS_DEFAULT && *autotags)
		find_non_local_tags(transport, &ref_map, &tail);

	/* Now append any refs to be updated opportunistically: */
	*tail = orefs;
	for (rm = orefs; rm; rm = rm->next) {
		rm->fetch_head_status = FETCH_HEAD_IGNORE;
		tail = &rm->next;
	}

	return ref_remove_duplicates(ref_map);
}

#define STORE_REF_ERROR_OTHER 1
#define STORE_REF_ERROR_DF_CONFLICT 2

static int s_update_ref(const char *action,
			struct ref *ref,
			int check_old)
{
	char msg[1024];
	char *rla = getenv("GIT_REFLOG_ACTION");
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	int ret, df_conflict = 0;

	if (dry_run)
		return 0;
	if (!rla)
		rla = default_rla.buf;
	snprintf(msg, sizeof(msg), "%s: %s", rla, action);

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, ref->name,
				   ref->new_oid.hash,
				   check_old ? ref->old_oid.hash : NULL,
				   0, msg, &err))
		goto fail;

	ret = ref_transaction_commit(transaction, &err);
	if (ret) {
		df_conflict = (ret == TRANSACTION_NAME_CONFLICT);
		goto fail;
	}

	ref_transaction_free(transaction);
	strbuf_release(&err);
	return 0;
fail:
	ref_transaction_free(transaction);
	error("%s", err.buf);
	strbuf_release(&err);
	return df_conflict ? STORE_REF_ERROR_DF_CONFLICT
			   : STORE_REF_ERROR_OTHER;
}

static int refcol_width = 10;
static int compact_format;

static void adjust_refcol_width(const struct ref *ref)
{
	int max, rlen, llen, len;

	/* uptodate lines are only shown on high verbosity level */
	if (!verbosity && !oidcmp(&ref->peer_ref->old_oid, &ref->old_oid))
		return;

	max    = term_columns();
	rlen   = utf8_strwidth(prettify_refname(ref->name));

	llen   = utf8_strwidth(prettify_refname(ref->peer_ref->name));

	/*
	 * rough estimation to see if the output line is too long and
	 * should not be counted (we can't do precise calculation
	 * anyway because we don't know if the error explanation part
	 * will be printed in update_local_ref)
	 */
	if (compact_format) {
		llen = 0;
		max = max * 2 / 3;
	}
	len = 21 /* flag and summary */ + rlen + 4 /* -> */ + llen;
	if (len >= max)
		return;

	/*
	 * Not precise calculation for compact mode because '*' can
	 * appear on the left hand side of '->' and shrink the column
	 * back.
	 */
	if (refcol_width < rlen)
		refcol_width = rlen;
}

static void prepare_format_display(struct ref *ref_map)
{
	struct ref *rm;
	const char *format = "full";

	git_config_get_string_const("fetch.output", &format);
	if (!strcasecmp(format, "full"))
		compact_format = 0;
	else if (!strcasecmp(format, "compact"))
		compact_format = 1;
	else
		die(_("configuration fetch.output contains invalid value %s"),
		    format);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->status == REF_STATUS_REJECT_SHALLOW ||
		    !rm->peer_ref ||
		    !strcmp(rm->name, "HEAD"))
			continue;

		adjust_refcol_width(rm);
	}
}

static void print_remote_to_local(struct strbuf *display,
				  const char *remote, const char *local)
{
	strbuf_addf(display, "%-*s -> %s", refcol_width, remote, local);
}

static int find_and_replace(struct strbuf *haystack,
			    const char *needle,
			    const char *placeholder)
{
	const char *p = strstr(haystack->buf, needle);
	int plen, nlen;

	if (!p)
		return 0;

	if (p > haystack->buf && p[-1] != '/')
		return 0;

	plen = strlen(p);
	nlen = strlen(needle);
	if (plen > nlen && p[nlen] != '/')
		return 0;

	strbuf_splice(haystack, p - haystack->buf, nlen,
		      placeholder, strlen(placeholder));
	return 1;
}

static void print_compact(struct strbuf *display,
			  const char *remote, const char *local)
{
	struct strbuf r = STRBUF_INIT;
	struct strbuf l = STRBUF_INIT;

	if (!strcmp(remote, local)) {
		strbuf_addf(display, "%-*s -> *", refcol_width, remote);
		return;
	}

	strbuf_addstr(&r, remote);
	strbuf_addstr(&l, local);

	if (!find_and_replace(&r, local, "*"))
		find_and_replace(&l, remote, "*");
	print_remote_to_local(display, r.buf, l.buf);

	strbuf_release(&r);
	strbuf_release(&l);
}

static void format_display(struct strbuf *display, char code,
			   const char *summary, const char *error,
			   const char *remote, const char *local,
			   int summary_width)
{
	int width = (summary_width + strlen(summary) - gettext_width(summary));

	strbuf_addf(display, "%c %-*s ", code, width, summary);
	if (!compact_format)
		print_remote_to_local(display, remote, local);
	else
		print_compact(display, remote, local);
	if (error)
		strbuf_addf(display, "  (%s)", error);
}

static int update_local_ref(struct ref *ref,
			    const char *remote,
			    const struct ref *remote_ref,
			    struct strbuf *display,
			    int summary_width)
{
	struct commit *current = NULL, *updated;
	enum object_type type;
	struct branch *current_branch = branch_get(NULL);
	const char *pretty_ref = prettify_refname(ref->name);

	type = sha1_object_info(ref->new_oid.hash, NULL);
	if (type < 0)
		die(_("object %s not found"), oid_to_hex(&ref->new_oid));

	if (!oidcmp(&ref->old_oid, &ref->new_oid)) {
		if (verbosity > 0)
			format_display(display, '=', _("[up to date]"), NULL,
				       remote, pretty_ref, summary_width);
		return 0;
	}

	if (current_branch &&
	    !strcmp(ref->name, current_branch->name) &&
	    !(update_head_ok || is_bare_repository()) &&
	    !is_null_oid(&ref->old_oid)) {
		/*
		 * If this is the head, and it's not okay to update
		 * the head, and the old value of the head isn't empty...
		 */
		format_display(display, '!', _("[rejected]"),
			       _("can't fetch in current branch"),
			       remote, pretty_ref, summary_width);
		return 1;
	}

	if (!is_null_oid(&ref->old_oid) &&
	    starts_with(ref->name, "refs/tags/")) {
		int r;
		r = s_update_ref("updating tag", ref, 0);
		format_display(display, r ? '!' : 't', _("[tag update]"),
			       r ? _("unable to update local ref") : NULL,
			       remote, pretty_ref, summary_width);
		return r;
	}

	current = lookup_commit_reference_gently(ref->old_oid.hash, 1);
	updated = lookup_commit_reference_gently(ref->new_oid.hash, 1);
	if (!current || !updated) {
		const char *msg;
		const char *what;
		int r;
		/*
		 * Nicely describe the new ref we're fetching.
		 * Base this on the remote's ref name, as it's
		 * more likely to follow a standard layout.
		 */
		const char *name = remote_ref ? remote_ref->name : "";
		if (starts_with(name, "refs/tags/")) {
			msg = "storing tag";
			what = _("[new tag]");
		} else if (starts_with(name, "refs/heads/")) {
			msg = "storing head";
			what = _("[new branch]");
		} else {
			msg = "storing ref";
			what = _("[new ref]");
		}

		if ((recurse_submodules != RECURSE_SUBMODULES_OFF) &&
		    (recurse_submodules != RECURSE_SUBMODULES_ON))
			check_for_new_submodule_commits(&ref->new_oid);
		r = s_update_ref(msg, ref, 0);
		format_display(display, r ? '!' : '*', what,
			       r ? _("unable to update local ref") : NULL,
			       remote, pretty_ref, summary_width);
		return r;
	}

	if (in_merge_bases(current, updated)) {
		struct strbuf quickref = STRBUF_INIT;
		int r;
		strbuf_add_unique_abbrev(&quickref, current->object.oid.hash, DEFAULT_ABBREV);
		strbuf_addstr(&quickref, "..");
		strbuf_add_unique_abbrev(&quickref, ref->new_oid.hash, DEFAULT_ABBREV);
		if ((recurse_submodules != RECURSE_SUBMODULES_OFF) &&
		    (recurse_submodules != RECURSE_SUBMODULES_ON))
			check_for_new_submodule_commits(&ref->new_oid);
		r = s_update_ref("fast-forward", ref, 1);
		format_display(display, r ? '!' : ' ', quickref.buf,
			       r ? _("unable to update local ref") : NULL,
			       remote, pretty_ref, summary_width);
		strbuf_release(&quickref);
		return r;
	} else if (force || ref->force) {
		struct strbuf quickref = STRBUF_INIT;
		int r;
		strbuf_add_unique_abbrev(&quickref, current->object.oid.hash, DEFAULT_ABBREV);
		strbuf_addstr(&quickref, "...");
		strbuf_add_unique_abbrev(&quickref, ref->new_oid.hash, DEFAULT_ABBREV);
		if ((recurse_submodules != RECURSE_SUBMODULES_OFF) &&
		    (recurse_submodules != RECURSE_SUBMODULES_ON))
			check_for_new_submodule_commits(&ref->new_oid);
		r = s_update_ref("forced-update", ref, 1);
		format_display(display, r ? '!' : '+', quickref.buf,
			       r ? _("unable to update local ref") : _("forced update"),
			       remote, pretty_ref, summary_width);
		strbuf_release(&quickref);
		return r;
	} else {
		format_display(display, '!', _("[rejected]"), _("non-fast-forward"),
			       remote, pretty_ref, summary_width);
		return 1;
	}
}

static int iterate_ref_map(void *cb_data, unsigned char sha1[20])
{
	struct ref **rm = cb_data;
	struct ref *ref = *rm;

	while (ref && ref->status == REF_STATUS_REJECT_SHALLOW)
		ref = ref->next;
	if (!ref)
		return -1; /* end of the list */
	*rm = ref->next;
	hashcpy(sha1, ref->old_oid.hash);
	return 0;
}

static int store_updated_refs(const char *raw_url, const char *remote_name,
		struct ref *ref_map)
{
	FILE *fp;
	struct commit *commit;
	int url_len, i, rc = 0;
	struct strbuf note = STRBUF_INIT;
	const char *what, *kind;
	struct ref *rm;
	char *url;
	const char *filename = dry_run ? "/dev/null" : git_path_fetch_head();
	int want_status;
	int summary_width = transport_summary_width(ref_map);

	fp = fopen(filename, "a");
	if (!fp)
		return error_errno(_("cannot open %s"), filename);

	if (raw_url)
		url = transport_anonymize_url(raw_url);
	else
		url = xstrdup("foreign");

	rm = ref_map;
	if (check_connected(iterate_ref_map, &rm, NULL)) {
		rc = error(_("%s did not send all necessary objects\n"), url);
		goto abort;
	}

	prepare_format_display(ref_map);

	/*
	 * We do a pass for each fetch_head_status type in their enum order, so
	 * merged entries are written before not-for-merge. That lets readers
	 * use FETCH_HEAD as a refname to refer to the ref to be merged.
	 */
	for (want_status = FETCH_HEAD_MERGE;
	     want_status <= FETCH_HEAD_IGNORE;
	     want_status++) {
		for (rm = ref_map; rm; rm = rm->next) {
			struct ref *ref = NULL;
			const char *merge_status_marker = "";

			if (rm->status == REF_STATUS_REJECT_SHALLOW) {
				if (want_status == FETCH_HEAD_MERGE)
					warning(_("reject %s because shallow roots are not allowed to be updated"),
						rm->peer_ref ? rm->peer_ref->name : rm->name);
				continue;
			}

			commit = lookup_commit_reference_gently(rm->old_oid.hash, 1);
			if (!commit)
				rm->fetch_head_status = FETCH_HEAD_NOT_FOR_MERGE;

			if (rm->fetch_head_status != want_status)
				continue;

			if (rm->peer_ref) {
				ref = alloc_ref(rm->peer_ref->name);
				oidcpy(&ref->old_oid, &rm->peer_ref->old_oid);
				oidcpy(&ref->new_oid, &rm->old_oid);
				ref->force = rm->peer_ref->force;
			}


			if (!strcmp(rm->name, "HEAD")) {
				kind = "";
				what = "";
			}
			else if (starts_with(rm->name, "refs/heads/")) {
				kind = "branch";
				what = rm->name + 11;
			}
			else if (starts_with(rm->name, "refs/tags/")) {
				kind = "tag";
				what = rm->name + 10;
			}
			else if (starts_with(rm->name, "refs/remotes/")) {
				kind = "remote-tracking branch";
				what = rm->name + 13;
			}
			else {
				kind = "";
				what = rm->name;
			}

			url_len = strlen(url);
			for (i = url_len - 1; url[i] == '/' && 0 <= i; i--)
				;
			url_len = i + 1;
			if (4 < i && !strncmp(".git", url + i - 3, 4))
				url_len = i - 3;

			strbuf_reset(&note);
			if (*what) {
				if (*kind)
					strbuf_addf(&note, "%s ", kind);
				strbuf_addf(&note, "'%s' of ", what);
			}
			switch (rm->fetch_head_status) {
			case FETCH_HEAD_NOT_FOR_MERGE:
				merge_status_marker = "not-for-merge";
				/* fall-through */
			case FETCH_HEAD_MERGE:
				fprintf(fp, "%s\t%s\t%s",
					oid_to_hex(&rm->old_oid),
					merge_status_marker,
					note.buf);
				for (i = 0; i < url_len; ++i)
					if ('\n' == url[i])
						fputs("\\n", fp);
					else
						fputc(url[i], fp);
				fputc('\n', fp);
				break;
			default:
				/* do not write anything to FETCH_HEAD */
				break;
			}

			strbuf_reset(&note);
			if (ref) {
				rc |= update_local_ref(ref, what, rm, &note,
						       summary_width);
				free(ref);
			} else
				format_display(&note, '*',
					       *kind ? kind : "branch", NULL,
					       *what ? what : "HEAD",
					       "FETCH_HEAD", summary_width);
			if (note.len) {
				if (verbosity >= 0 && !shown_url) {
					fprintf(stderr, _("From %.*s\n"),
							url_len, url);
					shown_url = 1;
				}
				if (verbosity >= 0)
					fprintf(stderr, " %s\n", note.buf);
			}
		}
	}

	if (rc & STORE_REF_ERROR_DF_CONFLICT)
		error(_("some local refs could not be updated; try running\n"
		      " 'git remote prune %s' to remove any old, conflicting "
		      "branches"), remote_name);

 abort:
	strbuf_release(&note);
	free(url);
	fclose(fp);
	return rc;
}

/*
 * We would want to bypass the object transfer altogether if
 * everything we are going to fetch already exists and is connected
 * locally.
 */
static int quickfetch(struct ref *ref_map)
{
	struct ref *rm = ref_map;
	struct check_connected_options opt = CHECK_CONNECTED_INIT;

	/*
	 * If we are deepening a shallow clone we already have these
	 * objects reachable.  Running rev-list here will return with
	 * a good (0) exit status and we'll bypass the fetch that we
	 * really need to perform.  Claiming failure now will ensure
	 * we perform the network exchange to deepen our history.
	 */
	if (deepen)
		return -1;
	opt.quiet = 1;
	return check_connected(iterate_ref_map, &rm, &opt);
}

static int fetch_refs(struct transport *transport, struct ref *ref_map)
{
	int ret = quickfetch(ref_map);
	if (ret)
		ret = transport_fetch_refs(transport, ref_map);
	if (!ret)
		ret |= store_updated_refs(transport->url,
				transport->remote->name,
				ref_map);
	transport_unlock_pack(transport);
	return ret;
}

static int prune_refs(struct refspec *refs, int ref_count, struct ref *ref_map,
		const char *raw_url)
{
	int url_len, i, result = 0;
	struct ref *ref, *stale_refs = get_stale_heads(refs, ref_count, ref_map);
	char *url;
	int summary_width = transport_summary_width(stale_refs);
	const char *dangling_msg = dry_run
		? _("   (%s will become dangling)")
		: _("   (%s has become dangling)");

	if (raw_url)
		url = transport_anonymize_url(raw_url);
	else
		url = xstrdup("foreign");

	url_len = strlen(url);
	for (i = url_len - 1; url[i] == '/' && 0 <= i; i--)
		;

	url_len = i + 1;
	if (4 < i && !strncmp(".git", url + i - 3, 4))
		url_len = i - 3;

	if (!dry_run) {
		struct string_list refnames = STRING_LIST_INIT_NODUP;

		for (ref = stale_refs; ref; ref = ref->next)
			string_list_append(&refnames, ref->name);

		result = delete_refs(&refnames, 0);
		string_list_clear(&refnames, 0);
	}

	if (verbosity >= 0) {
		for (ref = stale_refs; ref; ref = ref->next) {
			struct strbuf sb = STRBUF_INIT;
			if (!shown_url) {
				fprintf(stderr, _("From %.*s\n"), url_len, url);
				shown_url = 1;
			}
			format_display(&sb, '-', _("[deleted]"), NULL,
				       _("(none)"), prettify_refname(ref->name),
				       summary_width);
			fprintf(stderr, " %s\n",sb.buf);
			strbuf_release(&sb);
			warn_dangling_symref(stderr, dangling_msg, ref->name);
		}
	}

	free(url);
	free_refs(stale_refs);
	return result;
}

static void check_not_current_branch(struct ref *ref_map)
{
	struct branch *current_branch = branch_get(NULL);

	if (is_bare_repository() || !current_branch)
		return;

	for (; ref_map; ref_map = ref_map->next)
		if (ref_map->peer_ref && !strcmp(current_branch->refname,
					ref_map->peer_ref->name))
			die(_("Refusing to fetch into current branch %s "
			    "of non-bare repository"), current_branch->refname);
}

static int truncate_fetch_head(void)
{
	const char *filename = git_path_fetch_head();
	FILE *fp = fopen_for_writing(filename);

	if (!fp)
		return error_errno(_("cannot open %s"), filename);
	fclose(fp);
	return 0;
}

static void set_option(struct transport *transport, const char *name, const char *value)
{
	int r = transport_set_option(transport, name, value);
	if (r < 0)
		die(_("Option \"%s\" value \"%s\" is not valid for %s"),
		    name, value, transport->url);
	if (r > 0)
		warning(_("Option \"%s\" is ignored for %s\n"),
			name, transport->url);
}

static struct transport *prepare_transport(struct remote *remote, int deepen)
{
	struct transport *transport;
	transport = transport_get(remote, NULL);
	transport_set_verbosity(transport, verbosity, progress);
	transport->family = family;
	if (upload_pack)
		set_option(transport, TRANS_OPT_UPLOADPACK, upload_pack);
	if (keep)
		set_option(transport, TRANS_OPT_KEEP, "yes");
	if (depth)
		set_option(transport, TRANS_OPT_DEPTH, depth);
	if (deepen && deepen_since)
		set_option(transport, TRANS_OPT_DEEPEN_SINCE, deepen_since);
	if (deepen && deepen_not.nr)
		set_option(transport, TRANS_OPT_DEEPEN_NOT,
			   (const char *)&deepen_not);
	if (deepen_relative)
		set_option(transport, TRANS_OPT_DEEPEN_RELATIVE, "yes");
	if (update_shallow)
		set_option(transport, TRANS_OPT_UPDATE_SHALLOW, "yes");
	return transport;
}

static void backfill_tags(struct transport *transport, struct ref *ref_map)
{
	int cannot_reuse;

	/*
	 * Once we have set TRANS_OPT_DEEPEN_SINCE, we can't unset it
	 * when remote helper is used (setting it to an empty string
	 * is not unsetting). We could extend the remote helper
	 * protocol for that, but for now, just force a new connection
	 * without deepen-since. Similar story for deepen-not.
	 */
	cannot_reuse = transport->cannot_reuse ||
		deepen_since || deepen_not.nr;
	if (cannot_reuse) {
		gsecondary = prepare_transport(transport->remote, 0);
		transport = gsecondary;
	}

	transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, NULL);
	transport_set_option(transport, TRANS_OPT_DEPTH, "0");
	transport_set_option(transport, TRANS_OPT_DEEPEN_RELATIVE, NULL);
	fetch_refs(transport, ref_map);

	if (gsecondary) {
		transport_disconnect(gsecondary);
		gsecondary = NULL;
	}
}

static int do_fetch(struct transport *transport,
		    struct refspec *refs, int ref_count)
{
	struct string_list existing_refs = STRING_LIST_INIT_DUP;
	struct ref *ref_map;
	struct ref *rm;
	int autotags = (transport->remote->fetch_tags == 1);
	int retcode = 0;

	for_each_ref(add_existing, &existing_refs);

	if (tags == TAGS_DEFAULT) {
		if (transport->remote->fetch_tags == 2)
			tags = TAGS_SET;
		if (transport->remote->fetch_tags == -1)
			tags = TAGS_UNSET;
	}

	if (!transport->get_refs_list || !transport->fetch)
		die(_("Don't know how to fetch from %s"), transport->url);

	/* if not appending, truncate FETCH_HEAD */
	if (!append && !dry_run) {
		retcode = truncate_fetch_head();
		if (retcode)
			goto cleanup;
	}

	ref_map = get_ref_map(transport, refs, ref_count, tags, &autotags);
	if (!update_head_ok)
		check_not_current_branch(ref_map);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref) {
			struct string_list_item *peer_item =
				string_list_lookup(&existing_refs,
						   rm->peer_ref->name);
			if (peer_item) {
				struct object_id *old_oid = peer_item->util;
				oidcpy(&rm->peer_ref->old_oid, old_oid);
			}
		}
	}

	if (tags == TAGS_DEFAULT && autotags)
		transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, "1");
	if (prune) {
		/*
		 * We only prune based on refspecs specified
		 * explicitly (via command line or configuration); we
		 * don't care whether --tags was specified.
		 */
		if (ref_count) {
			prune_refs(refs, ref_count, ref_map, transport->url);
		} else {
			prune_refs(transport->remote->fetch,
				   transport->remote->fetch_refspec_nr,
				   ref_map,
				   transport->url);
		}
	}
	if (fetch_refs(transport, ref_map)) {
		free_refs(ref_map);
		retcode = 1;
		goto cleanup;
	}
	free_refs(ref_map);

	/* if neither --no-tags nor --tags was specified, do automated tag
	 * following ... */
	if (tags == TAGS_DEFAULT && autotags) {
		struct ref **tail = &ref_map;
		ref_map = NULL;
		find_non_local_tags(transport, &ref_map, &tail);
		if (ref_map)
			backfill_tags(transport, ref_map);
		free_refs(ref_map);
	}

 cleanup:
	string_list_clear(&existing_refs, 1);
	return retcode;
}

static int get_one_remote_for_fetch(struct remote *remote, void *priv)
{
	struct string_list *list = priv;
	if (!remote->skip_default_update)
		string_list_append(list, remote->name);
	return 0;
}

struct remote_group_data {
	const char *name;
	struct string_list *list;
};

static int get_remote_group(const char *key, const char *value, void *priv)
{
	struct remote_group_data *g = priv;

	if (skip_prefix(key, "remotes.", &key) && !strcmp(key, g->name)) {
		/* split list by white space */
		while (*value) {
			size_t wordlen = strcspn(value, " \t\n");

			if (wordlen >= 1)
				string_list_append_nodup(g->list,
						   xstrndup(value, wordlen));
			value += wordlen + (value[wordlen] != '\0');
		}
	}

	return 0;
}

static int add_remote_or_group(const char *name, struct string_list *list)
{
	int prev_nr = list->nr;
	struct remote_group_data g;
	g.name = name; g.list = list;

	git_config(get_remote_group, &g);
	if (list->nr == prev_nr) {
		struct remote *remote = remote_get(name);
		if (!remote_is_configured(remote, 0))
			return 0;
		string_list_append(list, remote->name);
	}
	return 1;
}

static void add_options_to_argv(struct argv_array *argv)
{
	if (dry_run)
		argv_array_push(argv, "--dry-run");
	if (prune != -1)
		argv_array_push(argv, prune ? "--prune" : "--no-prune");
	if (update_head_ok)
		argv_array_push(argv, "--update-head-ok");
	if (force)
		argv_array_push(argv, "--force");
	if (keep)
		argv_array_push(argv, "--keep");
	if (recurse_submodules == RECURSE_SUBMODULES_ON)
		argv_array_push(argv, "--recurse-submodules");
	else if (recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND)
		argv_array_push(argv, "--recurse-submodules=on-demand");
	if (tags == TAGS_SET)
		argv_array_push(argv, "--tags");
	else if (tags == TAGS_UNSET)
		argv_array_push(argv, "--no-tags");
	if (verbosity >= 2)
		argv_array_push(argv, "-v");
	if (verbosity >= 1)
		argv_array_push(argv, "-v");
	else if (verbosity < 0)
		argv_array_push(argv, "-q");

}

static int fetch_multiple(struct string_list *list)
{
	int i, result = 0;
	struct argv_array argv = ARGV_ARRAY_INIT;

	if (!append && !dry_run) {
		int errcode = truncate_fetch_head();
		if (errcode)
			return errcode;
	}

	argv_array_pushl(&argv, "fetch", "--append", NULL);
	add_options_to_argv(&argv);

	for (i = 0; i < list->nr; i++) {
		const char *name = list->items[i].string;
		argv_array_push(&argv, name);
		if (verbosity >= 0)
			printf(_("Fetching %s\n"), name);
		if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
			error(_("Could not fetch %s"), name);
			result = 1;
		}
		argv_array_pop(&argv);
	}

	argv_array_clear(&argv);
	return result;
}

static int fetch_one(struct remote *remote, int argc, const char **argv)
{
	static const char **refs = NULL;
	struct refspec *refspec;
	int ref_nr = 0;
	int exit_code;

	if (!remote)
		die(_("No remote repository specified.  Please, specify either a URL or a\n"
		    "remote name from which new revisions should be fetched."));

	gtransport = prepare_transport(remote, 1);

	if (prune < 0) {
		/* no command line request */
		if (0 <= gtransport->remote->prune)
			prune = gtransport->remote->prune;
		else if (0 <= fetch_prune_config)
			prune = fetch_prune_config;
		else
			prune = PRUNE_BY_DEFAULT;
	}

	if (argc > 0) {
		int j = 0;
		int i;
		refs = xcalloc(st_add(argc, 1), sizeof(const char *));
		for (i = 0; i < argc; i++) {
			if (!strcmp(argv[i], "tag")) {
				i++;
				if (i >= argc)
					die(_("You need to specify a tag name."));
				refs[j++] = xstrfmt("refs/tags/%s:refs/tags/%s",
						    argv[i], argv[i]);
			} else
				refs[j++] = argv[i];
		}
		refs[j] = NULL;
		ref_nr = j;
	}

	sigchain_push_common(unlock_pack_on_signal);
	atexit(unlock_pack);
	refspec = parse_fetch_refspec(ref_nr, refs);
	exit_code = do_fetch(gtransport, refspec, ref_nr);
	free_refspec(ref_nr, refspec);
	transport_disconnect(gtransport);
	gtransport = NULL;
	return exit_code;
}

int cmd_fetch(int argc, const char **argv, const char *prefix)
{
	int i;
	struct string_list list = STRING_LIST_INIT_DUP;
	struct remote *remote;
	int result = 0;
	struct argv_array argv_gc_auto = ARGV_ARRAY_INIT;

	packet_trace_identity("fetch");

	/* Record the command line for the reflog */
	strbuf_addstr(&default_rla, "fetch");
	for (i = 1; i < argc; i++)
		strbuf_addf(&default_rla, " %s", argv[i]);

	git_config(git_fetch_config, NULL);

	argc = parse_options(argc, argv, prefix,
			     builtin_fetch_options, builtin_fetch_usage, 0);

	if (deepen_relative) {
		if (deepen_relative < 0)
			die(_("Negative depth in --deepen is not supported"));
		if (depth)
			die(_("--deepen and --depth are mutually exclusive"));
		depth = xstrfmt("%d", deepen_relative);
	}
	if (unshallow) {
		if (depth)
			die(_("--depth and --unshallow cannot be used together"));
		else if (!is_repository_shallow())
			die(_("--unshallow on a complete repository does not make sense"));
		else
			depth = xstrfmt("%d", INFINITE_DEPTH);
	}

	/* no need to be strict, transport_set_option() will validate it again */
	if (depth && atoi(depth) < 1)
		die(_("depth %s is not a positive number"), depth);
	if (depth || deepen_since || deepen_not.nr)
		deepen = 1;

	if (recurse_submodules != RECURSE_SUBMODULES_OFF) {
		if (recurse_submodules_default) {
			int arg = parse_fetch_recurse_submodules_arg("--recurse-submodules-default", recurse_submodules_default);
			set_config_fetch_recurse_submodules(arg);
		}
		gitmodules_config();
		git_config(submodule_config, NULL);
	}

	if (all) {
		if (argc == 1)
			die(_("fetch --all does not take a repository argument"));
		else if (argc > 1)
			die(_("fetch --all does not make sense with refspecs"));
		(void) for_each_remote(get_one_remote_for_fetch, &list);
		result = fetch_multiple(&list);
	} else if (argc == 0) {
		/* No arguments -- use default remote */
		remote = remote_get(NULL);
		result = fetch_one(remote, argc, argv);
	} else if (multiple) {
		/* All arguments are assumed to be remotes or groups */
		for (i = 0; i < argc; i++)
			if (!add_remote_or_group(argv[i], &list))
				die(_("No such remote or remote group: %s"), argv[i]);
		result = fetch_multiple(&list);
	} else {
		/* Single remote or group */
		(void) add_remote_or_group(argv[0], &list);
		if (list.nr > 1) {
			/* More than one remote */
			if (argc > 1)
				die(_("Fetching a group and specifying refspecs does not make sense"));
			result = fetch_multiple(&list);
		} else {
			/* Zero or one remotes */
			remote = remote_get(argv[0]);
			result = fetch_one(remote, argc-1, argv+1);
		}
	}

	if (!result && (recurse_submodules != RECURSE_SUBMODULES_OFF)) {
		struct argv_array options = ARGV_ARRAY_INIT;

		add_options_to_argv(&options);
		result = fetch_populated_submodules(&options,
						    submodule_prefix,
						    recurse_submodules,
						    verbosity < 0,
						    max_children);
		argv_array_clear(&options);
	}

	string_list_clear(&list, 0);

	close_all_packs();

	argv_array_pushl(&argv_gc_auto, "gc", "--auto", NULL);
	if (verbosity < 0)
		argv_array_push(&argv_gc_auto, "--quiet");
	run_command_v_opt(argv_gc_auto.argv, RUN_GIT_CMD);
	argv_array_clear(&argv_gc_auto);

	return result;
}
