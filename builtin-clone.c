/*
 * Builtin "git clone"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>,
 *		 2008 Daniel Barkalow <barkalow@iabervon.org>
 * Based on git-commit.sh by Junio C Hamano and Linus Torvalds
 *
 * Clone a repository into a different directory that does not yet exist.
 */

#include "cache.h"
#include "parse-options.h"
#include "fetch-pack.h"
#include "refs.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "transport.h"
#include "strbuf.h"
#include "dir.h"
#include "pack-refs.h"

/*
 * Overall FIXMEs:
 *  - respect DB_ENVIRONMENT for .git/objects.
 *
 * Implementation notes:
 *  - dropping use-separate-remote and no-separate-remote compatibility
 *
 */
static const char * const builtin_clone_usage[] = {
	"git clone [options] [--] <repo> [<dir>]",
	NULL
};

static int option_quiet, option_no_checkout, option_bare, option_mirror;
static int option_local, option_no_hardlinks, option_shared;
static char *option_template, *option_reference, *option_depth;
static char *option_origin = NULL;
static char *option_upload_pack = "git-upload-pack";

static struct option builtin_clone_options[] = {
	OPT__QUIET(&option_quiet),
	OPT_BOOLEAN('n', "no-checkout", &option_no_checkout,
		    "don't create a checkout"),
	OPT_BOOLEAN(0, "bare", &option_bare, "create a bare repository"),
	OPT_BOOLEAN(0, "naked", &option_bare, "create a bare repository"),
	OPT_BOOLEAN(0, "mirror", &option_mirror,
		    "create a mirror repository (implies bare)"),
	OPT_BOOLEAN('l', "local", &option_local,
		    "to clone from a local repository"),
	OPT_BOOLEAN(0, "no-hardlinks", &option_no_hardlinks,
		    "don't use local hardlinks, always copy"),
	OPT_BOOLEAN('s', "shared", &option_shared,
		    "setup as shared repository"),
	OPT_STRING(0, "template", &option_template, "path",
		   "path the template repository"),
	OPT_STRING(0, "reference", &option_reference, "repo",
		   "reference repository"),
	OPT_STRING('o', "origin", &option_origin, "branch",
		   "use <branch> instead or 'origin' to track upstream"),
	OPT_STRING('u', "upload-pack", &option_upload_pack, "path",
		   "path to git-upload-pack on the remote"),
	OPT_STRING(0, "depth", &option_depth, "depth",
		    "create a shallow clone of that depth"),

	OPT_END()
};

static char *get_repo_path(const char *repo, int *is_bundle)
{
	static char *suffix[] = { "/.git", ".git", "" };
	static char *bundle_suffix[] = { ".bundle", "" };
	struct stat st;
	int i;

	for (i = 0; i < ARRAY_SIZE(suffix); i++) {
		const char *path;
		path = mkpath("%s%s", repo, suffix[i]);
		if (!stat(path, &st) && S_ISDIR(st.st_mode)) {
			*is_bundle = 0;
			return xstrdup(make_nonrelative_path(path));
		}
	}

	for (i = 0; i < ARRAY_SIZE(bundle_suffix); i++) {
		const char *path;
		path = mkpath("%s%s", repo, bundle_suffix[i]);
		if (!stat(path, &st) && S_ISREG(st.st_mode)) {
			*is_bundle = 1;
			return xstrdup(make_nonrelative_path(path));
		}
	}

	return NULL;
}

static char *guess_dir_name(const char *repo, int is_bundle, int is_bare)
{
	const char *end = repo + strlen(repo), *start;

	/*
	 * Strip trailing slashes and /.git
	 */
	while (repo < end && is_dir_sep(end[-1]))
		end--;
	if (end - repo > 5 && is_dir_sep(end[-5]) &&
	    !strncmp(end - 4, ".git", 4)) {
		end -= 5;
		while (repo < end && is_dir_sep(end[-1]))
			end--;
	}

	/*
	 * Find last component, but be prepared that repo could have
	 * the form  "remote.example.com:foo.git", i.e. no slash
	 * in the directory part.
	 */
	start = end;
	while (repo < start && !is_dir_sep(start[-1]) && start[-1] != ':')
		start--;

	/*
	 * Strip .{bundle,git}.
	 */
	if (is_bundle) {
		if (end - start > 7 && !strncmp(end - 7, ".bundle", 7))
			end -= 7;
	} else {
		if (end - start > 4 && !strncmp(end - 4, ".git", 4))
			end -= 4;
	}

	if (is_bare) {
		char *result = xmalloc(end - start + 5);
		sprintf(result, "%.*s.git", (int)(end - start), start);
		return result;
	}

	return xstrndup(start, end - start);
}

static int is_directory(const char *path)
{
	struct stat buf;

	return !stat(path, &buf) && S_ISDIR(buf.st_mode);
}

static void setup_reference(const char *repo)
{
	const char *ref_git;
	char *ref_git_copy;

	struct remote *remote;
	struct transport *transport;
	const struct ref *extra;

	ref_git = make_absolute_path(option_reference);

	if (is_directory(mkpath("%s/.git/objects", ref_git)))
		ref_git = mkpath("%s/.git", ref_git);
	else if (!is_directory(mkpath("%s/objects", ref_git)))
		die("reference repository '%s' is not a local directory.",
		    option_reference);

	ref_git_copy = xstrdup(ref_git);

	add_to_alternates_file(ref_git_copy);

	remote = remote_get(ref_git_copy);
	transport = transport_get(remote, ref_git_copy);
	for (extra = transport_get_remote_refs(transport); extra;
	     extra = extra->next)
		add_extra_ref(extra->name, extra->old_sha1, 0);

	transport_disconnect(transport);

	free(ref_git_copy);
}

static void copy_or_link_directory(char *src, char *dest)
{
	struct dirent *de;
	struct stat buf;
	int src_len, dest_len;
	DIR *dir;

	dir = opendir(src);
	if (!dir)
		die("failed to open %s\n", src);

	if (mkdir(dest, 0777)) {
		if (errno != EEXIST)
			die("failed to create directory %s\n", dest);
		else if (stat(dest, &buf))
			die("failed to stat %s\n", dest);
		else if (!S_ISDIR(buf.st_mode))
			die("%s exists and is not a directory\n", dest);
	}

	src_len = strlen(src);
	src[src_len] = '/';
	dest_len = strlen(dest);
	dest[dest_len] = '/';

	while ((de = readdir(dir)) != NULL) {
		strcpy(src + src_len + 1, de->d_name);
		strcpy(dest + dest_len + 1, de->d_name);
		if (stat(src, &buf)) {
			warning ("failed to stat %s\n", src);
			continue;
		}
		if (S_ISDIR(buf.st_mode)) {
			if (de->d_name[0] != '.')
				copy_or_link_directory(src, dest);
			continue;
		}

		if (unlink(dest) && errno != ENOENT)
			die("failed to unlink %s\n", dest);
		if (!option_no_hardlinks) {
			if (!link(src, dest))
				continue;
			if (option_local)
				die("failed to create link %s\n", dest);
			option_no_hardlinks = 1;
		}
		if (copy_file(dest, src, 0666))
			die("failed to copy file to %s\n", dest);
	}
	closedir(dir);
}

static const struct ref *clone_local(const char *src_repo,
				     const char *dest_repo)
{
	const struct ref *ret;
	char src[PATH_MAX];
	char dest[PATH_MAX];
	struct remote *remote;
	struct transport *transport;

	if (option_shared)
		add_to_alternates_file(src_repo);
	else {
		snprintf(src, PATH_MAX, "%s/objects", src_repo);
		snprintf(dest, PATH_MAX, "%s/objects", dest_repo);
		copy_or_link_directory(src, dest);
	}

	remote = remote_get(src_repo);
	transport = transport_get(remote, src_repo);
	ret = transport_get_remote_refs(transport);
	transport_disconnect(transport);
	return ret;
}

static const char *junk_work_tree;
static const char *junk_git_dir;
pid_t junk_pid;

static void remove_junk(void)
{
	struct strbuf sb;
	if (getpid() != junk_pid)
		return;
	strbuf_init(&sb, 0);
	if (junk_git_dir) {
		strbuf_addstr(&sb, junk_git_dir);
		remove_dir_recursively(&sb, 0);
		strbuf_reset(&sb);
	}
	if (junk_work_tree) {
		strbuf_addstr(&sb, junk_work_tree);
		remove_dir_recursively(&sb, 0);
		strbuf_reset(&sb);
	}
}

static void remove_junk_on_signal(int signo)
{
	remove_junk();
	signal(SIGINT, SIG_DFL);
	raise(signo);
}

static const struct ref *locate_head(const struct ref *refs,
				     const struct ref *mapped_refs,
				     const struct ref **remote_head_p)
{
	const struct ref *remote_head = NULL;
	const struct ref *remote_master = NULL;
	const struct ref *r;
	for (r = refs; r; r = r->next)
		if (!strcmp(r->name, "HEAD"))
			remote_head = r;

	for (r = mapped_refs; r; r = r->next)
		if (!strcmp(r->name, "refs/heads/master"))
			remote_master = r;

	if (remote_head_p)
		*remote_head_p = remote_head;

	/* If there's no HEAD value at all, never mind. */
	if (!remote_head)
		return NULL;

	/* If refs/heads/master could be right, it is. */
	if (remote_master && !hashcmp(remote_master->old_sha1,
				      remote_head->old_sha1))
		return remote_master;

	/* Look for another ref that points there */
	for (r = mapped_refs; r; r = r->next)
		if (r != remote_head &&
		    !hashcmp(r->old_sha1, remote_head->old_sha1))
			return r;

	/* Nothing is the same */
	return NULL;
}

static struct ref *write_remote_refs(const struct ref *refs,
		struct refspec *refspec, const char *reflog)
{
	struct ref *local_refs = NULL;
	struct ref **tail = &local_refs;
	struct ref *r;

	get_fetch_map(refs, refspec, &tail, 0);
	if (!option_mirror)
		get_fetch_map(refs, tag_refspec, &tail, 0);

	for (r = local_refs; r; r = r->next)
		add_extra_ref(r->peer_ref->name, r->old_sha1, 0);

	pack_refs(PACK_REFS_ALL);
	clear_extra_refs();

	return local_refs;
}

int cmd_clone(int argc, const char **argv, const char *prefix)
{
	int use_local_hardlinks = 1;
	int use_separate_remote = 1;
	int is_bundle = 0;
	struct stat buf;
	const char *repo_name, *repo, *work_tree, *git_dir;
	char *path, *dir;
	const struct ref *refs, *head_points_at, *remote_head, *mapped_refs;
	char branch_top[256], key[256], value[256];
	struct strbuf reflog_msg;
	struct transport *transport = NULL;
	char *src_ref_prefix = "refs/heads/";

	struct refspec refspec;

	junk_pid = getpid();

	argc = parse_options(argc, argv, builtin_clone_options,
			     builtin_clone_usage, 0);

	if (argc == 0)
		die("You must specify a repository to clone.");

	if (option_no_hardlinks)
		use_local_hardlinks = 0;

	if (option_mirror)
		option_bare = 1;

	if (option_bare) {
		if (option_origin)
			die("--bare and --origin %s options are incompatible.",
			    option_origin);
		option_no_checkout = 1;
		use_separate_remote = 0;
	}

	if (!option_origin)
		option_origin = "origin";

	repo_name = argv[0];

	path = get_repo_path(repo_name, &is_bundle);
	if (path)
		repo = path;
	else if (!strchr(repo_name, ':'))
		repo = xstrdup(make_absolute_path(repo_name));
	else
		repo = repo_name;

	if (argc == 2)
		dir = xstrdup(argv[1]);
	else
		dir = guess_dir_name(repo_name, is_bundle, option_bare);

	if (!stat(dir, &buf))
		die("destination directory '%s' already exists.", dir);

	strbuf_init(&reflog_msg, 0);
	strbuf_addf(&reflog_msg, "clone: from %s", repo);

	if (option_bare)
		work_tree = NULL;
	else {
		work_tree = getenv("GIT_WORK_TREE");
		if (work_tree && !stat(work_tree, &buf))
			die("working tree '%s' already exists.", work_tree);
	}

	if (option_bare || work_tree)
		git_dir = xstrdup(dir);
	else {
		work_tree = dir;
		git_dir = xstrdup(mkpath("%s/.git", dir));
	}

	if (!option_bare) {
		junk_work_tree = work_tree;
		if (safe_create_leading_directories_const(work_tree) < 0)
			die("could not create leading directories of '%s'",
					work_tree);
		if (mkdir(work_tree, 0755))
			die("could not create work tree dir '%s'.", work_tree);
		set_git_work_tree(work_tree);
	}
	junk_git_dir = git_dir;
	atexit(remove_junk);
	signal(SIGINT, remove_junk_on_signal);

	setenv(CONFIG_ENVIRONMENT, xstrdup(mkpath("%s/config", git_dir)), 1);

	if (safe_create_leading_directories_const(git_dir) < 0)
		die("could not create leading directories of '%s'", git_dir);
	set_git_dir(make_absolute_path(git_dir));

	init_db(option_template, option_quiet ? INIT_DB_QUIET : 0);

	/*
	 * At this point, the config exists, so we do not need the
	 * environment variable.  We actually need to unset it, too, to
	 * re-enable parsing of the global configs.
	 */
	unsetenv(CONFIG_ENVIRONMENT);

	if (option_reference)
		setup_reference(git_dir);

	git_config(git_default_config, NULL);

	if (option_bare) {
		if (option_mirror)
			src_ref_prefix = "refs/";
		strcpy(branch_top, src_ref_prefix);

		git_config_set("core.bare", "true");
	} else {
		snprintf(branch_top, sizeof(branch_top),
			 "refs/remotes/%s/", option_origin);
	}

	if (option_mirror || !option_bare) {
		/* Configure the remote */
		if (option_mirror) {
			snprintf(key, sizeof(key),
					"remote.%s.mirror", option_origin);
			git_config_set(key, "true");
		}

		snprintf(key, sizeof(key), "remote.%s.url", option_origin);
		git_config_set(key, repo);

		snprintf(key, sizeof(key), "remote.%s.fetch", option_origin);
		snprintf(value, sizeof(value),
				"+%s*:%s*", src_ref_prefix, branch_top);
		git_config_set_multivar(key, value, "^$", 0);
	}

	refspec.force = 0;
	refspec.pattern = 1;
	refspec.src = src_ref_prefix;
	refspec.dst = branch_top;

	if (path && !is_bundle)
		refs = clone_local(path, git_dir);
	else {
		struct remote *remote = remote_get(argv[0]);
		transport = transport_get(remote, remote->url[0]);

		if (!transport->get_refs_list || !transport->fetch)
			die("Don't know how to clone %s", transport->url);

		transport_set_option(transport, TRANS_OPT_KEEP, "yes");

		if (option_depth)
			transport_set_option(transport, TRANS_OPT_DEPTH,
					     option_depth);

		if (option_quiet)
			transport->verbose = -1;

		if (option_upload_pack)
			transport_set_option(transport, TRANS_OPT_UPLOADPACK,
					     option_upload_pack);

		refs = transport_get_remote_refs(transport);
		transport_fetch_refs(transport, refs);
	}

	clear_extra_refs();

	mapped_refs = write_remote_refs(refs, &refspec, reflog_msg.buf);

	head_points_at = locate_head(refs, mapped_refs, &remote_head);

	if (head_points_at) {
		/* Local default branch link */
		create_symref("HEAD", head_points_at->name, NULL);

		if (!option_bare) {
			struct strbuf head_ref;
			const char *head = head_points_at->name;

			if (!prefixcmp(head, "refs/heads/"))
				head += 11;

			/* Set up the initial local branch */

			/* Local branch initial value */
			update_ref(reflog_msg.buf, "HEAD",
				   head_points_at->old_sha1,
				   NULL, 0, DIE_ON_ERR);

			strbuf_init(&head_ref, 0);
			strbuf_addstr(&head_ref, branch_top);
			strbuf_addstr(&head_ref, "HEAD");

			/* Remote branch link */
			create_symref(head_ref.buf,
				      head_points_at->peer_ref->name,
				      reflog_msg.buf);

			snprintf(key, sizeof(key), "branch.%s.remote", head);
			git_config_set(key, option_origin);
			snprintf(key, sizeof(key), "branch.%s.merge", head);
			git_config_set(key, head_points_at->name);
		}
	} else if (remote_head) {
		/* Source had detached HEAD pointing somewhere. */
		if (!option_bare)
			update_ref(reflog_msg.buf, "HEAD",
				   remote_head->old_sha1,
				   NULL, REF_NODEREF, DIE_ON_ERR);
	} else {
		/* Nothing to checkout out */
		if (!option_no_checkout)
			warning("remote HEAD refers to nonexistent ref, "
				"unable to checkout.\n");
		option_no_checkout = 1;
	}

	if (transport)
		transport_unlock_pack(transport);

	if (!option_no_checkout) {
		struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));
		struct unpack_trees_options opts;
		struct tree *tree;
		struct tree_desc t;
		int fd;

		/* We need to be in the new work tree for the checkout */
		setup_work_tree();

		fd = hold_locked_index(lock_file, 1);

		memset(&opts, 0, sizeof opts);
		opts.update = 1;
		opts.merge = 1;
		opts.fn = oneway_merge;
		opts.verbose_update = !option_quiet;
		opts.src_index = &the_index;
		opts.dst_index = &the_index;

		tree = parse_tree_indirect(remote_head->old_sha1);
		parse_tree(tree);
		init_tree_desc(&t, tree->buffer, tree->size);
		unpack_trees(1, &t, &opts);

		if (write_cache(fd, active_cache, active_nr) ||
		    commit_locked_index(lock_file))
			die("unable to write new index file");
	}

	strbuf_release(&reflog_msg);
	junk_pid = 0;
	return 0;
}
