/*
 * "git fetch"
 */
#include "cache.h"
#include "refs.h"
#include "commit.h"
#include "builtin.h"
#include "path-list.h"
#include "remote.h"
#include "transport.h"

static const char fetch_usage[] = "git-fetch [-a | --append] [--upload-pack <upload-pack>] [-f | --force] [--no-tags] [-t | --tags] [-k | --keep] [-u | --update-head-ok] [--depth <depth>] [-v | --verbose] [<repository> <refspec>...]";

static int append, force, tags, no_tags, update_head_ok, verbose, quiet;
static char *default_rla = NULL;
static struct transport *transport;

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
		           struct ref *remote_refs,
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

		/* Not fetched to a tracking branch?  We need to fetch
		 * it anyway to allow this branch's "branch.$name.merge"
		 * to be honored by git-pull.
		 */
		refspec.src = branch->merge[i]->src;
		refspec.dst = NULL;
		refspec.pattern = 0;
		refspec.force = 0;
		get_fetch_map(remote_refs, &refspec, tail);
		for (rm = *old_tail; rm; rm = rm->next)
			rm->merge = 1;
	}
}

static struct ref *get_ref_map(struct transport *transport,
			       struct refspec *refs, int ref_count, int tags,
			       int *autotags)
{
	int i;
	struct ref *rm;
	struct ref *ref_map = NULL;
	struct ref **tail = &ref_map;

	struct ref *remote_refs = transport_get_remote_refs(transport);

	if (ref_count || tags) {
		for (i = 0; i < ref_count; i++) {
			get_fetch_map(remote_refs, &refs[i], &tail);
			if (refs[i].dst && refs[i].dst[0])
				*autotags = 1;
		}
		/* Merge everything on the command line, but not --tags */
		for (rm = ref_map; rm; rm = rm->next)
			rm->merge = 1;
		if (tags) {
			struct refspec refspec;
			refspec.src = "refs/tags/";
			refspec.dst = "refs/tags/";
			refspec.pattern = 1;
			refspec.force = 0;
			get_fetch_map(remote_refs, &refspec, &tail);
		}
	} else {
		/* Use the defaults */
		struct remote *remote = transport->remote;
		struct branch *branch = branch_get(NULL);
		int has_merge = branch_has_merge_config(branch);
		if (remote && (remote->fetch_refspec_nr || has_merge)) {
			for (i = 0; i < remote->fetch_refspec_nr; i++) {
				get_fetch_map(remote_refs, &remote->fetch[i], &tail);
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
			if (has_merge && !strcmp(branch->remote_name,
						remote->name))
				add_merge_config(&ref_map, remote_refs, branch, &tail);
		} else {
			ref_map = get_remote_ref(remote_refs, "HEAD");
			ref_map->merge = 1;
		}
	}
	ref_remove_duplicates(ref_map);

	return ref_map;
}

static void show_new(enum object_type type, unsigned char *sha1_new)
{
	fprintf(stderr, "  %s: %s\n", typename(type),
		find_unique_abbrev(sha1_new, DEFAULT_ABBREV));
}

static int s_update_ref(const char *action,
			struct ref *ref,
			int check_old)
{
	char msg[1024];
	char *rla = getenv("GIT_REFLOG_ACTION");
	static struct ref_lock *lock;

	if (!rla)
		rla = default_rla;
	snprintf(msg, sizeof(msg), "%s: %s", rla, action);
	lock = lock_any_ref_for_update(ref->name,
				       check_old ? ref->old_sha1 : NULL, 0);
	if (!lock)
		return 1;
	if (write_ref_sha1(lock, ref->new_sha1, msg) < 0)
		return 1;
	return 0;
}

static int update_local_ref(struct ref *ref,
			    const char *note,
			    int verbose)
{
	char oldh[41], newh[41];
	struct commit *current = NULL, *updated;
	enum object_type type;
	struct branch *current_branch = branch_get(NULL);

	type = sha1_object_info(ref->new_sha1, NULL);
	if (type < 0)
		die("object %s not found", sha1_to_hex(ref->new_sha1));

	if (!*ref->name) {
		/* Not storing */
		if (verbose) {
			fprintf(stderr, "* fetched %s\n", note);
			show_new(type, ref->new_sha1);
		}
		return 0;
	}

	if (!hashcmp(ref->old_sha1, ref->new_sha1)) {
		if (verbose) {
			fprintf(stderr, "* %s: same as %s\n",
				ref->name, note);
			show_new(type, ref->new_sha1);
		}
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
		fprintf(stderr,
			" * %s: Cannot fetch into the current branch.\n",
			ref->name);
		return 1;
	}

	if (!is_null_sha1(ref->old_sha1) &&
	    !prefixcmp(ref->name, "refs/tags/")) {
		fprintf(stderr, "* %s: updating with %s\n",
			ref->name, note);
		show_new(type, ref->new_sha1);
		return s_update_ref("updating tag", ref, 0);
	}

	current = lookup_commit_reference_gently(ref->old_sha1, 1);
	updated = lookup_commit_reference_gently(ref->new_sha1, 1);
	if (!current || !updated) {
		char *msg;
		if (!strncmp(ref->name, "refs/tags/", 10))
			msg = "storing tag";
		else
			msg = "storing head";
		fprintf(stderr, "* %s: storing %s\n",
			ref->name, note);
		show_new(type, ref->new_sha1);
		return s_update_ref(msg, ref, 0);
	}

	strcpy(oldh, find_unique_abbrev(current->object.sha1, DEFAULT_ABBREV));
	strcpy(newh, find_unique_abbrev(ref->new_sha1, DEFAULT_ABBREV));

	if (in_merge_bases(current, &updated, 1)) {
		fprintf(stderr, "* %s: fast forward to %s\n",
			ref->name, note);
		fprintf(stderr, "  old..new: %s..%s\n", oldh, newh);
		return s_update_ref("fast forward", ref, 1);
	}
	if (!force && !ref->force) {
		fprintf(stderr,
			"* %s: not updating to non-fast forward %s\n",
			ref->name, note);
		fprintf(stderr,
			"  old...new: %s...%s\n", oldh, newh);
		return 1;
	}
	fprintf(stderr,
		"* %s: forcing update to non-fast forward %s\n",
		ref->name, note);
	fprintf(stderr, "  old...new: %s...%s\n", oldh, newh);
	return s_update_ref("forced-update", ref, 1);
}

static void store_updated_refs(const char *url, struct ref *ref_map)
{
	FILE *fp;
	struct commit *commit;
	int url_len, i, note_len;
	char note[1024];
	const char *what, *kind;
	struct ref *rm;

	fp = fopen(git_path("FETCH_HEAD"), "a");
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
			update_local_ref(ref, note, verbose);
	}
	fclose(fp);
}

static int fetch_refs(struct transport *transport, struct ref *ref_map)
{
	int ret = transport_fetch_refs(transport, ref_map);
	if (!ret)
		store_updated_refs(transport->url, ref_map);
	transport_unlock_pack(transport);
	return ret;
}

static int add_existing(const char *refname, const unsigned char *sha1,
			int flag, void *cbdata)
{
	struct path_list *list = (struct path_list *)cbdata;
	path_list_insert(refname, list);
	return 0;
}

static struct ref *find_non_local_tags(struct transport *transport,
				       struct ref *fetch_map)
{
	static struct path_list existing_refs = { NULL, 0, 0, 0 };
	struct path_list new_refs = { NULL, 0, 0, 1 };
	char *ref_name;
	int ref_name_len;
	unsigned char *ref_sha1;
	struct ref *tag_ref;
	struct ref *rm = NULL;
	struct ref *ref_map = NULL;
	struct ref **tail = &ref_map;
	struct ref *ref;

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

		if (!path_list_has_path(&existing_refs, ref_name) &&
		    !path_list_has_path(&new_refs, ref_name) &&
		    lookup_object(ref->old_sha1)) {
			fprintf(stderr, "Auto-following %s\n",
				ref_name);

			path_list_insert(ref_name, &new_refs);

			rm = alloc_ref(strlen(ref_name) + 1);
			strcpy(rm->name, ref_name);
			rm->peer_ref = alloc_ref(strlen(ref_name) + 1);
			strcpy(rm->peer_ref->name, ref_name);
			hashcpy(rm->old_sha1, ref_sha1);

			*tail = rm;
			tail = &rm->next;
		}
		free(ref_name);
	}

	return ref_map;
}

static int do_fetch(struct transport *transport,
		    struct refspec *refs, int ref_count)
{
	struct ref *ref_map, *fetch_map;
	struct ref *rm;
	int autotags = (transport->remote->fetch_tags == 1);
	if (transport->remote->fetch_tags == 2 && !no_tags)
		tags = 1;
	if (transport->remote->fetch_tags == -1)
		no_tags = 1;

	if (!transport->get_refs_list || !transport->fetch)
		die("Don't know how to fetch from %s", transport->url);

	/* if not appending, truncate FETCH_HEAD */
	if (!append)
		fclose(fopen(git_path("FETCH_HEAD"), "w"));

	ref_map = get_ref_map(transport, refs, ref_count, tags, &autotags);

	for (rm = ref_map; rm; rm = rm->next) {
		if (rm->peer_ref)
			read_ref(rm->peer_ref->name, rm->peer_ref->old_sha1);
	}

	if (fetch_refs(transport, ref_map)) {
		free_refs(ref_map);
		return 1;
	}

	fetch_map = ref_map;

	/* if neither --no-tags nor --tags was specified, do automated tag
	 * following ... */
	if (!(tags || no_tags) && autotags) {
		ref_map = find_non_local_tags(transport, fetch_map);
		if (ref_map) {
			transport_set_option(transport, TRANS_OPT_DEPTH, "0");
			fetch_refs(transport, ref_map);
		}
		free_refs(ref_map);
	}

	free_refs(fetch_map);

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
	int i, j, rla_offset;
	static const char **refs = NULL;
	int ref_nr = 0;
	int cmd_len = 0;
	const char *depth = NULL, *upload_pack = NULL;
	int keep = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		cmd_len += strlen(arg);

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "--append") || !strcmp(arg, "-a")) {
			append = 1;
			continue;
		}
		if (!prefixcmp(arg, "--upload-pack=")) {
			upload_pack = arg + 14;
			continue;
		}
		if (!strcmp(arg, "--upload-pack")) {
			i++;
			if (i == argc)
				usage(fetch_usage);
			upload_pack = argv[i];
			continue;
		}
		if (!strcmp(arg, "--force") || !strcmp(arg, "-f")) {
			force = 1;
			continue;
		}
		if (!strcmp(arg, "--no-tags")) {
			no_tags = 1;
			continue;
		}
		if (!strcmp(arg, "--tags") || !strcmp(arg, "-t")) {
			tags = 1;
			continue;
		}
		if (!strcmp(arg, "--keep") || !strcmp(arg, "-k")) {
			keep = 1;
			continue;
		}
		if (!strcmp(arg, "--update-head-ok") || !strcmp(arg, "-u")) {
			update_head_ok = 1;
			continue;
		}
		if (!prefixcmp(arg, "--depth=")) {
			depth = arg + 8;
			continue;
		}
		if (!strcmp(arg, "--depth")) {
			i++;
			if (i == argc)
				usage(fetch_usage);
			depth = argv[i];
			continue;
		}
		if (!strcmp(arg, "--quiet")) {
			quiet = 1;
			continue;
		}
		if (!strcmp(arg, "--verbose") || !strcmp(arg, "-v")) {
			verbose++;
			continue;
		}
		usage(fetch_usage);
	}

	for (j = i; j < argc; j++)
		cmd_len += strlen(argv[j]);

	default_rla = xmalloc(cmd_len + 5 + argc + 1);
	sprintf(default_rla, "fetch");
	rla_offset = strlen(default_rla);
	for (j = 1; j < argc; j++) {
		sprintf(default_rla + rla_offset, " %s", argv[j]);
		rla_offset += strlen(argv[j]) + 1;
	}

	if (i == argc)
		remote = remote_get(NULL);
	else
		remote = remote_get(argv[i++]);

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

	if (i < argc) {
		int j = 0;
		refs = xcalloc(argc - i + 1, sizeof(const char *));
		while (i < argc) {
			if (!strcmp(argv[i], "tag")) {
				char *ref;
				i++;
				ref = xmalloc(strlen(argv[i]) * 2 + 22);
				strcpy(ref, "refs/tags/");
				strcat(ref, argv[i]);
				strcat(ref, ":refs/tags/");
				strcat(ref, argv[i]);
				refs[j++] = ref;
			} else
				refs[j++] = argv[i];
			i++;
		}
		refs[j] = NULL;
		ref_nr = j;
	}

	signal(SIGINT, unlock_pack_on_signal);
	atexit(unlock_pack);
	return do_fetch(transport, parse_ref_spec(ref_nr, refs), ref_nr);
}
