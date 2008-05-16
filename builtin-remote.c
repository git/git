#include "cache.h"
#include "parse-options.h"
#include "transport.h"
#include "remote.h"
#include "path-list.h"
#include "strbuf.h"
#include "run-command.h"
#include "refs.h"

static const char * const builtin_remote_usage[] = {
	"git remote",
	"git remote add <name> <url>",
	"git remote rm <name>",
	"git remote show <name>",
	"git remote prune <name>",
	"git remote update [group]",
	NULL
};

static int verbose;

static int show_all(void);

static inline int postfixcmp(const char *string, const char *postfix)
{
	int len1 = strlen(string), len2 = strlen(postfix);
	if (len1 < len2)
		return 1;
	return strcmp(string + len1 - len2, postfix);
}

static inline const char *skip_prefix(const char *name, const char *prefix)
{
	return !name ? "" :
		prefixcmp(name, prefix) ?  name : name + strlen(prefix);
}

static int opt_parse_track(const struct option *opt, const char *arg, int not)
{
	struct path_list *list = opt->value;
	if (not)
		path_list_clear(list, 0);
	else
		path_list_append(arg, list);
	return 0;
}

static int fetch_remote(const char *name)
{
	const char *argv[] = { "fetch", name, NULL };
	printf("Updating %s\n", name);
	if (run_command_v_opt(argv, RUN_GIT_CMD))
		return error("Could not fetch %s", name);
	return 0;
}

static int add(int argc, const char **argv)
{
	int fetch = 0, mirror = 0;
	struct path_list track = { NULL, 0, 0 };
	const char *master = NULL;
	struct remote *remote;
	struct strbuf buf, buf2;
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

	strbuf_init(&buf, 0);
	strbuf_init(&buf2, 0);

	strbuf_addf(&buf2, "refs/heads/test:refs/remotes/%s/test", name);
	if (!valid_fetch_refspec(buf2.buf))
		die("'%s' is not a valid remote name", name);

	strbuf_addf(&buf, "remote.%s.url", name);
	if (git_config_set(buf.buf, url))
		return 1;

	strbuf_reset(&buf);
	strbuf_addf(&buf, "remote.%s.fetch", name);

	if (track.nr == 0)
		path_list_append("*", &track);
	for (i = 0; i < track.nr; i++) {
		struct path_list_item *item = track.items + i;

		strbuf_reset(&buf2);
		strbuf_addch(&buf2, '+');
		if (mirror)
			strbuf_addf(&buf2, "refs/%s:refs/%s",
					item->path, item->path);
		else
			strbuf_addf(&buf2, "refs/heads/%s:refs/remotes/%s/%s",
					item->path, name, item->path);
		if (git_config_set_multivar(buf.buf, buf2.buf, "^$", 0))
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
	path_list_clear(&track, 0);

	return 0;
}

struct branch_info {
	char *remote;
	struct path_list merge;
};

static struct path_list branch_list;

static int config_read_branches(const char *key, const char *value)
{
	if (!prefixcmp(key, "branch.")) {
		char *name;
		struct path_list_item *item;
		struct branch_info *info;
		enum { REMOTE, MERGE } type;

		key += 7;
		if (!postfixcmp(key, ".remote")) {
			name = xstrndup(key, strlen(key) - 7);
			type = REMOTE;
		} else if (!postfixcmp(key, ".merge")) {
			name = xstrndup(key, strlen(key) - 6);
			type = MERGE;
		} else
			return 0;

		item = path_list_insert(name, &branch_list);

		if (!item->util)
			item->util = xcalloc(sizeof(struct branch_info), 1);
		info = item->util;
		if (type == REMOTE) {
			if (info->remote)
				warning("more than one branch.%s", key);
			info->remote = xstrdup(value);
		} else {
			char *space = strchr(value, ' ');
			value = skip_prefix(value, "refs/heads/");
			while (space) {
				char *merge;
				merge = xstrndup(value, space - value);
				path_list_append(merge, &info->merge);
				value = skip_prefix(space + 1, "refs/heads/");
				space = strchr(value, ' ');
			}
			path_list_append(xstrdup(value), &info->merge);
		}
	}
	return 0;
}

static void read_branches(void)
{
	if (branch_list.nr)
		return;
	git_config(config_read_branches);
	sort_path_list(&branch_list);
}

struct ref_states {
	struct remote *remote;
	struct strbuf remote_prefix;
	struct path_list new, stale, tracked;
};

static int handle_one_branch(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct ref_states *states = cb_data;
	struct refspec refspec;

	memset(&refspec, 0, sizeof(refspec));
	refspec.dst = (char *)refname;
	if (!remote_find_tracking(states->remote, &refspec)) {
		struct path_list_item *item;
		const char *name = skip_prefix(refspec.src, "refs/heads/");
		/* symbolic refs pointing nowhere were handled already */
		if ((flags & REF_ISSYMREF) ||
				unsorted_path_list_has_path(&states->tracked,
					name) ||
				unsorted_path_list_has_path(&states->new,
					name))
			return 0;
		item = path_list_append(name, &states->stale);
		item->util = xstrdup(refname);
	}
	return 0;
}

static int get_ref_states(const struct ref *ref, struct ref_states *states)
{
	struct ref *fetch_map = NULL, **tail = &fetch_map;
	int i;

	for (i = 0; i < states->remote->fetch_refspec_nr; i++)
		if (get_fetch_map(ref, states->remote->fetch + i, &tail, 1))
			die("Could not get fetch map for refspec %s",
				states->remote->fetch_refspec[i]);

	states->new.strdup_paths = states->tracked.strdup_paths = 1;
	for (ref = fetch_map; ref; ref = ref->next) {
		struct path_list *target = &states->tracked;
		unsigned char sha1[20];
		void *util = NULL;

		if (!ref->peer_ref || read_ref(ref->peer_ref->name, sha1))
			target = &states->new;
		else {
			target = &states->tracked;
			if (hashcmp(sha1, ref->new_sha1))
				util = &states;
		}
		path_list_append(skip_prefix(ref->name, "refs/heads/"),
				target)->util = util;
	}
	free_refs(fetch_map);

	strbuf_addf(&states->remote_prefix,
		"refs/remotes/%s/", states->remote->name);
	for_each_ref(handle_one_branch, states);
	sort_path_list(&states->stale);

	return 0;
}

struct branches_for_remote {
	const char *prefix;
	struct path_list *branches;
};

static int add_branch_for_removal(const char *refname,
	const unsigned char *sha1, int flags, void *cb_data)
{
	struct branches_for_remote *branches = cb_data;

	if (!prefixcmp(refname, branches->prefix)) {
		struct path_list_item *item;

		/* make sure that symrefs are deleted */
		if (flags & REF_ISSYMREF)
			return unlink(git_path(refname));

		item = path_list_append(refname, branches->branches);
		item->util = xmalloc(20);
		hashcpy(item->util, sha1);
	}

	return 0;
}

static int remove_branches(struct path_list *branches)
{
	int i, result = 0;
	for (i = 0; i < branches->nr; i++) {
		struct path_list_item *item = branches->items + i;
		const char *refname = item->path;
		unsigned char *sha1 = item->util;

		if (delete_ref(refname, sha1))
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
	struct strbuf buf;
	struct path_list branches = { NULL, 0, 0, 1 };
	struct branches_for_remote cb_data = { NULL, &branches };
	int i;

	if (argc != 2)
		usage_with_options(builtin_remote_usage, options);

	remote = remote_get(argv[1]);
	if (!remote)
		die("No such remote: %s", argv[1]);

	strbuf_init(&buf, 0);
	strbuf_addf(&buf, "remote.%s", remote->name);
	if (git_config_rename_section(buf.buf, NULL) < 1)
		return error("Could not remove config section '%s'", buf.buf);

	read_branches();
	for (i = 0; i < branch_list.nr; i++) {
		struct path_list_item *item = branch_list.items + i;
		struct branch_info *info = item->util;
		if (info->remote && !strcmp(info->remote, remote->name)) {
			const char *keys[] = { "remote", "merge", NULL }, **k;
			for (k = keys; *k; k++) {
				strbuf_reset(&buf);
				strbuf_addf(&buf, "branch.%s.%s",
						item->path, *k);
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
	strbuf_reset(&buf);
	strbuf_addf(&buf, "refs/remotes/%s/", remote->name);
	cb_data.prefix = buf.buf;
	i = for_each_ref(add_branch_for_removal, &cb_data);
	strbuf_release(&buf);

	if (!i)
		i = remove_branches(&branches);
	path_list_clear(&branches, 1);

	return i;
}

static void show_list(const char *title, struct path_list *list)
{
	int i;

	if (!list->nr)
		return;

	printf(title, list->nr > 1 ? "es" : "");
	printf("\n    ");
	for (i = 0; i < list->nr; i++)
		printf("%s%s", i ? " " : "", list->items[i].path);
	printf("\n");
}

static int show_or_prune(int argc, const char **argv, int prune)
{
	int dry_run = 0, result = 0;
	struct option options[] = {
		OPT_GROUP("show specific options"),
		OPT__DRY_RUN(&dry_run),
		OPT_END()
	};
	struct ref_states states;

	argc = parse_options(argc, argv, options, builtin_remote_usage, 0);

	if (argc < 1) {
		if (!prune)
			return show_all();
		usage_with_options(builtin_remote_usage, options);
	}

	memset(&states, 0, sizeof(states));
	for (; argc; argc--, argv++) {
		struct transport *transport;
		const struct ref *ref;
		struct strbuf buf;
		int i, got_states;

		states.remote = remote_get(*argv);
		if (!states.remote)
			return error("No such remote: %s", *argv);
		transport = transport_get(NULL, states.remote->url_nr > 0 ?
			states.remote->url[0] : NULL);
		ref = transport_get_remote_refs(transport);
		transport_disconnect(transport);

		read_branches();
		got_states = get_ref_states(ref, &states);
		if (got_states)
			result = error("Error getting local info for '%s'",
					states.remote->name);

		if (prune) {
			struct strbuf buf;
			int prefix_len;

			strbuf_init(&buf, 0);
			if (states.remote->fetch_refspec_nr == 1 &&
					states.remote->fetch->pattern &&
					!strcmp(states.remote->fetch->src,
						states.remote->fetch->dst))
				/* handle --mirror remote */
				strbuf_addstr(&buf, "refs/heads/");
			else
				strbuf_addf(&buf, "refs/remotes/%s/", *argv);
			prefix_len = buf.len;

			for (i = 0; i < states.stale.nr; i++) {
				strbuf_setlen(&buf, prefix_len);
				strbuf_addstr(&buf, states.stale.items[i].path);
				result |= delete_ref(buf.buf, NULL);
			}

			strbuf_release(&buf);
			goto cleanup_states;
		}

		printf("* remote %s\n  URL: %s\n", *argv,
			states.remote->url_nr > 0 ?
				states.remote->url[0] : "(no URL)");

		for (i = 0; i < branch_list.nr; i++) {
			struct path_list_item *branch = branch_list.items + i;
			struct branch_info *info = branch->util;
			int j;

			if (!info->merge.nr || strcmp(*argv, info->remote))
				continue;
			printf("  Remote branch%s merged with 'git pull' "
				"while on branch %s\n   ",
				info->merge.nr > 1 ? "es" : "",
				branch->path);
			for (j = 0; j < info->merge.nr; j++)
				printf(" %s", info->merge.items[j].path);
			printf("\n");
		}

		if (got_states)
			continue;
		strbuf_init(&buf, 0);
		strbuf_addf(&buf, "  New remote branch%%s (next fetch will "
			"store in remotes/%s)", states.remote->name);
		show_list(buf.buf, &states.new);
		strbuf_release(&buf);
		show_list("  Stale tracking branch%s (use 'git remote prune')",
				&states.stale);
		show_list("  Tracked remote branch%s",
				&states.tracked);

		if (states.remote->push_refspec_nr) {
			printf("  Local branch%s pushed with 'git push'\n   ",
				states.remote->push_refspec_nr > 1 ?
					"es" : "");
			for (i = 0; i < states.remote->push_refspec_nr; i++) {
				struct refspec *spec = states.remote->push + i;
				printf(" %s%s%s%s", spec->force ? "+" : "",
					skip_prefix(spec->src, "refs/heads/"),
					spec->dst ? ":" : "",
					skip_prefix(spec->dst, "refs/heads/"));
			}
			printf("\n");
		}
cleanup_states:
		/* NEEDSWORK: free remote */
		path_list_clear(&states.new, 0);
		path_list_clear(&states.stale, 0);
		path_list_clear(&states.tracked, 0);
	}

	return result;
}

static int get_one_remote_for_update(struct remote *remote, void *priv)
{
	struct path_list *list = priv;
	if (!remote->skip_default_update)
		path_list_append(xstrdup(remote->name), list);
	return 0;
}

struct remote_group {
	const char *name;
	struct path_list *list;
} remote_group;

static int get_remote_group(const char *key, const char *value)
{
	if (!prefixcmp(key, "remotes.") &&
			!strcmp(key + 8, remote_group.name)) {
		/* split list by white space */
		int space = strcspn(value, " \t\n");
		while (*value) {
			if (space > 1)
				path_list_append(xstrndup(value, space),
						remote_group.list);
			value += space + (value[space] != '\0');
			space = strcspn(value, " \t\n");
		}
	}

	return 0;
}

static int update(int argc, const char **argv)
{
	int i, result = 0;
	struct path_list list = { NULL, 0, 0, 0 };
	static const char *default_argv[] = { NULL, "default", NULL };

	if (argc < 2) {
		argc = 2;
		argv = default_argv;
	}

	remote_group.list = &list;
	for (i = 1; i < argc; i++) {
		remote_group.name = argv[i];
		result = git_config(get_remote_group);
	}

	if (!result && !list.nr  && argc == 2 && !strcmp(argv[1], "default"))
		result = for_each_remote(get_one_remote_for_update, &list);

	for (i = 0; i < list.nr; i++)
		result |= fetch_remote(list.items[i].path);

	/* all names were strdup()ed or strndup()ed */
	list.strdup_paths = 1;
	path_list_clear(&list, 0);

	return result;
}

static int get_one_entry(struct remote *remote, void *priv)
{
	struct path_list *list = priv;

	path_list_append(remote->name, list)->util = remote->url_nr ?
		(void *)remote->url[0] : NULL;
	if (remote->url_nr > 1)
		warning("Remote %s has more than one URL", remote->name);

	return 0;
}

static int show_all(void)
{
	struct path_list list = { NULL, 0, 0 };
	int result = for_each_remote(get_one_entry, &list);

	if (!result) {
		int i;

		sort_path_list(&list);
		for (i = 0; i < list.nr; i++) {
			struct path_list_item *item = list.items + i;
			printf("%s%s%s\n", item->path,
				verbose ? "\t" : "",
				verbose && item->util ?
					(const char *)item->util : "");
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
	else if (!strcmp(argv[0], "rm"))
		result = rm(argc, argv);
	else if (!strcmp(argv[0], "show"))
		result = show_or_prune(argc, argv, 0);
	else if (!strcmp(argv[0], "prune"))
		result = show_or_prune(argc, argv, 1);
	else if (!strcmp(argv[0], "update"))
		result = update(argc, argv);
	else {
		error("Unknown subcommand: %s", argv[0]);
		usage_with_options(builtin_remote_usage, options);
	}

	return result ? 1 : 0;
}
