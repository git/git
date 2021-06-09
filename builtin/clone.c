/*
 * Builtin "git clone"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>,
 *		 2008 Daniel Barkalow <barkalow@iabervon.org>
 * Based on git-commit.sh by Junio C Hamano and Linus Torvalds
 *
 * Clone a repository into a different directory that does not yet exist.
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "builtin.h"
#include "config.h"
#include "lockfile.h"
#include "parse-options.h"
#include "fetch-pack.h"
#include "refs.h"
#include "refspec.h"
#include "object-store.h"
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
#include "connected.h"
#include "packfile.h"
#include "list-objects-filter-options.h"

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
static char *option_upload_pack = "git-upload-pack";
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
static struct list_objects_filter_options filter_options;
static struct string_list server_options = STRING_LIST_INIT_NODUP;
static int option_remote_submodules;

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
		 N_("create a mirror repository (implies bare)")),
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
	OPT_STRING_LIST(0, "shallow-exclude", &option_not, N_("revision"),
			N_("deepen history of shallow clone, excluding rev")),
	OPT_BOOL(0, "single-branch", &option_single_branch,
		    N_("clone only one branch, HEAD or --branch")),
	OPT_BOOL(0, "no-tags", &option_no_tags,
		 N_("don't clone any tags, and make later fetches not to follow them")),
	OPT_BOOL(0, "shallow-submodules", &option_shallow_submodules,
		    N_("any cloned submodules will be shallow")),
	OPT_STRING(0, "separate-git-dir", &real_git_dir, N_("gitdir"),
		   N_("separate git dir from working tree")),
	OPT_STRING_LIST('c', "config", &option_config, N_("key=value"),
			N_("set config inside the new repository")),
	OPT_STRING_LIST(0, "server-option", &server_options,
			N_("server-specific"), N_("option to transmit")),
	OPT_SET_INT('4', "ipv4", &family, N_("use IPv4 addresses only"),
			TRANSPORT_FAMILY_IPV4),
	OPT_SET_INT('6', "ipv6", &family, N_("use IPv6 addresses only"),
			TRANSPORT_FAMILY_IPV6),
	OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
	OPT_BOOL(0, "remote-submodules", &option_remote_submodules,
		    N_("any cloned submodules will use their remote-tracking branch")),
	OPT_BOOL(0, "sparse", &option_sparse_checkout,
		    N_("initialize sparse-checkout file to include only files at root")),
	OPT_END()
};

static const char *get_repo_path_1(struct strbuf *path, int *is_bundle)
{
	static char *suffix[] = { "/.git", "", ".git/.git", ".git" };
	static char *bundle_suffix[] = { ".bundle", "" };
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

static char *guess_dir_name(const char *repo, int is_bundle, int is_bare)
{
	const char *end = repo + strlen(repo), *start, *ptr;
	size_t len;
	char *dir;

	/*
	 * Skip scheme.
	 */
	start = strstr(repo, "://");
	if (start == NULL)
		start = repo;
	else
		start += 3;

	/*
	 * Skip authentication data. The stripping does happen
	 * greedily, such that we strip up to the last '@' inside
	 * the host part.
	 */
	for (ptr = start; ptr < end && !is_dir_sep(*ptr); ptr++) {
		if (*ptr == '@')
			start = ptr + 1;
	}

	/*
	 * Strip trailing spaces, slashes and /.git
	 */
	while (start < end && (is_dir_sep(end[-1]) || isspace(end[-1])))
		end--;
	if (end - start > 5 && is_dir_sep(end[-5]) &&
	    !strncmp(end - 4, ".git", 4)) {
		end -= 5;
		while (start < end && is_dir_sep(end[-1]))
			end--;
	}

	/*
	 * Strip trailing port number if we've got only a
	 * hostname (that is, there is no dir separator but a
	 * colon). This check is required such that we do not
	 * strip URI's like '/foo/bar:2222.git', which should
	 * result in a dir '2222' being guessed due to backwards
	 * compatibility.
	 */
	if (memchr(start, '/', end - start) == NULL
	    && memchr(start, ':', end - start) != NULL) {
		ptr = end;
		while (start < ptr && isdigit(ptr[-1]) && ptr[-1] != ':')
			ptr--;
		if (start < ptr && ptr[-1] == ':')
			end = ptr - 1;
	}

	/*
	 * Find last component. To remain backwards compatible we
	 * also regard colons as path separators, such that
	 * cloning a repository 'foo:bar.git' would result in a
	 * directory 'bar' being guessed.
	 */
	ptr = end;
	while (start < ptr && !is_dir_sep(ptr[-1]) && ptr[-1] != ':')
		ptr--;
	start = ptr;

	/*
	 * Strip .{bundle,git}.
	 */
	len = end - start;
	strip_suffix_mem(start, &len, is_bundle ? ".bundle" : ".git");

	if (!len || (len == 1 && *start == '/'))
		die(_("No directory name could be guessed.\n"
		      "Please specify a directory on the command line"));

	if (is_bare)
		dir = xstrfmt("%.*s.git", (int)len, start);
	else
		dir = xstrndup(start, len);
	/*
	 * Replace sequences of 'control' characters and whitespace
	 * with one ascii space, remove leading and trailing spaces.
	 */
	if (*dir) {
		char *out = dir;
		int prev_space = 1 /* strip leading whitespace */;
		for (end = dir; *end; ++end) {
			char ch = *end;
			if ((unsigned char)ch < '\x20')
				ch = '\x20';
			if (isspace(ch)) {
				if (prev_space)
					continue;
				prev_space = 1;
			} else
				prev_space = 0;
			*out++ = ch;
		}
		*out = '\0';
		if (out > dir && prev_space)
			out[-1] = '\0';
	}
	return dir;
}

static void strip_trailing_slashes(char *dir)
{
	char *end = dir + strlen(dir);

	while (dir < end - 1 && is_dir_sep(end[-1]))
		end--;
	*end = '\0';
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
	unsigned int flags;
	struct strbuf realpath = STRBUF_INIT;

	mkdir_if_missing(dest->buf, 0777);

	flags = DIR_ITERATOR_PEDANTIC | DIR_ITERATOR_FOLLOW_SYMLINKS;
	iter = dir_iterator_begin(src->buf, flags);

	if (!iter)
		die_errno(_("failed to start iterator over '%s'"), src->buf);

	strbuf_addch(src, '/');
	src_len = src->len;
	strbuf_addch(dest, '/');
	dest_len = dest->len;

	while ((iter_status = dir_iterator_advance(iter)) == ITER_OK) {
		strbuf_setlen(src, src_len);
		strbuf_addstr(src, iter->relative_path);
		strbuf_setlen(dest, dest_len);
		strbuf_addstr(dest, iter->relative_path);

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
			strbuf_realpath(&realpath, src->buf, 1);
			if (!link(realpath.buf, dest->buf))
				continue;
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

	strbuf_release(&realpath);
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

	if (option_single_branch) {
		struct ref *remote_head = NULL;

		if (!option_branch)
			remote_head = guess_remote_head(head, refs, 0);
		else {
			local_refs = NULL;
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
			get_fetch_map(remote_head, tag_refspec, &tail, 0);
		}
	} else {
		int i;
		for (i = 0; i < refspec->nr; i++)
			get_fetch_map(refs, &refspec->items[i], &tail, 0);
	}

	if (!option_mirror && !option_single_branch && !option_no_tags)
		get_fetch_map(refs, tag_refspec, &tail, 0);

	return local_refs;
}

static void write_remote_refs(const struct ref *local_refs)
{
	const struct ref *r;

	struct ref_transaction *t;
	struct strbuf err = STRBUF_INIT;

	t = ref_transaction_begin(&err);
	if (!t)
		die("%s", err.buf);

	for (r = local_refs; r; r = r->next) {
		if (!r->peer_ref)
			continue;
		if (ref_transaction_create(t, r->peer_ref->name, &r->old_oid,
					   0, NULL, &err))
			die("%s", err.buf);
	}

	if (initial_ref_transaction_commit(t, &err))
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
		if (!has_object_file_with_flags(&ref->old_oid,
						OBJECT_INFO_QUICK |
						OBJECT_INFO_SKIP_FETCH_OBJECT))
			continue;
		update_ref(msg, ref->name, &ref->old_oid, NULL, 0,
			   UPDATE_REFS_DIE_ON_ERR);
	}
}

static int iterate_ref_map(void *cb_data, struct object_id *oid)
{
	struct ref **rm = cb_data;
	struct ref *ref = *rm;

	/*
	 * Skip anything missing a peer_ref, which we are not
	 * actually going to write a ref for.
	 */
	while (ref && !ref->peer_ref)
		ref = ref->next;
	/* Returning -1 notes "end of list" to the caller. */
	if (!ref)
		return -1;

	oidcpy(oid, &ref->old_oid);
	*rm = ref->next;
	return 0;
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
		if (create_symref(head_ref.buf,
				  remote_head_points_at->peer_ref->name,
				  msg) < 0)
			die(_("unable to update %s"), head_ref.buf);
		strbuf_release(&head_ref);
	}
}

static void update_head(const struct ref *our, const struct ref *remote,
			const char *msg)
{
	const char *head;
	if (our && skip_prefix(our->name, "refs/heads/", &head)) {
		/* Local default branch link */
		if (create_symref("HEAD", our->name, NULL) < 0)
			die(_("unable to update HEAD"));
		if (!option_bare) {
			update_ref(msg, "HEAD", &our->old_oid, NULL, 0,
				   UPDATE_REFS_DIE_ON_ERR);
			install_branch_config(0, head, remote_name, our->name);
		}
	} else if (our) {
		struct commit *c = lookup_commit_reference(the_repository,
							   &our->old_oid);
		/* --branch specifies a non-branch (i.e. tags), detach HEAD */
		update_ref(msg, "HEAD", &c->object.oid, NULL, REF_NO_DEREF,
			   UPDATE_REFS_DIE_ON_ERR);
	} else if (remote) {
		/*
		 * We know remote HEAD points to a non-branch, or
		 * HEAD points to a branch but we don't know which one.
		 * Detach HEAD in all these cases.
		 */
		update_ref(msg, "HEAD", &remote->old_oid, NULL, REF_NO_DEREF,
			   UPDATE_REFS_DIE_ON_ERR);
	}
}

static int git_sparse_checkout_init(const char *repo)
{
	struct strvec argv = STRVEC_INIT;
	int result = 0;
	strvec_pushl(&argv, "-C", repo, "sparse-checkout", "init", NULL);

	/*
	 * We must apply the setting in the current process
	 * for the later checkout to use the sparse-checkout file.
	 */
	core_apply_sparse_checkout = 1;

	if (run_command_v_opt(argv.v, RUN_GIT_CMD)) {
		error(_("failed to initialize sparse-checkout"));
		result = 1;
	}

	strvec_clear(&argv);
	return result;
}

static int checkout(int submodule_progress)
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

	head = resolve_refdup("HEAD", RESOLVE_REF_READING, &oid, NULL);
	if (!head) {
		warning(_("remote HEAD refers to nonexistent ref, "
			  "unable to checkout.\n"));
		return 0;
	}
	if (!strcmp(head, "HEAD")) {
		if (advice_detached_head)
			detach_advice(oid_to_hex(&oid));
		FREE_AND_NULL(head);
	} else {
		if (!starts_with(head, "refs/heads/"))
			die(_("HEAD not found below refs/heads!"));
	}

	/* We need to be in the new work tree for the checkout */
	setup_work_tree();

	hold_locked_index(&lock_file, LOCK_DIE_ON_ERROR);

	memset(&opts, 0, sizeof opts);
	opts.update = 1;
	opts.merge = 1;
	opts.clone = 1;
	opts.fn = oneway_merge;
	opts.verbose_update = (option_verbosity >= 0);
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	init_checkout_metadata(&opts.meta, head, &oid, NULL);

	tree = parse_tree_indirect(&oid);
	parse_tree(tree);
	init_tree_desc(&t, tree->buffer, tree->size);
	if (unpack_trees(1, &t, &opts) < 0)
		die(_("unable to checkout working tree"));

	free(head);

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		die(_("unable to write new index file"));

	err |= run_hook_le(NULL, "post-checkout", oid_to_hex(null_oid()),
			   oid_to_hex(&oid), "1", NULL);

	if (!err && (option_recurse_submodules.nr > 0)) {
		struct strvec args = STRVEC_INIT;
		strvec_pushl(&args, "submodule", "update", "--require-init", "--recursive", NULL);

		if (option_shallow_submodules == 1)
			strvec_push(&args, "--depth=1");

		if (max_jobs != -1)
			strvec_pushf(&args, "--jobs=%d", max_jobs);

		if (submodule_progress)
			strvec_push(&args, "--progress");

		if (option_verbosity < 0)
			strvec_push(&args, "--quiet");

		if (option_remote_submodules) {
			strvec_push(&args, "--remote");
			strvec_push(&args, "--no-fetch");
		}

		if (option_single_branch >= 0)
			strvec_push(&args, option_single_branch ?
					       "--single-branch" :
					       "--no-single-branch");

		err = run_command_v_opt(args.v, RUN_GIT_CMD);
		strvec_clear(&args);
	}

	return err;
}

static int git_clone_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "clone.defaultremotename")) {
		free(remote_name);
		remote_name = xstrdup(v);
	}
	if (!strcmp(k, "clone.rejectshallow"))
		config_reject_shallow = git_config_bool(k, v);

	return git_default_config(k, v, cb);
}

static int write_one_config(const char *key, const char *value, void *data)
{
	/*
	 * give git_clone_config a chance to write config values back to the
	 * environment, since git_config_set_multivar_gently only deals with
	 * config-file writes
	 */
	int apply_failed = git_clone_config(key, value, data);
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
	static const char* argv[] = { "repack", "-a", "-d", NULL };
	char *alternates = git_pathdup("objects/info/alternates");

	if (!access(alternates, F_OK)) {
		if (run_command_v_opt(argv, RUN_GIT_CMD|RUN_COMMAND_NO_STDIN))
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

int cmd_clone(int argc, const char **argv, const char *prefix)
{
	int is_bundle = 0, is_local;
	int reject_shallow = 0;
	const char *repo_name, *repo, *work_tree, *git_dir;
	char *path = NULL, *dir, *display_repo = NULL;
	int dest_exists, real_dest_exists = 0;
	const struct ref *refs, *remote_head;
	struct ref *remote_head_points_at = NULL;
	const struct ref *our_head_points_at;
	struct ref *mapped_refs;
	const struct ref *ref;
	struct strbuf key = STRBUF_INIT;
	struct strbuf branch_top = STRBUF_INIT, reflog_msg = STRBUF_INIT;
	struct transport *transport = NULL;
	const char *src_ref_prefix = "refs/heads/";
	struct remote *remote;
	int err = 0, complete_refs_before_fetch = 1;
	int submodule_progress;

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

	if (option_mirror)
		option_bare = 1;

	if (option_bare) {
		if (option_origin)
			die(_("--bare and --origin %s options are incompatible."),
			    option_origin);
		if (real_git_dir)
			die(_("--bare and --separate-git-dir are incompatible."));
		option_no_checkout = 1;
	}

	repo_name = argv[0];

	path = get_repo_path(repo_name, &is_bundle);
	if (path) {
		FREE_AND_NULL(path);
		repo = absolute_pathdup(repo_name);
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
		dir = guess_dir_name(repo_name, is_bundle, option_bare);
	strip_trailing_slashes(dir);

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

	init_db(git_dir, real_git_dir, option_template, GIT_HASH_UNKNOWN, NULL,
		INIT_DB_QUIET);

	if (real_git_dir)
		git_dir = real_git_dir;

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
	 * apply the remote name provided by --origin only after this second
	 * call to git_config, to ensure it overrides all config-based values.
	 */
	if (option_origin != NULL)
		remote_name = xstrdup(option_origin);

	if (remote_name == NULL)
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

	if (option_sparse_checkout && git_sparse_checkout_init(dir))
		return 1;

	remote = remote_get(remote_name);

	refspec_appendf(&remote->fetch, "+%s*:%s*", src_ref_prefix,
			branch_top.buf);

	transport = transport_get(remote, remote->url[0]);
	transport_set_verbosity(transport, option_verbosity, option_progress);
	transport->family = family;

	path = get_repo_path(remote->url[0], &is_bundle);
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
	transport->cloning = 1;

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

	if (refs) {
		int hash_algo = hash_algo_by_ptr(transport_get_hash_algo(transport));

		/*
		 * Now that we know what algorithm the remote side is using,
		 * let's set ours to the same thing.
		 */
		initialize_repository_version(hash_algo, 1);
		repo_set_hash_algo(the_repository, hash_algo);

		mapped_refs = wanted_peer_refs(refs, &remote->fetch);
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
			err = transport_fetch_refs(transport, mapped_refs);
			if (err)
				goto cleanup;
		}

		remote_head = find_ref_by_name(refs, "HEAD");
		remote_head_points_at =
			guess_remote_head(remote_head, mapped_refs, 0);

		if (option_branch) {
			our_head_points_at =
				find_remote_branch(mapped_refs, option_branch);

			if (!our_head_points_at)
				die(_("Remote branch %s not found in upstream %s"),
				    option_branch, remote_name);
		}
		else
			our_head_points_at = remote_head_points_at;
	}
	else {
		if (option_branch)
			die(_("Remote branch %s not found in upstream %s"),
					option_branch, remote_name);

		warning(_("You appear to have cloned an empty repository."));
		mapped_refs = NULL;
		our_head_points_at = NULL;
		remote_head_points_at = NULL;
		remote_head = NULL;
		option_no_checkout = 1;
		if (!option_bare) {
			const char *branch;
			char *ref;

			if (transport_ls_refs_options.unborn_head_target &&
			    skip_prefix(transport_ls_refs_options.unborn_head_target,
					"refs/heads/", &branch)) {
				ref = transport_ls_refs_options.unborn_head_target;
				transport_ls_refs_options.unborn_head_target = NULL;
				create_symref("HEAD", ref, reflog_msg.buf);
			} else {
				branch = git_default_branch_name(0);
				ref = xstrfmt("refs/heads/%s", branch);
			}

			install_branch_config(0, branch, remote_name, ref);
			free(ref);
		}
	}

	write_refspec_config(src_ref_prefix, our_head_points_at,
			remote_head_points_at, &branch_top);

	if (filter_options.choice)
		partial_clone_register(remote_name, &filter_options);

	if (is_local)
		clone_local(path, git_dir);
	else if (refs && complete_refs_before_fetch) {
		err = transport_fetch_refs(transport, mapped_refs);
		if (err)
			goto cleanup;
	}

	update_remote_refs(refs, mapped_refs, remote_head_points_at,
			   branch_top.buf, reflog_msg.buf, transport,
			   !is_local);

	update_head(our_head_points_at, remote_head, reflog_msg.buf);

	/*
	 * We want to show progress for recursive submodule clones iff
	 * we did so for the main clone. But only the transport knows
	 * the final decision for this flag, so we need to rescue the value
	 * before we free the transport.
	 */
	submodule_progress = transport->progress;

	transport_unlock_pack(transport);
	transport_disconnect(transport);

	if (option_dissociate) {
		close_object_store(the_repository->objects);
		dissociate_from_references();
	}

	junk_mode = JUNK_LEAVE_REPO;
	err = checkout(submodule_progress);

cleanup:
	free(remote_name);
	strbuf_release(&reflog_msg);
	strbuf_release(&branch_top);
	strbuf_release(&key);
	free_refs(mapped_refs);
	free_refs(remote_head_points_at);
	free(dir);
	free(path);
	UNLEAK(repo);
	junk_mode = JUNK_LEAVE_ALL;

	strvec_clear(&transport_ls_refs_options.ref_prefixes);
	free(transport_ls_refs_options.unborn_head_target);
	return err;
}
