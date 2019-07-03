/*
 * "git fetch"
 */
#include "cache.h"
#include "config.h"
#include "repository.h"
#include "refs.h"
#include "refspec.h"
#include "object-store.h"
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
#include "packfile.h"
#include "list-objects-filter-options.h"
#include "commit-reach.h"

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

static int fetch_prune_tags_config = -1; /* unspecified */
static int prune_tags = -1; /* unspecified */
#define PRUNE_TAGS_BY_DEFAULT 0 /* do we prune tags by default? */

static int all, append, dry_run, force, keep, multiple, update_head_ok, verbosity, deepen_relative;
static int progress = -1;
static int enable_auto_gc = 1;
static int tags = TAGS_DEFAULT, unshallow, update_shallow, deepen;
static int max_children = 1;
static enum transport_family family;
static const char *depth;
static const char *deepen_since;
static const char *upload_pack;
static struct string_list deepen_not = STRING_LIST_INIT_NODUP;
static struct strbuf default_rla = STRBUF_INIT;
static struct transport *gtransport;
static struct transport *gsecondary;
static const char *submodule_prefix = "";
static int recurse_submodules = RECURSE_SUBMODULES_DEFAULT;
static int recurse_submodules_default = RECURSE_SUBMODULES_ON_DEMAND;
static int shown_url = 0;
static struct refspec refmap = REFSPEC_INIT_FETCH;
static struct list_objects_filter_options filter_options;
static struct string_list server_options = STRING_LIST_INIT_DUP;
static struct string_list negotiation_tip = STRING_LIST_INIT_NODUP;

static int git_fetch_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "fetch.prune")) {
		fetch_prune_config = git_config_bool(k, v);
		return 0;
	}

	if (!strcmp(k, "fetch.prunetags")) {
		fetch_prune_tags_config = git_config_bool(k, v);
		return 0;
	}

	if (!strcmp(k, "submodule.recurse")) {
		int r = git_config_bool(k, v) ?
			RECURSE_SUBMODULES_ON : RECURSE_SUBMODULES_OFF;
		recurse_submodules = r;
	}

	if (!strcmp(k, "submodule.fetchjobs")) {
		max_children = parse_submodule_fetchjobs(k, v);
		return 0;
	} else if (!strcmp(k, "fetch.recursesubmodules")) {
		recurse_submodules = parse_fetch_recurse_submodules_arg(k, v);
		return 0;
	}

	return git_default_config(k, v, cb);
}

static int parse_refmap_arg(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);

	/*
	 * "git fetch --refmap='' origin foo"
	 * can be used to tell the command not to store anywhere
	 */
	refspec_append(&refmap, arg);

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
	OPT__FORCE(&force, N_("force overwrite of local reference"), 0),
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
	OPT_BOOL('P', "prune-tags", &prune_tags,
		 N_("prune local tags no longer on remote and clobber changed tags")),
	{ OPTION_CALLBACK, 0, "recurse-submodules", &recurse_submodules, N_("on-demand"),
		    N_("control recursive fetching of submodules"),
		    PARSE_OPT_OPTARG, option_fetch_parse_recurse_submodules },
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
	OPT_SET_INT_F(0, "unshallow", &unshallow,
		      N_("convert to a complete repository"),
		      1, PARSE_OPT_NONEG),
	{ OPTION_STRING, 0, "submodule-prefix", &submodule_prefix, N_("dir"),
		   N_("prepend this to submodule path output"), PARSE_OPT_HIDDEN },
	{ OPTION_CALLBACK, 0, "recurse-submodules-default",
		   &recurse_submodules_default, N_("on-demand"),
		   N_("default for recursive fetching of submodules "
		      "(lower priority than config files)"),
		   PARSE_OPT_HIDDEN, option_fetch_parse_recurse_submodules },
	OPT_BOOL(0, "update-shallow", &update_shallow,
		 N_("accept refs that update .git/shallow")),
	{ OPTION_CALLBACK, 0, "refmap", NULL, N_("refmap"),
	  N_("specify fetch refmap"), PARSE_OPT_NONEG, parse_refmap_arg },
	OPT_STRING_LIST('o', "server-option", &server_options, N_("server-specific"), N_("option to transmit")),
	OPT_SET_INT('4', "ipv4", &family, N_("use IPv4 addresses only"),
			TRANSPORT_FAMILY_IPV4),
	OPT_SET_INT('6', "ipv6", &family, N_("use IPv6 addresses only"),
			TRANSPORT_FAMILY_IPV6),
	OPT_STRING_LIST(0, "negotiation-tip", &negotiation_tip, N_("revision"),
			N_("report that we have only objects reachable from this object")),
	OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
	OPT_BOOL(0, "auto-gc", &enable_auto_gc,
		 N_("run 'gc --auto' after fetching")),
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
		struct refspec_item refspec;

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

static int will_fetch(struct ref **head, const unsigned char *sha1)
{
	struct ref *rm = *head;
	while (rm) {
		if (hasheq(rm->old_oid.hash, sha1))
			return 1;
		rm = rm->next;
	}
	return 0;
}

struct refname_hash_entry {
	struct hashmap_entry ent; /* must be the first member */
	struct object_id oid;
	int ignore;
	char refname[FLEX_ARRAY];
};

static int refname_hash_entry_cmp(const void *hashmap_cmp_fn_data,
				  const void *e1_,
				  const void *e2_,
				  const void *keydata)
{
	const struct refname_hash_entry *e1 = e1_;
	const struct refname_hash_entry *e2 = e2_;

	return strcmp(e1->refname, keydata ? keydata : e2->refname);
}

static struct refname_hash_entry *refname_hash_add(struct hashmap *map,
						   const char *refname,
						   const struct object_id *oid)
{
	struct refname_hash_entry *ent;
	size_t len = strlen(refname);

	FLEX_ALLOC_MEM(ent, refname, refname, len);
	hashmap_entry_init(ent, strhash(refname));
	oidcpy(&ent->oid, oid);
	hashmap_add(map, ent);
	return ent;
}

static int add_one_refname(const char *refname,
			   const struct object_id *oid,
			   int flag, void *cbdata)
{
	struct hashmap *refname_map = cbdata;

	(void) refname_hash_add(refname_map, refname, oid);
	return 0;
}

static void refname_hash_init(struct hashmap *map)
{
	hashmap_init(map, refname_hash_entry_cmp, NULL, 0);
}

static int refname_hash_exists(struct hashmap *map, const char *refname)
{
	return !!hashmap_get_from_hash(map, strhash(refname), refname);
}

static void clear_item(struct refname_hash_entry *item)
{
	item->ignore = 1;
}

static void find_non_local_tags(const struct ref *refs,
				struct ref **head,
				struct ref ***tail)
{
	struct hashmap existing_refs;
	struct hashmap remote_refs;
	struct string_list remote_refs_list = STRING_LIST_INIT_NODUP;
	struct string_list_item *remote_ref_item;
	const struct ref *ref;
	struct refname_hash_entry *item = NULL;

	refname_hash_init(&existing_refs);
	refname_hash_init(&remote_refs);

	for_each_ref(add_one_refname, &existing_refs);
	for (ref = refs; ref; ref = ref->next) {
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
			    !has_object_file_with_flags(&ref->old_oid,
							OBJECT_INFO_QUICK) &&
			    !will_fetch(head, ref->old_oid.hash) &&
			    !has_object_file_with_flags(&item->oid, OBJECT_INFO_QUICK) &&
			    !will_fetch(head, item->oid.hash))
				clear_item(item);
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
		    !has_object_file_with_flags(&item->oid, OBJECT_INFO_QUICK) &&
		    !will_fetch(head, item->oid.hash))
			clear_item(item);

		item = NULL;

		/* skip duplicates and refs that we already have */
		if (refname_hash_exists(&remote_refs, ref->name) ||
		    refname_hash_exists(&existing_refs, ref->name))
			continue;

		item = refname_hash_add(&remote_refs, ref->name, &ref->old_oid);
		string_list_insert(&remote_refs_list, ref->name);
	}
	hashmap_free(&existing_refs, 1);

	/*
	 * We may have a final lightweight tag that needs to be
	 * checked to see if it needs fetching.
	 */
	if (item &&
	    !has_object_file_with_flags(&item->oid, OBJECT_INFO_QUICK) &&
	    !will_fetch(head, item->oid.hash))
		clear_item(item);

	/*
	 * For all the tags in the remote_refs_list,
	 * add them to the list of refs to be fetched
	 */
	for_each_string_list_item(remote_ref_item, &remote_refs_list) {
		const char *refname = remote_ref_item->string;
		struct ref *rm;

		item = hashmap_get_from_hash(&remote_refs, strhash(refname), refname);
		if (!item)
			BUG("unseen remote ref?");

		/* Unless we have already decided to ignore this item... */
		if (item->ignore)
			continue;

		rm = alloc_ref(item->refname);
		rm->peer_ref = alloc_ref(item->refname);
		oidcpy(&rm->old_oid, &item->oid);
		**tail = rm;
		*tail = &rm->next;
	}
	hashmap_free(&remote_refs, 1);
	string_list_clear(&remote_refs_list, 0);
}

static struct ref *get_ref_map(struct remote *remote,
			       const struct ref *remote_refs,
			       struct refspec *rs,
			       int tags, int *autotags)
{
	int i;
	struct ref *rm;
	struct ref *ref_map = NULL;
	struct ref **tail = &ref_map;

	/* opportunistically-updated references: */
	struct ref *orefs = NULL, **oref_tail = &orefs;

	struct hashmap existing_refs;

	if (rs->nr) {
		struct refspec *fetch_refspec;

		for (i = 0; i < rs->nr; i++) {
			get_fetch_map(remote_refs, &rs->items[i], &tail, 0);
			if (rs->items[i].dst && rs->items[i].dst[0])
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
		if (refmap.nr)
			fetch_refspec = &refmap;
		else
			fetch_refspec = &remote->fetch;

		for (i = 0; i < fetch_refspec->nr; i++)
			get_fetch_map(ref_map, &fetch_refspec->items[i], &oref_tail, 1);
	} else if (refmap.nr) {
		die("--refmap option is only meaningful with command-line refspec(s).");
	} else {
		/* Use the defaults */
		struct branch *branch = branch_get(NULL);
		int has_merge = branch_has_merge_config(branch);
		if (remote &&
		    (remote->fetch.nr ||
		     /* Note: has_merge implies non-NULL branch->remote_name */
		     (has_merge && !strcmp(branch->remote_name, remote->name)))) {
			for (i = 0; i < remote->fetch.nr; i++) {
				get_fetch_map(remote_refs, &remote->fetch.items[i], &tail, 0);
				if (remote->fetch.items[i].dst &&
				    remote->fetch.items[i].dst[0])
					*autotags = 1;
				if (!i && !has_merge && ref_map &&
				    !remote->fetch.items[0].pattern)
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
		find_non_local_tags(remote_refs, &ref_map, &tail);

	/* Now append any refs to be updated opportunistically: */
	*tail = orefs;
	for (rm = orefs; rm; rm = rm->next) {
		rm->fetch_head_status = FETCH_HEAD_IGNORE;
		tail = &rm->next;
	}

	ref_map = ref_remove_duplicates(ref_map);

	refname_hash_init(&existing_refs);
	for_each_ref(add_one_refname, &existing_refs);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref) {
			const char *refname = rm->peer_ref->name;
			struct refname_hash_entry *peer_item;

			peer_item = hashmap_get_from_hash(&existing_refs,
							  strhash(refname),
							  refname);
			if (peer_item) {
				struct object_id *old_oid = &peer_item->oid;
				oidcpy(&rm->peer_ref->old_oid, old_oid);
			}
		}
	}
	hashmap_free(&existing_refs, 1);

	return ref_map;
}

#define STORE_REF_ERROR_OTHER 1
#define STORE_REF_ERROR_DF_CONFLICT 2

static int s_update_ref(const char *action,
			struct ref *ref,
			int check_old)
{
	char *msg;
	char *rla = getenv("GIT_REFLOG_ACTION");
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	int ret, df_conflict = 0;

	if (dry_run)
		return 0;
	if (!rla)
		rla = default_rla.buf;
	msg = xstrfmt("%s: %s", rla, action);

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, ref->name,
				   &ref->new_oid,
				   check_old ? &ref->old_oid : NULL,
				   0, msg, &err))
		goto fail;

	ret = ref_transaction_commit(transaction, &err);
	if (ret) {
		df_conflict = (ret == TRANSACTION_NAME_CONFLICT);
		goto fail;
	}

	ref_transaction_free(transaction);
	strbuf_release(&err);
	free(msg);
	return 0;
fail:
	ref_transaction_free(transaction);
	error("%s", err.buf);
	strbuf_release(&err);
	free(msg);
	return df_conflict ? STORE_REF_ERROR_DF_CONFLICT
			   : STORE_REF_ERROR_OTHER;
}

static int refcol_width = 10;
static int compact_format;

static void adjust_refcol_width(const struct ref *ref)
{
	int max, rlen, llen, len;

	/* uptodate lines are only shown on high verbosity level */
	if (!verbosity && oideq(&ref->peer_ref->old_oid, &ref->old_oid))
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
	const char *p = NULL;
	int plen, nlen;

	nlen = strlen(needle);
	if (ends_with(haystack->buf, needle))
		p = haystack->buf + haystack->len - nlen;
	else
		p = strstr(haystack->buf, needle);
	if (!p)
		return 0;

	if (p > haystack->buf && p[-1] != '/')
		return 0;

	plen = strlen(p);
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

	type = oid_object_info(the_repository, &ref->new_oid, NULL);
	if (type < 0)
		die(_("object %s not found"), oid_to_hex(&ref->new_oid));

	if (oideq(&ref->old_oid, &ref->new_oid)) {
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
		if (force || ref->force) {
			int r;
			r = s_update_ref("updating tag", ref, 0);
			format_display(display, r ? '!' : 't', _("[tag update]"),
				       r ? _("unable to update local ref") : NULL,
				       remote, pretty_ref, summary_width);
			return r;
		} else {
			format_display(display, '!', _("[rejected]"), _("would clobber existing tag"),
				       remote, pretty_ref, summary_width);
			return 1;
		}
	}

	current = lookup_commit_reference_gently(the_repository,
						 &ref->old_oid, 1);
	updated = lookup_commit_reference_gently(the_repository,
						 &ref->new_oid, 1);
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

		r = s_update_ref(msg, ref, 0);
		format_display(display, r ? '!' : '*', what,
			       r ? _("unable to update local ref") : NULL,
			       remote, pretty_ref, summary_width);
		return r;
	}

	if (in_merge_bases(current, updated)) {
		struct strbuf quickref = STRBUF_INIT;
		int r;
		strbuf_add_unique_abbrev(&quickref, &current->object.oid, DEFAULT_ABBREV);
		strbuf_addstr(&quickref, "..");
		strbuf_add_unique_abbrev(&quickref, &ref->new_oid, DEFAULT_ABBREV);
		r = s_update_ref("fast-forward", ref, 1);
		format_display(display, r ? '!' : ' ', quickref.buf,
			       r ? _("unable to update local ref") : NULL,
			       remote, pretty_ref, summary_width);
		strbuf_release(&quickref);
		return r;
	} else if (force || ref->force) {
		struct strbuf quickref = STRBUF_INIT;
		int r;
		strbuf_add_unique_abbrev(&quickref, &current->object.oid, DEFAULT_ABBREV);
		strbuf_addstr(&quickref, "...");
		strbuf_add_unique_abbrev(&quickref, &ref->new_oid, DEFAULT_ABBREV);
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

static int iterate_ref_map(void *cb_data, struct object_id *oid)
{
	struct ref **rm = cb_data;
	struct ref *ref = *rm;

	while (ref && ref->status == REF_STATUS_REJECT_SHALLOW)
		ref = ref->next;
	if (!ref)
		return -1; /* end of the list */
	*rm = ref->next;
	oidcpy(oid, &ref->old_oid);
	return 0;
}

static int store_updated_refs(const char *raw_url, const char *remote_name,
			      int connectivity_checked, struct ref *ref_map)
{
	FILE *fp;
	struct commit *commit;
	int url_len, i, rc = 0;
	struct strbuf note = STRBUF_INIT;
	const char *what, *kind;
	struct ref *rm;
	char *url;
	const char *filename = dry_run ? "/dev/null" : git_path_fetch_head(the_repository);
	int want_status;
	int summary_width = transport_summary_width(ref_map);

	fp = fopen(filename, "a");
	if (!fp)
		return error_errno(_("cannot open %s"), filename);

	if (raw_url)
		url = transport_anonymize_url(raw_url);
	else
		url = xstrdup("foreign");

	if (!connectivity_checked) {
		rm = ref_map;
		if (check_connected(iterate_ref_map, &rm, NULL)) {
			rc = error(_("%s did not send all necessary objects\n"), url);
			goto abort;
		}
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

			commit = lookup_commit_reference_gently(the_repository,
								&rm->old_oid,
								1);
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

			if (recurse_submodules != RECURSE_SUBMODULES_OFF)
				check_for_new_submodule_commits(&rm->old_oid);

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
static int check_exist_and_connected(struct ref *ref_map)
{
	struct ref *rm = ref_map;
	struct check_connected_options opt = CHECK_CONNECTED_INIT;
	struct ref *r;

	/*
	 * If we are deepening a shallow clone we already have these
	 * objects reachable.  Running rev-list here will return with
	 * a good (0) exit status and we'll bypass the fetch that we
	 * really need to perform.  Claiming failure now will ensure
	 * we perform the network exchange to deepen our history.
	 */
	if (deepen)
		return -1;

	/*
	 * check_connected() allows objects to merely be promised, but
	 * we need all direct targets to exist.
	 */
	for (r = rm; r; r = r->next) {
		if (!has_object_file(&r->old_oid))
			return -1;
	}

	opt.quiet = 1;
	return check_connected(iterate_ref_map, &rm, &opt);
}

static int fetch_refs(struct transport *transport, struct ref *ref_map)
{
	int ret = check_exist_and_connected(ref_map);
	if (ret)
		ret = transport_fetch_refs(transport, ref_map);
	if (!ret)
		/*
		 * Keep the new pack's ".keep" file around to allow the caller
		 * time to update refs to reference the new objects.
		 */
		return 0;
	transport_unlock_pack(transport);
	return ret;
}

/* Update local refs based on the ref values fetched from a remote */
static int consume_refs(struct transport *transport, struct ref *ref_map)
{
	int connectivity_checked = transport->smart_options
		? transport->smart_options->connectivity_checked : 0;
	int ret = store_updated_refs(transport->url,
				     transport->remote->name,
				     connectivity_checked,
				     ref_map);
	transport_unlock_pack(transport);
	return ret;
}

static int prune_refs(struct refspec *rs, struct ref *ref_map,
		      const char *raw_url)
{
	int url_len, i, result = 0;
	struct ref *ref, *stale_refs = get_stale_heads(rs, ref_map);
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

		result = delete_refs("fetch: prune", &refnames, 0);
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
	const char *filename = git_path_fetch_head(the_repository);
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


static int add_oid(const char *refname, const struct object_id *oid, int flags,
		   void *cb_data)
{
	struct oid_array *oids = cb_data;

	oid_array_append(oids, oid);
	return 0;
}

static void add_negotiation_tips(struct git_transport_options *smart_options)
{
	struct oid_array *oids = xcalloc(1, sizeof(*oids));
	int i;

	for (i = 0; i < negotiation_tip.nr; i++) {
		const char *s = negotiation_tip.items[i].string;
		int old_nr;
		if (!has_glob_specials(s)) {
			struct object_id oid;
			if (get_oid(s, &oid))
				die("%s is not a valid object", s);
			oid_array_append(oids, &oid);
			continue;
		}
		old_nr = oids->nr;
		for_each_glob_ref(add_oid, s, oids);
		if (old_nr == oids->nr)
			warning("Ignoring --negotiation-tip=%s because it does not match any refs",
				s);
	}
	smart_options->negotiation_tips = oids;
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
	if (filter_options.choice) {
		struct strbuf expanded_filter_spec = STRBUF_INIT;
		expand_list_objects_filter_spec(&filter_options,
						&expanded_filter_spec);
		set_option(transport, TRANS_OPT_LIST_OBJECTS_FILTER,
			   expanded_filter_spec.buf);
		set_option(transport, TRANS_OPT_FROM_PROMISOR, "1");
		strbuf_release(&expanded_filter_spec);
	}
	if (negotiation_tip.nr) {
		if (transport->smart_options)
			add_negotiation_tips(transport->smart_options);
		else
			warning("Ignoring --negotiation-tip because the protocol does not support it.");
	}
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
	if (!fetch_refs(transport, ref_map))
		consume_refs(transport, ref_map);

	if (gsecondary) {
		transport_disconnect(gsecondary);
		gsecondary = NULL;
	}
}

static int do_fetch(struct transport *transport,
		    struct refspec *rs)
{
	struct ref *ref_map;
	int autotags = (transport->remote->fetch_tags == 1);
	int retcode = 0;
	const struct ref *remote_refs;
	struct argv_array ref_prefixes = ARGV_ARRAY_INIT;
	int must_list_refs = 1;

	if (tags == TAGS_DEFAULT) {
		if (transport->remote->fetch_tags == 2)
			tags = TAGS_SET;
		if (transport->remote->fetch_tags == -1)
			tags = TAGS_UNSET;
	}

	/* if not appending, truncate FETCH_HEAD */
	if (!append && !dry_run) {
		retcode = truncate_fetch_head();
		if (retcode)
			goto cleanup;
	}

	if (rs->nr) {
		int i;

		refspec_ref_prefixes(rs, &ref_prefixes);

		/*
		 * We can avoid listing refs if all of them are exact
		 * OIDs
		 */
		must_list_refs = 0;
		for (i = 0; i < rs->nr; i++) {
			if (!rs->items[i].exact_sha1) {
				must_list_refs = 1;
				break;
			}
		}
	} else if (transport->remote && transport->remote->fetch.nr)
		refspec_ref_prefixes(&transport->remote->fetch, &ref_prefixes);

	if (tags == TAGS_SET || tags == TAGS_DEFAULT) {
		must_list_refs = 1;
		if (ref_prefixes.argc)
			argv_array_push(&ref_prefixes, "refs/tags/");
	}

	if (must_list_refs)
		remote_refs = transport_get_remote_refs(transport, &ref_prefixes);
	else
		remote_refs = NULL;

	argv_array_clear(&ref_prefixes);

	ref_map = get_ref_map(transport->remote, remote_refs, rs,
			      tags, &autotags);
	if (!update_head_ok)
		check_not_current_branch(ref_map);

	if (tags == TAGS_DEFAULT && autotags)
		transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, "1");
	if (prune) {
		/*
		 * We only prune based on refspecs specified
		 * explicitly (via command line or configuration); we
		 * don't care whether --tags was specified.
		 */
		if (rs->nr) {
			prune_refs(rs, ref_map, transport->url);
		} else {
			prune_refs(&transport->remote->fetch,
				   ref_map,
				   transport->url);
		}
	}
	if (fetch_refs(transport, ref_map) || consume_refs(transport, ref_map)) {
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
		find_non_local_tags(remote_refs, &ref_map, &tail);
		if (ref_map)
			backfill_tags(transport, ref_map);
		free_refs(ref_map);
	}

 cleanup:
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
	if (prune_tags != -1)
		argv_array_push(argv, prune_tags ? "--prune-tags" : "--no-prune-tags");
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

	argv_array_pushl(&argv, "fetch", "--append", "--no-auto-gc", NULL);
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

/*
 * Fetching from the promisor remote should use the given filter-spec
 * or inherit the default filter-spec from the config.
 */
static inline void fetch_one_setup_partial(struct remote *remote)
{
	/*
	 * Explicit --no-filter argument overrides everything, regardless
	 * of any prior partial clones and fetches.
	 */
	if (filter_options.no_filter)
		return;

	/*
	 * If no prior partial clone/fetch and the current fetch DID NOT
	 * request a partial-fetch, do a normal fetch.
	 */
	if (!repository_format_partial_clone && !filter_options.choice)
		return;

	/*
	 * If this is the FIRST partial-fetch request, we enable partial
	 * on this repo and remember the given filter-spec as the default
	 * for subsequent fetches to this remote.
	 */
	if (!repository_format_partial_clone && filter_options.choice) {
		partial_clone_register(remote->name, &filter_options);
		return;
	}

	/*
	 * We are currently limited to only ONE promisor remote and only
	 * allow partial-fetches from the promisor remote.
	 */
	if (strcmp(remote->name, repository_format_partial_clone)) {
		if (filter_options.choice)
			die(_("--filter can only be used with the remote "
			      "configured in extensions.partialClone"));
		return;
	}

	/*
	 * Do a partial-fetch from the promisor remote using either the
	 * explicitly given filter-spec or inherit the filter-spec from
	 * the config.
	 */
	if (!filter_options.choice)
		partial_clone_get_default_filter_spec(&filter_options);
	return;
}

static int fetch_one(struct remote *remote, int argc, const char **argv, int prune_tags_ok)
{
	struct refspec rs = REFSPEC_INIT_FETCH;
	int i;
	int exit_code;
	int maybe_prune_tags;
	int remote_via_config = remote_is_configured(remote, 0);

	if (!remote)
		die(_("No remote repository specified.  Please, specify either a URL or a\n"
		    "remote name from which new revisions should be fetched."));

	gtransport = prepare_transport(remote, 1);

	if (prune < 0) {
		/* no command line request */
		if (0 <= remote->prune)
			prune = remote->prune;
		else if (0 <= fetch_prune_config)
			prune = fetch_prune_config;
		else
			prune = PRUNE_BY_DEFAULT;
	}

	if (prune_tags < 0) {
		/* no command line request */
		if (0 <= remote->prune_tags)
			prune_tags = remote->prune_tags;
		else if (0 <= fetch_prune_tags_config)
			prune_tags = fetch_prune_tags_config;
		else
			prune_tags = PRUNE_TAGS_BY_DEFAULT;
	}

	maybe_prune_tags = prune_tags_ok && prune_tags;
	if (maybe_prune_tags && remote_via_config)
		refspec_append(&remote->fetch, TAG_REFSPEC);

	if (maybe_prune_tags && (argc || !remote_via_config))
		refspec_append(&rs, TAG_REFSPEC);

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "tag")) {
			char *tag;
			i++;
			if (i >= argc)
				die(_("You need to specify a tag name."));

			tag = xstrfmt("refs/tags/%s:refs/tags/%s",
				      argv[i], argv[i]);
			refspec_append(&rs, tag);
			free(tag);
		} else {
			refspec_append(&rs, argv[i]);
		}
	}

	if (server_options.nr)
		gtransport->server_options = &server_options;

	sigchain_push_common(unlock_pack_on_signal);
	atexit(unlock_pack);
	sigchain_push(SIGPIPE, SIG_IGN);
	exit_code = do_fetch(gtransport, &rs);
	sigchain_pop(SIGPIPE);
	refspec_clear(&rs);
	transport_disconnect(gtransport);
	gtransport = NULL;
	return exit_code;
}

int cmd_fetch(int argc, const char **argv, const char *prefix)
{
	int i;
	struct string_list list = STRING_LIST_INIT_DUP;
	struct remote *remote = NULL;
	int result = 0;
	int prune_tags_ok = 1;
	struct argv_array argv_gc_auto = ARGV_ARRAY_INIT;

	packet_trace_identity("fetch");

	fetch_if_missing = 0;

	/* Record the command line for the reflog */
	strbuf_addstr(&default_rla, "fetch");
	for (i = 1; i < argc; i++)
		strbuf_addf(&default_rla, " %s", argv[i]);

	fetch_config_from_gitmodules(&max_children, &recurse_submodules);
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
		else if (!is_repository_shallow(the_repository))
			die(_("--unshallow on a complete repository does not make sense"));
		else
			depth = xstrfmt("%d", INFINITE_DEPTH);
	}

	/* no need to be strict, transport_set_option() will validate it again */
	if (depth && atoi(depth) < 1)
		die(_("depth %s is not a positive number"), depth);
	if (depth || deepen_since || deepen_not.nr)
		deepen = 1;

	if (filter_options.choice && !repository_format_partial_clone)
		die("--filter can only be used when extensions.partialClone is set");

	if (all) {
		if (argc == 1)
			die(_("fetch --all does not take a repository argument"));
		else if (argc > 1)
			die(_("fetch --all does not make sense with refspecs"));
		(void) for_each_remote(get_one_remote_for_fetch, &list);
	} else if (argc == 0) {
		/* No arguments -- use default remote */
		remote = remote_get(NULL);
	} else if (multiple) {
		/* All arguments are assumed to be remotes or groups */
		for (i = 0; i < argc; i++)
			if (!add_remote_or_group(argv[i], &list))
				die(_("No such remote or remote group: %s"), argv[i]);
	} else {
		/* Single remote or group */
		(void) add_remote_or_group(argv[0], &list);
		if (list.nr > 1) {
			/* More than one remote */
			if (argc > 1)
				die(_("Fetching a group and specifying refspecs does not make sense"));
		} else {
			/* Zero or one remotes */
			remote = remote_get(argv[0]);
			prune_tags_ok = (argc == 1);
			argc--;
			argv++;
		}
	}

	if (remote) {
		if (filter_options.choice || repository_format_partial_clone)
			fetch_one_setup_partial(remote);
		result = fetch_one(remote, argc, argv, prune_tags_ok);
	} else {
		if (filter_options.choice)
			die(_("--filter can only be used with the remote "
			      "configured in extensions.partialclone"));
		/* TODO should this also die if we have a previous partial-clone? */
		result = fetch_multiple(&list);
	}

	if (!result && (recurse_submodules != RECURSE_SUBMODULES_OFF)) {
		struct argv_array options = ARGV_ARRAY_INIT;

		add_options_to_argv(&options);
		result = fetch_populated_submodules(the_repository,
						    &options,
						    submodule_prefix,
						    recurse_submodules,
						    recurse_submodules_default,
						    verbosity < 0,
						    max_children);
		argv_array_clear(&options);
	}

	string_list_clear(&list, 0);

	close_object_store(the_repository->objects);

	if (enable_auto_gc) {
		argv_array_pushl(&argv_gc_auto, "gc", "--auto", NULL);
		if (verbosity < 0)
			argv_array_push(&argv_gc_auto, "--quiet");
		run_command_v_opt(argv_gc_auto.argv, RUN_GIT_CMD);
		argv_array_clear(&argv_gc_auto);
	}

	return result;
}
