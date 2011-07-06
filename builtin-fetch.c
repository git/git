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

static const char * const builtin_fetch_usage[] = {
	"git fetch [options] [<repository> <refspec>...]",
	NULL
};

enum {
	TAGS_UNSET = 0,
	TAGS_DEFAULT = 1,
	TAGS_SET = 2
};

static int append, force, keep, update_head_ok, verbose, quiet;
static int tags = TAGS_DEFAULT;
static const char *depth;
static const char *upload_pack;
static struct strbuf default_rla = STRBUF_INIT;
static struct transport *transport;

static struct option builtin_fetch_options[] = {
	OPT__QUIET(&quiet),
	OPT__VERBOSE(&verbose),
	OPT_BOOLEAN('a', "append", &append,
		    "append to .git/FETCH_HEAD instead of overwriting"),
	OPT_STRING(0, "upload-pack", &upload_pack, "PATH",
		   "path to upload pack on remote end"),
	OPT_BOOLEAN('f', "force", &force,
		    "force overwrite of local branch"),
	OPT_SET_INT('t', "tags", &tags,
		    "fetch all tags and associated objects", TAGS_SET),
	OPT_SET_INT('n', NULL, &tags,
		    "do not fetch all tags (--no-tags)", TAGS_UNSET),
	OPT_BOOLEAN('k', "keep", &keep, "keep downloaded pack"),
	OPT_BOOLEAN('u', "update-head-ok", &update_head_ok,
		    "allow updating of HEAD ref"),
	OPT_STRING(0, "depth", &depth, "DEPTH",
		   "deepen history of shallow clone"),
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
	signal(SIGINT, SIG_DFL);
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
		 * Not fetched to a tracking branch?  We need to fetch
		 * it anyway to allow this branch's "branch.$name.merge"
		 * to be honored by git-pull, but we do not have to
		 * fail if branch.$name.merge is misconfigured to point
		 * at a nonexisting branch.  If we were indeed called by
		 * git-pull, it will notice the misconfiguration because
		 * there is no entry in the resulting FETCH_HEAD marked
		 * for merging.
		 */
		refspec.src = branch->merge[i]->src;
		refspec.dst = NULL;
		refspec.pattern = 0;
		refspec.force = 0;
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
		if (remote && (remote->fetch_refspec_nr || has_merge)) {
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
			 */
			if (has_merge &&
			    !strcmp(branch->remote_name, remote->name))
				add_merge_config(&ref_map, remote_refs, branch, &tail);
		} else {
			ref_map = get_remote_ref(remote_refs, "HEAD");
			if (!ref_map)
				die("Couldn't find remote ref HEAD");
			ref_map->merge = 1;
			tail = &ref_map->next;
		}
	}
	if (tags == TAGS_DEFAULT && *autotags)
		find_non_local_tags(transport, &ref_map, &tail);
	ref_remove_duplicates(ref_map);

	return ref_map;
}

static int s_update_ref(const char *action,
			struct ref *ref,
			int check_old)
{
	char msg[1024];
	char *rla = getenv("GIT_REFLOG_ACTION");
	static struct ref_lock *lock;

	if (!rla)
		rla = default_rla.buf;
	snprintf(msg, sizeof(msg), "%s: %s", rla, action);
	lock = lock_any_ref_for_update(ref->name,
				       check_old ? ref->old_sha1 : NULL, 0);
	if (!lock)
		return 2;
	if (write_ref_sha1(lock, ref->new_sha1, msg) < 0)
		return 2;
	return 0;
}

#define SUMMARY_WIDTH (2 * DEFAULT_ABBREV + 3)
#define REFCOL_WIDTH  10

static int update_local_ref(struct ref *ref,
			    const char *remote,
			    int verbose,
			    char *display)
{
	struct commit *current = NULL, *updated;
	enum object_type type;
	struct branch *current_branch = branch_get(NULL);
	const char *pretty_ref = ref->name + (
		!prefixcmp(ref->name, "refs/heads/") ? 11 :
		!prefixcmp(ref->name, "refs/tags/") ? 10 :
		!prefixcmp(ref->name, "refs/remotes/") ? 13 :
		0);

	*display = 0;
	type = sha1_object_info(ref->new_sha1, NULL);
	if (type < 0)
		die("object %s not found", sha1_to_hex(ref->new_sha1));

	if (!hashcmp(ref->old_sha1, ref->new_sha1)) {
		if (verbose)
			sprintf(display, "= %-*s %-*s -> %s", SUMMARY_WIDTH,
				"[up to date]", REFCOL_WIDTH, remote,
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
		sprintf(display, "! %-*s %-*s -> %s  (can't fetch in current branch)",
			SUMMARY_WIDTH, "[rejected]", REFCOL_WIDTH, remote,
			pretty_ref);
		return 1;
	}

	if (!is_null_sha1(ref->old_sha1) &&
	    !prefixcmp(ref->name, "refs/tags/")) {
		int r;
		r = s_update_ref("updating tag", ref, 0);
		sprintf(display, "%c %-*s %-*s -> %s%s", r ? '!' : '-',
			SUMMARY_WIDTH, "[tag update]", REFCOL_WIDTH, remote,
			pretty_ref, r ? "  (unable to update local ref)" : "");
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
			what = "[new tag]";
		}
		else {
			msg = "storing head";
			what = "[new branch]";
		}

		r = s_update_ref(msg, ref, 0);
		sprintf(display, "%c %-*s %-*s -> %s%s", r ? '!' : '*',
			SUMMARY_WIDTH, what, REFCOL_WIDTH, remote, pretty_ref,
			r ? "  (unable to update local ref)" : "");
		return r;
	}

	if (in_merge_bases(current, &updated, 1)) {
		char quickref[83];
		int r;
		strcpy(quickref, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
		strcat(quickref, "..");
		strcat(quickref, find_unique_abbrev(ref->new_sha1, DEFAULT_ABBREV));
		r = s_update_ref("fast forward", ref, 1);
		sprintf(display, "%c %-*s %-*s -> %s%s", r ? '!' : ' ',
			SUMMARY_WIDTH, quickref, REFCOL_WIDTH, remote,
			pretty_ref, r ? "  (unable to update local ref)" : "");
		return r;
	} else if (force || ref->force) {
		char quickref[84];
		int r;
		strcpy(quickref, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
		strcat(quickref, "...");
		strcat(quickref, find_unique_abbrev(ref->new_sha1, DEFAULT_ABBREV));
		r = s_update_ref("forced-update", ref, 1);
		sprintf(display, "%c %-*s %-*s -> %s  (%s)", r ? '!' : '+',
			SUMMARY_WIDTH, quickref, REFCOL_WIDTH, remote,
			pretty_ref,
			r ? "unable to update local ref" : "forced update");
		return r;
	} else {
		sprintf(display, "! %-*s %-*s -> %s  (non fast forward)",
			SUMMARY_WIDTH, "[rejected]", REFCOL_WIDTH, remote,
			pretty_ref);
		return 1;
	}
}

static int store_updated_refs(const char *url, const char *remote_name,
		struct ref *ref_map)
{
	FILE *fp;
	struct commit *commit;
	int url_len, i, note_len, shown_url = 0, rc = 0;
	char note[1024];
	const char *what, *kind;
	struct ref *rm;
	char *filename = git_path("FETCH_HEAD");

	fp = fopen(filename, "a");
	if (!fp)
		return error("cannot open %s: %s\n", filename, strerror(errno));
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
			kind = "remote branch";
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
		note_len += sprintf(note + note_len, "%.*s", url_len, url);
		fprintf(fp, "%s\t%s\t%s\n",
			sha1_to_hex(commit ? commit->object.sha1 :
				    rm->old_sha1),
			rm->merge ? "" : "not-for-merge",
			note);

		if (ref)
			rc |= update_local_ref(ref, what, verbose, note);
		else
			sprintf(note, "* %-*s %-*s -> FETCH_HEAD",
				SUMMARY_WIDTH, *kind ? kind : "branch",
				 REFCOL_WIDTH, *what ? what : "HEAD");
		if (*note) {
			if (!shown_url) {
				fprintf(stderr, "From %.*s\n",
						url_len, url);
				shown_url = 1;
			}
			fprintf(stderr, " %s\n", note);
		}
	}
	fclose(fp);
	if (rc & 2)
		error("some local refs could not be updated; try running\n"
		      " 'git remote prune %s' to remove any old, conflicting "
		      "branches", remote_name);
	return rc;
}

/*
 * We would want to bypass the object transfer altogether if
 * everything we are going to fetch already exists and connected
 * locally.
 *
 * The refs we are going to fetch are in to_fetch (nr_heads in
 * total).  If running
 *
 *  $ git-rev-list --objects to_fetch[0] to_fetch[1] ... --not --all
 *
 * does not error out, that means everything reachable from the
 * refs we are going to fetch exists and is connected to some of
 * our existing refs.
 */
static int quickfetch(struct ref *ref_map)
{
	struct child_process revlist;
	struct ref *ref;
	char **argv;
	int i, err;

	/*
	 * If we are deepening a shallow clone we already have these
	 * objects reachable.  Running rev-list here will return with
	 * a good (0) exit status and we'll bypass the fetch that we
	 * really need to perform.  Claiming failure now will ensure
	 * we perform the network exchange to deepen our history.
	 */
	if (depth)
		return -1;

	for (i = 0, ref = ref_map; ref; ref = ref->next)
		i++;
	if (!i)
		return 0;

	argv = xmalloc(sizeof(*argv) * (i + 6));
	i = 0;
	argv[i++] = xstrdup("rev-list");
	argv[i++] = xstrdup("--quiet");
	argv[i++] = xstrdup("--objects");
	for (ref = ref_map; ref; ref = ref->next)
		argv[i++] = xstrdup(sha1_to_hex(ref->old_sha1));
	argv[i++] = xstrdup("--not");
	argv[i++] = xstrdup("--all");
	argv[i++] = NULL;

	memset(&revlist, 0, sizeof(revlist));
	revlist.argv = (const char**)argv;
	revlist.git_cmd = 1;
	revlist.no_stdin = 1;
	revlist.no_stdout = 1;
	revlist.no_stderr = 1;
	err = run_command(&revlist);

	for (i = 0; argv[i]; i++)
		free(argv[i]);
	free(argv);
	return err;
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

static int add_existing(const char *refname, const unsigned char *sha1,
			int flag, void *cbdata)
{
	struct string_list *list = (struct string_list *)cbdata;
	string_list_insert(refname, list);
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
	struct string_list existing_refs = { NULL, 0, 0, 0 };
	struct string_list new_refs = { NULL, 0, 0, 1 };
	char *ref_name;
	int ref_name_len;
	const unsigned char *ref_sha1;
	const struct ref *tag_ref;
	struct ref *rm = NULL;
	const struct ref *ref;

	for_each_ref(add_existing, &existing_refs);
	for (ref = transport_get_remote_refs(transport); ref; ref = ref->next) {
		if (prefixcmp(ref->name, "refs/tags"))
			continue;

		ref_name = xstrdup(ref->name);
		ref_name_len = strlen(ref_name);
		ref_sha1 = ref->old_sha1;

		if (!strcmp(ref_name + ref_name_len - 3, "^{}")) {
			ref_name[ref_name_len - 3] = 0;
			tag_ref = transport_get_remote_refs(transport);
			while (tag_ref) {
				if (!strcmp(tag_ref->name, ref_name)) {
					ref_sha1 = tag_ref->old_sha1;
					break;
				}
				tag_ref = tag_ref->next;
			}
		}

		if (!string_list_has_string(&existing_refs, ref_name) &&
		    !string_list_has_string(&new_refs, ref_name) &&
		    (has_sha1_file(ref->old_sha1) ||
		     will_fetch(head, ref->old_sha1))) {
			string_list_insert(ref_name, &new_refs);

			rm = alloc_ref_from_str(ref_name);
			rm->peer_ref = alloc_ref_from_str(ref_name);
			hashcpy(rm->old_sha1, ref_sha1);

			**tail = rm;
			*tail = &rm->next;
		}
		free(ref_name);
	}
	string_list_clear(&existing_refs, 0);
	string_list_clear(&new_refs, 0);
}

static int do_fetch(struct transport *transport,
		    struct refspec *refs, int ref_count)
{
	struct ref *ref_map;
	struct ref *rm;
	int autotags = (transport->remote->fetch_tags == 1);
	if (transport->remote->fetch_tags == 2 && tags != TAGS_UNSET)
		tags = TAGS_SET;
	if (transport->remote->fetch_tags == -1)
		tags = TAGS_UNSET;

	if (!transport->get_refs_list || !transport->fetch)
		die("Don't know how to fetch from %s", transport->url);

	/* if not appending, truncate FETCH_HEAD */
	if (!append) {
		char *filename = git_path("FETCH_HEAD");
		FILE *fp = fopen(filename, "w");
		if (!fp)
			return error("cannot open %s: %s\n", filename, strerror(errno));
		fclose(fp);
	}

	ref_map = get_ref_map(transport, refs, ref_count, tags, &autotags);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref)
			read_ref(rm->peer_ref->name, rm->peer_ref->old_sha1);
	}

	if (tags == TAGS_DEFAULT && autotags)
		transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, "1");
	if (fetch_refs(transport, ref_map)) {
		free_refs(ref_map);
		return 1;
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
		die("Option \"%s\" value \"%s\" is not valid for %s\n",
			name, value, transport->url);
	if (r > 0)
		warning("Option \"%s\" is ignored for %s\n",
			name, transport->url);
}

int cmd_fetch(int argc, const char **argv, const char *prefix)
{
	struct remote *remote;
	int i;
	static const char **refs = NULL;
	int ref_nr = 0;
	int exit_code;

	/* Record the command line for the reflog */
	strbuf_addstr(&default_rla, "fetch");
	for (i = 1; i < argc; i++)
		strbuf_addf(&default_rla, " %s", argv[i]);

	argc = parse_options(argc, argv,
			     builtin_fetch_options, builtin_fetch_usage, 0);

	if (argc == 0)
		remote = remote_get(NULL);
	else
		remote = remote_get(argv[0]);

	transport = transport_get(remote, remote->url[0]);
	if (verbose >= 2)
		transport->verbose = 1;
	if (quiet)
		transport->verbose = -1;
	if (upload_pack)
		set_option(TRANS_OPT_UPLOADPACK, upload_pack);
	if (keep)
		set_option(TRANS_OPT_KEEP, "yes");
	if (depth)
		set_option(TRANS_OPT_DEPTH, depth);

	if (!transport->url)
		die("Where do you want to fetch from today?");

	if (argc > 1) {
		int j = 0;
		refs = xcalloc(argc + 1, sizeof(const char *));
		for (i = 1; i < argc; i++) {
			if (!strcmp(argv[i], "tag")) {
				char *ref;
				i++;
				if (i >= argc)
					die("You need to specify a tag name.");
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

	signal(SIGINT, unlock_pack_on_signal);
	atexit(unlock_pack);
	exit_code = do_fetch(transport,
			parse_fetch_refspec(ref_nr, refs), ref_nr);
	transport_disconnect(transport);
	transport = NULL;
	return exit_code;
}
