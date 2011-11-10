#include "cache.h"
#include "parse-options.h"
#include "transport.h"
#include "remote.h"
#include "string-list.h"
#include "strbuf.h"
#include "run-command.h"
#include "refs.h"

static const char * const builtin_remote_usage[] = {
	"git remote [-v | --verbose]",
	"git remote add [-t <branch>] [-m <master>] [-f] [--mirror] <name> <url>",
	"git remote rename <old> <new>",
	"git remote rm <name>",
	"git remote set-head <name> [-a | -d | <branch>]",
	"git remote show [-n] <name>",
	"git remote prune [-n | --dry-run] <name>",
	"git remote [-v | --verbose] update [-p | --prune] [group]",
	NULL
};

#define GET_REF_STATES (1<<0)
#define GET_HEAD_NAMES (1<<1)
#define GET_PUSH_REF_STATES (1<<2)

static int verbose;

static int show_all(void);
static int prune_remote(const char *remote, int dry_run);

static inline int postfixcmp(const char *string, const char *postfix)
{
	int len1 = strlen(string), len2 = strlen(postfix);
	if (len1 < len2)
		return 1;
	return strcmp(string + len1 - len2, postfix);
}

static int opt_parse_track(const struct option *opt, const char *arg, int not)
{
	struct string_list *list = opt->value;
	if (not)
		string_list_clear(list, 0);
	else
		string_list_append(arg, list);
	return 0;
}

static int fetch_remote(const char *name)
{
	const char *argv[] = { "fetch", name, NULL, NULL };
	if (verbose) {
		argv[1] = "-v";
		argv[2] = name;
	}
	printf("Updating %s\n", name);
	if (run_command_v_opt(argv, RUN_GIT_CMD))
		return error("Could not fetch %s", name);
	return 0;
}

static int add(int argc, const char **argv)
{
	int fetch = 0, mirror = 0;
	struct string_list track = { NULL, 0, 0 };
	const char *master = NULL;
	struct remote *remote;
	struct strbuf buf = STRBUF_INIT, buf2 = STRBUF_INIT;
	const char *name, *url;
	int i;

	struct option options[] = {
		OPT_GROUP("add specific options"),
		OPT_BOOLEAN('f', "fetch", &fetch, "fetch the remote branches"),
		OPT_CALLBACK('t', "track", &track, "branch",
			"branch(es) to track", opt_parse_track),
		OPT_STRING('m', "master", &master, "branch", "master branch"),
		OPT_BOOLEAN(0, "mirror", &mirror, "no separate remotes"),
		OPT_END()
	};

	argc = parse_options(argc, argv, options, builtin_remote_usage, 0);

	if (argc < 2)
		usage_with_options(builtin_remote_usage, options);

	name = argv[0];
	url = argv[1];

	remote = remote_get(name);
	if (remote && (remote->url_nr > 1 || strcmp(name, remote->url[0]) ||
			remote->fetch_refspec_nr))
		die("remote %s already exists.", name);

	strbuf_addf(&buf2, "refs/heads/test:refs/remotes/%s/test", name);
	if (!valid_fetch_refspec(buf2.buf))
		die("'%s' is not a valid remote name", name);

	strbuf_addf(&buf, "remote.%s.url", name);
	if (git_config_set(buf.buf, url))
		return 1;

	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.fetch", name);

	if (track.nr == 0)
		string_list_append("*", &track);
	for (i = 0; i < track.nr; i++) {
		struct string_list_item *item = track.items + i;

		strbuf_reset(&buf2);
		strbuf_addch(&buf2, '+');
		if (mirror)
			strbuf_addf(&buf2, "refs/%s:refs/%s",
					item->string, item->string);
		else
			strbuf_addf(&buf2, "refs/heads/%s:refs/remotes/%s/%s",
					item->string, name, item->string);
		if (git_config_set_multivar(buf.buf, buf2.buf, "^$", 0))
			return 1;
	}

	if (mirror) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "remote.%s.mirror", name);
		if (git_config_set(buf.buf, "true"))
			return 1;
	}

	if (fetch && fetch_remote(name))
		return 1;

	if (master) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/remotes/%s/HEAD", name);

		strbuf_reset(&buf2);
		strbuf_addf(&buf2, "refs/remotes/%s/%s", name, master);

		if (create_symref(buf.buf, buf2.buf, "remote add"))
			return error("Could not setup master '%s'", master);
	}

	strbuf_release(&buf);
	strbuf_release(&buf2);
	string_list_clear(&track, 0);

	return 0;
}

struct branch_info {
	char *remote_name;
	struct string_list merge;
	int rebase;
};

static struct string_list branch_list;

static const char *abbrev_ref(const char *name, const char *prefix)
{
	const char *abbrev = skip_prefix(name, prefix);
	if (abbrev)
		return abbrev;
	return name;
}
#define abbrev_branch(name) abbrev_ref((name), "refs/heads/")

static int config_read_branches(const char *key, const char *value, void *cb)
{
	if (!prefixcmp(key, "branch.")) {
		const char *orig_key = key;
		char *name;
		struct string_list_item *item;
		struct branch_info *info;
		enum { REMOTE, MERGE, REBASE } type;

		key += 7;
		if (!postfixcmp(key, ".remote")) {
			name = xstrndup(key, strlen(key) - 7);
			type = REMOTE;
		} else if (!postfixcmp(key, ".merge")) {
			name = xstrndup(key, strlen(key) - 6);
			type = MERGE;
		} else if (!postfixcmp(key, ".rebase")) {
			name = xstrndup(key, strlen(key) - 7);
			type = REBASE;
		} else
			return 0;

		item = string_list_insert(name, &branch_list);

		if (!item->util)
			item->util = xcalloc(sizeof(struct branch_info), 1);
		info = item->util;
		if (type == REMOTE) {
			if (info->remote_name)
				warning("more than one %s", orig_key);
			info->remote_name = xstrdup(value);
		} else if (type == MERGE) {
			char *space = strchr(value, ' ');
			value = abbrev_branch(value);
			while (space) {
				char *merge;
				merge = xstrndup(value, space - value);
				string_list_append(merge, &info->merge);
				value = abbrev_branch(space + 1);
				space = strchr(value, ' ');
			}
			string_list_append(xstrdup(value), &info->merge);
		} else
			info->rebase = git_config_bool(orig_key, value);
	}
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
	struct string_list new, stale, tracked, heads, push;
	int queried;
};

static int handle_one_branch(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct ref_states *states = cb_data;
	struct refspec refspec;

	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (!remote_find_tracking(states->remote, &refspec)) {
		struct string_list_item *item;
		const char *name = abbrev_branch(refspec.src);
		/* symbolic refs pointing nowhere were handled already */
		if ((flags & REF_ISSYMREF) ||
		    string_list_has_string(&states->tracked, name) ||
		    string_list_has_string(&states->new, name))
			return 0;
		item = string_list_append(name, &states->stale);
		item->util = xstrdup(refname);
	}
	return 0;
}

static int get_ref_states(const struct ref *remote_refs, struct ref_states *states)
{
	struct ref *fetch_map = NULL, **tail = &fetch_map;
	struct ref *ref;
	int i;

	for (i = 0; i < states->remote->fetch_refspec_nr; i++)
		if (get_fetch_map(remote_refs, states->remote->fetch + i, &tail, 1))
			die("Could not get fetch map for refspec %s",
				states->remote->fetch_refspec[i]);

	states->new.strdup_strings = states->tracked.strdup_strings = 1;
	for (ref = fetch_map; ref; ref = ref->next) {
		unsigned char sha1[20];
		if (!ref->peer_ref || read_ref(ref->peer_ref->name, sha1))
			string_list_append(abbrev_branch(ref->name), &states->new);
		else
			string_list_append(abbrev_branch(ref->name), &states->tracked);
	}
	free_refs(fetch_map);

	sort_string_list(&states->new);
	sort_string_list(&states->tracked);
	for_each_ref(handle_one_branch, states);
	sort_string_list(&states->stale);

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
		PUSH_STATUS_NOTQUERIED,
	} status;
};

static int get_push_ref_states(const struct ref *remote_refs,
	struct ref_states *states)
{
	struct remote *remote = states->remote;
	struct ref *ref, *local_refs, *push_map, **push_tail;
	if (remote->mirror)
		return 0;

	local_refs = get_local_heads();
	ref = push_map = copy_ref_list(remote_refs);
	while (ref->next)
		ref = ref->next;
	push_tail = &ref->next;

	match_refs(local_refs, push_map, &push_tail, remote->push_refspec_nr,
		   remote->push_refspec, MATCH_REFS_NONE);

	states->push.strdup_strings = 1;
	for (ref = push_map; ref; ref = ref->next) {
		struct string_list_item *item;
		struct push_info *info;

		if (!ref->peer_ref)
			continue;
		hashcpy(ref->new_sha1, ref->peer_ref->new_sha1);

		item = string_list_append(abbrev_branch(ref->peer_ref->name),
					  &states->push);
		item->util = xcalloc(sizeof(struct push_info), 1);
		info = item->util;
		info->forced = ref->force;
		info->dest = xstrdup(abbrev_branch(ref->name));

		if (is_null_sha1(ref->new_sha1)) {
			info->status = PUSH_STATUS_DELETE;
		} else if (!hashcmp(ref->old_sha1, ref->new_sha1))
			info->status = PUSH_STATUS_UPTODATE;
		else if (is_null_sha1(ref->old_sha1))
			info->status = PUSH_STATUS_CREATE;
		else if (has_sha1_file(ref->old_sha1) &&
			 ref_newer(ref->new_sha1, ref->old_sha1))
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

	states->push.strdup_strings = 1;
	if (!remote->push_refspec_nr) {
		item = string_list_append("(matching)", &states->push);
		info = item->util = xcalloc(sizeof(struct push_info), 1);
		info->status = PUSH_STATUS_NOTQUERIED;
		info->dest = xstrdup(item->string);
	}
	for (i = 0; i < remote->push_refspec_nr; i++) {
		struct refspec *spec = remote->push + i;
		if (spec->matching)
			item = string_list_append("(matching)", &states->push);
		else if (strlen(spec->src))
			item = string_list_append(spec->src, &states->push);
		else
			item = string_list_append("(delete)", &states->push);

		info = item->util = xcalloc(sizeof(struct push_info), 1);
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
	struct refspec refspec;

	refspec.force = 0;
	refspec.pattern = 1;
	refspec.src = refspec.dst = "refs/heads/*";
	states->heads.strdup_strings = 1;
	get_fetch_map(remote_refs, &refspec, &fetch_map_tail, 0);
	matches = guess_remote_head(find_ref_by_name(remote_refs, "HEAD"),
				    fetch_map, 1);
	for(ref = matches; ref; ref = ref->next)
		string_list_append(abbrev_branch(ref->name), &states->heads);

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
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct branches_for_remote *branches = cb_data;
	struct refspec refspec;
	struct string_list_item *item;
	struct known_remote *kr;

	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (remote_find_tracking(branches->remote, &refspec))
		return 0;

	/* don't delete a branch if another remote also uses it */
	for (kr = branches->keep->list; kr; kr = kr->next) {
		memset(&refspec, 0, sizeof(refspec));
		refspec.dst = (char *)refname;
		if (!remote_find_tracking(kr->remote, &refspec))
			return 0;
	}

	/* don't delete non-remote refs */
	if (prefixcmp(refname, "refs/remotes")) {
		/* advise user how to delete local branches */
		if (!prefixcmp(refname, "refs/heads/"))
			string_list_append(abbrev_branch(refname),
					   branches->skipped);
		/* silently skip over other non-remote refs */
		return 0;
	}

	/* make sure that symrefs are deleted */
	if (flags & REF_ISSYMREF)
		return unlink(git_path("%s", refname));

	item = string_list_append(refname, branches->branches);
	item->util = xmalloc(20);
	hashcpy(item->util, sha1);

	return 0;
}

struct rename_info {
	const char *old;
	const char *new;
	struct string_list *remote_branches;
};

static int read_remote_branches(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct rename_info *rename = cb_data;
	struct strbuf buf = STRBUF_INIT;
	struct string_list_item *item;
	int flag;
	unsigned char orig_sha1[20];
	const char *symref;

	strbuf_addf(&buf, "refs/remotes/%s", rename->old);
	if(!prefixcmp(refname, buf.buf)) {
		item = string_list_append(xstrdup(refname), rename->remote_branches);
		symref = resolve_ref(refname, orig_sha1, 1, &flag);
		if (flag & REF_ISSYMREF)
			item->util = xstrdup(symref);
		else
			item->util = NULL;
	}

	return 0;
}

static int migrate_file(struct remote *remote)
{
	struct strbuf buf = STRBUF_INIT;
	int i;
	char *path = NULL;

	strbuf_addf(&buf, "remote.%s.url", remote->name);
	for (i = 0; i < remote->url_nr; i++)
		if (git_config_set_multivar(buf.buf, remote->url[i], "^$", 0))
			return error("Could not append '%s' to '%s'",
					remote->url[i], buf.buf);
	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.push", remote->name);
	for (i = 0; i < remote->push_refspec_nr; i++)
		if (git_config_set_multivar(buf.buf, remote->push_refspec[i], "^$", 0))
			return error("Could not append '%s' to '%s'",
					remote->push_refspec[i], buf.buf);
	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.fetch", remote->name);
	for (i = 0; i < remote->fetch_refspec_nr; i++)
		if (git_config_set_multivar(buf.buf, remote->fetch_refspec[i], "^$", 0))
			return error("Could not append '%s' to '%s'",
					remote->fetch_refspec[i], buf.buf);
	if (remote->origin == REMOTE_REMOTES)
		path = git_path("remotes/%s", remote->name);
	else if (remote->origin == REMOTE_BRANCHES)
		path = git_path("branches/%s", remote->name);
	if (path && unlink(path))
		warning("failed to remove '%s'", path);
	return 0;
}

static int mv(int argc, const char **argv)
{
	struct option options[] = {
		OPT_END()
	};
	struct remote *oldremote, *newremote;
	struct strbuf buf = STRBUF_INIT, buf2 = STRBUF_INIT, buf3 = STRBUF_INIT;
	struct string_list remote_branches = { NULL, 0, 0, 0 };
	struct rename_info rename;
	int i;

	if (argc != 3)
		usage_with_options(builtin_remote_usage, options);

	rename.old = argv[1];
	rename.new = argv[2];
	rename.remote_branches = &remote_branches;

	oldremote = remote_get(rename.old);
	if (!oldremote)
		die("No such remote: %s", rename.old);

	if (!strcmp(rename.old, rename.new) && oldremote->origin != REMOTE_CONFIG)
		return migrate_file(oldremote);

	newremote = remote_get(rename.new);
	if (newremote && (newremote->url_nr > 1 || newremote->fetch_refspec_nr))
		die("remote %s already exists.", rename.new);

	strbuf_addf(&buf, "refs/heads/test:refs/remotes/%s/test", rename.new);
	if (!valid_fetch_refspec(buf.buf))
		die("'%s' is not a valid remote name", rename.new);

	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s", rename.old);
	strbuf_addf(&buf2, "remote.%s", rename.new);
	if (git_config_rename_section(buf.buf, buf2.buf) < 1)
		return error("Could not rename config section '%s' to '%s'",
				buf.buf, buf2.buf);

	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.fetch", rename.new);
	if (git_config_set_multivar(buf.buf, NULL, NULL, 1))
		return error("Could not remove config section '%s'", buf.buf);
	for (i = 0; i < oldremote->fetch_refspec_nr; i++) {
		char *ptr;

		strbuf_reset(&buf2);
		strbuf_addstr(&buf2, oldremote->fetch_refspec[i]);
		ptr = strstr(buf2.buf, rename.old);
		if (ptr)
			strbuf_splice(&buf2, ptr-buf2.buf, strlen(rename.old),
					rename.new, strlen(rename.new));
		if (git_config_set_multivar(buf.buf, buf2.buf, "^$", 0))
			return error("Could not append '%s'", buf.buf);
	}

	read_branches();
	for (i = 0; i < branch_list.nr; i++) {
		struct string_list_item *item = branch_list.items + i;
		struct branch_info *info = item->util;
		if (info->remote_name && !strcmp(info->remote_name, rename.old)) {
			strbuf_reset(&buf);
			strbuf_addf(&buf, "branch.%s.remote", item->string);
			if (git_config_set(buf.buf, rename.new)) {
				return error("Could not set '%s'", buf.buf);
			}
		}
	}

	/*
	 * First remove symrefs, then rename the rest, finally create
	 * the new symrefs.
	 */
	for_each_ref(read_remote_branches, &rename);
	for (i = 0; i < remote_branches.nr; i++) {
		struct string_list_item *item = remote_branches.items + i;
		int flag = 0;
		unsigned char sha1[20];

		resolve_ref(item->string, sha1, 1, &flag);
		if (!(flag & REF_ISSYMREF))
			continue;
		if (delete_ref(item->string, NULL, REF_NODEREF))
			die("deleting '%s' failed", item->string);
	}
	for (i = 0; i < remote_branches.nr; i++) {
		struct string_list_item *item = remote_branches.items + i;

		if (item->util)
			continue;
		strbuf_reset(&buf);
		strbuf_addstr(&buf, item->string);
		strbuf_splice(&buf, strlen("refs/remotes/"), strlen(rename.old),
				rename.new, strlen(rename.new));
		strbuf_reset(&buf2);
		strbuf_addf(&buf2, "remote: renamed %s to %s",
				item->string, buf.buf);
		if (rename_ref(item->string, buf.buf, buf2.buf))
			die("renaming '%s' failed", item->string);
	}
	for (i = 0; i < remote_branches.nr; i++) {
		struct string_list_item *item = remote_branches.items + i;

		if (!item->util)
			continue;
		strbuf_reset(&buf);
		strbuf_addstr(&buf, item->string);
		strbuf_splice(&buf, strlen("refs/remotes/"), strlen(rename.old),
				rename.new, strlen(rename.new));
		strbuf_reset(&buf2);
		strbuf_addstr(&buf2, item->util);
		strbuf_splice(&buf2, strlen("refs/remotes/"), strlen(rename.old),
				rename.new, strlen(rename.new));
		strbuf_reset(&buf3);
		strbuf_addf(&buf3, "remote: renamed %s to %s",
				item->string, buf.buf);
		if (create_symref(buf.buf, buf2.buf, buf3.buf))
			die("creating '%s' failed", buf.buf);
	}
	return 0;
}

static int remove_branches(struct string_list *branches)
{
	int i, result = 0;
	for (i = 0; i < branches->nr; i++) {
		struct string_list_item *item = branches->items + i;
		const char *refname = item->string;
		unsigned char *sha1 = item->util;

		if (delete_ref(refname, sha1, 0))
			result |= error("Could not remove branch %s", refname);
	}
	return result;
}

static int rm(int argc, const char **argv)
{
	struct option options[] = {
		OPT_END()
	};
	struct remote *remote;
	struct strbuf buf = STRBUF_INIT;
	struct known_remotes known_remotes = { NULL, NULL };
	struct string_list branches = { NULL, 0, 0, 1 };
	struct string_list skipped = { NULL, 0, 0, 1 };
	struct branches_for_remote cb_data = {
		NULL, &branches, &skipped, &known_remotes
	};
	int i, result;

	if (argc != 2)
		usage_with_options(builtin_remote_usage, options);

	remote = remote_get(argv[1]);
	if (!remote)
		die("No such remote: %s", argv[1]);

	known_remotes.to_delete = remote;
	for_each_remote(add_known_remote, &known_remotes);

	strbuf_addf(&buf, "remote.%s", remote->name);
	if (git_config_rename_section(buf.buf, NULL) < 1)
		return error("Could not remove config section '%s'", buf.buf);

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
				if (git_config_set(buf.buf, NULL)) {
					strbuf_release(&buf);
					return -1;
				}
			}
		}
	}

	/*
	 * We cannot just pass a function to for_each_ref() which deletes
	 * the branches one by one, since for_each_ref() relies on cached
	 * refs, which are invalidated when deleting a branch.
	 */
	cb_data.remote = remote;
	result = for_each_ref(add_branch_for_removal, &cb_data);
	strbuf_release(&buf);

	if (!result)
		result = remove_branches(&branches);
	string_list_clear(&branches, 1);

	if (skipped.nr) {
		fprintf(stderr, skipped.nr == 1 ?
			"Note: A non-remote branch was not removed; "
			"to delete it, use:\n" :
			"Note: Non-remote branches were not removed; "
			"to delete them, use:\n");
		for (i = 0; i < skipped.nr; i++)
			fprintf(stderr, "  git branch -d %s\n",
				skipped.items[i].string);
	}
	string_list_clear(&skipped, 0);

	return result;
}

void clear_push_info(void *util, const char *string)
{
	struct push_info *info = util;
	free(info->dest);
	free(info);
}

static void free_remote_ref_states(struct ref_states *states)
{
	string_list_clear(&states->new, 0);
	string_list_clear(&states->stale, 0);
	string_list_clear(&states->tracked, 0);
	string_list_clear(&states->heads, 0);
	string_list_clear_func(&states->push, clear_push_info);
}

static int append_ref_to_tracked_list(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct ref_states *states = cb_data;
	struct refspec refspec;

	if (flags & REF_ISSYMREF)
		return 0;

	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (!remote_find_tracking(states->remote, &refspec))
		string_list_append(abbrev_branch(refspec.src), &states->tracked);

	return 0;
}

static int get_remote_ref_states(const char *name,
				 struct ref_states *states,
				 int query)
{
	struct transport *transport;
	const struct ref *remote_refs;

	states->remote = remote_get(name);
	if (!states->remote)
		return error("No such remote: %s", name);

	read_branches();

	if (query) {
		transport = transport_get(NULL, states->remote->url_nr > 0 ?
			states->remote->url[0] : NULL);
		remote_refs = transport_get_remote_refs(transport);
		transport_disconnect(transport);

		states->queried = 1;
		if (query & GET_REF_STATES)
			get_ref_states(remote_refs, states);
		if (query & GET_HEAD_NAMES)
			get_head_names(remote_refs, states);
		if (query & GET_PUSH_REF_STATES)
			get_push_ref_states(remote_refs, states);
	} else {
		for_each_ref(append_ref_to_tracked_list, states);
		sort_string_list(&states->tracked);
		get_push_ref_states_noquery(states);
	}

	return 0;
}

struct show_info {
	struct string_list *list;
	struct ref_states *states;
	int width, width2;
	int any_rebase;
};

int add_remote_to_show_info(struct string_list_item *item, void *cb_data)
{
	struct show_info *info = cb_data;
	int n = strlen(item->string);
	if (n > info->width)
		info->width = n;
	string_list_insert(item->string, info->list);
	return 0;
}

int show_remote_info_item(struct string_list_item *item, void *cb_data)
{
	struct show_info *info = cb_data;
	struct ref_states *states = info->states;
	const char *name = item->string;

	if (states->queried) {
		const char *fmt = "%s";
		const char *arg = "";
		if (string_list_has_string(&states->new, name)) {
			fmt = " new (next fetch will store in remotes/%s)";
			arg = states->remote->name;
		} else if (string_list_has_string(&states->tracked, name))
			arg = " tracked";
		else if (string_list_has_string(&states->stale, name))
			arg = " stale (use 'git remote prune' to remove)";
		else
			arg = " ???";
		printf("    %-*s", info->width, name);
		printf(fmt, arg);
		printf("\n");
	} else
		printf("    %s\n", name);

	return 0;
}

int add_local_to_show_info(struct string_list_item *branch_item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct ref_states *states = show_info->states;
	struct branch_info *branch_info = branch_item->util;
	struct string_list_item *item;
	int n;

	if (!branch_info->merge.nr || !branch_info->remote_name ||
	    strcmp(states->remote->name, branch_info->remote_name))
		return 0;
	if ((n = strlen(branch_item->string)) > show_info->width)
		show_info->width = n;
	if (branch_info->rebase)
		show_info->any_rebase = 1;

	item = string_list_insert(branch_item->string, show_info->list);
	item->util = branch_info;

	return 0;
}

int show_local_info_item(struct string_list_item *item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct branch_info *branch_info = item->util;
	struct string_list *merge = &branch_info->merge;
	const char *also;
	int i;

	if (branch_info->rebase && branch_info->merge.nr > 1) {
		error("invalid branch.%s.merge; cannot rebase onto > 1 branch",
			item->string);
		return 0;
	}

	printf("    %-*s ", show_info->width, item->string);
	if (branch_info->rebase) {
		printf("rebases onto remote %s\n", merge->items[0].string);
		return 0;
	} else if (show_info->any_rebase) {
		printf(" merges with remote %s\n", merge->items[0].string);
		also = "    and with remote";
	} else {
		printf("merges with remote %s\n", merge->items[0].string);
		also = "   and with remote";
	}
	for (i = 1; i < merge->nr; i++)
		printf("    %-*s %s %s\n", show_info->width, "", also,
		       merge->items[i].string);

	return 0;
}

int add_push_to_show_info(struct string_list_item *push_item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct push_info *push_info = push_item->util;
	struct string_list_item *item;
	int n;
	if ((n = strlen(push_item->string)) > show_info->width)
		show_info->width = n;
	if ((n = strlen(push_info->dest)) > show_info->width2)
		show_info->width2 = n;
	item = string_list_append(push_item->string, show_info->list);
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

int show_push_info_item(struct string_list_item *item, void *cb_data)
{
	struct show_info *show_info = cb_data;
	struct push_info *push_info = item->util;
	char *src = item->string, *status = NULL;

	switch (push_info->status) {
	case PUSH_STATUS_CREATE:
		status = "create";
		break;
	case PUSH_STATUS_DELETE:
		status = "delete";
		src = "(none)";
		break;
	case PUSH_STATUS_UPTODATE:
		status = "up to date";
		break;
	case PUSH_STATUS_FASTFORWARD:
		status = "fast forwardable";
		break;
	case PUSH_STATUS_OUTOFDATE:
		status = "local out of date";
		break;
	case PUSH_STATUS_NOTQUERIED:
		break;
	}
	if (status)
		printf("    %-*s %s to %-*s (%s)\n", show_info->width, src,
			push_info->forced ? "forces" : "pushes",
			show_info->width2, push_info->dest, status);
	else
		printf("    %-*s %s to %s\n", show_info->width, src,
			push_info->forced ? "forces" : "pushes",
			push_info->dest);
	return 0;
}

static int show(int argc, const char **argv)
{
	int no_query = 0, result = 0, query_flag = 0;
	struct option options[] = {
		OPT_GROUP("show specific options"),
		OPT_BOOLEAN('n', NULL, &no_query, "do not query remotes"),
		OPT_END()
	};
	struct ref_states states;
	struct string_list info_list = { NULL, 0, 0, 0 };
	struct show_info info;

	argc = parse_options(argc, argv, options, builtin_remote_usage, 0);

	if (argc < 1)
		return show_all();

	if (!no_query)
		query_flag = (GET_REF_STATES | GET_HEAD_NAMES | GET_PUSH_REF_STATES);

	memset(&states, 0, sizeof(states));
	memset(&info, 0, sizeof(info));
	info.states = &states;
	info.list = &info_list;
	for (; argc; argc--, argv++) {
		int i;

		get_remote_ref_states(*argv, &states, query_flag);

		printf("* remote %s\n  URL: %s\n", *argv,
			states.remote->url_nr > 0 ?
				states.remote->url[0] : "(no URL)");
		if (no_query)
			printf("  HEAD branch: (not queried)\n");
		else if (!states.heads.nr)
			printf("  HEAD branch: (unknown)\n");
		else if (states.heads.nr == 1)
			printf("  HEAD branch: %s\n", states.heads.items[0].string);
		else {
			printf("  HEAD branch (remote HEAD is ambiguous,"
			       " may be one of the following):\n");
			for (i = 0; i < states.heads.nr; i++)
				printf("    %s\n", states.heads.items[i].string);
		}

		/* remote branch info */
		info.width = 0;
		for_each_string_list(add_remote_to_show_info, &states.new, &info);
		for_each_string_list(add_remote_to_show_info, &states.tracked, &info);
		for_each_string_list(add_remote_to_show_info, &states.stale, &info);
		if (info.list->nr)
			printf("  Remote branch%s:%s\n",
			       info.list->nr > 1 ? "es" : "",
				no_query ? " (status not queried)" : "");
		for_each_string_list(show_remote_info_item, info.list, &info);
		string_list_clear(info.list, 0);

		/* git pull info */
		info.width = 0;
		info.any_rebase = 0;
		for_each_string_list(add_local_to_show_info, &branch_list, &info);
		if (info.list->nr)
			printf("  Local branch%s configured for 'git pull':\n",
			       info.list->nr > 1 ? "es" : "");
		for_each_string_list(show_local_info_item, info.list, &info);
		string_list_clear(info.list, 0);

		/* git push info */
		if (states.remote->mirror)
			printf("  Local refs will be mirrored by 'git push'\n");

		info.width = info.width2 = 0;
		for_each_string_list(add_push_to_show_info, &states.push, &info);
		qsort(info.list->items, info.list->nr,
			sizeof(*info.list->items), cmp_string_with_push);
		if (info.list->nr)
			printf("  Local ref%s configured for 'git push'%s:\n",
				info.list->nr > 1 ? "s" : "",
				no_query ? " (status not queried)" : "");
		for_each_string_list(show_push_info_item, info.list, &info);
		string_list_clear(info.list, 0);

		free_remote_ref_states(&states);
	}

	return result;
}

static int set_head(int argc, const char **argv)
{
	int i, opt_a = 0, opt_d = 0, result = 0;
	struct strbuf buf = STRBUF_INIT, buf2 = STRBUF_INIT;
	char *head_name = NULL;

	struct option options[] = {
		OPT_GROUP("set-head specific options"),
		OPT_BOOLEAN('a', "auto", &opt_a,
			    "set refs/remotes/<name>/HEAD according to remote"),
		OPT_BOOLEAN('d', "delete", &opt_d,
			    "delete refs/remotes/<name>/HEAD"),
		OPT_END()
	};
	argc = parse_options(argc, argv, options, builtin_remote_usage, 0);
	if (argc)
		strbuf_addf(&buf, "refs/remotes/%s/HEAD", argv[0]);

	if (!opt_a && !opt_d && argc == 2) {
		head_name = xstrdup(argv[1]);
	} else if (opt_a && !opt_d && argc == 1) {
		struct ref_states states;
		memset(&states, 0, sizeof(states));
		get_remote_ref_states(argv[0], &states, GET_HEAD_NAMES);
		if (!states.heads.nr)
			result |= error("Cannot determine remote HEAD");
		else if (states.heads.nr > 1) {
			result |= error("Multiple remote HEAD branches. "
					"Please choose one explicitly with:");
			for (i = 0; i < states.heads.nr; i++)
				fprintf(stderr, "  git remote set-head %s %s\n",
					argv[0], states.heads.items[i].string);
		} else
			head_name = xstrdup(states.heads.items[0].string);
		free_remote_ref_states(&states);
	} else if (opt_d && !opt_a && argc == 1) {
		if (delete_ref(buf.buf, NULL, REF_NODEREF))
			result |= error("Could not delete %s", buf.buf);
	} else
		usage_with_options(builtin_remote_usage, options);

	if (head_name) {
		unsigned char sha1[20];
		strbuf_addf(&buf2, "refs/remotes/%s/%s", argv[0], head_name);
		/* make sure it's valid */
		if (!resolve_ref(buf2.buf, sha1, 1, NULL))
			result |= error("Not a valid ref: %s", buf2.buf);
		else if (create_symref(buf.buf, buf2.buf, "remote set-head"))
			result |= error("Could not setup %s", buf.buf);
		if (opt_a)
			printf("%s/HEAD set to %s\n", argv[0], head_name);
		free(head_name);
	}

	strbuf_release(&buf);
	strbuf_release(&buf2);
	return result;
}

static int prune(int argc, const char **argv)
{
	int dry_run = 0, result = 0;
	struct option options[] = {
		OPT_GROUP("prune specific options"),
		OPT__DRY_RUN(&dry_run),
		OPT_END()
	};

	argc = parse_options(argc, argv, options, builtin_remote_usage, 0);

	if (argc < 1)
		usage_with_options(builtin_remote_usage, options);

	for (; argc; argc--, argv++)
		result |= prune_remote(*argv, dry_run);

	return result;
}

static int prune_remote(const char *remote, int dry_run)
{
	int result = 0, i;
	struct ref_states states;
	const char *dangling_msg = dry_run
		? " %s will become dangling!\n"
		: " %s has become dangling!\n";

	memset(&states, 0, sizeof(states));
	get_remote_ref_states(remote, &states, GET_REF_STATES);

	if (states.stale.nr) {
		printf("Pruning %s\n", remote);
		printf("URL: %s\n",
		       states.remote->url_nr
		       ? states.remote->url[0]
		       : "(no URL)");
	}

	for (i = 0; i < states.stale.nr; i++) {
		const char *refname = states.stale.items[i].util;

		if (!dry_run)
			result |= delete_ref(refname, NULL, 0);

		printf(" * [%s] %s\n", dry_run ? "would prune" : "pruned",
		       abbrev_ref(refname, "refs/remotes/"));
		warn_dangling_symref(dangling_msg, refname);
	}

	free_remote_ref_states(&states);
	return result;
}

static int get_one_remote_for_update(struct remote *remote, void *priv)
{
	struct string_list *list = priv;
	if (!remote->skip_default_update)
		string_list_append(remote->name, list);
	return 0;
}

struct remote_group {
	const char *name;
	struct string_list *list;
} remote_group;

static int get_remote_group(const char *key, const char *value, void *num_hits)
{
	if (!prefixcmp(key, "remotes.") &&
			!strcmp(key + 8, remote_group.name)) {
		/* split list by white space */
		int space = strcspn(value, " \t\n");
		while (*value) {
			if (space > 1) {
				string_list_append(xstrndup(value, space),
						remote_group.list);
				++*((int *)num_hits);
			}
			value += space + (value[space] != '\0');
			space = strcspn(value, " \t\n");
		}
	}

	return 0;
}

static int update(int argc, const char **argv)
{
	int i, result = 0, prune = 0;
	struct string_list list = { NULL, 0, 0, 0 };
	static const char *default_argv[] = { NULL, "default", NULL };
	struct option options[] = {
		OPT_GROUP("update specific options"),
		OPT_BOOLEAN('p', "prune", &prune,
			    "prune remotes after fetching"),
		OPT_END()
	};

	argc = parse_options(argc, argv, options, builtin_remote_usage,
			     PARSE_OPT_KEEP_ARGV0);
	if (argc < 2) {
		argc = 2;
		argv = default_argv;
	}

	remote_group.list = &list;
	for (i = 1; i < argc; i++) {
		int groups_found = 0;
		remote_group.name = argv[i];
		result = git_config(get_remote_group, &groups_found);
		if (!groups_found && (i != 1 || strcmp(argv[1], "default"))) {
			struct remote *remote;
			if (!remote_is_configured(argv[i]))
				die("No such remote or remote group: %s",
				    argv[i]);
			remote = remote_get(argv[i]);
			string_list_append(remote->name, remote_group.list);
		}
	}

	if (!result && !list.nr  && argc == 2 && !strcmp(argv[1], "default"))
		result = for_each_remote(get_one_remote_for_update, &list);

	for (i = 0; i < list.nr; i++) {
		int err = fetch_remote(list.items[i].string);
		result |= err;
		if (!err && prune)
			result |= prune_remote(list.items[i].string, 0);
	}

	/* all names were strdup()ed or strndup()ed */
	list.strdup_strings = 1;
	string_list_clear(&list, 0);

	return result;
}

static int get_one_entry(struct remote *remote, void *priv)
{
	struct string_list *list = priv;

	if (remote->url_nr > 0) {
		int i;

		for (i = 0; i < remote->url_nr; i++)
			string_list_append(remote->name, list)->util = (void *)remote->url[i];
	} else
		string_list_append(remote->name, list)->util = NULL;

	return 0;
}

static int show_all(void)
{
	struct string_list list = { NULL, 0, 0 };
	int result = for_each_remote(get_one_entry, &list);

	if (!result) {
		int i;

		sort_string_list(&list);
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
	return result;
}

int cmd_remote(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT__VERBOSE(&verbose),
		OPT_END()
	};
	int result;

	argc = parse_options(argc, argv, options, builtin_remote_usage,
		PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc < 1)
		result = show_all();
	else if (!strcmp(argv[0], "add"))
		result = add(argc, argv);
	else if (!strcmp(argv[0], "rename"))
		result = mv(argc, argv);
	else if (!strcmp(argv[0], "rm"))
		result = rm(argc, argv);
	else if (!strcmp(argv[0], "set-head"))
		result = set_head(argc, argv);
	else if (!strcmp(argv[0], "show"))
		result = show(argc, argv);
	else if (!strcmp(argv[0], "prune"))
		result = prune(argc, argv);
	else if (!strcmp(argv[0], "update"))
		result = update(argc, argv);
	else {
		error("Unknown subcommand: %s", argv[0]);
		usage_with_options(builtin_remote_usage, options);
	}

	return result ? 1 : 0;
}
