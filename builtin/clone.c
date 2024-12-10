/*
 * Builtin "git clone"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>,
 *		 2008 Daniel Barkalow <barkalow@iabervon.org>
 * Based on git-commit.sh by Junio C Hamano and Linus Torvalds
 *
 * Clone a repository into a different directory that does not yet exist.
 */
#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"

#include "abspath.h"
#include "advice.h"
#include "config.h"
#include "copy.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "parse-options.h"
#include "refs.h"
#include "refspec.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "transport.h"
#include "strbuf.h"
#include "dir.h"
#include "dir-iterator.h"
#include "iterator.h"
#include "sigchain.h"
#include "branch.h"
#include "remote.h"
#include "run-command.h"
#include "setup.h"
#include "connected.h"
#include "packfile.h"
#include "path.h"
#include "pkt-line.h"
#include "list-objects-filter-options.h"
#include "hook.h"
#include "bundle.h"
#include "bundle-uri.h"

/*
 * Overall FIXMEs:
 *  - respect DB_ENVIRONMENT for .git/objects.
 *
 * Implementation notes:
 *  - dropping use-separate-remote and no-separate-remote compatibility
 *
 */
static const char * const builtin_clone_usage[] = {
	N_("git clone [<options>] [--] <repo> [<dir>]"),
	NULL
};

static int option_no_checkout, option_bare, option_mirror, option_single_branch = -1;
static int option_local = -1, option_no_hardlinks, option_shared;
static int option_no_tags;
static int option_shallow_submodules;
static int option_reject_shallow = -1;    /* unspecified */
static int config_reject_shallow = -1;    /* unspecified */
static int deepen;
static char *option_template, *option_depth, *option_since;
static char *option_origin = NULL;
static char *remote_name = NULL;
static char *option_branch = NULL;
static struct string_list option_not = STRING_LIST_INIT_NODUP;
static const char *real_git_dir;
static const char *ref_format;
static const char *option_upload_pack = "git-upload-pack";
static int option_verbosity;
static int option_progress = -1;
static int option_sparse_checkout;
static enum transport_family family;
static struct string_list option_config = STRING_LIST_INIT_NODUP;
static struct string_list option_required_reference = STRING_LIST_INIT_NODUP;
static struct string_list option_optional_reference = STRING_LIST_INIT_NODUP;
static int option_dissociate;
static int max_jobs = -1;
static struct string_list option_recurse_submodules = STRING_LIST_INIT_NODUP;
static struct list_objects_filter_options filter_options = LIST_OBJECTS_FILTER_INIT;
static int option_filter_submodules = -1;    /* unspecified */
static int config_filter_submodules = -1;    /* unspecified */
static struct string_list server_options = STRING_LIST_INIT_NODUP;
static int option_remote_submodules;
static const char *bundle_uri;

static int recurse_submodules_cb(const struct option *opt,
				 const char *arg, int unset)
{
	if (unset)
		string_list_clear((struct string_list *)opt->value, 0);
	else if (arg)
		string_list_append((struct string_list *)opt->value, arg);
	else
		string_list_append((struct string_list *)opt->value,
				   (const char *)opt->defval);

	return 0;
}

static struct option builtin_clone_options[] = {
	OPT__VERBOSITY(&option_verbosity),
	OPT_BOOL(0, "progress", &option_progress,
		 N_("force progress reporting")),
	OPT_BOOL(0, "reject-shallow", &option_reject_shallow,
		 N_("don't clone shallow repository")),
	OPT_BOOL('n', "no-checkout", &option_no_checkout,
		 N_("don't create a checkout")),
	OPT_BOOL(0, "bare", &option_bare, N_("create a bare repository")),
	OPT_HIDDEN_BOOL(0, "naked", &option_bare,
			N_("create a bare repository")),
	OPT_BOOL(0, "mirror", &option_mirror,
		 N_("create a mirror repository (implies --bare)")),
	OPT_BOOL('l', "local", &option_local,
		N_("to clone from a local repository")),
	OPT_BOOL(0, "no-hardlinks", &option_no_hardlinks,
		    N_("don't use local hardlinks, always copy")),
	OPT_BOOL('s', "shared", &option_shared,
		    N_("setup as shared repository")),
	{ OPTION_CALLBACK, 0, "recurse-submodules", &option_recurse_submodules,
	  N_("pathspec"), N_("initialize submodules in the clone"),
	  PARSE_OPT_OPTARG, recurse_submodules_cb, (intptr_t)"." },
	OPT_ALIAS(0, "recursive", "recurse-submodules"),
	OPT_INTEGER('j', "jobs", &max_jobs,
		    N_("number of submodules cloned in parallel")),
	OPT_STRING(0, "template", &option_template, N_("template-directory"),
		   N_("directory from which templates will be used")),
	OPT_STRING_LIST(0, "reference", &option_required_reference, N_("repo"),
			N_("reference repository")),
	OPT_STRING_LIST(0, "reference-if-able", &option_optional_reference,
			N_("repo"), N_("reference repository")),
	OPT_BOOL(0, "dissociate", &option_dissociate,
		 N_("use --reference only while cloning")),
	OPT_STRING('o', "origin", &option_origin, N_("name"),
		   N_("use <name> instead of 'origin' to track upstream")),
	OPT_STRING('b', "branch", &option_branch, N_("branch"),
		   N_("checkout <branch> instead of the remote's HEAD")),
	OPT_STRING('u', "upload-pack", &option_upload_pack, N_("path"),
		   N_("path to git-upload-pack on the remote")),
	OPT_STRING(0, "depth", &option_depth, N_("depth"),
		    N_("create a shallow clone of that depth")),
	OPT_STRING(0, "shallow-since", &option_since, N_("time"),
		    N_("create a shallow clone since a specific time")),
	OPT_STRING_LIST(0, "shallow-exclude", &option_not, N_("ref"),
			N_("deepen history of shallow clone, excluding ref")),
	OPT_BOOL(0, "single-branch", &option_single_branch,
		    N_("clone only one branch, HEAD or --branch")),
	OPT_BOOL(0, "no-tags", &option_no_tags,
		 N_("don't clone any tags, and make later fetches not to follow them")),
	OPT_BOOL(0, "shallow-submodules", &option_shallow_submodules,
		    N_("any cloned submodules will be shallow")),
	OPT_STRING(0, "separate-git-dir", &real_git_dir, N_("gitdir"),
		   N_("separate git dir from working tree")),
	OPT_STRING(0, "ref-format", &ref_format, N_("format"),
		   N_("specify the reference format to use")),
	OPT_STRING_LIST('c', "config", &option_config, N_("key=value"),
			N_("set config inside the new repository")),
	OPT_STRING_LIST(0, "server-option", &server_options,
			N_("server-specific"), N_("option to transmit")),
	OPT_IPVERSION(&family),
	OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
	OPT_BOOL(0, "also-filter-submodules", &option_filter_submodules,
		    N_("apply partial clone filters to submodules")),
	OPT_BOOL(0, "remote-submodules", &option_remote_submodules,
		    N_("any cloned submodules will use their remote-tracking branch")),
	OPT_BOOL(0, "sparse", &option_sparse_checkout,
		    N_("initialize sparse-checkout file to include only files at root")),
	OPT_STRING(0, "bundle-uri", &bundle_uri,
		   N_("uri"), N_("a URI for downloading bundles before fetching from origin remote")),
	OPT_END()
};

static const char *get_repo_path_1(struct strbuf *path, int *is_bundle)
{
	static const char *suffix[] = { "/.git", "", ".git/.git", ".git" };
	static const char *bundle_suffix[] = { ".bundle", "" };
	size_t baselen = path->len;
	struct stat st;
	int i;

	for (i = 0; i < ARRAY_SIZE(suffix); i++) {
		strbuf_setlen(path, baselen);
		strbuf_addstr(path, suffix[i]);
		if (stat(path->buf, &st))
			continue;
		if (S_ISDIR(st.st_mode) && is_git_directory(path->buf)) {
			*is_bundle = 0;
			return path->buf;
		} else if (S_ISREG(st.st_mode) && st.st_size > 8) {
			/* Is it a "gitfile"? */
			char signature[8];
			const char *dst;
			int len, fd = open(path->buf, O_RDONLY);
			if (fd < 0)
				continue;
			len = read_in_full(fd, signature, 8);
			close(fd);
			if (len != 8 || strncmp(signature, "gitdir: ", 8))
				continue;
			dst = read_gitfile(path->buf);
			if (dst) {
				*is_bundle = 0;
				return dst;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(bundle_suffix); i++) {
		strbuf_setlen(path, baselen);
		strbuf_addstr(path, bundle_suffix[i]);
		if (!stat(path->buf, &st) && S_ISREG(st.st_mode)) {
			*is_bundle = 1;
			return path->buf;
		}
	}

	return NULL;
}

static char *get_repo_path(const char *repo, int *is_bundle)
{
	struct strbuf path = STRBUF_INIT;
	const char *raw;
	char *canon;

	strbuf_addstr(&path, repo);
	raw = get_repo_path_1(&path, is_bundle);
	canon = raw ? absolute_pathdup(raw) : NULL;
	strbuf_release(&path);
	return canon;
}

static int add_one_reference(struct string_list_item *item, void *cb_data)
{
	struct strbuf err = STRBUF_INIT;
	int *required = cb_data;
	char *ref_git = compute_alternate_path(item->string, &err);

	if (!ref_git) {
		if (*required)
			die("%s", err.buf);
		else
			fprintf(stderr,
				_("info: Could not add alternate for '%s': %s\n"),
				item->string, err.buf);
	} else {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addf(&sb, "%s/objects", ref_git);
		add_to_alternates_file(sb.buf);
		strbuf_release(&sb);
	}

	strbuf_release(&err);
	free(ref_git);
	return 0;
}

static void setup_reference(void)
{
	int required = 1;
	for_each_string_list(&option_required_reference,
			     add_one_reference, &required);
	required = 0;
	for_each_string_list(&option_optional_reference,
			     add_one_reference, &required);
}

static void copy_alternates(struct strbuf *src, const char *src_repo)
{
	/*
	 * Read from the source objects/info/alternates file
	 * and copy the entries to corresponding file in the
	 * destination repository with add_to_alternates_file().
	 * Both src and dst have "$path/objects/info/alternates".
	 *
	 * Instead of copying bit-for-bit from the original,
	 * we need to append to existing one so that the already
	 * created entry via "clone -s" is not lost, and also
	 * to turn entries with paths relative to the original
	 * absolute, so that they can be used in the new repository.
	 */
	FILE *in = xfopen(src->buf, "r");
	struct strbuf line = STRBUF_INIT;

	while (strbuf_getline(&line, in) != EOF) {
		char *abs_path;
		if (!line.len || line.buf[0] == '#')
			continue;
		if (is_absolute_path(line.buf)) {
			add_to_alternates_file(line.buf);
			continue;
		}
		abs_path = mkpathdup("%s/objects/%s", src_repo, line.buf);
		if (!normalize_path_copy(abs_path, abs_path))
			add_to_alternates_file(abs_path);
		else
			warning("skipping invalid relative alternate: %s/%s",
				src_repo, line.buf);
		free(abs_path);
	}
	strbuf_release(&line);
	fclose(in);
}

static void mkdir_if_missing(const char *pathname, mode_t mode)
{
	struct stat st;

	if (!mkdir(pathname, mode))
		return;

	if (errno != EEXIST)
		die_errno(_("failed to create directory '%s'"), pathname);
	else if (stat(pathname, &st))
		die_errno(_("failed to stat '%s'"), pathname);
	else if (!S_ISDIR(st.st_mode))
		die(_("%s exists and is not a directory"), pathname);
}

static void copy_or_link_directory(struct strbuf *src, struct strbuf *dest,
				   const char *src_repo)
{
	int src_len, dest_len;
	struct dir_iterator *iter;
	int iter_status;

	/*
	 * Refuse copying directories by default which aren't owned by us. The
	 * code that performs either the copying or hardlinking is not prepared
	 * to handle various edge cases where an adversary may for example
	 * racily swap out files for symlinks. This can cause us to
	 * inadvertently use the wrong source file.
	 *
	 * Furthermore, even if we were prepared to handle such races safely,
	 * creating hardlinks across user boundaries is an inherently unsafe
	 * operation as the hardlinked files can be rewritten at will by the
	 * potentially-untrusted user. We thus refuse to do so by default.
	 */
	die_upon_dubious_ownership(NULL, NULL, src_repo);

	mkdir_if_missing(dest->buf, 0777);

	iter = dir_iterator_begin(src->buf, DIR_ITERATOR_PEDANTIC);

	if (!iter) {
		if (errno == ENOTDIR) {
			int saved_errno = errno;
			struct stat st;

			if (!lstat(src->buf, &st) && S_ISLNK(st.st_mode))
				die(_("'%s' is a symlink, refusing to clone with --local"),
				    src->buf);
			errno = saved_errno;
		}
		die_errno(_("failed to start iterator over '%s'"), src->buf);
	}

	strbuf_addch(src, '/');
	src_len = src->len;
	strbuf_addch(dest, '/');
	dest_len = dest->len;

	while ((iter_status = dir_iterator_advance(iter)) == ITER_OK) {
		strbuf_setlen(src, src_len);
		strbuf_addstr(src, iter->relative_path);
		strbuf_setlen(dest, dest_len);
		strbuf_addstr(dest, iter->relative_path);

		if (S_ISLNK(iter->st.st_mode))
			die(_("symlink '%s' exists, refusing to clone with --local"),
			    iter->relative_path);

		if (S_ISDIR(iter->st.st_mode)) {
			mkdir_if_missing(dest->buf, 0777);
			continue;
		}

		/* Files that cannot be copied bit-for-bit... */
		if (!fspathcmp(iter->relative_path, "info/alternates")) {
			copy_alternates(src, src_repo);
			continue;
		}

		if (unlink(dest->buf) && errno != ENOENT)
			die_errno(_("failed to unlink '%s'"), dest->buf);
		if (!option_no_hardlinks) {
			if (!link(src->buf, dest->buf)) {
				struct stat st;

				/*
				 * Sanity-check whether the created hardlink
				 * actually links to the expected file now. This
				 * catches time-of-check-time-of-use bugs in
				 * case the source file was meanwhile swapped.
				 */
				if (lstat(dest->buf, &st))
					die(_("hardlink cannot be checked at '%s'"), dest->buf);
				if (st.st_mode != iter->st.st_mode ||
				    st.st_ino != iter->st.st_ino ||
				    st.st_dev != iter->st.st_dev ||
				    st.st_size != iter->st.st_size ||
				    st.st_uid != iter->st.st_uid ||
				    st.st_gid != iter->st.st_gid)
					die(_("hardlink different from source at '%s'"), dest->buf);

				continue;
			}
			if (option_local > 0)
				die_errno(_("failed to create link '%s'"), dest->buf);
			option_no_hardlinks = 1;
		}
		if (copy_file_with_time(dest->buf, src->buf, 0666))
			die_errno(_("failed to copy file to '%s'"), dest->buf);
	}

	if (iter_status != ITER_DONE) {
		strbuf_setlen(src, src_len);
		die(_("failed to iterate over '%s'"), src->buf);
	}
}

static void clone_local(const char *src_repo, const char *dest_repo)
{
	if (option_shared) {
		struct strbuf alt = STRBUF_INIT;
		get_common_dir(&alt, src_repo);
		strbuf_addstr(&alt, "/objects");
		add_to_alternates_file(alt.buf);
		strbuf_release(&alt);
	} else {
		struct strbuf src = STRBUF_INIT;
		struct strbuf dest = STRBUF_INIT;
		get_common_dir(&src, src_repo);
		get_common_dir(&dest, dest_repo);
		strbuf_addstr(&src, "/objects");
		strbuf_addstr(&dest, "/objects");
		copy_or_link_directory(&src, &dest, src_repo);
		strbuf_release(&src);
		strbuf_release(&dest);
	}

	if (0 <= option_verbosity)
		fprintf(stderr, _("done.\n"));
}

static const char *junk_work_tree;
static int junk_work_tree_flags;
static const char *junk_git_dir;
static int junk_git_dir_flags;
static enum {
	JUNK_LEAVE_NONE,
	JUNK_LEAVE_REPO,
	JUNK_LEAVE_ALL
} junk_mode = JUNK_LEAVE_NONE;

static const char junk_leave_repo_msg[] =
N_("Clone succeeded, but checkout failed.\n"
   "You can inspect what was checked out with 'git status'\n"
   "and retry with 'git restore --source=HEAD :/'\n");

static void remove_junk(void)
{
	struct strbuf sb = STRBUF_INIT;

	switch (junk_mode) {
	case JUNK_LEAVE_REPO:
		warning("%s", _(junk_leave_repo_msg));
		/* fall-through */
	case JUNK_LEAVE_ALL:
		return;
	default:
		/* proceed to removal */
		break;
	}

	if (junk_git_dir) {
		strbuf_addstr(&sb, junk_git_dir);
		remove_dir_recursively(&sb, junk_git_dir_flags);
		strbuf_reset(&sb);
	}
	if (junk_work_tree) {
		strbuf_addstr(&sb, junk_work_tree);
		remove_dir_recursively(&sb, junk_work_tree_flags);
	}
	strbuf_release(&sb);
}

static void remove_junk_on_signal(int signo)
{
	remove_junk();
	sigchain_pop(signo);
	raise(signo);
}

static struct ref *find_remote_branch(const struct ref *refs, const char *branch)
{
	struct ref *ref;
	struct strbuf head = STRBUF_INIT;
	strbuf_addstr(&head, "refs/heads/");
	strbuf_addstr(&head, branch);
	ref = find_ref_by_name(refs, head.buf);
	strbuf_release(&head);

	if (ref)
		return ref;

	strbuf_addstr(&head, "refs/tags/");
	strbuf_addstr(&head, branch);
	ref = find_ref_by_name(refs, head.buf);
	strbuf_release(&head);

	return ref;
}

static struct ref *wanted_peer_refs(const struct ref *refs,
		struct refspec *refspec)
{
	struct ref *head = copy_ref(find_ref_by_name(refs, "HEAD"));
	struct ref *local_refs = head;
	struct ref **tail = head ? &head->next : &local_refs;
	struct refspec_item tag_refspec;

	refspec_item_init(&tag_refspec, TAG_REFSPEC, 0);

	if (option_single_branch) {
		struct ref *remote_head = NULL;

		if (!option_branch)
			remote_head = guess_remote_head(head, refs, 0);
		else {
			free_one_ref(head);
			local_refs = head = NULL;
			tail = &local_refs;
			remote_head = copy_ref(find_remote_branch(refs, option_branch));
		}

		if (!remote_head && option_branch)
			warning(_("Could not find remote branch %s to clone."),
				option_branch);
		else {
			int i;
			for (i = 0; i < refspec->nr; i++)
				get_fetch_map(remote_head, &refspec->items[i],
					      &tail, 0);

			/* if --branch=tag, pull the requested tag explicitly */
			get_fetch_map(remote_head, &tag_refspec, &tail, 0);
		}
		free_refs(remote_head);
	} else {
		int i;
		for (i = 0; i < refspec->nr; i++)
			get_fetch_map(refs, &refspec->items[i], &tail, 0);
	}

	if (!option_mirror && !option_single_branch && !option_no_tags)
		get_fetch_map(refs, &tag_refspec, &tail, 0);

	refspec_item_clear(&tag_refspec);
	return local_refs;
}

static void write_remote_refs(const struct ref *local_refs)
{
	const struct ref *r;

	struct ref_transaction *t;
	struct strbuf err = STRBUF_INIT;

	t = ref_store_transaction_begin(get_main_ref_store(the_repository),
					REF_TRANSACTION_FLAG_INITIAL, &err);
	if (!t)
		die("%s", err.buf);

	for (r = local_refs; r; r = r->next) {
		if (!r->peer_ref)
			continue;
		if (ref_transaction_create(t, r->peer_ref->name, &r->old_oid,
					   NULL, 0, NULL, &err))
			die("%s", err.buf);
	}

	if (ref_transaction_commit(t, &err))
		die("%s", err.buf);

	strbuf_release(&err);
	ref_transaction_free(t);
}

static void write_followtags(const struct ref *refs, const char *msg)
{
	const struct ref *ref;
	for (ref = refs; ref; ref = ref->next) {
		if (!starts_with(ref->name, "refs/tags/"))
			continue;
		if (ends_with(ref->name, "^{}"))
			continue;
		if (!repo_has_object_file_with_flags(the_repository, &ref->old_oid,
						     OBJECT_INFO_QUICK |
						     OBJECT_INFO_SKIP_FETCH_OBJECT))
			continue;
		refs_update_ref(get_main_ref_store(the_repository), msg,
				ref->name, &ref->old_oid, NULL, 0,
				UPDATE_REFS_DIE_ON_ERR);
	}
}

static const struct object_id *iterate_ref_map(void *cb_data)
{
	struct ref **rm = cb_data;
	struct ref *ref = *rm;

	/*
	 * Skip anything missing a peer_ref, which we are not
	 * actually going to write a ref for.
	 */
	while (ref && !ref->peer_ref)
		ref = ref->next;
	if (!ref)
		return NULL;

	*rm = ref->next;
	return &ref->old_oid;
}

static void update_remote_refs(const struct ref *refs,
			       const struct ref *mapped_refs,
			       const struct ref *remote_head_points_at,
			       const char *branch_top,
			       const char *msg,
			       struct transport *transport,
			       int check_connectivity)
{
	const struct ref *rm = mapped_refs;

	if (check_connectivity) {
		struct check_connected_options opt = CHECK_CONNECTED_INIT;

		opt.transport = transport;
		opt.progress = transport->progress;

		if (check_connected(iterate_ref_map, &rm, &opt))
			die(_("remote did not send all necessary objects"));
	}

	if (refs) {
		write_remote_refs(mapped_refs);
		if (option_single_branch && !option_no_tags)
			write_followtags(refs, msg);
	}

	if (remote_head_points_at && !option_bare) {
		struct strbuf head_ref = STRBUF_INIT;
		strbuf_addstr(&head_ref, branch_top);
		strbuf_addstr(&head_ref, "HEAD");
		if (refs_update_symref(get_main_ref_store(the_repository), head_ref.buf,
				       remote_head_points_at->peer_ref->name,
				       msg) < 0)
			die(_("unable to update %s"), head_ref.buf);
		strbuf_release(&head_ref);
	}
}

static void update_head(const struct ref *our, const struct ref *remote,
			const char *unborn, const char *msg)
{
	const char *head;
	if (our && skip_prefix(our->name, "refs/heads/", &head)) {
		/* Local default branch link */
		if (refs_update_symref(get_main_ref_store(the_repository), "HEAD", our->name, NULL) < 0)
			die(_("unable to update HEAD"));
		if (!option_bare) {
			refs_update_ref(get_main_ref_store(the_repository),
					msg, "HEAD", &our->old_oid, NULL, 0,
					UPDATE_REFS_DIE_ON_ERR);
			install_branch_config(0, head, remote_name, our->name);
		}
	} else if (our) {
		struct commit *c = lookup_commit_reference(the_repository,
							   &our->old_oid);
		/* --branch specifies a non-branch (i.e. tags), detach HEAD */
		refs_update_ref(get_main_ref_store(the_repository), msg,
				"HEAD", &c->object.oid, NULL, REF_NO_DEREF,
				UPDATE_REFS_DIE_ON_ERR);
	} else if (remote) {
		/*
		 * We know remote HEAD points to a non-branch, or
		 * HEAD points to a branch but we don't know which one.
		 * Detach HEAD in all these cases.
		 */
		refs_update_ref(get_main_ref_store(the_repository), msg,
				"HEAD", &remote->old_oid, NULL, REF_NO_DEREF,
				UPDATE_REFS_DIE_ON_ERR);
	} else if (unborn && skip_prefix(unborn, "refs/heads/", &head)) {
		/*
		 * Unborn head from remote; same as "our" case above except
		 * that we have no ref to update.
		 */
		if (refs_update_symref(get_main_ref_store(the_repository), "HEAD", unborn, NULL) < 0)
			die(_("unable to update HEAD"));
		if (!option_bare)
			install_branch_config(0, head, remote_name, unborn);
	}
}

static int git_sparse_checkout_init(const char *repo)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int result = 0;
	strvec_pushl(&cmd.args, "-C", repo, "sparse-checkout", "set", NULL);

	/*
	 * We must apply the setting in the current process
	 * for the later checkout to use the sparse-checkout file.
	 */
	core_apply_sparse_checkout = 1;

	cmd.git_cmd = 1;
	if (run_command(&cmd)) {
		error(_("failed to initialize sparse-checkout"));
		result = 1;
	}

	return result;
}

static int checkout(int submodule_progress, int filter_submodules,
		    enum ref_storage_format ref_storage_format)
{
	struct object_id oid;
	char *head;
	struct lock_file lock_file = LOCK_INIT;
	struct unpack_trees_options opts;
	struct tree *tree;
	struct tree_desc t;
	int err = 0;

	if (option_no_checkout)
		return 0;

	head = refs_resolve_refdup(get_main_ref_store(the_repository), "HEAD",
				   RESOLVE_REF_READING, &oid, NULL);
	if (!head) {
		warning(_("remote HEAD refers to nonexistent ref, "
			  "unable to checkout"));
		return 0;
	}
	if (!strcmp(head, "HEAD")) {
		if (advice_enabled(ADVICE_DETACHED_HEAD))
			detach_advice(oid_to_hex(&oid));
		FREE_AND_NULL(head);
	} else {
		if (!starts_with(head, "refs/heads/"))
			die(_("HEAD not found below refs/heads!"));
	}

	/* We need to be in the new work tree for the checkout */
	setup_work_tree();

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);

	memset(&opts, 0, sizeof opts);
	opts.update = 1;
	opts.merge = 1;
	opts.clone = 1;
	opts.preserve_ignored = 0;
	opts.fn = oneway_merge;
	opts.verbose_update = (option_verbosity >= 0);
	opts.src_index = the_repository->index;
	opts.dst_index = the_repository->index;
	init_checkout_metadata(&opts.meta, head, &oid, NULL);

	tree = parse_tree_indirect(&oid);
	if (!tree)
		die(_("unable to parse commit %s"), oid_to_hex(&oid));
	if (parse_tree(tree) < 0)
		exit(128);
	init_tree_desc(&t, &tree->object.oid, tree->buffer, tree->size);
	if (unpack_trees(1, &t, &opts) < 0)
		die(_("unable to checkout working tree"));

	free(head);

	if (write_locked_index(the_repository->index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	err |= run_hooks_l(the_repository, "post-checkout", oid_to_hex(null_oid()),
			   oid_to_hex(&oid), "1", NULL);

	if (!err && (option_recurse_submodules.nr > 0)) {
		struct child_process cmd = CHILD_PROCESS_INIT;
		strvec_pushl(&cmd.args, "submodule", "update", "--require-init",
			     "--recursive", NULL);

		if (option_shallow_submodules == 1)
			strvec_push(&cmd.args, "--depth=1");

		if (max_jobs != -1)
			strvec_pushf(&cmd.args, "--jobs=%d", max_jobs);

		if (submodule_progress)
			strvec_push(&cmd.args, "--progress");

		if (option_verbosity < 0)
			strvec_push(&cmd.args, "--quiet");

		if (option_remote_submodules) {
			strvec_push(&cmd.args, "--remote");
			strvec_push(&cmd.args, "--no-fetch");
		}

		if (ref_storage_format != REF_STORAGE_FORMAT_UNKNOWN)
			strvec_pushf(&cmd.args, "--ref-format=%s",
				     ref_storage_format_to_name(ref_storage_format));

		if (filter_submodules && filter_options.choice)
			strvec_pushf(&cmd.args, "--filter=%s",
				     expand_list_objects_filter_spec(&filter_options));

		if (option_single_branch >= 0)
			strvec_push(&cmd.args, option_single_branch ?
					       "--single-branch" :
					       "--no-single-branch");

		cmd.git_cmd = 1;
		err = run_command(&cmd);
	}

	return err;
}

static int git_clone_config(const char *k, const char *v,
			    const struct config_context *ctx, void *cb)
{
	if (!strcmp(k, "clone.defaultremotename")) {
		if (!v)
			return config_error_nonbool(k);
		free(remote_name);
		remote_name = xstrdup(v);
	}
	if (!strcmp(k, "clone.rejectshallow"))
		config_reject_shallow = git_config_bool(k, v);
	if (!strcmp(k, "clone.filtersubmodules"))
		config_filter_submodules = git_config_bool(k, v);

	return git_default_config(k, v, ctx, cb);
}

static int write_one_config(const char *key, const char *value,
			    const struct config_context *ctx,
			    void *data)
{
	/*
	 * give git_clone_config a chance to write config values back to the
	 * environment, since git_config_set_multivar_gently only deals with
	 * config-file writes
	 */
	int apply_failed = git_clone_config(key, value, ctx, data);
	if (apply_failed)
		return apply_failed;

	return git_config_set_multivar_gently(key,
					      value ? value : "true",
					      CONFIG_REGEX_NONE, 0);
}

static void write_config(struct string_list *config)
{
	int i;

	for (i = 0; i < config->nr; i++) {
		if (git_config_parse_parameter(config->items[i].string,
					       write_one_config, NULL) < 0)
			die(_("unable to write parameters to config file"));
	}
}

static void write_refspec_config(const char *src_ref_prefix,
		const struct ref *our_head_points_at,
		const struct ref *remote_head_points_at,
		struct strbuf *branch_top)
{
	struct strbuf key = STRBUF_INIT;
	struct strbuf value = STRBUF_INIT;

	if (option_mirror || !option_bare) {
		if (option_single_branch && !option_mirror) {
			if (option_branch) {
				if (starts_with(our_head_points_at->name, "refs/tags/"))
					strbuf_addf(&value, "+%s:%s", our_head_points_at->name,
						our_head_points_at->name);
				else
					strbuf_addf(&value, "+%s:%s%s", our_head_points_at->name,
						branch_top->buf, option_branch);
			} else if (remote_head_points_at) {
				const char *head = remote_head_points_at->name;
				if (!skip_prefix(head, "refs/heads/", &head))
					BUG("remote HEAD points at non-head?");

				strbuf_addf(&value, "+%s:%s%s", remote_head_points_at->name,
						branch_top->buf, head);
			}
			/*
			 * otherwise, the next "git fetch" will
			 * simply fetch from HEAD without updating
			 * any remote-tracking branch, which is what
			 * we want.
			 */
		} else {
			strbuf_addf(&value, "+%s*:%s*", src_ref_prefix, branch_top->buf);
		}
		/* Configure the remote */
		if (value.len) {
			strbuf_addf(&key, "remote.%s.fetch", remote_name);
			git_config_set_multivar(key.buf, value.buf, "^$", 0);
			strbuf_reset(&key);

			if (option_mirror) {
				strbuf_addf(&key, "remote.%s.mirror", remote_name);
				git_config_set(key.buf, "true");
				strbuf_reset(&key);
			}
		}
	}

	strbuf_release(&key);
	strbuf_release(&value);
}

static void dissociate_from_references(void)
{
	char *alternates = git_pathdup("objects/info/alternates");

	if (!access(alternates, F_OK)) {
		struct child_process cmd = CHILD_PROCESS_INIT;

		cmd.git_cmd = 1;
		cmd.no_stdin = 1;
		strvec_pushl(&cmd.args, "repack", "-a", "-d", NULL);
		if (run_command(&cmd))
			die(_("cannot repack to clean up"));
		if (unlink(alternates) && errno != ENOENT)
			die_errno(_("cannot unlink temporary alternates file"));
	}
	free(alternates);
}

static int path_exists(const char *path)
{
	struct stat sb;
	return !stat(path, &sb);
}

int cmd_clone(int argc,
	      const char **argv,
	      const char *prefix,
	      struct repository *repository UNUSED)
{
	int is_bundle = 0, is_local;
	int reject_shallow = 0;
	const char *repo_name, *repo, *work_tree, *git_dir;
	char *repo_to_free = NULL;
	char *path = NULL, *dir, *display_repo = NULL;
	int dest_exists, real_dest_exists = 0;
	const struct ref *refs, *remote_head;
	struct ref *remote_head_points_at = NULL;
	const struct ref *our_head_points_at;
	char *unborn_head = NULL;
	struct ref *mapped_refs = NULL;
	const struct ref *ref;
	struct strbuf key = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf branch_top = STRBUF_INIT, reflog_msg = STRBUF_INIT;
	struct transport *transport = NULL;
	const char *src_ref_prefix = "refs/heads/";
	struct remote *remote;
	int err = 0, complete_refs_before_fetch = 1;
	int submodule_progress;
	int filter_submodules = 0;
	int hash_algo;
	enum ref_storage_format ref_storage_format = REF_STORAGE_FORMAT_UNKNOWN;
	const int do_not_override_repo_unix_permissions = -1;

	struct transport_ls_refs_options transport_ls_refs_options =
		TRANSPORT_LS_REFS_OPTIONS_INIT;

	packet_trace_identity("clone");

	git_config(git_clone_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_clone_options,
			     builtin_clone_usage, 0);

	if (argc > 2)
		usage_msg_opt(_("Too many arguments."),
			builtin_clone_usage, builtin_clone_options);

	if (argc == 0)
		usage_msg_opt(_("You must specify a repository to clone."),
			builtin_clone_usage, builtin_clone_options);

	if (option_depth || option_since || option_not.nr)
		deepen = 1;
	if (option_single_branch == -1)
		option_single_branch = deepen ? 1 : 0;

	if (ref_format) {
		ref_storage_format = ref_storage_format_by_name(ref_format);
		if (ref_storage_format == REF_STORAGE_FORMAT_UNKNOWN)
			die(_("unknown ref storage format '%s'"), ref_format);
	}

	if (option_mirror)
		option_bare = 1;

	if (option_bare) {
		if (real_git_dir)
			die(_("options '%s' and '%s' cannot be used together"), "--bare", "--separate-git-dir");
		option_no_checkout = 1;
	}

	if (bundle_uri && deepen)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--bundle-uri",
		    "--depth/--shallow-since/--shallow-exclude");

	repo_name = argv[0];

	path = get_repo_path(repo_name, &is_bundle);
	if (path) {
		FREE_AND_NULL(path);
		repo = repo_to_free = absolute_pathdup(repo_name);
	} else if (strchr(repo_name, ':')) {
		repo = repo_name;
		display_repo = transport_anonymize_url(repo);
	} else
		die(_("repository '%s' does not exist"), repo_name);

	/* no need to be strict, transport_set_option() will validate it again */
	if (option_depth && atoi(option_depth) < 1)
		die(_("depth %s is not a positive number"), option_depth);

	if (argc == 2)
		dir = xstrdup(argv[1]);
	else
		dir = git_url_basename(repo_name, is_bundle, option_bare);
	strip_dir_trailing_slashes(dir);

	dest_exists = path_exists(dir);
	if (dest_exists && !is_empty_dir(dir))
		die(_("destination path '%s' already exists and is not "
			"an empty directory."), dir);

	if (real_git_dir) {
		real_dest_exists = path_exists(real_git_dir);
		if (real_dest_exists && !is_empty_dir(real_git_dir))
			die(_("repository path '%s' already exists and is not "
				"an empty directory."), real_git_dir);
	}


	strbuf_addf(&reflog_msg, "clone: from %s",
		    display_repo ? display_repo : repo);
	free(display_repo);

	if (option_bare)
		work_tree = NULL;
	else {
		work_tree = getenv("GIT_WORK_TREE");
		if (work_tree && path_exists(work_tree))
			die(_("working tree '%s' already exists."), work_tree);
	}

	if (option_bare || work_tree)
		git_dir = xstrdup(dir);
	else {
		work_tree = dir;
		git_dir = mkpathdup("%s/.git", dir);
	}

	atexit(remove_junk);
	sigchain_push_common(remove_junk_on_signal);

	if (!option_bare) {
		if (safe_create_leading_directories_const(work_tree) < 0)
			die_errno(_("could not create leading directories of '%s'"),
				  work_tree);
		if (dest_exists)
			junk_work_tree_flags |= REMOVE_DIR_KEEP_TOPLEVEL;
		else if (mkdir(work_tree, 0777))
			die_errno(_("could not create work tree dir '%s'"),
				  work_tree);
		junk_work_tree = work_tree;
		set_git_work_tree(work_tree);
	}

	if (real_git_dir) {
		if (real_dest_exists)
			junk_git_dir_flags |= REMOVE_DIR_KEEP_TOPLEVEL;
		junk_git_dir = real_git_dir;
	} else {
		if (dest_exists)
			junk_git_dir_flags |= REMOVE_DIR_KEEP_TOPLEVEL;
		junk_git_dir = git_dir;
	}
	if (safe_create_leading_directories_const(git_dir) < 0)
		die(_("could not create leading directories of '%s'"), git_dir);

	if (0 <= option_verbosity) {
		if (option_bare)
			fprintf(stderr, _("Cloning into bare repository '%s'...\n"), dir);
		else
			fprintf(stderr, _("Cloning into '%s'...\n"), dir);
	}

	if (option_recurse_submodules.nr > 0) {
		struct string_list_item *item;
		struct strbuf sb = STRBUF_INIT;
		int val;

		/* remove duplicates */
		string_list_sort(&option_recurse_submodules);
		string_list_remove_duplicates(&option_recurse_submodules, 0);

		/*
		 * NEEDSWORK: In a multi-working-tree world, this needs to be
		 * set in the per-worktree config.
		 */
		for_each_string_list_item(item, &option_recurse_submodules) {
			strbuf_addf(&sb, "submodule.active=%s",
				    item->string);
			string_list_append(&option_config,
					   strbuf_detach(&sb, NULL));
		}

		if (!git_config_get_bool("submodule.stickyRecursiveClone", &val) &&
		    val)
			string_list_append(&option_config, "submodule.recurse=true");

		if (option_required_reference.nr &&
		    option_optional_reference.nr)
			die(_("clone --recursive is not compatible with "
			      "both --reference and --reference-if-able"));
		else if (option_required_reference.nr) {
			string_list_append(&option_config,
				"submodule.alternateLocation=superproject");
			string_list_append(&option_config,
				"submodule.alternateErrorStrategy=die");
		} else if (option_optional_reference.nr) {
			string_list_append(&option_config,
				"submodule.alternateLocation=superproject");
			string_list_append(&option_config,
				"submodule.alternateErrorStrategy=info");
		}
	}

	/*
	 * Initialize the repository, but skip initializing the reference
	 * database. We do not yet know about the object format of the
	 * repository, and reference backends may persist that information into
	 * their on-disk data structures.
	 */
	init_db(git_dir, real_git_dir, option_template, GIT_HASH_UNKNOWN,
		ref_storage_format, NULL,
		do_not_override_repo_unix_permissions, INIT_DB_QUIET | INIT_DB_SKIP_REFDB);

	if (real_git_dir) {
		free((char *)git_dir);
		git_dir = real_git_dir;
	}

	/*
	 * We have a chicken-and-egg situation between initializing the refdb
	 * and spawning transport helpers:
	 *
	 *   - Initializing the refdb requires us to know about the object
	 *     format. We thus have to spawn the transport helper to learn
	 *     about it.
	 *
	 *   - The transport helper may want to access the Git repository. But
	 *     because the refdb has not been initialized, we don't have "HEAD"
	 *     or "refs/". Thus, the helper cannot find the Git repository.
	 *
	 * Ideally, we would have structured the helper protocol such that it's
	 * mandatory for the helper to first announce its capabilities without
	 * yet assuming a fully initialized repository. Like that, we could
	 * have added a "lazy-refdb-init" capability that announces whether the
	 * helper is ready to handle not-yet-initialized refdbs. If any helper
	 * didn't support them, we would have fully initialized the refdb with
	 * the SHA1 object format, but later on bailed out if we found out that
	 * the remote repository used a different object format.
	 *
	 * But we didn't, and thus we use the following workaround to partially
	 * initialize the repository's refdb such that it can be discovered by
	 * Git commands. To do so, we:
	 *
	 *   - Create an invalid HEAD ref pointing at "refs/heads/.invalid".
	 *
	 *   - Create the "refs/" directory.
	 *
	 *   - Set up the ref storage format and repository version as
	 *     required.
	 *
	 * This is sufficient for Git commands to discover the Git directory.
	 */
	initialize_repository_version(GIT_HASH_UNKNOWN,
				      the_repository->ref_storage_format, 1);

	strbuf_addf(&buf, "%s/HEAD", git_dir);
	write_file(buf.buf, "ref: refs/heads/.invalid");

	strbuf_reset(&buf);
	strbuf_addf(&buf, "%s/refs", git_dir);
	safe_create_dir(buf.buf, 1);

	/*
	 * additional config can be injected with -c, make sure it's included
	 * after init_db, which clears the entire config environment.
	 */
	write_config(&option_config);

	/*
	 * re-read config after init_db and write_config to pick up any config
	 * injected by --template and --config, respectively.
	 */
	git_config(git_clone_config, NULL);

	/*
	 * If option_reject_shallow is specified from CLI option,
	 * ignore config_reject_shallow from git_clone_config.
	 */
	if (config_reject_shallow != -1)
		reject_shallow = config_reject_shallow;
	if (option_reject_shallow != -1)
		reject_shallow = option_reject_shallow;

	/*
	 * If option_filter_submodules is specified from CLI option,
	 * ignore config_filter_submodules from git_clone_config.
	 */
	if (config_filter_submodules != -1)
		filter_submodules = config_filter_submodules;
	if (option_filter_submodules != -1)
		filter_submodules = option_filter_submodules;

	/*
	 * Exit if the user seems to be doing something silly with submodule
	 * filter flags (but not with filter configs, as those should be
	 * set-and-forget).
	 */
	if (option_filter_submodules > 0 && !filter_options.choice)
		die(_("the option '%s' requires '%s'"),
		    "--also-filter-submodules", "--filter");
	if (option_filter_submodules > 0 && !option_recurse_submodules.nr)
		die(_("the option '%s' requires '%s'"),
		    "--also-filter-submodules", "--recurse-submodules");

	/*
	 * apply the remote name provided by --origin only after this second
	 * call to git_config, to ensure it overrides all config-based values.
	 */
	if (option_origin) {
		free(remote_name);
		remote_name = xstrdup(option_origin);
	}

	if (!remote_name)
		remote_name = xstrdup("origin");

	if (!valid_remote_name(remote_name))
		die(_("'%s' is not a valid remote name"), remote_name);

	if (option_bare) {
		if (option_mirror)
			src_ref_prefix = "refs/";
		strbuf_addstr(&branch_top, src_ref_prefix);

		git_config_set("core.bare", "true");
	} else {
		strbuf_addf(&branch_top, "refs/remotes/%s/", remote_name);
	}

	strbuf_addf(&key, "remote.%s.url", remote_name);
	git_config_set(key.buf, repo);
	strbuf_reset(&key);

	if (option_no_tags) {
		strbuf_addf(&key, "remote.%s.tagOpt", remote_name);
		git_config_set(key.buf, "--no-tags");
		strbuf_reset(&key);
	}

	if (option_required_reference.nr || option_optional_reference.nr)
		setup_reference();

	remote = remote_get_early(remote_name);

	refspec_appendf(&remote->fetch, "+%s*:%s*", src_ref_prefix,
			branch_top.buf);

	path = get_repo_path(remote->url.v[0], &is_bundle);
	is_local = option_local != 0 && path && !is_bundle;
	if (is_local) {
		if (option_depth)
			warning(_("--depth is ignored in local clones; use file:// instead."));
		if (option_since)
			warning(_("--shallow-since is ignored in local clones; use file:// instead."));
		if (option_not.nr)
			warning(_("--shallow-exclude is ignored in local clones; use file:// instead."));
		if (filter_options.choice)
			warning(_("--filter is ignored in local clones; use file:// instead."));
		if (!access(mkpath("%s/shallow", path), F_OK)) {
			if (reject_shallow)
				die(_("source repository is shallow, reject to clone."));
			if (option_local > 0)
				warning(_("source repository is shallow, ignoring --local"));
			is_local = 0;
		}
	}
	if (option_local > 0 && !is_local)
		warning(_("--local is ignored"));

	transport = transport_get(remote, path ? path : remote->url.v[0]);
	transport_set_verbosity(transport, option_verbosity, option_progress);
	transport->family = family;
	transport->cloning = 1;

	if (is_bundle) {
		struct bundle_header header = BUNDLE_HEADER_INIT;
		int fd = read_bundle_header(path, &header);
		int has_filter = header.filter.choice != LOFC_DISABLED;

		if (fd > 0)
			close(fd);
		bundle_header_release(&header);
		if (has_filter)
			die(_("cannot clone from filtered bundle"));
	}

	transport_set_option(transport, TRANS_OPT_KEEP, "yes");

	if (reject_shallow)
		transport_set_option(transport, TRANS_OPT_REJECT_SHALLOW, "1");
	if (option_depth)
		transport_set_option(transport, TRANS_OPT_DEPTH,
				     option_depth);
	if (option_since)
		transport_set_option(transport, TRANS_OPT_DEEPEN_SINCE,
				     option_since);
	if (option_not.nr)
		transport_set_option(transport, TRANS_OPT_DEEPEN_NOT,
				     (const char *)&option_not);
	if (option_single_branch)
		transport_set_option(transport, TRANS_OPT_FOLLOWTAGS, "1");

	if (option_upload_pack)
		transport_set_option(transport, TRANS_OPT_UPLOADPACK,
				     option_upload_pack);

	if (server_options.nr)
		transport->server_options = &server_options;

	if (filter_options.choice) {
		const char *spec =
			expand_list_objects_filter_spec(&filter_options);
		transport_set_option(transport, TRANS_OPT_LIST_OBJECTS_FILTER,
				     spec);
		transport_set_option(transport, TRANS_OPT_FROM_PROMISOR, "1");
	}

	if (transport->smart_options && !deepen && !filter_options.choice)
		transport->smart_options->check_self_contained_and_connected = 1;

	strvec_push(&transport_ls_refs_options.ref_prefixes, "HEAD");
	refspec_ref_prefixes(&remote->fetch,
			     &transport_ls_refs_options.ref_prefixes);
	if (option_branch)
		expand_ref_prefix(&transport_ls_refs_options.ref_prefixes,
				  option_branch);
	if (!option_no_tags)
		strvec_push(&transport_ls_refs_options.ref_prefixes,
			    "refs/tags/");

	refs = transport_get_remote_refs(transport, &transport_ls_refs_options);

	/*
	 * Now that we know what algorithm the remote side is using, let's set
	 * ours to the same thing.
	 */
	hash_algo = hash_algo_by_ptr(transport_get_hash_algo(transport));
	initialize_repository_version(hash_algo, the_repository->ref_storage_format, 1);
	repo_set_hash_algo(the_repository, hash_algo);
	create_reference_database(the_repository->ref_storage_format, NULL, 1);

	/*
	 * Before fetching from the remote, download and install bundle
	 * data from the --bundle-uri option.
	 */
	if (bundle_uri) {
		struct remote_state *state;
		int has_heuristic = 0;

		/*
		 * We need to save the remote state as our remote's lifetime is
		 * tied to it.
		 */
		state = the_repository->remote_state;
		the_repository->remote_state = NULL;
		repo_clear(the_repository);

		/* At this point, we need the_repository to match the cloned repo. */
		if (repo_init(the_repository, git_dir, work_tree, -1))
			warning(_("failed to initialize the repo, skipping bundle URI"));
		else if (fetch_bundle_uri(the_repository, bundle_uri, &has_heuristic))
			warning(_("failed to fetch objects from bundle URI '%s'"),
				bundle_uri);
		else if (has_heuristic)
			git_config_set_gently("fetch.bundleuri", bundle_uri);

		remote_state_clear(the_repository->remote_state);
		free(the_repository->remote_state);
		the_repository->remote_state = state;
	} else {
		/*
		* Populate transport->got_remote_bundle_uri and
		* transport->bundle_uri. We might get nothing.
		*/
		transport_get_remote_bundle_uri(transport);

		if (transport->bundles &&
		    hashmap_get_size(&transport->bundles->bundles)) {
			struct remote_state *state;

			/*
			 * We need to save the remote state as our remote's
			 * lifetime is tied to it.
			 */
			state = the_repository->remote_state;
			the_repository->remote_state = NULL;
			repo_clear(the_repository);

			/* At this point, we need the_repository to match the cloned repo. */
			if (repo_init(the_repository, git_dir, work_tree, -1))
				warning(_("failed to initialize the repo, skipping bundle URI"));
			else if (fetch_bundle_list(the_repository,
						   transport->bundles))
				warning(_("failed to fetch advertised bundles"));

			remote_state_clear(the_repository->remote_state);
			free(the_repository->remote_state);
			the_repository->remote_state = state;
		} else {
			clear_bundle_list(transport->bundles);
			FREE_AND_NULL(transport->bundles);
		}
	}

	if (refs)
		mapped_refs = wanted_peer_refs(refs, &remote->fetch);

	if (mapped_refs) {
		/*
		 * transport_get_remote_refs() may return refs with null sha-1
		 * in mapped_refs (see struct transport->get_refs_list
		 * comment). In that case we need fetch it early because
		 * remote_head code below relies on it.
		 *
		 * for normal clones, transport_get_remote_refs() should
		 * return reliable ref set, we can delay cloning until after
		 * remote HEAD check.
		 */
		for (ref = refs; ref; ref = ref->next)
			if (is_null_oid(&ref->old_oid)) {
				complete_refs_before_fetch = 0;
				break;
			}

		if (!is_local && !complete_refs_before_fetch) {
			if (transport_fetch_refs(transport, mapped_refs))
				die(_("remote transport reported error"));
		}
	}

	remote_head = find_ref_by_name(refs, "HEAD");
	remote_head_points_at = guess_remote_head(remote_head, mapped_refs, 0);

	if (option_branch) {
		our_head_points_at = find_remote_branch(mapped_refs, option_branch);
		if (!our_head_points_at)
			die(_("Remote branch %s not found in upstream %s"),
			    option_branch, remote_name);
	} else if (remote_head_points_at) {
		our_head_points_at = remote_head_points_at;
	} else if (remote_head) {
		our_head_points_at = NULL;
	} else {
		char *to_free = NULL;
		const char *branch;

		if (!mapped_refs) {
			warning(_("You appear to have cloned an empty repository."));
			option_no_checkout = 1;
		}

		if (transport_ls_refs_options.unborn_head_target &&
		    skip_prefix(transport_ls_refs_options.unborn_head_target,
				"refs/heads/", &branch)) {
			unborn_head  = xstrdup(transport_ls_refs_options.unborn_head_target);
		} else {
			branch = to_free = repo_default_branch_name(the_repository, 0);
			unborn_head = xstrfmt("refs/heads/%s", branch);
		}

		/*
		 * We may have selected a local default branch name "foo",
		 * and even though the remote's HEAD does not point there,
		 * it may still have a "foo" branch. If so, set it up so
		 * that we can follow the usual checkout code later.
		 *
		 * Note that for an empty repo we'll already have set
		 * option_no_checkout above, which would work against us here.
		 * But for an empty repo, find_remote_branch() can never find
		 * a match.
		 */
		our_head_points_at = find_remote_branch(mapped_refs, branch);

		free(to_free);
	}

	write_refspec_config(src_ref_prefix, our_head_points_at,
			remote_head_points_at, &branch_top);

	if (filter_options.choice)
		partial_clone_register(remote_name, &filter_options);

	if (is_local)
		clone_local(path, git_dir);
	else if (mapped_refs && complete_refs_before_fetch) {
		if (transport_fetch_refs(transport, mapped_refs))
			die(_("remote transport reported error"));
	}

	update_remote_refs(refs, mapped_refs, remote_head_points_at,
			   branch_top.buf, reflog_msg.buf, transport,
			   !is_local);

	update_head(our_head_points_at, remote_head, unborn_head, reflog_msg.buf);

	/*
	 * We want to show progress for recursive submodule clones iff
	 * we did so for the main clone. But only the transport knows
	 * the final decision for this flag, so we need to rescue the value
	 * before we free the transport.
	 */
	submodule_progress = transport->progress;

	transport_unlock_pack(transport, 0);
	transport_disconnect(transport);

	if (option_dissociate) {
		close_object_store(the_repository->objects);
		dissociate_from_references();
	}

	if (option_sparse_checkout && git_sparse_checkout_init(dir))
		return 1;

	junk_mode = JUNK_LEAVE_REPO;
	err = checkout(submodule_progress, filter_submodules,
		       ref_storage_format);

	free(remote_name);
	strbuf_release(&reflog_msg);
	strbuf_release(&branch_top);
	strbuf_release(&buf);
	strbuf_release(&key);
	free_refs(mapped_refs);
	free_refs(remote_head_points_at);
	free(unborn_head);
	free(dir);
	free(path);
	free(repo_to_free);
	junk_mode = JUNK_LEAVE_ALL;

	transport_ls_refs_options_release(&transport_ls_refs_options);
	return err;
}
