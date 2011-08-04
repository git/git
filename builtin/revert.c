#include "cache.h"
#include "builtin.h"
#include "object.h"
#include "commit.h"
#include "tag.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "utf8.h"
#include "parse-options.h"
#include "cache-tree.h"
#include "diff.h"
#include "revision.h"
#include "rerere.h"
#include "merge-recursive.h"
#include "refs.h"

/*
 * This implements the builtins revert and cherry-pick.
 *
 * Copyright (c) 2007 Johannes E. Schindelin
 *
 * Based on git-revert.sh, which is
 *
 * Copyright (c) 2005 Linus Torvalds
 * Copyright (c) 2005 Junio C Hamano
 */

static const char * const revert_usage[] = {
	"git revert [options] <commit-ish>",
	NULL
};

static const char * const cherry_pick_usage[] = {
	"git cherry-pick [options] <commit-ish>",
	NULL
};

static int edit, no_replay, no_commit, mainline, signoff, allow_ff;
static enum { REVERT, CHERRY_PICK } action;
static struct commit *commit;
static int commit_argc;
static const char **commit_argv;
static int allow_rerere_auto;

static const char *me;

/* Merge strategy. */
static const char *strategy;
static const char **xopts;
static size_t xopts_nr, xopts_alloc;

#define GIT_REFLOG_ACTION "GIT_REFLOG_ACTION"

static char *get_encoding(const char *message);

static const char * const *revert_or_cherry_pick_usage(void)
{
	return action == REVERT ? revert_usage : cherry_pick_usage;
}

static int option_parse_x(const struct option *opt,
			  const char *arg, int unset)
{
	if (unset)
		return 0;

	ALLOC_GROW(xopts, xopts_nr + 1, xopts_alloc);
	xopts[xopts_nr++] = xstrdup(arg);
	return 0;
}

static void parse_args(int argc, const char **argv)
{
	const char * const * usage_str = revert_or_cherry_pick_usage();
	int noop;
	struct option options[] = {
		OPT_BOOLEAN('n', "no-commit", &no_commit, "don't automatically commit"),
		OPT_BOOLEAN('e', "edit", &edit, "edit the commit message"),
		{ OPTION_BOOLEAN, 'r', NULL, &noop, NULL, "no-op (backward compatibility)",
		  PARSE_OPT_NOARG | PARSE_OPT_HIDDEN, NULL, 0 },
		OPT_BOOLEAN('s', "signoff", &signoff, "add Signed-off-by:"),
		OPT_INTEGER('m', "mainline", &mainline, "parent number"),
		OPT_RERERE_AUTOUPDATE(&allow_rerere_auto),
		OPT_STRING(0, "strategy", &strategy, "strategy", "merge strategy"),
		OPT_CALLBACK('X', "strategy-option", &xopts, "option",
			"option for merge strategy", option_parse_x),
		OPT_END(),
		OPT_END(),
		OPT_END(),
	};

	if (action == CHERRY_PICK) {
		struct option cp_extra[] = {
			OPT_BOOLEAN('x', NULL, &no_replay, "append commit name"),
			OPT_BOOLEAN(0, "ff", &allow_ff, "allow fast-forward"),
			OPT_END(),
		};
		if (parse_options_concat(options, ARRAY_SIZE(options), cp_extra))
			die(_("program error"));
	}

	commit_argc = parse_options(argc, argv, NULL, options, usage_str,
				    PARSE_OPT_KEEP_ARGV0 |
				    PARSE_OPT_KEEP_UNKNOWN);
	if (commit_argc < 2)
		usage_with_options(usage_str, options);

	commit_argv = argv;
}

struct commit_message {
	char *parent_label;
	const char *label;
	const char *subject;
	char *reencoded_message;
	const char *message;
};

static int get_message(const char *raw_message, struct commit_message *out)
{
	const char *encoding;
	const char *abbrev, *subject;
	int abbrev_len, subject_len;
	char *q;

	if (!raw_message)
		return -1;
	encoding = get_encoding(raw_message);
	if (!encoding)
		encoding = "UTF-8";
	if (!git_commit_encoding)
		git_commit_encoding = "UTF-8";

	out->reencoded_message = NULL;
	out->message = raw_message;
	if (strcmp(encoding, git_commit_encoding))
		out->reencoded_message = reencode_string(raw_message,
					git_commit_encoding, encoding);
	if (out->reencoded_message)
		out->message = out->reencoded_message;

	abbrev = find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV);
	abbrev_len = strlen(abbrev);

	subject_len = find_commit_subject(out->message, &subject);

	out->parent_label = xmalloc(strlen("parent of ") + abbrev_len +
			      strlen("... ") + subject_len + 1);
	q = out->parent_label;
	q = mempcpy(q, "parent of ", strlen("parent of "));
	out->label = q;
	q = mempcpy(q, abbrev, abbrev_len);
	q = mempcpy(q, "... ", strlen("... "));
	out->subject = q;
	q = mempcpy(q, subject, subject_len);
	*q = '\0';
	return 0;
}

static void free_message(struct commit_message *msg)
{
	free(msg->parent_label);
	free(msg->reencoded_message);
}

static char *get_encoding(const char *message)
{
	const char *p = message, *eol;

	if (!p)
		die (_("Could not read commit message of %s"),
				sha1_to_hex(commit->object.sha1));
	while (*p && *p != '\n') {
		for (eol = p + 1; *eol && *eol != '\n'; eol++)
			; /* do nothing */
		if (!prefixcmp(p, "encoding ")) {
			char *result = xmalloc(eol - 8 - p);
			strlcpy(result, p + 9, eol - 8 - p);
			return result;
		}
		p = eol;
		if (*p == '\n')
			p++;
	}
	return NULL;
}

static void add_message_to_msg(struct strbuf *msgbuf, const char *message)
{
	const char *p = message;
	while (*p && (*p != '\n' || p[1] != '\n'))
		p++;

	if (!*p)
		strbuf_addstr(msgbuf, sha1_to_hex(commit->object.sha1));

	p += 2;
	strbuf_addstr(msgbuf, p);
}

static void write_cherry_pick_head(void)
{
	int fd;
	struct strbuf buf = STRBUF_INIT;

	strbuf_addf(&buf, "%s\n", sha1_to_hex(commit->object.sha1));

	fd = open(git_path("CHERRY_PICK_HEAD"), O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"),
			  git_path("CHERRY_PICK_HEAD"));
	if (write_in_full(fd, buf.buf, buf.len) != buf.len || close(fd))
		die_errno(_("Could not write to '%s'"), git_path("CHERRY_PICK_HEAD"));
	strbuf_release(&buf);
}

static void print_advice(void)
{
	char *msg = getenv("GIT_CHERRY_PICK_HELP");

	if (msg) {
		fprintf(stderr, "%s\n", msg);
		/*
		 * A conflict has occured but the porcelain
		 * (typically rebase --interactive) wants to take care
		 * of the commit itself so remove CHERRY_PICK_HEAD
		 */
		unlink(git_path("CHERRY_PICK_HEAD"));
		return;
	}

	advise("after resolving the conflicts, mark the corrected paths");
	advise("with 'git add <paths>' or 'git rm <paths>'");
	advise("and commit the result with 'git commit'");
}

static void write_message(struct strbuf *msgbuf, const char *filename)
{
	static struct lock_file msg_file;

	int msg_fd = hold_lock_file_for_update(&msg_file, filename,
					       LOCK_DIE_ON_ERROR);
	if (write_in_full(msg_fd, msgbuf->buf, msgbuf->len) < 0)
		die_errno(_("Could not write to %s."), filename);
	strbuf_release(msgbuf);
	if (commit_lock_file(&msg_file) < 0)
		die(_("Error wrapping up %s"), filename);
}

static struct tree *empty_tree(void)
{
	struct tree *tree = xcalloc(1, sizeof(struct tree));

	tree->object.parsed = 1;
	tree->object.type = OBJ_TREE;
	pretend_sha1_file(NULL, 0, OBJ_TREE, tree->object.sha1);
	return tree;
}

static NORETURN void die_dirty_index(const char *me)
{
	if (read_cache_unmerged()) {
		die_resolve_conflict(me);
	} else {
		if (advice_commit_before_merge) {
			if (action == REVERT)
				die(_("Your local changes would be overwritten by revert.\n"
					  "Please, commit your changes or stash them to proceed."));
			else
				die(_("Your local changes would be overwritten by cherry-pick.\n"
					  "Please, commit your changes or stash them to proceed."));
		} else {
			if (action == REVERT)
				die(_("Your local changes would be overwritten by revert.\n"));
			else
				die(_("Your local changes would be overwritten by cherry-pick.\n"));
		}
	}
}

static int fast_forward_to(const unsigned char *to, const unsigned char *from)
{
	struct ref_lock *ref_lock;

	read_cache();
	if (checkout_fast_forward(from, to))
		exit(1); /* the callee should have complained already */
	ref_lock = lock_any_ref_for_update("HEAD", from, 0);
	return write_ref_sha1(ref_lock, to, "cherry-pick");
}

static int do_recursive_merge(struct commit *base, struct commit *next,
			      const char *base_label, const char *next_label,
			      unsigned char *head, struct strbuf *msgbuf)
{
	struct merge_options o;
	struct tree *result, *next_tree, *base_tree, *head_tree;
	int clean, index_fd;
	const char **xopt;
	static struct lock_file index_lock;

	index_fd = hold_locked_index(&index_lock, 1);

	read_cache();

	init_merge_options(&o);
	o.ancestor = base ? base_label : "(empty tree)";
	o.branch1 = "HEAD";
	o.branch2 = next ? next_label : "(empty tree)";

	head_tree = parse_tree_indirect(head);
	next_tree = next ? next->tree : empty_tree();
	base_tree = base ? base->tree : empty_tree();

	for (xopt = xopts; xopt != xopts + xopts_nr; xopt++)
		parse_merge_opt(&o, *xopt);

	clean = merge_trees(&o,
			    head_tree,
			    next_tree, base_tree, &result);

	if (active_cache_changed &&
	    (write_cache(index_fd, active_cache, active_nr) ||
	     commit_locked_index(&index_lock)))
		/* TRANSLATORS: %s will be "revert" or "cherry-pick" */
		die(_("%s: Unable to write new index file"), me);
	rollback_lock_file(&index_lock);

	if (!clean) {
		int i;
		strbuf_addstr(msgbuf, "\nConflicts:\n\n");
		for (i = 0; i < active_nr;) {
			struct cache_entry *ce = active_cache[i++];
			if (ce_stage(ce)) {
				strbuf_addch(msgbuf, '\t');
				strbuf_addstr(msgbuf, ce->name);
				strbuf_addch(msgbuf, '\n');
				while (i < active_nr && !strcmp(ce->name,
						active_cache[i]->name))
					i++;
			}
		}
	}

	return !clean;
}

/*
 * If we are cherry-pick, and if the merge did not result in
 * hand-editing, we will hit this commit and inherit the original
 * author date and name.
 * If we are revert, or if our cherry-pick results in a hand merge,
 * we had better say that the current user is responsible for that.
 */
static int run_git_commit(const char *defmsg)
{
	/* 6 is max possible length of our args array including NULL */
	const char *args[6];
	int i = 0;

	args[i++] = "commit";
	args[i++] = "-n";
	if (signoff)
		args[i++] = "-s";
	if (!edit) {
		args[i++] = "-F";
		args[i++] = defmsg;
	}
	args[i] = NULL;

	return run_command_v_opt(args, RUN_GIT_CMD);
}

static int do_pick_commit(void)
{
	unsigned char head[20];
	struct commit *base, *next, *parent;
	const char *base_label, *next_label;
	struct commit_message msg = { NULL, NULL, NULL, NULL, NULL };
	char *defmsg = NULL;
	struct strbuf msgbuf = STRBUF_INIT;
	int res;

	if (no_commit) {
		/*
		 * We do not intend to commit immediately.  We just want to
		 * merge the differences in, so let's compute the tree
		 * that represents the "current" state for merge-recursive
		 * to work on.
		 */
		if (write_cache_as_tree(head, 0, NULL))
			die (_("Your index file is unmerged."));
	} else {
		if (get_sha1("HEAD", head))
			die (_("You do not have a valid HEAD"));
		if (index_differs_from("HEAD", 0))
			die_dirty_index(me);
	}
	discard_cache();

	if (!commit->parents) {
		parent = NULL;
	}
	else if (commit->parents->next) {
		/* Reverting or cherry-picking a merge commit */
		int cnt;
		struct commit_list *p;

		if (!mainline)
			die(_("Commit %s is a merge but no -m option was given."),
			    sha1_to_hex(commit->object.sha1));

		for (cnt = 1, p = commit->parents;
		     cnt != mainline && p;
		     cnt++)
			p = p->next;
		if (cnt != mainline || !p)
			die(_("Commit %s does not have parent %d"),
			    sha1_to_hex(commit->object.sha1), mainline);
		parent = p->item;
	} else if (0 < mainline)
		die(_("Mainline was specified but commit %s is not a merge."),
		    sha1_to_hex(commit->object.sha1));
	else
		parent = commit->parents->item;

	if (allow_ff && parent && !hashcmp(parent->object.sha1, head))
		return fast_forward_to(commit->object.sha1, head);

	if (parent && parse_commit(parent) < 0)
		/* TRANSLATORS: The first %s will be "revert" or
		   "cherry-pick", the second %s a SHA1 */
		die(_("%s: cannot parse parent commit %s"),
		    me, sha1_to_hex(parent->object.sha1));

	if (get_message(commit->buffer, &msg) != 0)
		die(_("Cannot get commit message for %s"),
				sha1_to_hex(commit->object.sha1));

	/*
	 * "commit" is an existing commit.  We would want to apply
	 * the difference it introduces since its first parent "prev"
	 * on top of the current HEAD if we are cherry-pick.  Or the
	 * reverse of it if we are revert.
	 */

	defmsg = git_pathdup("MERGE_MSG");

	if (action == REVERT) {
		base = commit;
		base_label = msg.label;
		next = parent;
		next_label = msg.parent_label;
		strbuf_addstr(&msgbuf, "Revert \"");
		strbuf_addstr(&msgbuf, msg.subject);
		strbuf_addstr(&msgbuf, "\"\n\nThis reverts commit ");
		strbuf_addstr(&msgbuf, sha1_to_hex(commit->object.sha1));

		if (commit->parents && commit->parents->next) {
			strbuf_addstr(&msgbuf, ", reversing\nchanges made to ");
			strbuf_addstr(&msgbuf, sha1_to_hex(parent->object.sha1));
		}
		strbuf_addstr(&msgbuf, ".\n");
	} else {
		base = parent;
		base_label = msg.parent_label;
		next = commit;
		next_label = msg.label;
		add_message_to_msg(&msgbuf, msg.message);
		if (no_replay) {
			strbuf_addstr(&msgbuf, "(cherry picked from commit ");
			strbuf_addstr(&msgbuf, sha1_to_hex(commit->object.sha1));
			strbuf_addstr(&msgbuf, ")\n");
		}
		if (!no_commit)
			write_cherry_pick_head();
	}

	if (!strategy || !strcmp(strategy, "recursive") || action == REVERT) {
		res = do_recursive_merge(base, next, base_label, next_label,
					 head, &msgbuf);
		write_message(&msgbuf, defmsg);
	} else {
		struct commit_list *common = NULL;
		struct commit_list *remotes = NULL;

		write_message(&msgbuf, defmsg);

		commit_list_insert(base, &common);
		commit_list_insert(next, &remotes);
		res = try_merge_command(strategy, xopts_nr, xopts, common,
					sha1_to_hex(head), remotes);
		free_commit_list(common);
		free_commit_list(remotes);
	}

	if (res) {
		error(action == REVERT
		      ? _("could not revert %s... %s")
		      : _("could not apply %s... %s"),
		      find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV),
		      msg.subject);
		print_advice();
		rerere(allow_rerere_auto);
	} else {
		if (!no_commit)
			res = run_git_commit(defmsg);
	}

	free_message(&msg);
	free(defmsg);

	return res;
}

static void prepare_revs(struct rev_info *revs)
{
	int argc;

	init_revisions(revs, NULL);
	revs->no_walk = 1;
	if (action != REVERT)
		revs->reverse = 1;

	argc = setup_revisions(commit_argc, commit_argv, revs, NULL);
	if (argc > 1)
		usage(*revert_or_cherry_pick_usage());

	if (prepare_revision_walk(revs))
		die(_("revision walk setup failed"));

	if (!revs->commits)
		die(_("empty commit set passed"));
}

static void read_and_refresh_cache(const char *me)
{
	static struct lock_file index_lock;
	int index_fd = hold_locked_index(&index_lock, 0);
	if (read_index_preload(&the_index, NULL) < 0)
		die(_("git %s: failed to read the index"), me);
	refresh_index(&the_index, REFRESH_QUIET|REFRESH_UNMERGED, NULL, NULL, NULL);
	if (the_index.cache_changed) {
		if (write_index(&the_index, index_fd) ||
		    commit_locked_index(&index_lock))
			die(_("git %s: failed to refresh the index"), me);
	}
	rollback_lock_file(&index_lock);
}

static int revert_or_cherry_pick(int argc, const char **argv)
{
	struct rev_info revs;

	git_config(git_default_config, NULL);
	me = action == REVERT ? "revert" : "cherry-pick";
	setenv(GIT_REFLOG_ACTION, me, 0);
	parse_args(argc, argv);

	if (allow_ff) {
		if (signoff)
			die(_("cherry-pick --ff cannot be used with --signoff"));
		if (no_commit)
			die(_("cherry-pick --ff cannot be used with --no-commit"));
		if (no_replay)
			die(_("cherry-pick --ff cannot be used with -x"));
		if (edit)
			die(_("cherry-pick --ff cannot be used with --edit"));
	}

	read_and_refresh_cache(me);

	prepare_revs(&revs);

	while ((commit = get_revision(&revs))) {
		int res = do_pick_commit();
		if (res)
			return res;
	}

	return 0;
}

int cmd_revert(int argc, const char **argv, const char *prefix)
{
	if (isatty(0))
		edit = 1;
	action = REVERT;
	return revert_or_cherry_pick(argc, argv);
}

int cmd_cherry_pick(int argc, const char **argv, const char *prefix)
{
	action = CHERRY_PICK;
	return revert_or_cherry_pick(argc, argv);
}
