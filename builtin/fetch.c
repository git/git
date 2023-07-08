/*
 * "git fetch"
 */
#include "builtin.h"
#include "advice.h"
#include "config.h"
#include "gettext.h"
#include "environment.h"
#include "hex.h"
#include "repository.h"
#include "refs.h"
#include "refspec.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "oidset.h"
#include "oid-array.h"
#include "commit.h"
#include "string-list.h"
#include "remote.h"
#include "transport.h"
#include "run-command.h"
#include "parse-options.h"
#include "sigchain.h"
#include "submodule-config.h"
#include "submodule.h"
#include "connected.h"
#include "strvec.h"
#include "utf8.h"
#include "packfile.h"
#include "pager.h"
#include "path.h"
#include "pkt-line.h"
#include "list-objects-filter-options.h"
#include "commit-reach.h"
#include "branch.h"
#include "promisor-remote.h"
#include "commit-graph.h"
#include "shallow.h"
#include "trace.h"
#include "trace2.h"
#include "worktree.h"
#include "bundle-uri.h"

#define FORCED_UPDATES_DELAY_WARNING_IN_MS (10 * 1000)

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

enum display_format {
	DISPLAY_FORMAT_FULL,
	DISPLAY_FORMAT_COMPACT,
	DISPLAY_FORMAT_PORCELAIN,
};

struct display_state {
	struct strbuf buf;

	int refcol_width;
	enum display_format format;

	char *url;
	int url_len, shown_url;
};

static uint64_t forced_updates_ms = 0;
static int prefetch = 0;
static int prune = -1; /* unspecified */
#define PRUNE_BY_DEFAULT 0 /* do we prune by default? */

static int prune_tags = -1; /* unspecified */
#define PRUNE_TAGS_BY_DEFAULT 0 /* do we prune tags by default? */

static int append, dry_run, force, keep, update_head_ok;
static int write_fetch_head = 1;
static int verbosity, deepen_relative, set_upstream, refetch;
static int progress = -1;
static int tags = TAGS_DEFAULT, update_shallow, deepen;
static int atomic_fetch;
static enum transport_family family;
static const char *depth;
static const char *deepen_since;
static const char *upload_pack;
static struct string_list deepen_not = STRING_LIST_INIT_NODUP;
static struct strbuf default_rla = STRBUF_INIT;
static struct transport *gtransport;
static struct transport *gsecondary;
static struct refspec refmap = REFSPEC_INIT_FETCH;
static struct list_objects_filter_options filter_options = LIST_OBJECTS_FILTER_INIT;
static struct string_list server_options = STRING_LIST_INIT_DUP;
static struct string_list negotiation_tip = STRING_LIST_INIT_NODUP;

struct fetch_config {
	enum display_format display_format;
	int prune;
	int prune_tags;
	int show_forced_updates;
	int recurse_submodules;
	int parallel;
	int submodule_fetch_jobs;
};

static int git_fetch_config(const char *k, const char *v,
			    const struct config_context *ctx, void *cb)
{
	struct fetch_config *fetch_config = cb;

	if (!strcmp(k, "fetch.prune")) {
		fetch_config->prune = git_config_bool(k, v);
		return 0;
	}

	if (!strcmp(k, "fetch.prunetags")) {
		fetch_config->prune_tags = git_config_bool(k, v);
		return 0;
	}

	if (!strcmp(k, "fetch.showforcedupdates")) {
		fetch_config->show_forced_updates = git_config_bool(k, v);
		return 0;
	}

	if (!strcmp(k, "submodule.recurse")) {
		int r = git_config_bool(k, v) ?
			RECURSE_SUBMODULES_ON : RECURSE_SUBMODULES_OFF;
		fetch_config->recurse_submodules = r;
	}

	if (!strcmp(k, "submodule.fetchjobs")) {
		fetch_config->submodule_fetch_jobs = parse_submodule_fetchjobs(k, v, ctx->kvi);
		return 0;
	} else if (!strcmp(k, "fetch.recursesubmodules")) {
		fetch_config->recurse_submodules = parse_fetch_recurse_submodules_arg(k, v);
		return 0;
	}

	if (!strcmp(k, "fetch.parallel")) {
		fetch_config->parallel = git_config_int(k, v, ctx->kvi);
		if (fetch_config->parallel < 0)
			die(_("fetch.parallel cannot be negative"));
		if (!fetch_config->parallel)
			fetch_config->parallel = online_cpus();
		return 0;
	}

	if (!strcmp(k, "fetch.output")) {
		if (!v)
			return config_error_nonbool(k);
		else if (!strcasecmp(v, "full"))
			fetch_config->display_format = DISPLAY_FORMAT_FULL;
		else if (!strcasecmp(v, "compact"))
			fetch_config->display_format = DISPLAY_FORMAT_COMPACT;
		else
			die(_("invalid value for '%s': '%s'"),
			    "fetch.output", v);
	}

	return git_default_config(k, v, ctx, cb);
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

static void unlock_pack(unsigned int flags)
{
	if (gtransport)
		transport_unlock_pack(gtransport, flags);
	if (gsecondary)
		transport_unlock_pack(gsecondary, flags);
}

static void unlock_pack_atexit(void)
{
	unlock_pack(0);
}

static void unlock_pack_on_signal(int signo)
{
	unlock_pack(TRANSPORT_UNLOCK_PACK_IN_SIGNAL_HANDLER);
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

static void create_fetch_oidset(struct ref **head, struct oidset *out)
{
	struct ref *rm = *head;
	while (rm) {
		oidset_insert(out, &rm->old_oid);
		rm = rm->next;
	}
}

struct refname_hash_entry {
	struct hashmap_entry ent;
	struct object_id oid;
	int ignore;
	char refname[FLEX_ARRAY];
};

static int refname_hash_entry_cmp(const void *hashmap_cmp_fn_data UNUSED,
				  const struct hashmap_entry *eptr,
				  const struct hashmap_entry *entry_or_key,
				  const void *keydata)
{
	const struct refname_hash_entry *e1, *e2;

	e1 = container_of(eptr, const struct refname_hash_entry, ent);
	e2 = container_of(entry_or_key, const struct refname_hash_entry, ent);
	return strcmp(e1->refname, keydata ? keydata : e2->refname);
}

static struct refname_hash_entry *refname_hash_add(struct hashmap *map,
						   const char *refname,
						   const struct object_id *oid)
{
	struct refname_hash_entry *ent;
	size_t len = strlen(refname);

	FLEX_ALLOC_MEM(ent, refname, refname, len);
	hashmap_entry_init(&ent->ent, strhash(refname));
	oidcpy(&ent->oid, oid);
	hashmap_add(map, &ent->ent);
	return ent;
}

static int add_one_refname(const char *refname,
			   const struct object_id *oid,
			   int flag UNUSED, void *cbdata)
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


static void add_already_queued_tags(const char *refname,
				    const struct object_id *old_oid,
				    const struct object_id *new_oid,
				    void *cb_data)
{
	struct hashmap *queued_tags = cb_data;
	if (starts_with(refname, "refs/tags/") && new_oid)
		(void) refname_hash_add(queued_tags, refname, new_oid);
}

static void find_non_local_tags(const struct ref *refs,
				struct ref_transaction *transaction,
				struct ref **head,
				struct ref ***tail)
{
	struct hashmap existing_refs;
	struct hashmap remote_refs;
	struct oidset fetch_oids = OIDSET_INIT;
	struct string_list remote_refs_list = STRING_LIST_INIT_NODUP;
	struct string_list_item *remote_ref_item;
	const struct ref *ref;
	struct refname_hash_entry *item = NULL;
	const int quick_flags = OBJECT_INFO_QUICK | OBJECT_INFO_SKIP_FETCH_OBJECT;

	refname_hash_init(&existing_refs);
	refname_hash_init(&remote_refs);
	create_fetch_oidset(head, &fetch_oids);

	for_each_ref(add_one_refname, &existing_refs);

	/*
	 * If we already have a transaction, then we need to filter out all
	 * tags which have already been queued up.
	 */
	if (transaction)
		ref_transaction_for_each_queued_update(transaction,
						       add_already_queued_tags,
						       &existing_refs);

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
			    !repo_has_object_file_with_flags(the_repository, &ref->old_oid, quick_flags) &&
			    !oidset_contains(&fetch_oids, &ref->old_oid) &&
			    !repo_has_object_file_with_flags(the_repository, &item->oid, quick_flags) &&
			    !oidset_contains(&fetch_oids, &item->oid))
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
		    !repo_has_object_file_with_flags(the_repository, &item->oid, quick_flags) &&
		    !oidset_contains(&fetch_oids, &item->oid))
			clear_item(item);

		item = NULL;

		/* skip duplicates and refs that we already have */
		if (refname_hash_exists(&remote_refs, ref->name) ||
		    refname_hash_exists(&existing_refs, ref->name))
			continue;

		item = refname_hash_add(&remote_refs, ref->name, &ref->old_oid);
		string_list_insert(&remote_refs_list, ref->name);
	}
	hashmap_clear_and_free(&existing_refs, struct refname_hash_entry, ent);

	/*
	 * We may have a final lightweight tag that needs to be
	 * checked to see if it needs fetching.
	 */
	if (item &&
	    !repo_has_object_file_with_flags(the_repository, &item->oid, quick_flags) &&
	    !oidset_contains(&fetch_oids, &item->oid))
		clear_item(item);

	/*
	 * For all the tags in the remote_refs_list,
	 * add them to the list of refs to be fetched
	 */
	for_each_string_list_item(remote_ref_item, &remote_refs_list) {
		const char *refname = remote_ref_item->string;
		struct ref *rm;
		unsigned int hash = strhash(refname);

		item = hashmap_get_entry_from_hash(&remote_refs, hash, refname,
					struct refname_hash_entry, ent);
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
	hashmap_clear_and_free(&remote_refs, struct refname_hash_entry, ent);
	string_list_clear(&remote_refs_list, 0);
	oidset_clear(&fetch_oids);
}

static void filter_prefetch_refspec(struct refspec *rs)
{
	int i;

	if (!prefetch)
		return;

	for (i = 0; i < rs->nr; i++) {
		struct strbuf new_dst = STRBUF_INIT;
		char *old_dst;
		const char *sub = NULL;

		if (rs->items[i].negative)
			continue;
		if (!rs->items[i].dst ||
		    (rs->items[i].src &&
		     !strncmp(rs->items[i].src,
			      ref_namespace[NAMESPACE_TAGS].ref,
			      strlen(ref_namespace[NAMESPACE_TAGS].ref)))) {
			int j;

			free(rs->items[i].src);
			free(rs->items[i].dst);

			for (j = i + 1; j < rs->nr; j++) {
				rs->items[j - 1] = rs->items[j];
				rs->raw[j - 1] = rs->raw[j];
			}
			rs->nr--;
			i--;
			continue;
		}

		old_dst = rs->items[i].dst;
		strbuf_addstr(&new_dst, ref_namespace[NAMESPACE_PREFETCH].ref);

		/*
		 * If old_dst starts with "refs/", then place
		 * sub after that prefix. Otherwise, start at
		 * the beginning of the string.
		 */
		if (!skip_prefix(old_dst, "refs/", &sub))
			sub = old_dst;
		strbuf_addstr(&new_dst, sub);

		rs->items[i].dst = strbuf_detach(&new_dst, NULL);
		rs->items[i].force = 1;

		free(old_dst);
	}
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
	int existing_refs_populated = 0;

	filter_prefetch_refspec(rs);
	if (remote)
		filter_prefetch_refspec(&remote->fetch);

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
		die("--refmap option is only meaningful with command-line refspec(s)");
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
		} else if (!prefetch) {
			ref_map = get_remote_ref(remote_refs, "HEAD");
			if (!ref_map)
				die(_("couldn't find remote ref HEAD"));
			ref_map->fetch_head_status = FETCH_HEAD_MERGE;
			tail = &ref_map->next;
		}
	}

	if (tags == TAGS_SET)
		/* also fetch all tags */
		get_fetch_map(remote_refs, tag_refspec, &tail, 0);
	else if (tags == TAGS_DEFAULT && *autotags)
		find_non_local_tags(remote_refs, NULL, &ref_map, &tail);

	/* Now append any refs to be updated opportunistically: */
	*tail = orefs;
	for (rm = orefs; rm; rm = rm->next) {
		rm->fetch_head_status = FETCH_HEAD_IGNORE;
		tail = &rm->next;
	}

	/*
	 * apply negative refspecs first, before we remove duplicates. This is
	 * necessary as negative refspecs might remove an otherwise conflicting
	 * duplicate.
	 */
	if (rs->nr)
		ref_map = apply_negative_refspecs(ref_map, rs);
	else
		ref_map = apply_negative_refspecs(ref_map, &remote->fetch);

	ref_map = ref_remove_duplicates(ref_map);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref) {
			const char *refname = rm->peer_ref->name;
			struct refname_hash_entry *peer_item;
			unsigned int hash = strhash(refname);

			if (!existing_refs_populated) {
				refname_hash_init(&existing_refs);
				for_each_ref(add_one_refname, &existing_refs);
				existing_refs_populated = 1;
			}

			peer_item = hashmap_get_entry_from_hash(&existing_refs,
						hash, refname,
						struct refname_hash_entry, ent);
			if (peer_item) {
				struct object_id *old_oid = &peer_item->oid;
				oidcpy(&rm->peer_ref->old_oid, old_oid);
			}
		}
	}
	if (existing_refs_populated)
		hashmap_clear_and_free(&existing_refs, struct refname_hash_entry, ent);

	return ref_map;
}

#define STORE_REF_ERROR_OTHER 1
#define STORE_REF_ERROR_DF_CONFLICT 2

static int s_update_ref(const char *action,
			struct ref *ref,
			struct ref_transaction *transaction,
			int check_old)
{
	char *msg;
	char *rla = getenv("GIT_REFLOG_ACTION");
	struct ref_transaction *our_transaction = NULL;
	struct strbuf err = STRBUF_INIT;
	int ret;

	if (dry_run)
		return 0;
	if (!rla)
		rla = default_rla.buf;
	msg = xstrfmt("%s: %s", rla, action);

	/*
	 * If no transaction was passed to us, we manage the transaction
	 * ourselves. Otherwise, we trust the caller to handle the transaction
	 * lifecycle.
	 */
	if (!transaction) {
		transaction = our_transaction = ref_transaction_begin(&err);
		if (!transaction) {
			ret = STORE_REF_ERROR_OTHER;
			goto out;
		}
	}

	ret = ref_transaction_update(transaction, ref->name, &ref->new_oid,
				     check_old ? &ref->old_oid : NULL,
				     0, msg, &err);
	if (ret) {
		ret = STORE_REF_ERROR_OTHER;
		goto out;
	}

	if (our_transaction) {
		switch (ref_transaction_commit(our_transaction, &err)) {
		case 0:
			break;
		case TRANSACTION_NAME_CONFLICT:
			ret = STORE_REF_ERROR_DF_CONFLICT;
			goto out;
		default:
			ret = STORE_REF_ERROR_OTHER;
			goto out;
		}
	}

out:
	ref_transaction_free(our_transaction);
	if (ret)
		error("%s", err.buf);
	strbuf_release(&err);
	free(msg);
	return ret;
}

static int refcol_width(const struct ref *ref_map, int compact_format)
{
	const struct ref *ref;
	int max, width = 10;

	max = term_columns();
	if (compact_format)
		max = max * 2 / 3;

	for (ref = ref_map; ref; ref = ref->next) {
		int rlen, llen = 0, len;

		if (ref->status == REF_STATUS_REJECT_SHALLOW ||
		    !ref->peer_ref ||
		    !strcmp(ref->name, "HEAD"))
			continue;

		/* uptodate lines are only shown on high verbosity level */
		if (verbosity <= 0 && oideq(&ref->peer_ref->old_oid, &ref->old_oid))
			continue;

		rlen = utf8_strwidth(prettify_refname(ref->name));
		if (!compact_format)
			llen = utf8_strwidth(prettify_refname(ref->peer_ref->name));

		/*
		 * rough estimation to see if the output line is too long and
		 * should not be counted (we can't do precise calculation
		 * anyway because we don't know if the error explanation part
		 * will be printed in update_local_ref)
		 */
		len = 21 /* flag and summary */ + rlen + 4 /* -> */ + llen;
		if (len >= max)
			continue;

		if (width < rlen)
			width = rlen;
	}

	return width;
}

static void display_state_init(struct display_state *display_state, struct ref *ref_map,
			       const char *raw_url, enum display_format format)
{
	int i;

	memset(display_state, 0, sizeof(*display_state));
	strbuf_init(&display_state->buf, 0);
	display_state->format = format;

	if (raw_url)
		display_state->url = transport_anonymize_url(raw_url);
	else
		display_state->url = xstrdup("foreign");

	display_state->url_len = strlen(display_state->url);
	for (i = display_state->url_len - 1; display_state->url[i] == '/' && 0 <= i; i--)
		;
	display_state->url_len = i + 1;
	if (4 < i && !strncmp(".git", display_state->url + i - 3, 4))
		display_state->url_len = i - 3;

	if (verbosity < 0)
		return;

	switch (display_state->format) {
	case DISPLAY_FORMAT_FULL:
	case DISPLAY_FORMAT_COMPACT:
		display_state->refcol_width = refcol_width(ref_map,
							   display_state->format == DISPLAY_FORMAT_COMPACT);
		break;
	case DISPLAY_FORMAT_PORCELAIN:
		/* We don't need to precompute anything here. */
		break;
	default:
		BUG("unexpected display format %d", display_state->format);
	}
}

static void display_state_release(struct display_state *display_state)
{
	strbuf_release(&display_state->buf);
	free(display_state->url);
}

static void print_remote_to_local(struct display_state *display_state,
				  const char *remote, const char *local)
{
	strbuf_addf(&display_state->buf, "%-*s -> %s",
		    display_state->refcol_width, remote, local);
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

static void print_compact(struct display_state *display_state,
			  const char *remote, const char *local)
{
	struct strbuf r = STRBUF_INIT;
	struct strbuf l = STRBUF_INIT;

	if (!strcmp(remote, local)) {
		strbuf_addf(&display_state->buf, "%-*s -> *", display_state->refcol_width, remote);
		return;
	}

	strbuf_addstr(&r, remote);
	strbuf_addstr(&l, local);

	if (!find_and_replace(&r, local, "*"))
		find_and_replace(&l, remote, "*");
	print_remote_to_local(display_state, r.buf, l.buf);

	strbuf_release(&r);
	strbuf_release(&l);
}

static void display_ref_update(struct display_state *display_state, char code,
			       const char *summary, const char *error,
			       const char *remote, const char *local,
			       const struct object_id *old_oid,
			       const struct object_id *new_oid,
			       int summary_width)
{
	FILE *f = stderr;

	if (verbosity < 0)
		return;

	strbuf_reset(&display_state->buf);

	switch (display_state->format) {
	case DISPLAY_FORMAT_FULL:
	case DISPLAY_FORMAT_COMPACT: {
		int width;

		if (!display_state->shown_url) {
			strbuf_addf(&display_state->buf, _("From %.*s\n"),
				    display_state->url_len, display_state->url);
			display_state->shown_url = 1;
		}

		width = (summary_width + strlen(summary) - gettext_width(summary));
		remote = prettify_refname(remote);
		local = prettify_refname(local);

		strbuf_addf(&display_state->buf, " %c %-*s ", code, width, summary);

		if (display_state->format != DISPLAY_FORMAT_COMPACT)
			print_remote_to_local(display_state, remote, local);
		else
			print_compact(display_state, remote, local);

		if (error)
			strbuf_addf(&display_state->buf, "  (%s)", error);

		break;
	}
	case DISPLAY_FORMAT_PORCELAIN:
		strbuf_addf(&display_state->buf, "%c %s %s %s", code,
			    oid_to_hex(old_oid), oid_to_hex(new_oid), local);
		f = stdout;
		break;
	default:
		BUG("unexpected display format %d", display_state->format);
	};
	strbuf_addch(&display_state->buf, '\n');

	fputs(display_state->buf.buf, f);
}

static int update_local_ref(struct ref *ref,
			    struct ref_transaction *transaction,
			    struct display_state *display_state,
			    const struct ref *remote_ref,
			    int summary_width,
			    const struct fetch_config *config)
{
	struct commit *current = NULL, *updated;
	int fast_forward = 0;

	if (!repo_has_object_file(the_repository, &ref->new_oid))
		die(_("object %s not found"), oid_to_hex(&ref->new_oid));

	if (oideq(&ref->old_oid, &ref->new_oid)) {
		if (verbosity > 0)
			display_ref_update(display_state, '=', _("[up to date]"), NULL,
					   remote_ref->name, ref->name,
					   &ref->old_oid, &ref->new_oid, summary_width);
		return 0;
	}

	if (!update_head_ok &&
	    !is_null_oid(&ref->old_oid) &&
	    branch_checked_out(ref->name)) {
		/*
		 * If this is the head, and it's not okay to update
		 * the head, and the old value of the head isn't empty...
		 */
		display_ref_update(display_state, '!', _("[rejected]"),
				   _("can't fetch into checked-out branch"),
				   remote_ref->name, ref->name,
				   &ref->old_oid, &ref->new_oid, summary_width);
		return 1;
	}

	if (!is_null_oid(&ref->old_oid) &&
	    starts_with(ref->name, "refs/tags/")) {
		if (force || ref->force) {
			int r;
			r = s_update_ref("updating tag", ref, transaction, 0);
			display_ref_update(display_state, r ? '!' : 't', _("[tag update]"),
					   r ? _("unable to update local ref") : NULL,
					   remote_ref->name, ref->name,
					   &ref->old_oid, &ref->new_oid, summary_width);
			return r;
		} else {
			display_ref_update(display_state, '!', _("[rejected]"),
					   _("would clobber existing tag"),
					   remote_ref->name, ref->name,
					   &ref->old_oid, &ref->new_oid, summary_width);
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
		if (starts_with(remote_ref->name, "refs/tags/")) {
			msg = "storing tag";
			what = _("[new tag]");
		} else if (starts_with(remote_ref->name, "refs/heads/")) {
			msg = "storing head";
			what = _("[new branch]");
		} else {
			msg = "storing ref";
			what = _("[new ref]");
		}

		r = s_update_ref(msg, ref, transaction, 0);
		display_ref_update(display_state, r ? '!' : '*', what,
				   r ? _("unable to update local ref") : NULL,
				   remote_ref->name, ref->name,
				   &ref->old_oid, &ref->new_oid, summary_width);
		return r;
	}

	if (config->show_forced_updates) {
		uint64_t t_before = getnanotime();
		fast_forward = repo_in_merge_bases(the_repository, current,
						   updated);
		forced_updates_ms += (getnanotime() - t_before) / 1000000;
	} else {
		fast_forward = 1;
	}

	if (fast_forward) {
		struct strbuf quickref = STRBUF_INIT;
		int r;

		strbuf_add_unique_abbrev(&quickref, &current->object.oid, DEFAULT_ABBREV);
		strbuf_addstr(&quickref, "..");
		strbuf_add_unique_abbrev(&quickref, &ref->new_oid, DEFAULT_ABBREV);
		r = s_update_ref("fast-forward", ref, transaction, 1);
		display_ref_update(display_state, r ? '!' : ' ', quickref.buf,
				   r ? _("unable to update local ref") : NULL,
				   remote_ref->name, ref->name,
				   &ref->old_oid, &ref->new_oid, summary_width);
		strbuf_release(&quickref);
		return r;
	} else if (force || ref->force) {
		struct strbuf quickref = STRBUF_INIT;
		int r;
		strbuf_add_unique_abbrev(&quickref, &current->object.oid, DEFAULT_ABBREV);
		strbuf_addstr(&quickref, "...");
		strbuf_add_unique_abbrev(&quickref, &ref->new_oid, DEFAULT_ABBREV);
		r = s_update_ref("forced-update", ref, transaction, 1);
		display_ref_update(display_state, r ? '!' : '+', quickref.buf,
				   r ? _("unable to update local ref") : _("forced update"),
				   remote_ref->name, ref->name,
				   &ref->old_oid, &ref->new_oid, summary_width);
		strbuf_release(&quickref);
		return r;
	} else {
		display_ref_update(display_state, '!', _("[rejected]"), _("non-fast-forward"),
				   remote_ref->name, ref->name,
				   &ref->old_oid, &ref->new_oid, summary_width);
		return 1;
	}
}

static const struct object_id *iterate_ref_map(void *cb_data)
{
	struct ref **rm = cb_data;
	struct ref *ref = *rm;

	while (ref && ref->status == REF_STATUS_REJECT_SHALLOW)
		ref = ref->next;
	if (!ref)
		return NULL;
	*rm = ref->next;
	return &ref->old_oid;
}

struct fetch_head {
	FILE *fp;
	struct strbuf buf;
};

static int open_fetch_head(struct fetch_head *fetch_head)
{
	const char *filename = git_path_fetch_head(the_repository);

	if (write_fetch_head) {
		fetch_head->fp = fopen(filename, "a");
		if (!fetch_head->fp)
			return error_errno(_("cannot open '%s'"), filename);
		strbuf_init(&fetch_head->buf, 0);
	} else {
		fetch_head->fp = NULL;
	}

	return 0;
}

static void append_fetch_head(struct fetch_head *fetch_head,
			      const struct object_id *old_oid,
			      enum fetch_head_status fetch_head_status,
			      const char *note,
			      const char *url, size_t url_len)
{
	char old_oid_hex[GIT_MAX_HEXSZ + 1];
	const char *merge_status_marker;
	size_t i;

	if (!fetch_head->fp)
		return;

	switch (fetch_head_status) {
	case FETCH_HEAD_NOT_FOR_MERGE:
		merge_status_marker = "not-for-merge";
		break;
	case FETCH_HEAD_MERGE:
		merge_status_marker = "";
		break;
	default:
		/* do not write anything to FETCH_HEAD */
		return;
	}

	strbuf_addf(&fetch_head->buf, "%s\t%s\t%s",
		    oid_to_hex_r(old_oid_hex, old_oid), merge_status_marker, note);
	for (i = 0; i < url_len; ++i)
		if ('\n' == url[i])
			strbuf_addstr(&fetch_head->buf, "\\n");
		else
			strbuf_addch(&fetch_head->buf, url[i]);
	strbuf_addch(&fetch_head->buf, '\n');

	/*
	 * When using an atomic fetch, we do not want to update FETCH_HEAD if
	 * any of the reference updates fails. We thus have to write all
	 * updates to a buffer first and only commit it as soon as all
	 * references have been successfully updated.
	 */
	if (!atomic_fetch) {
		strbuf_write(&fetch_head->buf, fetch_head->fp);
		strbuf_reset(&fetch_head->buf);
	}
}

static void commit_fetch_head(struct fetch_head *fetch_head)
{
	if (!fetch_head->fp || !atomic_fetch)
		return;
	strbuf_write(&fetch_head->buf, fetch_head->fp);
}

static void close_fetch_head(struct fetch_head *fetch_head)
{
	if (!fetch_head->fp)
		return;

	fclose(fetch_head->fp);
	strbuf_release(&fetch_head->buf);
}

static const char warn_show_forced_updates[] =
N_("fetch normally indicates which branches had a forced update,\n"
   "but that check has been disabled; to re-enable, use '--show-forced-updates'\n"
   "flag or run 'git config fetch.showForcedUpdates true'");
static const char warn_time_show_forced_updates[] =
N_("it took %.2f seconds to check forced updates; you can use\n"
   "'--no-show-forced-updates' or run 'git config fetch.showForcedUpdates false'\n"
   "to avoid this check\n");

static int store_updated_refs(struct display_state *display_state,
			      const char *remote_name,
			      int connectivity_checked,
			      struct ref_transaction *transaction, struct ref *ref_map,
			      struct fetch_head *fetch_head,
			      const struct fetch_config *config)
{
	int rc = 0;
	struct strbuf note = STRBUF_INIT;
	const char *what, *kind;
	struct ref *rm;
	int want_status;
	int summary_width = 0;

	if (verbosity >= 0)
		summary_width = transport_summary_width(ref_map);

	if (!connectivity_checked) {
		struct check_connected_options opt = CHECK_CONNECTED_INIT;

		opt.exclude_hidden_refs_section = "fetch";
		rm = ref_map;
		if (check_connected(iterate_ref_map, &rm, &opt)) {
			rc = error(_("%s did not send all necessary objects\n"),
				   display_state->url);
			goto abort;
		}
	}

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

			if (rm->status == REF_STATUS_REJECT_SHALLOW) {
				if (want_status == FETCH_HEAD_MERGE)
					warning(_("rejected %s because shallow roots are not allowed to be updated"),
						rm->peer_ref ? rm->peer_ref->name : rm->name);
				continue;
			}

			/*
			 * When writing FETCH_HEAD we need to determine whether
			 * we already have the commit or not. If not, then the
			 * reference is not for merge and needs to be written
			 * to the reflog after other commits which we already
			 * have. We're not interested in this property though
			 * in case FETCH_HEAD is not to be updated, so we can
			 * skip the classification in that case.
			 */
			if (fetch_head->fp) {
				struct commit *commit = NULL;

				/*
				 * References in "refs/tags/" are often going to point
				 * to annotated tags, which are not part of the
				 * commit-graph. We thus only try to look up refs in
				 * the graph which are not in that namespace to not
				 * regress performance in repositories with many
				 * annotated tags.
				 */
				if (!starts_with(rm->name, "refs/tags/"))
					commit = lookup_commit_in_graph(the_repository, &rm->old_oid);
				if (!commit) {
					commit = lookup_commit_reference_gently(the_repository,
										&rm->old_oid,
										1);
					if (!commit)
						rm->fetch_head_status = FETCH_HEAD_NOT_FOR_MERGE;
				}
			}

			if (rm->fetch_head_status != want_status)
				continue;

			if (rm->peer_ref) {
				ref = alloc_ref(rm->peer_ref->name);
				oidcpy(&ref->old_oid, &rm->peer_ref->old_oid);
				oidcpy(&ref->new_oid, &rm->old_oid);
				ref->force = rm->peer_ref->force;
			}

			if (config->recurse_submodules != RECURSE_SUBMODULES_OFF &&
			    (!rm->peer_ref || !oideq(&ref->old_oid, &ref->new_oid))) {
				check_for_new_submodule_commits(&rm->old_oid);
			}

			if (!strcmp(rm->name, "HEAD")) {
				kind = "";
				what = "";
			} else if (skip_prefix(rm->name, "refs/heads/", &what)) {
				kind = "branch";
			} else if (skip_prefix(rm->name, "refs/tags/", &what)) {
				kind = "tag";
			} else if (skip_prefix(rm->name, "refs/remotes/", &what)) {
				kind = "remote-tracking branch";
			} else {
				kind = "";
				what = rm->name;
			}

			strbuf_reset(&note);
			if (*what) {
				if (*kind)
					strbuf_addf(&note, "%s ", kind);
				strbuf_addf(&note, "'%s' of ", what);
			}

			append_fetch_head(fetch_head, &rm->old_oid,
					  rm->fetch_head_status,
					  note.buf, display_state->url,
					  display_state->url_len);

			if (ref) {
				rc |= update_local_ref(ref, transaction, display_state,
						       rm, summary_width, config);
				free(ref);
			} else if (write_fetch_head || dry_run) {
				/*
				 * Display fetches written to FETCH_HEAD (or
				 * would be written to FETCH_HEAD, if --dry-run
				 * is set).
				 */
				display_ref_update(display_state, '*',
						   *kind ? kind : "branch", NULL,
						   rm->name,
						   "FETCH_HEAD",
						   &rm->new_oid, &rm->old_oid,
						   summary_width);
			}
		}
	}

	if (rc & STORE_REF_ERROR_DF_CONFLICT)
		error(_("some local refs could not be updated; try running\n"
		      " 'git remote prune %s' to remove any old, conflicting "
		      "branches"), remote_name);

	if (advice_enabled(ADVICE_FETCH_SHOW_FORCED_UPDATES)) {
		if (!config->show_forced_updates) {
			warning(_(warn_show_forced_updates));
		} else if (forced_updates_ms > FORCED_UPDATES_DELAY_WARNING_IN_MS) {
			warning(_(warn_time_show_forced_updates),
				forced_updates_ms / 1000.0);
		}
	}

 abort:
	strbuf_release(&note);
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
	 * Similarly, if we need to refetch, we always want to perform a full
	 * fetch ignoring existing objects.
	 */
	if (refetch)
		return -1;


	/*
	 * check_connected() allows objects to merely be promised, but
	 * we need all direct targets to exist.
	 */
	for (r = rm; r; r = r->next) {
		if (!repo_has_object_file_with_flags(the_repository, &r->old_oid,
						     OBJECT_INFO_SKIP_FETCH_OBJECT))
			return -1;
	}

	opt.quiet = 1;
	opt.exclude_hidden_refs_section = "fetch";
	return check_connected(iterate_ref_map, &rm, &opt);
}

static int fetch_and_consume_refs(struct display_state *display_state,
				  struct transport *transport,
				  struct ref_transaction *transaction,
				  struct ref *ref_map,
				  struct fetch_head *fetch_head,
				  const struct fetch_config *config)
{
	int connectivity_checked = 1;
	int ret;

	/*
	 * We don't need to perform a fetch in case we can already satisfy all
	 * refs.
	 */
	ret = check_exist_and_connected(ref_map);
	if (ret) {
		trace2_region_enter("fetch", "fetch_refs", the_repository);
		ret = transport_fetch_refs(transport, ref_map);
		trace2_region_leave("fetch", "fetch_refs", the_repository);
		if (ret)
			goto out;
		connectivity_checked = transport->smart_options ?
			transport->smart_options->connectivity_checked : 0;
	}

	trace2_region_enter("fetch", "consume_refs", the_repository);
	ret = store_updated_refs(display_state, transport->remote->name,
				 connectivity_checked, transaction, ref_map,
				 fetch_head, config);
	trace2_region_leave("fetch", "consume_refs", the_repository);

out:
	transport_unlock_pack(transport, 0);
	return ret;
}

static int prune_refs(struct display_state *display_state,
		      struct refspec *rs,
		      struct ref_transaction *transaction,
		      struct ref *ref_map)
{
	int result = 0;
	struct ref *ref, *stale_refs = get_stale_heads(rs, ref_map);
	struct strbuf err = STRBUF_INIT;
	const char *dangling_msg = dry_run
		? _("   (%s will become dangling)")
		: _("   (%s has become dangling)");

	if (!dry_run) {
		if (transaction) {
			for (ref = stale_refs; ref; ref = ref->next) {
				result = ref_transaction_delete(transaction, ref->name, NULL, 0,
								"fetch: prune", &err);
				if (result)
					goto cleanup;
			}
		} else {
			struct string_list refnames = STRING_LIST_INIT_NODUP;

			for (ref = stale_refs; ref; ref = ref->next)
				string_list_append(&refnames, ref->name);

			result = delete_refs("fetch: prune", &refnames, 0);
			string_list_clear(&refnames, 0);
		}
	}

	if (verbosity >= 0) {
		int summary_width = transport_summary_width(stale_refs);

		for (ref = stale_refs; ref; ref = ref->next) {
			display_ref_update(display_state, '-', _("[deleted]"), NULL,
					   _("(none)"), ref->name,
					   &ref->new_oid, &ref->old_oid,
					   summary_width);
			warn_dangling_symref(stderr, dangling_msg, ref->name);
		}
	}

cleanup:
	strbuf_release(&err);
	free_refs(stale_refs);
	return result;
}

static void check_not_current_branch(struct ref *ref_map)
{
	const char *path;
	for (; ref_map; ref_map = ref_map->next)
		if (ref_map->peer_ref &&
		    starts_with(ref_map->peer_ref->name, "refs/heads/") &&
		    (path = branch_checked_out(ref_map->peer_ref->name)))
			die(_("refusing to fetch into branch '%s' "
			      "checked out at '%s'"),
			    ref_map->peer_ref->name, path);
}

static int truncate_fetch_head(void)
{
	const char *filename = git_path_fetch_head(the_repository);
	FILE *fp = fopen_for_writing(filename);

	if (!fp)
		return error_errno(_("cannot open '%s'"), filename);
	fclose(fp);
	return 0;
}

static void set_option(struct transport *transport, const char *name, const char *value)
{
	int r = transport_set_option(transport, name, value);
	if (r < 0)
		die(_("option \"%s\" value \"%s\" is not valid for %s"),
		    name, value, transport->url);
	if (r > 0)
		warning(_("option \"%s\" is ignored for %s\n"),
			name, transport->url);
}


static int add_oid(const char *refname UNUSED,
		   const struct object_id *oid,
		   int flags UNUSED, void *cb_data)
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
			if (repo_get_oid(the_repository, s, &oid))
				die(_("%s is not a valid object"), s);
			if (!has_object(the_repository, &oid, 0))
				die(_("the object %s does not exist"), s);
			oid_array_append(oids, &oid);
			continue;
		}
		old_nr = oids->nr;
		for_each_glob_ref(add_oid, s, oids);
		if (old_nr == oids->nr)
			warning("ignoring --negotiation-tip=%s because it does not match any refs",
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
	if (refetch)
		set_option(transport, TRANS_OPT_REFETCH, "yes");
	if (filter_options.choice) {
		const char *spec =
			expand_list_objects_filter_spec(&filter_options);
		set_option(transport, TRANS_OPT_LIST_OBJECTS_FILTER, spec);
		set_option(transport, TRANS_OPT_FROM_PROMISOR, "1");
	}
	if (negotiation_tip.nr) {
		if (transport->smart_options)
			add_negotiation_tips(transport->smart_options);
		else
			warning("ignoring --negotiation-tip because the protocol does not support it");
	}
	return transport;
}

static int backfill_tags(struct display_state *display_state,
			 struct transport *transport,
			 struct ref_transaction *transaction,
			 struct ref *ref_map,
			 struct fetch_head *fetch_head,
			 const struct fetch_config *config)
{
	int retcode, cannot_reuse;

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
	retcode = fetch_and_consume_refs(display_state, transport, transaction, ref_map,
					 fetch_head, config);

	if (gsecondary) {
		transport_disconnect(gsecondary);
		gsecondary = NULL;
	}

	return retcode;
}

static int do_fetch(struct transport *transport,
		    struct refspec *rs,
		    const struct fetch_config *config)
{
	struct ref_transaction *transaction = NULL;
	struct ref *ref_map = NULL;
	struct display_state display_state = { 0 };
	int autotags = (transport->remote->fetch_tags == 1);
	int retcode = 0;
	const struct ref *remote_refs;
	struct transport_ls_refs_options transport_ls_refs_options =
		TRANSPORT_LS_REFS_OPTIONS_INIT;
	int must_list_refs = 1;
	struct fetch_head fetch_head = { 0 };
	struct strbuf err = STRBUF_INIT;

	if (tags == TAGS_DEFAULT) {
		if (transport->remote->fetch_tags == 2)
			tags = TAGS_SET;
		if (transport->remote->fetch_tags == -1)
			tags = TAGS_UNSET;
	}

	/* if not appending, truncate FETCH_HEAD */
	if (!append && write_fetch_head) {
		retcode = truncate_fetch_head();
		if (retcode)
			goto cleanup;
	}

	if (rs->nr) {
		int i;

		refspec_ref_prefixes(rs, &transport_ls_refs_options.ref_prefixes);

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
	} else {
		struct branch *branch = branch_get(NULL);

		if (transport->remote->fetch.nr)
			refspec_ref_prefixes(&transport->remote->fetch,
					     &transport_ls_refs_options.ref_prefixes);
		if (branch_has_merge_config(branch) &&
		    !strcmp(branch->remote_name, transport->remote->name)) {
			int i;
			for (i = 0; i < branch->merge_nr; i++) {
				strvec_push(&transport_ls_refs_options.ref_prefixes,
					    branch->merge[i]->src);
			}
		}
	}

	if (tags == TAGS_SET || tags == TAGS_DEFAULT) {
		must_list_refs = 1;
		if (transport_ls_refs_options.ref_prefixes.nr)
			strvec_push(&transport_ls_refs_options.ref_prefixes,
				    "refs/tags/");
	}

	if (must_list_refs) {
		trace2_region_enter("fetch", "remote_refs", the_repository);
		remote_refs = transport_get_remote_refs(transport,
							&transport_ls_refs_options);
		trace2_region_leave("fetch", "remote_refs", the_repository);
	} else
		remote_refs = NULL;

	transport_ls_refs_options_release(&transport_ls_refs_options);

	ref_map = get_ref_map(transport->remote, remote_refs, rs,
			      tags, &autotags);
	if (!update_head_ok)
		check_not_current_branch(ref_map);

	retcode = open_fetch_head(&fetch_head);
	if (retcode)
		goto cleanup;

	display_state_init(&display_state, ref_map, transport->url,
			   config->display_format);

	if (atomic_fetch) {
		transaction = ref_transaction_begin(&err);
		if (!transaction) {
			retcode = error("%s", err.buf);
			goto cleanup;
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
		if (rs->nr) {
			retcode = prune_refs(&display_state, rs, transaction, ref_map);
		} else {
			retcode = prune_refs(&display_state, &transport->remote->fetch,
					     transaction, ref_map);
		}
		if (retcode != 0)
			retcode = 1;
	}

	if (fetch_and_consume_refs(&display_state, transport, transaction, ref_map,
				   &fetch_head, config)) {
		retcode = 1;
		goto cleanup;
	}

	/*
	 * If neither --no-tags nor --tags was specified, do automated tag
	 * following.
	 */
	if (tags == TAGS_DEFAULT && autotags) {
		struct ref *tags_ref_map = NULL, **tail = &tags_ref_map;

		find_non_local_tags(remote_refs, transaction, &tags_ref_map, &tail);
		if (tags_ref_map) {
			/*
			 * If backfilling of tags fails then we want to tell
			 * the user so, but we have to continue regardless to
			 * populate upstream information of the references we
			 * have already fetched above. The exception though is
			 * when `--atomic` is passed: in that case we'll abort
			 * the transaction and don't commit anything.
			 */
			if (backfill_tags(&display_state, transport, transaction, tags_ref_map,
					  &fetch_head, config))
				retcode = 1;
		}

		free_refs(tags_ref_map);
	}

	if (transaction) {
		if (retcode)
			goto cleanup;

		retcode = ref_transaction_commit(transaction, &err);
		if (retcode) {
			error("%s", err.buf);
			ref_transaction_free(transaction);
			transaction = NULL;
			goto cleanup;
		}
	}

	commit_fetch_head(&fetch_head);

	if (set_upstream) {
		struct branch *branch = branch_get("HEAD");
		struct ref *rm;
		struct ref *source_ref = NULL;

		/*
		 * We're setting the upstream configuration for the
		 * current branch. The relevant upstream is the
		 * fetched branch that is meant to be merged with the
		 * current one, i.e. the one fetched to FETCH_HEAD.
		 *
		 * When there are several such branches, consider the
		 * request ambiguous and err on the safe side by doing
		 * nothing and just emit a warning.
		 */
		for (rm = ref_map; rm; rm = rm->next) {
			if (!rm->peer_ref) {
				if (source_ref) {
					warning(_("multiple branches detected, incompatible with --set-upstream"));
					goto cleanup;
				} else {
					source_ref = rm;
				}
			}
		}
		if (source_ref) {
			if (!branch) {
				const char *shortname = source_ref->name;
				skip_prefix(shortname, "refs/heads/", &shortname);

				warning(_("could not set upstream of HEAD to '%s' from '%s' when "
					  "it does not point to any branch."),
					shortname, transport->remote->name);
				goto cleanup;
			}

			if (!strcmp(source_ref->name, "HEAD") ||
			    starts_with(source_ref->name, "refs/heads/"))
				install_branch_config(0,
						      branch->name,
						      transport->remote->name,
						      source_ref->name);
			else if (starts_with(source_ref->name, "refs/remotes/"))
				warning(_("not setting upstream for a remote remote-tracking branch"));
			else if (starts_with(source_ref->name, "refs/tags/"))
				warning(_("not setting upstream for a remote tag"));
			else
				warning(_("unknown branch type"));
		} else {
			warning(_("no source branch found;\n"
				  "you need to specify exactly one branch with the --set-upstream option"));
		}
	}

cleanup:
	if (retcode && transaction) {
		ref_transaction_abort(transaction, &err);
		error("%s", err.buf);
	}

	display_state_release(&display_state);
	close_fetch_head(&fetch_head);
	strbuf_release(&err);
	free_refs(ref_map);
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

static int get_remote_group(const char *key, const char *value,
			    const struct config_context *ctx UNUSED,
			    void *priv)
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

static void add_options_to_argv(struct strvec *argv,
				const struct fetch_config *config)
{
	if (dry_run)
		strvec_push(argv, "--dry-run");
	if (prune != -1)
		strvec_push(argv, prune ? "--prune" : "--no-prune");
	if (prune_tags != -1)
		strvec_push(argv, prune_tags ? "--prune-tags" : "--no-prune-tags");
	if (update_head_ok)
		strvec_push(argv, "--update-head-ok");
	if (force)
		strvec_push(argv, "--force");
	if (keep)
		strvec_push(argv, "--keep");
	if (config->recurse_submodules == RECURSE_SUBMODULES_ON)
		strvec_push(argv, "--recurse-submodules");
	else if (config->recurse_submodules == RECURSE_SUBMODULES_OFF)
		strvec_push(argv, "--no-recurse-submodules");
	else if (config->recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND)
		strvec_push(argv, "--recurse-submodules=on-demand");
	if (tags == TAGS_SET)
		strvec_push(argv, "--tags");
	else if (tags == TAGS_UNSET)
		strvec_push(argv, "--no-tags");
	if (verbosity >= 2)
		strvec_push(argv, "-v");
	if (verbosity >= 1)
		strvec_push(argv, "-v");
	else if (verbosity < 0)
		strvec_push(argv, "-q");
	if (family == TRANSPORT_FAMILY_IPV4)
		strvec_push(argv, "--ipv4");
	else if (family == TRANSPORT_FAMILY_IPV6)
		strvec_push(argv, "--ipv6");
	if (!write_fetch_head)
		strvec_push(argv, "--no-write-fetch-head");
	if (config->display_format == DISPLAY_FORMAT_PORCELAIN)
		strvec_pushf(argv, "--porcelain");
}

/* Fetch multiple remotes in parallel */

struct parallel_fetch_state {
	const char **argv;
	struct string_list *remotes;
	int next, result;
	const struct fetch_config *config;
};

static int fetch_next_remote(struct child_process *cp,
			     struct strbuf *out UNUSED,
			     void *cb, void **task_cb)
{
	struct parallel_fetch_state *state = cb;
	char *remote;

	if (state->next < 0 || state->next >= state->remotes->nr)
		return 0;

	remote = state->remotes->items[state->next++].string;
	*task_cb = remote;

	strvec_pushv(&cp->args, state->argv);
	strvec_push(&cp->args, remote);
	cp->git_cmd = 1;

	if (verbosity >= 0 && state->config->display_format != DISPLAY_FORMAT_PORCELAIN)
		printf(_("Fetching %s\n"), remote);

	return 1;
}

static int fetch_failed_to_start(struct strbuf *out UNUSED,
				 void *cb, void *task_cb)
{
	struct parallel_fetch_state *state = cb;
	const char *remote = task_cb;

	state->result = error(_("could not fetch %s"), remote);

	return 0;
}

static int fetch_finished(int result, struct strbuf *out,
			  void *cb, void *task_cb)
{
	struct parallel_fetch_state *state = cb;
	const char *remote = task_cb;

	if (result) {
		strbuf_addf(out, _("could not fetch '%s' (exit code: %d)\n"),
			    remote, result);
		state->result = -1;
	}

	return 0;
}

static int fetch_multiple(struct string_list *list, int max_children,
			  const struct fetch_config *config)
{
	int i, result = 0;
	struct strvec argv = STRVEC_INIT;

	if (!append && write_fetch_head) {
		int errcode = truncate_fetch_head();
		if (errcode)
			return errcode;
	}

	/*
	 * Cancel out the fetch.bundleURI config when running subprocesses,
	 * to avoid fetching from the same bundle list multiple times.
	 */
	strvec_pushl(&argv, "-c", "fetch.bundleURI=",
		     "fetch", "--append", "--no-auto-gc",
		     "--no-write-commit-graph", NULL);
	add_options_to_argv(&argv, config);

	if (max_children != 1 && list->nr != 1) {
		struct parallel_fetch_state state = { argv.v, list, 0, 0, config };
		const struct run_process_parallel_opts opts = {
			.tr2_category = "fetch",
			.tr2_label = "parallel/fetch",

			.processes = max_children,

			.get_next_task = &fetch_next_remote,
			.start_failure = &fetch_failed_to_start,
			.task_finished = &fetch_finished,
			.data = &state,
		};

		strvec_push(&argv, "--end-of-options");

		run_processes_parallel(&opts);
		result = state.result;
	} else
		for (i = 0; i < list->nr; i++) {
			const char *name = list->items[i].string;
			struct child_process cmd = CHILD_PROCESS_INIT;

			strvec_pushv(&cmd.args, argv.v);
			strvec_push(&cmd.args, name);
			if (verbosity >= 0 && config->display_format != DISPLAY_FORMAT_PORCELAIN)
				printf(_("Fetching %s\n"), name);
			cmd.git_cmd = 1;
			if (run_command(&cmd)) {
				error(_("could not fetch %s"), name);
				result = 1;
			}
		}

	strvec_clear(&argv);
	return !!result;
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
	if (!repo_has_promisor_remote(the_repository) && !filter_options.choice)
		return;

	/*
	 * If this is a partial-fetch request, we enable partial on
	 * this repo if not already enabled and remember the given
	 * filter-spec as the default for subsequent fetches to this
	 * remote if there is currently no default filter-spec.
	 */
	if (filter_options.choice) {
		partial_clone_register(remote->name, &filter_options);
		return;
	}

	/*
	 * Do a partial-fetch from the promisor remote using either the
	 * explicitly given filter-spec or inherit the filter-spec from
	 * the config.
	 */
	if (!filter_options.choice)
		partial_clone_get_default_filter_spec(&filter_options, remote->name);
	return;
}

static int fetch_one(struct remote *remote, int argc, const char **argv,
		     int prune_tags_ok, int use_stdin_refspecs,
		     const struct fetch_config *config)
{
	struct refspec rs = REFSPEC_INIT_FETCH;
	int i;
	int exit_code;
	int maybe_prune_tags;
	int remote_via_config = remote_is_configured(remote, 0);

	if (!remote)
		die(_("no remote repository specified; please specify either a URL or a\n"
		      "remote name from which new revisions should be fetched"));

	gtransport = prepare_transport(remote, 1);

	if (prune < 0) {
		/* no command line request */
		if (0 <= remote->prune)
			prune = remote->prune;
		else if (0 <= config->prune)
			prune = config->prune;
		else
			prune = PRUNE_BY_DEFAULT;
	}

	if (prune_tags < 0) {
		/* no command line request */
		if (0 <= remote->prune_tags)
			prune_tags = remote->prune_tags;
		else if (0 <= config->prune_tags)
			prune_tags = config->prune_tags;
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
			i++;
			if (i >= argc)
				die(_("you need to specify a tag name"));

			refspec_appendf(&rs, "refs/tags/%s:refs/tags/%s",
					argv[i], argv[i]);
		} else {
			refspec_append(&rs, argv[i]);
		}
	}

	if (use_stdin_refspecs) {
		struct strbuf line = STRBUF_INIT;
		while (strbuf_getline_lf(&line, stdin) != EOF)
			refspec_append(&rs, line.buf);
		strbuf_release(&line);
	}

	if (server_options.nr)
		gtransport->server_options = &server_options;

	sigchain_push_common(unlock_pack_on_signal);
	atexit(unlock_pack_atexit);
	sigchain_push(SIGPIPE, SIG_IGN);
	exit_code = do_fetch(gtransport, &rs, config);
	sigchain_pop(SIGPIPE);
	refspec_clear(&rs);
	transport_disconnect(gtransport);
	gtransport = NULL;
	return exit_code;
}

int cmd_fetch(int argc, const char **argv, const char *prefix)
{
	struct fetch_config config = {
		.display_format = DISPLAY_FORMAT_FULL,
		.prune = -1,
		.prune_tags = -1,
		.show_forced_updates = 1,
		.recurse_submodules = RECURSE_SUBMODULES_DEFAULT,
		.parallel = 1,
		.submodule_fetch_jobs = -1,
	};
	const char *submodule_prefix = "";
	const char *bundle_uri;
	struct string_list list = STRING_LIST_INIT_DUP;
	struct remote *remote = NULL;
	int all = 0, multiple = 0;
	int result = 0;
	int prune_tags_ok = 1;
	int enable_auto_gc = 1;
	int unshallow = 0;
	int max_jobs = -1;
	int recurse_submodules_cli = RECURSE_SUBMODULES_DEFAULT;
	int recurse_submodules_default = RECURSE_SUBMODULES_ON_DEMAND;
	int fetch_write_commit_graph = -1;
	int stdin_refspecs = 0;
	int negotiate_only = 0;
	int porcelain = 0;
	int i;

	struct option builtin_fetch_options[] = {
		OPT__VERBOSITY(&verbosity),
		OPT_BOOL(0, "all", &all,
			 N_("fetch from all remotes")),
		OPT_BOOL(0, "set-upstream", &set_upstream,
			 N_("set upstream for git pull/fetch")),
		OPT_BOOL('a', "append", &append,
			 N_("append to .git/FETCH_HEAD instead of overwriting")),
		OPT_BOOL(0, "atomic", &atomic_fetch,
			 N_("use atomic transaction to update references")),
		OPT_STRING(0, "upload-pack", &upload_pack, N_("path"),
			   N_("path to upload pack on remote end")),
		OPT__FORCE(&force, N_("force overwrite of local reference"), 0),
		OPT_BOOL('m', "multiple", &multiple,
			 N_("fetch from multiple remotes")),
		OPT_SET_INT('t', "tags", &tags,
			    N_("fetch all tags and associated objects"), TAGS_SET),
		OPT_SET_INT('n', NULL, &tags,
			    N_("do not fetch all tags (--no-tags)"), TAGS_UNSET),
		OPT_INTEGER('j', "jobs", &max_jobs,
			    N_("number of submodules fetched in parallel")),
		OPT_BOOL(0, "prefetch", &prefetch,
			 N_("modify the refspec to place all refs within refs/prefetch/")),
		OPT_BOOL('p', "prune", &prune,
			 N_("prune remote-tracking branches no longer on remote")),
		OPT_BOOL('P', "prune-tags", &prune_tags,
			 N_("prune local tags no longer on remote and clobber changed tags")),
		OPT_CALLBACK_F(0, "recurse-submodules", &recurse_submodules_cli, N_("on-demand"),
			    N_("control recursive fetching of submodules"),
			    PARSE_OPT_OPTARG, option_fetch_parse_recurse_submodules),
		OPT_BOOL(0, "dry-run", &dry_run,
			 N_("dry run")),
		OPT_BOOL(0, "porcelain", &porcelain, N_("machine-readable output")),
		OPT_BOOL(0, "write-fetch-head", &write_fetch_head,
			 N_("write fetched references to the FETCH_HEAD file")),
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
		OPT_SET_INT_F(0, "refetch", &refetch,
			      N_("re-fetch without negotiating common commits"),
			      1, PARSE_OPT_NONEG),
		{ OPTION_STRING, 0, "submodule-prefix", &submodule_prefix, N_("dir"),
			   N_("prepend this to submodule path output"), PARSE_OPT_HIDDEN },
		OPT_CALLBACK_F(0, "recurse-submodules-default",
			   &recurse_submodules_default, N_("on-demand"),
			   N_("default for recursive fetching of submodules "
			      "(lower priority than config files)"),
			   PARSE_OPT_HIDDEN, option_fetch_parse_recurse_submodules),
		OPT_BOOL(0, "update-shallow", &update_shallow,
			 N_("accept refs that update .git/shallow")),
		OPT_CALLBACK_F(0, "refmap", NULL, N_("refmap"),
			       N_("specify fetch refmap"), PARSE_OPT_NONEG, parse_refmap_arg),
		OPT_STRING_LIST('o', "server-option", &server_options, N_("server-specific"), N_("option to transmit")),
		OPT_SET_INT('4', "ipv4", &family, N_("use IPv4 addresses only"),
				TRANSPORT_FAMILY_IPV4),
		OPT_SET_INT('6', "ipv6", &family, N_("use IPv6 addresses only"),
				TRANSPORT_FAMILY_IPV6),
		OPT_STRING_LIST(0, "negotiation-tip", &negotiation_tip, N_("revision"),
				N_("report that we have only objects reachable from this object")),
		OPT_BOOL(0, "negotiate-only", &negotiate_only,
			 N_("do not fetch a packfile; instead, print ancestors of negotiation tips")),
		OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
		OPT_BOOL(0, "auto-maintenance", &enable_auto_gc,
			 N_("run 'maintenance --auto' after fetching")),
		OPT_BOOL(0, "auto-gc", &enable_auto_gc,
			 N_("run 'maintenance --auto' after fetching")),
		OPT_BOOL(0, "show-forced-updates", &config.show_forced_updates,
			 N_("check for forced-updates on all updated branches")),
		OPT_BOOL(0, "write-commit-graph", &fetch_write_commit_graph,
			 N_("write the commit-graph after fetching")),
		OPT_BOOL(0, "stdin", &stdin_refspecs,
			 N_("accept refspecs from stdin")),
		OPT_END()
	};

	packet_trace_identity("fetch");

	/* Record the command line for the reflog */
	strbuf_addstr(&default_rla, "fetch");
	for (i = 1; i < argc; i++) {
		/* This handles non-URLs gracefully */
		char *anon = transport_anonymize_url(argv[i]);

		strbuf_addf(&default_rla, " %s", anon);
		free(anon);
	}

	git_config(git_fetch_config, &config);
	if (the_repository->gitdir) {
		prepare_repo_settings(the_repository);
		the_repository->settings.command_requires_full_index = 0;
	}

	argc = parse_options(argc, argv, prefix,
			     builtin_fetch_options, builtin_fetch_usage, 0);

	if (recurse_submodules_cli != RECURSE_SUBMODULES_DEFAULT)
		config.recurse_submodules = recurse_submodules_cli;

	if (negotiate_only) {
		switch (recurse_submodules_cli) {
		case RECURSE_SUBMODULES_OFF:
		case RECURSE_SUBMODULES_DEFAULT:
			/*
			 * --negotiate-only should never recurse into
			 * submodules. Skip it by setting recurse_submodules to
			 * RECURSE_SUBMODULES_OFF.
			 */
			config.recurse_submodules = RECURSE_SUBMODULES_OFF;
			break;

		default:
			die(_("options '%s' and '%s' cannot be used together"),
			    "--negotiate-only", "--recurse-submodules");
		}
	}

	if (config.recurse_submodules != RECURSE_SUBMODULES_OFF) {
		int *sfjc = config.submodule_fetch_jobs == -1
			    ? &config.submodule_fetch_jobs : NULL;
		int *rs = config.recurse_submodules == RECURSE_SUBMODULES_DEFAULT
			  ? &config.recurse_submodules : NULL;

		fetch_config_from_gitmodules(sfjc, rs);
	}


	if (porcelain) {
		switch (recurse_submodules_cli) {
		case RECURSE_SUBMODULES_OFF:
		case RECURSE_SUBMODULES_DEFAULT:
			/*
			 * Reference updates in submodules would be ambiguous
			 * in porcelain mode, so we reject this combination.
			 */
			config.recurse_submodules = RECURSE_SUBMODULES_OFF;
			break;

		default:
			die(_("options '%s' and '%s' cannot be used together"),
			    "--porcelain", "--recurse-submodules");
		}

		config.display_format = DISPLAY_FORMAT_PORCELAIN;
	}

	if (negotiate_only && !negotiation_tip.nr)
		die(_("--negotiate-only needs one or more --negotiation-tip=*"));

	if (deepen_relative) {
		if (deepen_relative < 0)
			die(_("negative depth in --deepen is not supported"));
		if (depth)
			die(_("options '%s' and '%s' cannot be used together"), "--deepen", "--depth");
		depth = xstrfmt("%d", deepen_relative);
	}
	if (unshallow) {
		if (depth)
			die(_("options '%s' and '%s' cannot be used together"), "--depth", "--unshallow");
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

	/* FETCH_HEAD never gets updated in --dry-run mode */
	if (dry_run)
		write_fetch_head = 0;

	if (!max_jobs)
		max_jobs = online_cpus();

	if (!git_config_get_string_tmp("fetch.bundleuri", &bundle_uri) &&
	    fetch_bundle_uri(the_repository, bundle_uri, NULL))
		warning(_("failed to fetch bundles from '%s'"), bundle_uri);

	if (all) {
		if (argc == 1)
			die(_("fetch --all does not take a repository argument"));
		else if (argc > 1)
			die(_("fetch --all does not make sense with refspecs"));
		(void) for_each_remote(get_one_remote_for_fetch, &list);

		/* do not do fetch_multiple() of one */
		if (list.nr == 1)
			remote = remote_get(list.items[0].string);
	} else if (argc == 0) {
		/* No arguments -- use default remote */
		remote = remote_get(NULL);
	} else if (multiple) {
		/* All arguments are assumed to be remotes or groups */
		for (i = 0; i < argc; i++)
			if (!add_remote_or_group(argv[i], &list))
				die(_("no such remote or remote group: %s"),
				    argv[i]);
	} else {
		/* Single remote or group */
		(void) add_remote_or_group(argv[0], &list);
		if (list.nr > 1) {
			/* More than one remote */
			if (argc > 1)
				die(_("fetching a group and specifying refspecs does not make sense"));
		} else {
			/* Zero or one remotes */
			remote = remote_get(argv[0]);
			prune_tags_ok = (argc == 1);
			argc--;
			argv++;
		}
	}
	string_list_remove_duplicates(&list, 0);

	if (negotiate_only) {
		struct oidset acked_commits = OIDSET_INIT;
		struct oidset_iter iter;
		const struct object_id *oid;

		if (!remote)
			die(_("must supply remote when using --negotiate-only"));
		gtransport = prepare_transport(remote, 1);
		if (gtransport->smart_options) {
			gtransport->smart_options->acked_commits = &acked_commits;
		} else {
			warning(_("protocol does not support --negotiate-only, exiting"));
			result = 1;
			goto cleanup;
		}
		if (server_options.nr)
			gtransport->server_options = &server_options;
		result = transport_fetch_refs(gtransport, NULL);

		oidset_iter_init(&acked_commits, &iter);
		while ((oid = oidset_iter_next(&iter)))
			printf("%s\n", oid_to_hex(oid));
		oidset_clear(&acked_commits);
	} else if (remote) {
		if (filter_options.choice || repo_has_promisor_remote(the_repository))
			fetch_one_setup_partial(remote);
		result = fetch_one(remote, argc, argv, prune_tags_ok, stdin_refspecs,
				   &config);
	} else {
		int max_children = max_jobs;

		if (filter_options.choice)
			die(_("--filter can only be used with the remote "
			      "configured in extensions.partialclone"));

		if (atomic_fetch)
			die(_("--atomic can only be used when fetching "
			      "from one remote"));

		if (stdin_refspecs)
			die(_("--stdin can only be used when fetching "
			      "from one remote"));

		if (max_children < 0)
			max_children = config.parallel;

		/* TODO should this also die if we have a previous partial-clone? */
		result = fetch_multiple(&list, max_children, &config);
	}

	/*
	 * This is only needed after fetch_one(), which does not fetch
	 * submodules by itself.
	 *
	 * When we fetch from multiple remotes, fetch_multiple() has
	 * already updated submodules to grab commits necessary for
	 * the fetched history from each remote, so there is no need
	 * to fetch submodules from here.
	 */
	if (!result && remote && (config.recurse_submodules != RECURSE_SUBMODULES_OFF)) {
		struct strvec options = STRVEC_INIT;
		int max_children = max_jobs;

		if (max_children < 0)
			max_children = config.submodule_fetch_jobs;
		if (max_children < 0)
			max_children = config.parallel;

		add_options_to_argv(&options, &config);
		result = fetch_submodules(the_repository,
					  &options,
					  submodule_prefix,
					  config.recurse_submodules,
					  recurse_submodules_default,
					  verbosity < 0,
					  max_children);
		strvec_clear(&options);
	}

	/*
	 * Skip irrelevant tasks because we know objects were not
	 * fetched.
	 *
	 * NEEDSWORK: as a future optimization, we can return early
	 * whenever objects were not fetched e.g. if we already have all
	 * of them.
	 */
	if (negotiate_only)
		goto cleanup;

	prepare_repo_settings(the_repository);
	if (fetch_write_commit_graph > 0 ||
	    (fetch_write_commit_graph < 0 &&
	     the_repository->settings.fetch_write_commit_graph)) {
		int commit_graph_flags = COMMIT_GRAPH_WRITE_SPLIT;

		if (progress)
			commit_graph_flags |= COMMIT_GRAPH_WRITE_PROGRESS;

		write_commit_graph_reachable(the_repository->objects->odb,
					     commit_graph_flags,
					     NULL);
	}

	if (enable_auto_gc) {
		if (refetch) {
			/*
			 * Hint auto-maintenance strongly to encourage repacking,
			 * but respect config settings disabling it.
			 */
			int opt_val;
			if (git_config_get_int("gc.autopacklimit", &opt_val))
				opt_val = -1;
			if (opt_val != 0)
				git_config_push_parameter("gc.autoPackLimit=1");

			if (git_config_get_int("maintenance.incremental-repack.auto", &opt_val))
				opt_val = -1;
			if (opt_val != 0)
				git_config_push_parameter("maintenance.incremental-repack.auto=-1");
		}
		run_auto_maintenance(verbosity < 0);
	}

 cleanup:
	string_list_clear(&list, 0);
	return result;
}
