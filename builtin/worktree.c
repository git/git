#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "parse-options.h"
#include "argv-array.h"
#include "branch.h"
#include "refs.h"
#include "run-command.h"
#include "sigchain.h"
#include "refs.h"
#include "utf8.h"
#include "worktree.h"

static const char * const worktree_usage[] = {
	N_("git worktree add [<options>] <path> [<branch>]"),
	N_("git worktree list [<options>]"),
	N_("git worktree lock [<options>] <path>"),
	N_("git worktree prune [<options>]"),
	N_("git worktree unlock <path>"),
	NULL
};

struct add_opts {
	int force;
	int detach;
	int checkout;
	const char *new_branch;
	int force_new_branch;
};

static int show_only;
static int verbose;
static unsigned long expire;

static int prune_worktree(const char *id, struct strbuf *reason)
{
	struct stat st;
	char *path;
	int fd, len;

	if (!is_directory(git_path("worktrees/%s", id))) {
		strbuf_addf(reason, _("Removing worktrees/%s: not a valid directory"), id);
		return 1;
	}
	if (file_exists(git_path("worktrees/%s/locked", id)))
		return 0;
	if (stat(git_path("worktrees/%s/gitdir", id), &st)) {
		strbuf_addf(reason, _("Removing worktrees/%s: gitdir file does not exist"), id);
		return 1;
	}
	fd = open(git_path("worktrees/%s/gitdir", id), O_RDONLY);
	if (fd < 0) {
		strbuf_addf(reason, _("Removing worktrees/%s: unable to read gitdir file (%s)"),
			    id, strerror(errno));
		return 1;
	}
	len = st.st_size;
	path = xmallocz(len);
	read_in_full(fd, path, len);
	close(fd);
	while (len && (path[len - 1] == '\n' || path[len - 1] == '\r'))
		len--;
	if (!len) {
		strbuf_addf(reason, _("Removing worktrees/%s: invalid gitdir file"), id);
		free(path);
		return 1;
	}
	path[len] = '\0';
	if (!file_exists(path)) {
		struct stat st_link;
		free(path);
		/*
		 * the repo is moved manually and has not been
		 * accessed since?
		 */
		if (!stat(git_path("worktrees/%s/link", id), &st_link) &&
		    st_link.st_nlink > 1)
			return 0;
		if (st.st_mtime <= expire) {
			strbuf_addf(reason, _("Removing worktrees/%s: gitdir file points to non-existent location"), id);
			return 1;
		} else {
			return 0;
		}
	}
	free(path);
	return 0;
}

static void prune_worktrees(void)
{
	struct strbuf reason = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	DIR *dir = opendir(git_path("worktrees"));
	struct dirent *d;
	int ret;
	if (!dir)
		return;
	while ((d = readdir(dir)) != NULL) {
		if (is_dot_or_dotdot(d->d_name))
			continue;
		strbuf_reset(&reason);
		if (!prune_worktree(d->d_name, &reason))
			continue;
		if (show_only || verbose)
			printf("%s\n", reason.buf);
		if (show_only)
			continue;
		strbuf_reset(&path);
		strbuf_addstr(&path, git_path("worktrees/%s", d->d_name));
		ret = remove_dir_recursively(&path, 0);
		if (ret < 0 && errno == ENOTDIR)
			ret = unlink(path.buf);
		if (ret)
			error_errno(_("failed to remove '%s'"), path.buf);
	}
	closedir(dir);
	if (!show_only)
		rmdir(git_path("worktrees"));
	strbuf_release(&reason);
	strbuf_release(&path);
}

static int prune(int ac, const char **av, const char *prefix)
{
	struct option options[] = {
		OPT__DRY_RUN(&show_only, N_("do not remove, show only")),
		OPT__VERBOSE(&verbose, N_("report pruned objects")),
		OPT_EXPIRY_DATE(0, "expire", &expire,
				N_("expire objects older than <time>")),
		OPT_END()
	};

	expire = ULONG_MAX;
	ac = parse_options(ac, av, prefix, options, worktree_usage, 0);
	if (ac)
		usage_with_options(worktree_usage, options);
	prune_worktrees();
	return 0;
}

static char *junk_work_tree;
static char *junk_git_dir;
static int is_junk;
static pid_t junk_pid;

static void remove_junk(void)
{
	struct strbuf sb = STRBUF_INIT;
	if (!is_junk || getpid() != junk_pid)
		return;
	if (junk_git_dir) {
		strbuf_addstr(&sb, junk_git_dir);
		remove_dir_recursively(&sb, 0);
		strbuf_reset(&sb);
	}
	if (junk_work_tree) {
		strbuf_addstr(&sb, junk_work_tree);
		remove_dir_recursively(&sb, 0);
	}
	strbuf_release(&sb);
}

static void remove_junk_on_signal(int signo)
{
	remove_junk();
	sigchain_pop(signo);
	raise(signo);
}

static const char *worktree_basename(const char *path, int *olen)
{
	const char *name;
	int len;

	len = strlen(path);
	while (len && is_dir_sep(path[len - 1]))
		len--;

	for (name = path + len - 1; name > path; name--)
		if (is_dir_sep(*name)) {
			name++;
			break;
		}

	*olen = len;
	return name;
}

static int add_worktree(const char *path, const char *refname,
			const struct add_opts *opts)
{
	struct strbuf sb_git = STRBUF_INIT, sb_repo = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT;
	const char *name;
	struct stat st;
	struct child_process cp = CHILD_PROCESS_INIT;
	struct argv_array child_env = ARGV_ARRAY_INIT;
	int counter = 0, len, ret;
	struct strbuf symref = STRBUF_INIT;
	struct commit *commit = NULL;

	if (file_exists(path) && !is_empty_dir(path))
		die(_("'%s' already exists"), path);

	/* is 'refname' a branch or commit? */
	if (!opts->detach && !strbuf_check_branch_ref(&symref, refname) &&
		 ref_exists(symref.buf)) { /* it's a branch */
		if (!opts->force)
			die_if_checked_out(symref.buf, 0);
	} else { /* must be a commit */
		commit = lookup_commit_reference_by_name(refname);
		if (!commit)
			die(_("invalid reference: %s"), refname);
	}

	name = worktree_basename(path, &len);
	strbuf_addstr(&sb_repo,
		      git_path("worktrees/%.*s", (int)(path + len - name), name));
	len = sb_repo.len;
	if (safe_create_leading_directories_const(sb_repo.buf))
		die_errno(_("could not create leading directories of '%s'"),
			  sb_repo.buf);
	while (!stat(sb_repo.buf, &st)) {
		counter++;
		strbuf_setlen(&sb_repo, len);
		strbuf_addf(&sb_repo, "%d", counter);
	}
	name = strrchr(sb_repo.buf, '/') + 1;

	junk_pid = getpid();
	atexit(remove_junk);
	sigchain_push_common(remove_junk_on_signal);

	if (mkdir(sb_repo.buf, 0777))
		die_errno(_("could not create directory of '%s'"), sb_repo.buf);
	junk_git_dir = xstrdup(sb_repo.buf);
	is_junk = 1;

	/*
	 * lock the incomplete repo so prune won't delete it, unlock
	 * after the preparation is over.
	 */
	strbuf_addf(&sb, "%s/locked", sb_repo.buf);
	write_file(sb.buf, "initializing");

	strbuf_addf(&sb_git, "%s/.git", path);
	if (safe_create_leading_directories_const(sb_git.buf))
		die_errno(_("could not create leading directories of '%s'"),
			  sb_git.buf);
	junk_work_tree = xstrdup(path);

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/gitdir", sb_repo.buf);
	write_file(sb.buf, "%s", real_path(sb_git.buf));
	write_file(sb_git.buf, "gitdir: %s/worktrees/%s",
		   real_path(get_git_common_dir()), name);
	/*
	 * This is to keep resolve_ref() happy. We need a valid HEAD
	 * or is_git_directory() will reject the directory. Any value which
	 * looks like an object ID will do since it will be immediately
	 * replaced by the symbolic-ref or update-ref invocation in the new
	 * worktree.
	 */
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/HEAD", sb_repo.buf);
	write_file(sb.buf, "%s", sha1_to_hex(null_sha1));
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/commondir", sb_repo.buf);
	write_file(sb.buf, "../..");

	fprintf_ln(stderr, _("Preparing %s (identifier %s)"), path, name);

	argv_array_pushf(&child_env, "%s=%s", GIT_DIR_ENVIRONMENT, sb_git.buf);
	argv_array_pushf(&child_env, "%s=%s", GIT_WORK_TREE_ENVIRONMENT, path);
	cp.git_cmd = 1;

	if (commit)
		argv_array_pushl(&cp.args, "update-ref", "HEAD",
				 oid_to_hex(&commit->object.oid), NULL);
	else
		argv_array_pushl(&cp.args, "symbolic-ref", "HEAD",
				 symref.buf, NULL);
	cp.env = child_env.argv;
	ret = run_command(&cp);
	if (ret)
		goto done;

	if (opts->checkout) {
		cp.argv = NULL;
		argv_array_clear(&cp.args);
		argv_array_pushl(&cp.args, "reset", "--hard", NULL);
		cp.env = child_env.argv;
		ret = run_command(&cp);
		if (ret)
			goto done;
	}

	is_junk = 0;
	free(junk_work_tree);
	free(junk_git_dir);
	junk_work_tree = NULL;
	junk_git_dir = NULL;

done:
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/locked", sb_repo.buf);
	unlink_or_warn(sb.buf);
	argv_array_clear(&child_env);
	strbuf_release(&sb);
	strbuf_release(&symref);
	strbuf_release(&sb_repo);
	strbuf_release(&sb_git);
	return ret;
}

static int add(int ac, const char **av, const char *prefix)
{
	struct add_opts opts;
	const char *new_branch_force = NULL;
	const char *path, *branch;
	struct option options[] = {
		OPT__FORCE(&opts.force, N_("checkout <branch> even if already checked out in other worktree")),
		OPT_STRING('b', NULL, &opts.new_branch, N_("branch"),
			   N_("create a new branch")),
		OPT_STRING('B', NULL, &new_branch_force, N_("branch"),
			   N_("create or reset a branch")),
		OPT_BOOL(0, "detach", &opts.detach, N_("detach HEAD at named commit")),
		OPT_BOOL(0, "checkout", &opts.checkout, N_("populate the new working tree")),
		OPT_END()
	};

	memset(&opts, 0, sizeof(opts));
	opts.checkout = 1;
	ac = parse_options(ac, av, prefix, options, worktree_usage, 0);
	if (!!opts.detach + !!opts.new_branch + !!new_branch_force > 1)
		die(_("-b, -B, and --detach are mutually exclusive"));
	if (ac < 1 || ac > 2)
		usage_with_options(worktree_usage, options);

	path = prefix_filename(prefix, strlen(prefix), av[0]);
	branch = ac < 2 ? "HEAD" : av[1];

	if (!strcmp(branch, "-"))
		branch = "@{-1}";

	opts.force_new_branch = !!new_branch_force;
	if (opts.force_new_branch) {
		struct strbuf symref = STRBUF_INIT;

		opts.new_branch = new_branch_force;

		if (!opts.force &&
		    !strbuf_check_branch_ref(&symref, opts.new_branch) &&
		    ref_exists(symref.buf))
			die_if_checked_out(symref.buf, 0);
		strbuf_release(&symref);
	}

	if (ac < 2 && !opts.new_branch && !opts.detach) {
		int n;
		const char *s = worktree_basename(path, &n);
		opts.new_branch = xstrndup(s, n);
	}

	if (opts.new_branch) {
		struct child_process cp = CHILD_PROCESS_INIT;
		cp.git_cmd = 1;
		argv_array_push(&cp.args, "branch");
		if (opts.force_new_branch)
			argv_array_push(&cp.args, "--force");
		argv_array_push(&cp.args, opts.new_branch);
		argv_array_push(&cp.args, branch);
		if (run_command(&cp))
			return -1;
		branch = opts.new_branch;
	}

	return add_worktree(path, branch, &opts);
}

static void show_worktree_porcelain(struct worktree *wt)
{
	printf("worktree %s\n", wt->path);
	if (wt->is_bare)
		printf("bare\n");
	else {
		printf("HEAD %s\n", sha1_to_hex(wt->head_sha1));
		if (wt->is_detached)
			printf("detached\n");
		else
			printf("branch %s\n", wt->head_ref);
	}
	printf("\n");
}

static void show_worktree(struct worktree *wt, int path_maxlen, int abbrev_len)
{
	struct strbuf sb = STRBUF_INIT;
	int cur_path_len = strlen(wt->path);
	int path_adj = cur_path_len - utf8_strwidth(wt->path);

	strbuf_addf(&sb, "%-*s ", 1 + path_maxlen + path_adj, wt->path);
	if (wt->is_bare)
		strbuf_addstr(&sb, "(bare)");
	else {
		strbuf_addf(&sb, "%-*s ", abbrev_len,
				find_unique_abbrev(wt->head_sha1, DEFAULT_ABBREV));
		if (!wt->is_detached)
			strbuf_addf(&sb, "[%s]", shorten_unambiguous_ref(wt->head_ref, 0));
		else
			strbuf_addstr(&sb, "(detached HEAD)");
	}
	printf("%s\n", sb.buf);

	strbuf_release(&sb);
}

static void measure_widths(struct worktree **wt, int *abbrev, int *maxlen)
{
	int i;

	for (i = 0; wt[i]; i++) {
		int sha1_len;
		int path_len = strlen(wt[i]->path);

		if (path_len > *maxlen)
			*maxlen = path_len;
		sha1_len = strlen(find_unique_abbrev(wt[i]->head_sha1, *abbrev));
		if (sha1_len > *abbrev)
			*abbrev = sha1_len;
	}
}

static int list(int ac, const char **av, const char *prefix)
{
	int porcelain = 0;

	struct option options[] = {
		OPT_BOOL(0, "porcelain", &porcelain, N_("machine-readable output")),
		OPT_END()
	};

	ac = parse_options(ac, av, prefix, options, worktree_usage, 0);
	if (ac)
		usage_with_options(worktree_usage, options);
	else {
		struct worktree **worktrees = get_worktrees();
		int path_maxlen = 0, abbrev = DEFAULT_ABBREV, i;

		if (!porcelain)
			measure_widths(worktrees, &abbrev, &path_maxlen);

		for (i = 0; worktrees[i]; i++) {
			if (porcelain)
				show_worktree_porcelain(worktrees[i]);
			else
				show_worktree(worktrees[i], path_maxlen, abbrev);
		}
		free_worktrees(worktrees);
	}
	return 0;
}

static int lock_worktree(int ac, const char **av, const char *prefix)
{
	const char *reason = "", *old_reason;
	struct option options[] = {
		OPT_STRING(0, "reason", &reason, N_("string"),
			   N_("reason for locking")),
		OPT_END()
	};
	struct worktree **worktrees, *wt;

	ac = parse_options(ac, av, prefix, options, worktree_usage, 0);
	if (ac != 1)
		usage_with_options(worktree_usage, options);

	worktrees = get_worktrees();
	wt = find_worktree(worktrees, prefix, av[0]);
	if (!wt)
		die(_("'%s' is not a working tree"), av[0]);
	if (is_main_worktree(wt))
		die(_("The main working tree cannot be locked or unlocked"));

	old_reason = is_worktree_locked(wt);
	if (old_reason) {
		if (*old_reason)
			die(_("'%s' is already locked, reason: %s"),
			    av[0], old_reason);
		die(_("'%s' is already locked"), av[0]);
	}

	write_file(git_common_path("worktrees/%s/locked", wt->id),
		   "%s", reason);
	free_worktrees(worktrees);
	return 0;
}

static int unlock_worktree(int ac, const char **av, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};
	struct worktree **worktrees, *wt;
	int ret;

	ac = parse_options(ac, av, prefix, options, worktree_usage, 0);
	if (ac != 1)
		usage_with_options(worktree_usage, options);

	worktrees = get_worktrees();
	wt = find_worktree(worktrees, prefix, av[0]);
	if (!wt)
		die(_("'%s' is not a working tree"), av[0]);
	if (is_main_worktree(wt))
		die(_("The main working tree cannot be locked or unlocked"));
	if (!is_worktree_locked(wt))
		die(_("'%s' is not locked"), av[0]);
	ret = unlink_or_warn(git_common_path("worktrees/%s/locked", wt->id));
	free_worktrees(worktrees);
	return ret;
}

int cmd_worktree(int ac, const char **av, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	git_config(git_default_config, NULL);

	if (ac < 2)
		usage_with_options(worktree_usage, options);
	if (!prefix)
		prefix = "";
	if (!strcmp(av[1], "add"))
		return add(ac - 1, av + 1, prefix);
	if (!strcmp(av[1], "prune"))
		return prune(ac - 1, av + 1, prefix);
	if (!strcmp(av[1], "list"))
		return list(ac - 1, av + 1, prefix);
	if (!strcmp(av[1], "lock"))
		return lock_worktree(ac - 1, av + 1, prefix);
	if (!strcmp(av[1], "unlock"))
		return unlock_worktree(ac - 1, av + 1, prefix);
	usage_with_options(worktree_usage, options);
}
