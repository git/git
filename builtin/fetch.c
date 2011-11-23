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
#include "transport.h"
#include "submodule.h"

static const char * const builtin_fetch_usage[] = {
	"git fetch [<options>] [<repository> [<refspec>...]]",
	"git fetch [<options>] <group>",
	"git fetch --multiple [<options>] [(<repository> | <group>)...]",
	"git fetch --all [<options>]",
	NULL
};

enum {
	TAGS_UNSET = 0,
	TAGS_DEFAULT = 1,
	TAGS_SET = 2
};

static int all, append, dry_run, force, keep, multiple, prune, update_head_ok, verbosity;
static int progress, recurse_submodules = RECURSE_SUBMODULES_DEFAULT;
static int tags = TAGS_DEFAULT;
static const char *depth;
static const char *upload_pack;
static struct strbuf default_rla = STRBUF_INIT;
static struct transport *transport;
static const char *submodule_prefix = "";
static const char *recurse_submodules_default;

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

static struct option builtin_fetch_options[] = {
	OPT__VERBOSITY(&verbosity),
	OPT_BOOLEAN(0, "all", &all,
		    "fetch from all remotes"),
	OPT_BOOLEAN('a', "append", &append,
		    "append to .git/FETCH_HEAD instead of overwriting"),
	OPT_STRING(0, "upload-pack", &upload_pack, "path",
		   "path to upload pack on remote end"),
	OPT__FORCE(&force, "force overwrite of local branch"),
	OPT_BOOLEAN('m', "multiple", &multiple,
		    "fetch from multiple remotes"),
	OPT_SET_INT('t', "tags", &tags,
		    "fetch all tags and associated objects", TAGS_SET),
	OPT_SET_INT('n', NULL, &tags,
		    "do not fetch all tags (--no-tags)", TAGS_UNSET),
	OPT_BOOLEAN('p', "prune", &prune,
		    "prune remote-tracking branches no longer on remote"),
	{ OPTION_CALLBACK, 0, "recurse-submodules", NULL, "on-demand",
		    "control recursive fetching of submodules",
		    PARSE_OPT_OPTARG, option_parse_recurse_submodules },
	OPT_BOOLEAN(0, "dry-run", &dry_run,
		    "dry run"),
	OPT_BOOLEAN('k', "keep", &keep, "keep downloaded pack"),
	OPT_BOOLEAN('u', "update-head-ok", &update_head_ok,
		    "allow updating of HEAD ref"),
	OPT_BOOLEAN(0, "progress", &progress, "force progress reporting"),
	OPT_STRING(0, "depth", &depth, "depth",
		   "deepen history of shallow clone"),
	{ OPTION_STRING, 0, "submodule-prefix", &submodule_prefix, "dir",
		   "prepend this to submodule path output", PARSE_OPT_HIDDEN },
	{ OPTION_STRING, 0, "recurse-submodules-default",
		   &recurse_submodules_default, NULL,
		   "default mode for recursion", PARSE_OPT_HIDDEN },
	OPT_END()
};

static void unlock_pack(void)
{
	if (transport)
		transport_unlock_pack(transport);
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
				rm->merge = 1;
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
			rm->merge = 1;
	}
}

static void find_non_local_tags(struct transport *transport,
			struct ref **head,
			struct ref ***tail);

static struct ref *get_ref_map(struct transport *transport,
			       struct refspec *refs, int ref_count, int tags,
			       int *autotags)
{
	int i;
	struct ref *rm;
	struct ref *ref_map = NULL;
	struct ref **tail = &ref_map;

	const struct ref *remote_refs = transport_get_remote_refs(transport);

	if (ref_count || tags == TAGS_SET) {
		for (i = 0; i < ref_count; i++) {
			get_fetch_map(remote_refs, &refs[i], &tail, 0);
			if (refs[i].dst && refs[i].dst[0])
				*autotags = 1;
		}
		/* Merge everything on the command line, but not --tags */
		for (rm = ref_map; rm; rm = rm->next)
			rm->merge = 1;
		if (tags == TAGS_SET)
			get_fetch_map(remote_refs, tag_refspec, &tail, 0);
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
					ref_map->merge = 1;
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
			ref_map->merge = 1;
			tail = &ref_map->next;
		}
	}
	if (tags == TAGS_DEFAULT && *autotags)
		find_non_local_tags(transport, &ref_map, &tail);
	ref_remove_duplicates(ref_map);

	return ref_map;
}

#define STORE_REF_ERROR_OTHER 1
#define STORE_REF_ERROR_DF_CONFLICT 2

static int s_update_ref(const char *action,
			struct ref *ref,
			int check_old)
{
	char msg[1024];
	char *rla = getenv("GIT_REFLOG_ACTION");
	static struct ref_lock *lock;

	if (dry_run)
		return 0;
	if (!rla)
		rla = default_rla.buf;
	snprintf(msg, sizeof(msg), "%s: %s", rla, action);
	lock = lock_any_ref_for_update(ref->name,
				       check_old ? ref->old_sha1 : NULL, 0);
	if (!lock)
		return errno == ENOTDIR ? STORE_REF_ERROR_DF_CONFLICT :
					  STORE_REF_ERROR_OTHER;
	if (write_ref_sha1(lock, ref->new_sha1, msg) < 0)
		return errno == ENOTDIR ? STORE_REF_ERROR_DF_CONFLICT :
					  STORE_REF_ERROR_OTHER;
	return 0;
}

#define REFCOL_WIDTH  10

static int update_local_ref(struct ref *ref,
			    const char *remote,
			    char *display)
{
	struct commit *current = NULL, *updated;
	enum object_type type;
	struct branch *current_branch = branch_get(NULL);
	const char *pretty_ref = prettify_refname(ref->name);

	*display = 0;
	type = sha1_object_info(ref->new_sha1, NULL);
	if (type < 0)
		die(_("object %s not found"), sha1_to_hex(ref->new_sha1));

	if (!hashcmp(ref->old_sha1, ref->new_sha1)) {
		if (verbosity > 0)
			sprintf(display, "= %-*s %-*s -> %s", TRANSPORT_SUMMARY_WIDTH,
				_("[up to date]"), REFCOL_WIDTH, remote,
				pretty_ref);
		return 0;
	}

	if (current_branch &&
	    !strcmp(ref->name, current_branch->name) &&
	    !(update_head_ok || is_bare_repository()) &&
	    !is_null_sha1(ref->old_sha1)) {
		/*
		 * If this is the head, and it's not okay to update
		 * the head, and the old value of the head isn't empty...
		 */
		sprintf(display, _("! %-*s %-*s -> %s  (can't fetch in current branch)"),
			TRANSPORT_SUMMARY_WIDTH, _("[rejected]"), REFCOL_WIDTH, remote,
			pretty_ref);
		return 1;
	}

	if (!is_null_sha1(ref->old_sha1) &&
	    !prefixcmp(ref->name, "refs/tags/")) {
		int r;
		r = s_update_ref("updating tag", ref, 0);
		sprintf(display, "%c %-*s %-*s -> %s%s", r ? '!' : '-',
			TRANSPORT_SUMMARY_WIDTH, _("[tag update]"), REFCOL_WIDTH, remote,
			pretty_ref, r ? _("  (unable to update local ref)") : "");
		return r;
	}

	current = lookup_commit_reference_gently(ref->old_sha1, 1);
	updated = lookup_commit_reference_gently(ref->new_sha1, 1);
	if (!current || !updated) {
		const char *msg;
		const char *what;
		int r;
		if (!strncmp(ref->name, "refs/tags/", 10)) {
			msg = "storing tag";
			what = _("[new tag]");
		}
		else {
			msg = "storing head";
			what = _("[new branch]");
			if ((recurse_submodules != RECURSE_SUBMODULES_OFF) &&
			    (recurse_submodules != RECURSE_SUBMODULES_ON))
				check_for_new_submodule_commits(ref->new_sha1);
		}

		r = s_update_ref(msg, ref, 0);
		sprintf(display, "%c %-*s %-*s -> %s%s", r ? '!' : '*',
			TRANSPORT_SUMMARY_WIDTH, what, REFCOL_WIDTH, remote, pretty_ref,
			r ? _("  (unable to update local ref)") : "");
		return r;
	}

	if (in_merge_bases(current, &updated, 1)) {
		char quickref[83];
		int r;
		strcpy(quickref, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
		strcat(quickref, "..");
		strcat(quickref, find_unique_abbrev(ref->new_sha1, DEFAULT_ABBREV));
		if ((recurse_submodules != RECURSE_SUBMODULES_OFF) &&
		    (recurse_submodules != RECURSE_SUBMODULES_ON))
			check_for_new_submodule_commits(ref->new_sha1);
		r = s_update_ref("fast-forward", ref, 1);
		sprintf(display, "%c %-*s %-*s -> %s%s", r ? '!' : ' ',
			TRANSPORT_SUMMARY_WIDTH, quickref, REFCOL_WIDTH, remote,
			pretty_ref, r ? _("  (unable to update local ref)") : "");
		return r;
	} else if (force || ref->force) {
		char quickref[84];
		int r;
		strcpy(quickref, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
		strcat(quickref, "...");
		strcat(quickref, find_unique_abbrev(ref->new_sha1, DEFAULT_ABBREV));
		if ((recurse_submodules != RECURSE_SUBMODULES_OFF) &&
		    (recurse_submodules != RECURSE_SUBMODULES_ON))
			check_for_new_submodule_commits(ref->new_sha1);
		r = s_update_ref("forced-update", ref, 1);
		sprintf(display, "%c %-*s %-*s -> %s  (%s)", r ? '!' : '+',
			TRANSPORT_SUMMARY_WIDTH, quickref, REFCOL_WIDTH, remote,
			pretty_ref,
			r ? _("unable to update local ref") : _("forced update"));
		return r;
	} else {
		sprintf(display, "! %-*s %-*s -> %s  %s",
			TRANSPORT_SUMMARY_WIDTH, _("[rejected]"), REFCOL_WIDTH, remote,
			pretty_ref, _("(non-fast-forward)"));
		return 1;
	}
}

static int store_updated_refs(const char *raw_url, const char *remote_name,
		struct ref *ref_map)
{
	FILE *fp;
	struct commit *commit;
	int url_len, i, note_len, shown_url = 0, rc = 0;
	char note[1024];
	const char *what, *kind;
	struct ref *rm;
	char *url, *filename = dry_run ? "/dev/null" : git_path("FETCH_HEAD");

	fp = fopen(filename, "a");
	if (!fp)
		return error(_("cannot open %s: %s\n"), filename, strerror(errno));

	if (raw_url)
		url = transport_anonymize_url(raw_url);
	else
		url = xstrdup("foreign");
	for (rm = ref_map; rm; rm = rm->next) {
		struct ref *ref = NULL;

		if (rm->peer_ref) {
			ref = xcalloc(1, sizeof(*ref) + strlen(rm->peer_ref->name) + 1);
			strcpy(ref->name, rm->peer_ref->name);
			hashcpy(ref->old_sha1, rm->peer_ref->old_sha1);
			hashcpy(ref->new_sha1, rm->old_sha1);
			ref->force = rm->peer_ref->force;
		}

		commit = lookup_commit_reference_gently(rm->old_sha1, 1);
		if (!commit)
			rm->merge = 0;

		if (!strcmp(rm->name, "HEAD")) {
			kind = "";
			what = "";
		}
		else if (!prefixcmp(rm->name, "refs/heads/")) {
			kind = "branch";
			what = rm->name + 11;
		}
		else if (!prefixcmp(rm->name, "refs/tags/")) {
			kind = "tag";
			what = rm->name + 10;
		}
		else if (!prefixcmp(rm->name, "refs/remotes/")) {
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

		note_len = 0;
		if (*what) {
			if (*kind)
				note_len += sprintf(note + note_len, "%s ",
						    kind);
			note_len += sprintf(note + note_len, "'%s' of ", what);
		}
		note[note_len] = '\0';
		fprintf(fp, "%s\t%s\t%s",
			sha1_to_hex(commit ? commit->object.sha1 :
				    rm->old_sha1),
			rm->merge ? "" : "not-for-merge",
			note);
		for (i = 0; i < url_len; ++i)
			if ('\n' == url[i])
				fputs("\\n", fp);
			else
				fputc(url[i], fp);
		fputc('\n', fp);

		if (ref) {
			rc |= update_local_ref(ref, what, note);
			free(ref);
		} else
			sprintf(note, "* %-*s %-*s -> FETCH_HEAD",
				TRANSPORT_SUMMARY_WIDTH, *kind ? kind : "branch",
				 REFCOL_WIDTH, *what ? what : "HEAD");
		if (*note) {
			if (verbosity >= 0 && !shown_url) {
				fprintf(stderr, _("From %.*s\n"),
						url_len, url);
				shown_url = 1;
			}
			if (verbosity >= 0)
				fprintf(stderr, " %s\n", note);
		}
	}
	free(url);
	fclose(fp);
	if (rc & STORE_REF_ERROR_DF_CONFLICT)
		error(_("some local refs could not be updated; try running\n"
		      " 'git remote prune %s' to remove any old, conflicting "
		      "branches"), remote_name);
	return rc;
}

/*
 * We would want to bypass the object transfer altogether if
 * everything we are going to fetch already exists and is connected
 * locally.
 *
 * The refs we are going to fetch are in ref_map.  If running
 *
 *  $ git rev-list --objects --stdin --not --all
 *
 * (feeding all the refs in ref_map on its standard input)
 * does not error out, that means everything reachable from the
 * refs we are going to fetch exists and is connected to some of
 * our existing refs.
 */
static int quickfetch(struct ref *ref_map)
{
	struct child_process revlist;
	struct ref *ref;
	int err;
	const char *argv[] = {"rev-list",
		"--quiet", "--objects", "--stdin", "--not", "--all", NULL};

	/*
	 * If we are deepening a shallow clone we already have these
	 * objects reachable.  Running rev-list here will return with
	 * a good (0) exit status and we'll bypass the fetch that we
	 * really need to perform.  Claiming failure now will ensure
	 * we perform the network exchange to deepen our history.
	 */
	if (depth)
		return -1;

	if (!ref_map)
		return 0;

	memset(&revlist, 0, sizeof(revlist));
	revlist.argv = argv;
	revlist.git_cmd = 1;
	revlist.no_stdout = 1;
	revlist.no_stderr = 1;
	revlist.in = -1;

	err = start_command(&revlist);
	if (err) {
		error(_("could not run rev-list"));
		return err;
	}

	/*
	 * If rev-list --stdin encounters an unknown commit, it terminates,
	 * which will cause SIGPIPE in the write loop below.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);

	for (ref = ref_map; ref; ref = ref->next) {
		if (write_in_full(revlist.in, sha1_to_hex(ref->old_sha1), 40) < 0 ||
		    write_str_in_full(revlist.in, "\n") < 0) {
			if (errno != EPIPE && errno != EINVAL)
				error(_("failed write to rev-list: %s"), strerror(errno));
			err = -1;
			break;
		}
	}

	if (close(revlist.in)) {
		error(_("failed to close rev-list's stdin: %s"), strerror(errno));
		err = -1;
	}

	sigchain_pop(SIGPIPE);

	return finish_command(&revlist) || err;
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

static int prune_refs(struct refspec *refs, int ref_count, struct ref *ref_map)
{
	int result = 0;
	struct ref *ref, *stale_refs = get_stale_heads(refs, ref_count, ref_map);
	const char *dangling_msg = dry_run
		? _("   (%s will become dangling)\n")
		: _("   (%s has become dangling)\n");

	for (ref = stale_refs; ref; ref = ref->next) {
		if (!dry_run)
			result |= delete_ref(ref->name, NULL, 0);
		if (verbosity >= 0) {
			fprintf(stderr, " x %-*s %-*s -> %s\n",
				TRANSPORT_SUMMARY_WIDTH, _("[deleted]"),
				REFCOL_WIDTH, _("(none)"), prettify_refname(ref->name));
			warn_dangling_symref(stderr, dangling_msg, ref->name);
		}
	}
	free_refs(stale_refs);
	return result;
}

static int add_existing(const char *refname, const unsigned char *sha1,
			int flag, void *cbdata)
{
	struct string_list *list = (struct string_list *)cbdata;
	struct string_list_item *item = string_list_insert(list, refname);
	item->util = (void *)sha1;
	return 0;
}

static int will_fetch(struct ref **head, const unsigned char *sha1)
{
	struct ref *rm = *head;
	while (rm) {
		if (!hashcmp(rm->old_sha1, sha1))
			return 1;
		rm = rm->next;
	}
	return 0;
}

static void find_non_local_tags(struct transport *transport,
			struct ref **head,
			struct ref ***tail)
{
	struct string_list existing_refs = STRING_LIST_INIT_NODUP;
	struct string_list remote_refs = STRING_LIST_INIT_NODUP;
	const struct ref *ref;
	struct string_list_item *item = NULL;

	for_each_ref(add_existing, &existing_refs);
	for (ref = transport_get_remote_refs(transport); ref; ref = ref->next) {
		if (prefixcmp(ref->name, "refs/tags"))
			continue;

		/*
		 * The peeled ref always follows the matching base
		 * ref, so if we see a peeled ref that we don't want
		 * to fetch then we can mark the ref entry in the list
		 * as one to ignore by setting util to NULL.
		 */
		if (!suffixcmp(ref->name, "^{}")) {
			if (item && !has_sha1_file(ref->old_sha1) &&
			    !will_fetch(head, ref->old_sha1) &&
			    !has_sha1_file(item->util) &&
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
		if (item && !has_sha1_file(item->util) &&
		    !will_fetch(head, item->util))
			item->util = NULL;

		item = NULL;

		/* skip duplicates and refs that we already have */
		if (string_list_has_string(&remote_refs, ref->name) ||
		    string_list_has_string(&existing_refs, ref->name))
			continue;

		item = string_list_insert(&remote_refs, ref->name);
		item->util = (void *)ref->old_sha1;
	}
	string_list_clear(&existing_refs, 0);

	/*
	 * We may have a final lightweight tag that needs to be
	 * checked to see if it needs fetching.
	 */
	if (item && !has_sha1_file(item->util) &&
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
			hashcpy(rm->old_sha1, item->util);
			**tail = rm;
			*tail = &rm->next;
		}
	}

	string_list_clear(&remote_refs, 0);
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
	char *filename = git_path("FETCH_HEAD");
	FILE *fp = fopen(filename, "w");

	if (!fp)
		return error(_("cannot open %s: %s\n"), filename, strerror(errno));
	fclose(fp);
	return 0;
}

static int do_fetch(struct transport *transport,
		    struct refspec *refs, int ref_count)
{
	struct string_list existing_refs = STRING_LIST_INIT_NODUP;
	struct string_list_item *peer_item = NULL;
	struct ref *ref_map;
	struct ref *rm;
	int autotags = (transport->remote->fetch_tags == 1);

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
		int errcode = truncate_fetch_head();
		if (errcode)
			return errcode;
	}

	ref_map = get_ref_map(transport, refs, ref_count, tags, &autotags);
	if (!update_head_ok)
		check_not_current_branch(ref_map);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref) {
			peer_item = string_list_lookup(&existing_refs,
						       rm->peer_ref->name);
			if (peer_item)
				hashcpy(rm->peer_ref->old_sha1,
					peer_item->util);
		}
	}

	if (tags == TAGS_DEFAULT && autotags)
		transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, "1");
	if (fetch_refs(transport, ref_map)) {
		free_refs(ref_map);
		return 1;
	}
	if (prune) {
		/* If --tags was specified, pretend the user gave us the canonical tags refspec */
		if (tags == TAGS_SET) {
			const char *tags_str = "refs/tags/*:refs/tags/*";
			struct refspec *tags_refspec, *refspec;

			/* Copy the refspec and add the tags to it */
			refspec = xcalloc(ref_count + 1, sizeof(struct refspec));
			tags_refspec = parse_fetch_refspec(1, &tags_str);
			memcpy(refspec, refs, ref_count * sizeof(struct refspec));
			memcpy(&refspec[ref_count], tags_refspec, sizeof(struct refspec));
			ref_count++;

			prune_refs(refspec, ref_count, ref_map);

			ref_count--;
			/* The rest of the strings belong to fetch_one */
			free_refspec(1, tags_refspec);
			free(refspec);
		} else if (ref_count) {
			prune_refs(refs, ref_count, ref_map);
		} else {
			prune_refs(transport->remote->fetch, transport->remote->fetch_refspec_nr, ref_map);
		}
	}
	free_refs(ref_map);

	/* if neither --no-tags nor --tags was specified, do automated tag
	 * following ... */
	if (tags == TAGS_DEFAULT && autotags) {
		struct ref **tail = &ref_map;
		ref_map = NULL;
		find_non_local_tags(transport, &ref_map, &tail);
		if (ref_map) {
			transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, NULL);
			transport_set_option(transport, TRANS_OPT_DEPTH, "0");
			fetch_refs(transport, ref_map);
		}
		free_refs(ref_map);
	}

	return 0;
}

static void set_option(const char *name, const char *value)
{
	int r = transport_set_option(transport, name, value);
	if (r < 0)
		die(_("Option \"%s\" value \"%s\" is not valid for %s"),
			name, value, transport->url);
	if (r > 0)
		warning(_("Option \"%s\" is ignored for %s\n"),
			name, transport->url);
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

	if (!prefixcmp(key, "remotes.") &&
			!strcmp(key + 8, g->name)) {
		/* split list by white space */
		int space = strcspn(value, " \t\n");
		while (*value) {
			if (space > 1) {
				string_list_append(g->list,
						   xstrndup(value, space));
			}
			value += space + (value[space] != '\0');
			space = strcspn(value, " \t\n");
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
		struct remote *remote;
		if (!remote_is_configured(name))
			return 0;
		remote = remote_get(name);
		string_list_append(list, remote->name);
	}
	return 1;
}

static void add_options_to_argv(int *argc, const char **argv)
{
	if (dry_run)
		argv[(*argc)++] = "--dry-run";
	if (prune)
		argv[(*argc)++] = "--prune";
	if (update_head_ok)
		argv[(*argc)++] = "--update-head-ok";
	if (force)
		argv[(*argc)++] = "--force";
	if (keep)
		argv[(*argc)++] = "--keep";
	if (recurse_submodules == RECURSE_SUBMODULES_ON)
		argv[(*argc)++] = "--recurse-submodules";
	else if (recurse_submodules == RECURSE_SUBMODULES_ON_DEMAND)
		argv[(*argc)++] = "--recurse-submodules=on-demand";
	if (verbosity >= 2)
		argv[(*argc)++] = "-v";
	if (verbosity >= 1)
		argv[(*argc)++] = "-v";
	else if (verbosity < 0)
		argv[(*argc)++] = "-q";

}

static int fetch_multiple(struct string_list *list)
{
	int i, result = 0;
	const char *argv[12] = { "fetch", "--append" };
	int argc = 2;

	add_options_to_argv(&argc, argv);

	if (!append && !dry_run) {
		int errcode = truncate_fetch_head();
		if (errcode)
			return errcode;
	}

	for (i = 0; i < list->nr; i++) {
		const char *name = list->items[i].string;
		argv[argc] = name;
		argv[argc + 1] = NULL;
		if (verbosity >= 0)
			printf(_("Fetching %s\n"), name);
		if (run_command_v_opt(argv, RUN_GIT_CMD)) {
			error(_("Could not fetch %s"), name);
			result = 1;
		}
	}

	return result;
}

static int fetch_one(struct remote *remote, int argc, const char **argv)
{
	int i;
	static const char **refs = NULL;
	struct refspec *refspec;
	int ref_nr = 0;
	int exit_code;

	if (!remote)
		die(_("No remote repository specified.  Please, specify either a URL or a\n"
		    "remote name from which new revisions should be fetched."));

	transport = transport_get(remote, NULL);
	transport_set_verbosity(transport, verbosity, progress);
	if (upload_pack)
		set_option(TRANS_OPT_UPLOADPACK, upload_pack);
	if (keep)
		set_option(TRANS_OPT_KEEP, "yes");
	if (depth)
		set_option(TRANS_OPT_DEPTH, depth);

	if (argc > 0) {
		int j = 0;
		refs = xcalloc(argc + 1, sizeof(const char *));
		for (i = 0; i < argc; i++) {
			if (!strcmp(argv[i], "tag")) {
				char *ref;
				i++;
				if (i >= argc)
					die(_("You need to specify a tag name."));
				ref = xmalloc(strlen(argv[i]) * 2 + 22);
				strcpy(ref, "refs/tags/");
				strcat(ref, argv[i]);
				strcat(ref, ":refs/tags/");
				strcat(ref, argv[i]);
				refs[j++] = ref;
			} else
				refs[j++] = argv[i];
		}
		refs[j] = NULL;
		ref_nr = j;
	}

	sigchain_push_common(unlock_pack_on_signal);
	atexit(unlock_pack);
	refspec = parse_fetch_refspec(ref_nr, refs);
	exit_code = do_fetch(transport, refspec, ref_nr);
	free_refspec(ref_nr, refspec);
	transport_disconnect(transport);
	transport = NULL;
	return exit_code;
}

int cmd_fetch(int argc, const char **argv, const char *prefix)
{
	int i;
	struct string_list list = STRING_LIST_INIT_NODUP;
	struct remote *remote;
	int result = 0;

	packet_trace_identity("fetch");

	/* Record the command line for the reflog */
	strbuf_addstr(&default_rla, "fetch");
	for (i = 1; i < argc; i++)
		strbuf_addf(&default_rla, " %s", argv[i]);

	argc = parse_options(argc, argv, prefix,
			     builtin_fetch_options, builtin_fetch_usage, 0);

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
		const char *options[10];
		int num_options = 0;
		add_options_to_argv(&num_options, options);
		result = fetch_populated_submodules(num_options, options,
						    submodule_prefix,
						    recurse_submodules,
						    verbosity < 0);
	}

	/* All names were strdup()ed or strndup()ed */
	list.strdup_strings = 1;
	string_list_clear(&list, 0);

	return result;
}
