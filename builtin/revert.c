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
#include "dir.h"
#include "sequencer.h"

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
	"git revert <subcommand>",
	NULL
};

static const char * const cherry_pick_usage[] = {
	"git cherry-pick [options] <commit-ish>",
	"git cherry-pick <subcommand>",
	NULL
};

enum replay_action { REVERT, CHERRY_PICK };
enum replay_subcommand { REPLAY_NONE, REPLAY_RESET, REPLAY_CONTINUE };

struct replay_opts {
	enum replay_action action;
	enum replay_subcommand subcommand;

	/* Boolean options */
	int edit;
	int record_origin;
	int no_commit;
	int signoff;
	int allow_ff;
	int allow_rerere_auto;

	int mainline;
	int commit_argc;
	const char **commit_argv;

	/* Merge strategy */
	const char *strategy;
	const char **xopts;
	size_t xopts_nr, xopts_alloc;
};

#define GIT_REFLOG_ACTION "GIT_REFLOG_ACTION"

static const char *action_name(const struct replay_opts *opts)
{
	return opts->action == REVERT ? "revert" : "cherry-pick";
}

static char *get_encoding(const char *message);

static const char * const *revert_or_cherry_pick_usage(struct replay_opts *opts)
{
	return opts->action == REVERT ? revert_usage : cherry_pick_usage;
}

static int option_parse_x(const struct option *opt,
			  const char *arg, int unset)
{
	struct replay_opts **opts_ptr = opt->value;
	struct replay_opts *opts = *opts_ptr;

	if (unset)
		return 0;

	ALLOC_GROW(opts->xopts, opts->xopts_nr + 1, opts->xopts_alloc);
	opts->xopts[opts->xopts_nr++] = xstrdup(arg);
	return 0;
}

static void verify_opt_compatible(const char *me, const char *base_opt, ...)
{
	const char *this_opt;
	va_list ap;

	va_start(ap, base_opt);
	while ((this_opt = va_arg(ap, const char *))) {
		if (va_arg(ap, int))
			break;
	}
	va_end(ap);

	if (this_opt)
		die(_("%s: %s cannot be used with %s"), me, this_opt, base_opt);
}

static void verify_opt_mutually_compatible(const char *me, ...)
{
	const char *opt1, *opt2;
	va_list ap;

	va_start(ap, me);
	while ((opt1 = va_arg(ap, const char *))) {
		if (va_arg(ap, int))
			break;
	}
	if (opt1) {
		while ((opt2 = va_arg(ap, const char *))) {
			if (va_arg(ap, int))
				break;
		}
	}

	if (opt1 && opt2)
		die(_("%s: %s cannot be used with %s"),	me, opt1, opt2);
}

static void parse_args(int argc, const char **argv, struct replay_opts *opts)
{
	const char * const * usage_str = revert_or_cherry_pick_usage(opts);
	const char *me = action_name(opts);
	int reset = 0;
	int contin = 0;
	struct option options[] = {
		OPT_BOOLEAN(0, "reset", &reset, "forget the current operation"),
		OPT_BOOLEAN(0, "continue", &contin, "continue the current operation"),
		OPT_BOOLEAN('n', "no-commit", &opts->no_commit, "don't automatically commit"),
		OPT_BOOLEAN('e', "edit", &opts->edit, "edit the commit message"),
		OPT_NOOP_NOARG('r', NULL),
		OPT_BOOLEAN('s', "signoff", &opts->signoff, "add Signed-off-by:"),
		OPT_INTEGER('m', "mainline", &opts->mainline, "parent number"),
		OPT_RERERE_AUTOUPDATE(&opts->allow_rerere_auto),
		OPT_STRING(0, "strategy", &opts->strategy, "strategy", "merge strategy"),
		OPT_CALLBACK('X', "strategy-option", &opts, "option",
			"option for merge strategy", option_parse_x),
		OPT_END(),
		OPT_END(),
		OPT_END(),
	};

	if (opts->action == CHERRY_PICK) {
		struct option cp_extra[] = {
			OPT_BOOLEAN('x', NULL, &opts->record_origin, "append commit name"),
			OPT_BOOLEAN(0, "ff", &opts->allow_ff, "allow fast-forward"),
			OPT_END(),
		};
		if (parse_options_concat(options, ARRAY_SIZE(options), cp_extra))
			die(_("program error"));
	}

	opts->commit_argc = parse_options(argc, argv, NULL, options, usage_str,
					PARSE_OPT_KEEP_ARGV0 |
					PARSE_OPT_KEEP_UNKNOWN);

	/* Check for incompatible subcommands */
	verify_opt_mutually_compatible(me,
				"--reset", reset,
				"--continue", contin,
				NULL);

	/* Set the subcommand */
	if (reset)
		opts->subcommand = REPLAY_RESET;
	else if (contin)
		opts->subcommand = REPLAY_CONTINUE;
	else
		opts->subcommand = REPLAY_NONE;

	/* Check for incompatible command line arguments */
	if (opts->subcommand != REPLAY_NONE) {
		char *this_operation;
		if (opts->subcommand == REPLAY_RESET)
			this_operation = "--reset";
		else
			this_operation = "--continue";

		verify_opt_compatible(me, this_operation,
				"--no-commit", opts->no_commit,
				"--signoff", opts->signoff,
				"--mainline", opts->mainline,
				"--strategy", opts->strategy ? 1 : 0,
				"--strategy-option", opts->xopts ? 1 : 0,
				"-x", opts->record_origin,
				"--ff", opts->allow_ff,
				NULL);
	}

	else if (opts->commit_argc < 2)
		usage_with_options(usage_str, options);

	if (opts->allow_ff)
		verify_opt_compatible(me, "--ff",
				"--signoff", opts->signoff,
				"--no-commit", opts->no_commit,
				"-x", opts->record_origin,
				"--edit", opts->edit,
				NULL);
	opts->commit_argv = argv;
}

struct commit_message {
	char *parent_label;
	const char *label;
	const char *subject;
	char *reencoded_message;
	const char *message;
};

static int get_message(struct commit *commit, struct commit_message *out)
{
	const char *encoding;
	const char *abbrev, *subject;
	int abbrev_len, subject_len;
	char *q;

	if (!commit->buffer)
		return -1;
	encoding = get_encoding(commit->buffer);
	if (!encoding)
		encoding = "UTF-8";
	if (!git_commit_encoding)
		git_commit_encoding = "UTF-8";

	out->reencoded_message = NULL;
	out->message = commit->buffer;
	if (strcmp(encoding, git_commit_encoding))
		out->reencoded_message = reencode_string(commit->buffer,
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

static void write_cherry_pick_head(struct commit *commit)
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
	return lookup_tree((const unsigned char *)EMPTY_TREE_SHA1_BIN);
}

static int error_dirty_index(struct replay_opts *opts)
{
	if (read_cache_unmerged())
		return error_resolve_conflict(action_name(opts));

	/* Different translation strings for cherry-pick and revert */
	if (opts->action == CHERRY_PICK)
		error(_("Your local changes would be overwritten by cherry-pick."));
	else
		error(_("Your local changes would be overwritten by revert."));

	if (advice_commit_before_merge)
		advise(_("Commit your changes or stash them to proceed."));
	return -1;
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
			      unsigned char *head, struct strbuf *msgbuf,
			      struct replay_opts *opts)
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

	for (xopt = opts->xopts; xopt != opts->xopts + opts->xopts_nr; xopt++)
		parse_merge_opt(&o, *xopt);

	clean = merge_trees(&o,
			    head_tree,
			    next_tree, base_tree, &result);

	if (active_cache_changed &&
	    (write_cache(index_fd, active_cache, active_nr) ||
	     commit_locked_index(&index_lock)))
		/* TRANSLATORS: %s will be "revert" or "cherry-pick" */
		die(_("%s: Unable to write new index file"), action_name(opts));
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
static int run_git_commit(const char *defmsg, struct replay_opts *opts)
{
	/* 6 is max possible length of our args array including NULL */
	const char *args[6];
	int i = 0;

	args[i++] = "commit";
	args[i++] = "-n";
	if (opts->signoff)
		args[i++] = "-s";
	if (!opts->edit) {
		args[i++] = "-F";
		args[i++] = defmsg;
	}
	args[i] = NULL;

	return run_command_v_opt(args, RUN_GIT_CMD);
}

static int do_pick_commit(struct commit *commit, struct replay_opts *opts)
{
	unsigned char head[20];
	struct commit *base, *next, *parent;
	const char *base_label, *next_label;
	struct commit_message msg = { NULL, NULL, NULL, NULL, NULL };
	char *defmsg = NULL;
	struct strbuf msgbuf = STRBUF_INIT;
	int res;

	if (opts->no_commit) {
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
			return error(_("You do not have a valid HEAD"));
		if (index_differs_from("HEAD", 0))
			return error_dirty_index(opts);
	}
	discard_cache();

	if (!commit->parents) {
		parent = NULL;
	}
	else if (commit->parents->next) {
		/* Reverting or cherry-picking a merge commit */
		int cnt;
		struct commit_list *p;

		if (!opts->mainline)
			return error(_("Commit %s is a merge but no -m option was given."),
				sha1_to_hex(commit->object.sha1));

		for (cnt = 1, p = commit->parents;
		     cnt != opts->mainline && p;
		     cnt++)
			p = p->next;
		if (cnt != opts->mainline || !p)
			return error(_("Commit %s does not have parent %d"),
				sha1_to_hex(commit->object.sha1), opts->mainline);
		parent = p->item;
	} else if (0 < opts->mainline)
		return error(_("Mainline was specified but commit %s is not a merge."),
			sha1_to_hex(commit->object.sha1));
	else
		parent = commit->parents->item;

	if (opts->allow_ff && parent && !hashcmp(parent->object.sha1, head))
		return fast_forward_to(commit->object.sha1, head);

	if (parent && parse_commit(parent) < 0)
		/* TRANSLATORS: The first %s will be "revert" or
		   "cherry-pick", the second %s a SHA1 */
		return error(_("%s: cannot parse parent commit %s"),
			action_name(opts), sha1_to_hex(parent->object.sha1));

	if (get_message(commit, &msg) != 0)
		return error(_("Cannot get commit message for %s"),
			sha1_to_hex(commit->object.sha1));

	/*
	 * "commit" is an existing commit.  We would want to apply
	 * the difference it introduces since its first parent "prev"
	 * on top of the current HEAD if we are cherry-pick.  Or the
	 * reverse of it if we are revert.
	 */

	defmsg = git_pathdup("MERGE_MSG");

	if (opts->action == REVERT) {
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
		const char *p;

		base = parent;
		base_label = msg.parent_label;
		next = commit;
		next_label = msg.label;

		/*
		 * Append the commit log message to msgbuf; it starts
		 * after the tree, parent, author, committer
		 * information followed by "\n\n".
		 */
		p = strstr(msg.message, "\n\n");
		if (p) {
			p += 2;
			strbuf_addstr(&msgbuf, p);
		}

		if (opts->record_origin) {
			strbuf_addstr(&msgbuf, "(cherry picked from commit ");
			strbuf_addstr(&msgbuf, sha1_to_hex(commit->object.sha1));
			strbuf_addstr(&msgbuf, ")\n");
		}
		if (!opts->no_commit)
			write_cherry_pick_head(commit);
	}

	if (!opts->strategy || !strcmp(opts->strategy, "recursive") || opts->action == REVERT) {
		res = do_recursive_merge(base, next, base_label, next_label,
					 head, &msgbuf, opts);
		write_message(&msgbuf, defmsg);
	} else {
		struct commit_list *common = NULL;
		struct commit_list *remotes = NULL;

		write_message(&msgbuf, defmsg);

		commit_list_insert(base, &common);
		commit_list_insert(next, &remotes);
		res = try_merge_command(opts->strategy, opts->xopts_nr, opts->xopts,
					common, sha1_to_hex(head), remotes);
		free_commit_list(common);
		free_commit_list(remotes);
	}

	if (res) {
		error(opts->action == REVERT
		      ? _("could not revert %s... %s")
		      : _("could not apply %s... %s"),
		      find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV),
		      msg.subject);
		print_advice();
		rerere(opts->allow_rerere_auto);
	} else {
		if (!opts->no_commit)
			res = run_git_commit(defmsg, opts);
	}

	free_message(&msg);
	free(defmsg);

	return res;
}

static void prepare_revs(struct rev_info *revs, struct replay_opts *opts)
{
	int argc;

	init_revisions(revs, NULL);
	revs->no_walk = 1;
	if (opts->action != REVERT)
		revs->reverse = 1;

	argc = setup_revisions(opts->commit_argc, opts->commit_argv, revs, NULL);
	if (argc > 1)
		usage(*revert_or_cherry_pick_usage(opts));

	if (prepare_revision_walk(revs))
		die(_("revision walk setup failed"));

	if (!revs->commits)
		die(_("empty commit set passed"));
}

static void read_and_refresh_cache(struct replay_opts *opts)
{
	static struct lock_file index_lock;
	int index_fd = hold_locked_index(&index_lock, 0);
	if (read_index_preload(&the_index, NULL) < 0)
		die(_("git %s: failed to read the index"), action_name(opts));
	refresh_index(&the_index, REFRESH_QUIET|REFRESH_UNMERGED, NULL, NULL, NULL);
	if (the_index.cache_changed) {
		if (write_index(&the_index, index_fd) ||
		    commit_locked_index(&index_lock))
			die(_("git %s: failed to refresh the index"), action_name(opts));
	}
	rollback_lock_file(&index_lock);
}

/*
 * Append a commit to the end of the commit_list.
 *
 * next starts by pointing to the variable that holds the head of an
 * empty commit_list, and is updated to point to the "next" field of
 * the last item on the list as new commits are appended.
 *
 * Usage example:
 *
 *     struct commit_list *list;
 *     struct commit_list **next = &list;
 *
 *     next = commit_list_append(c1, next);
 *     next = commit_list_append(c2, next);
 *     assert(commit_list_count(list) == 2);
 *     return list;
 */
static struct commit_list **commit_list_append(struct commit *commit,
					       struct commit_list **next)
{
	struct commit_list *new = xmalloc(sizeof(struct commit_list));
	new->item = commit;
	*next = new;
	new->next = NULL;
	return &new->next;
}

static int format_todo(struct strbuf *buf, struct commit_list *todo_list,
		struct replay_opts *opts)
{
	struct commit_list *cur = NULL;
	struct commit_message msg = { NULL, NULL, NULL, NULL, NULL };
	const char *sha1_abbrev = NULL;
	const char *action_str = opts->action == REVERT ? "revert" : "pick";

	for (cur = todo_list; cur; cur = cur->next) {
		sha1_abbrev = find_unique_abbrev(cur->item->object.sha1, DEFAULT_ABBREV);
		if (get_message(cur->item, &msg))
			return error(_("Cannot get commit message for %s"), sha1_abbrev);
		strbuf_addf(buf, "%s %s %s\n", action_str, sha1_abbrev, msg.subject);
	}
	return 0;
}

static struct commit *parse_insn_line(char *start, struct replay_opts *opts)
{
	unsigned char commit_sha1[20];
	char sha1_abbrev[40];
	enum replay_action action;
	int insn_len = 0;
	char *p, *q;

	if (!prefixcmp(start, "pick ")) {
		action = CHERRY_PICK;
		insn_len = strlen("pick");
		p = start + insn_len + 1;
	} else if (!prefixcmp(start, "revert ")) {
		action = REVERT;
		insn_len = strlen("revert");
		p = start + insn_len + 1;
	} else
		return NULL;

	q = strchr(p, ' ');
	if (!q)
		return NULL;
	q++;

	strlcpy(sha1_abbrev, p, q - p);

	/*
	 * Verify that the action matches up with the one in
	 * opts; we don't support arbitrary instructions
	 */
	if (action != opts->action) {
		const char *action_str;
		action_str = action == REVERT ? "revert" : "cherry-pick";
		error(_("Cannot %s during a %s"), action_str, action_name(opts));
		return NULL;
	}

	if (get_sha1(sha1_abbrev, commit_sha1) < 0)
		return NULL;

	return lookup_commit_reference(commit_sha1);
}

static int parse_insn_buffer(char *buf, struct commit_list **todo_list,
			struct replay_opts *opts)
{
	struct commit_list **next = todo_list;
	struct commit *commit;
	char *p = buf;
	int i;

	for (i = 1; *p; i++) {
		commit = parse_insn_line(p, opts);
		if (!commit)
			return error(_("Could not parse line %d."), i);
		next = commit_list_append(commit, next);
		p = strchrnul(p, '\n');
		if (*p)
			p++;
	}
	if (!*todo_list)
		return error(_("No commits parsed."));
	return 0;
}

static void read_populate_todo(struct commit_list **todo_list,
			struct replay_opts *opts)
{
	const char *todo_file = git_path(SEQ_TODO_FILE);
	struct strbuf buf = STRBUF_INIT;
	int fd, res;

	fd = open(todo_file, O_RDONLY);
	if (fd < 0)
		die_errno(_("Could not open %s."), todo_file);
	if (strbuf_read(&buf, fd, 0) < 0) {
		close(fd);
		strbuf_release(&buf);
		die(_("Could not read %s."), todo_file);
	}
	close(fd);

	res = parse_insn_buffer(buf.buf, todo_list, opts);
	strbuf_release(&buf);
	if (res)
		die(_("Unusable instruction sheet: %s"), todo_file);
}

static int populate_opts_cb(const char *key, const char *value, void *data)
{
	struct replay_opts *opts = data;
	int error_flag = 1;

	if (!value)
		error_flag = 0;
	else if (!strcmp(key, "options.no-commit"))
		opts->no_commit = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.edit"))
		opts->edit = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.signoff"))
		opts->signoff = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.record-origin"))
		opts->record_origin = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.allow-ff"))
		opts->allow_ff = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.mainline"))
		opts->mainline = git_config_int(key, value);
	else if (!strcmp(key, "options.strategy"))
		git_config_string(&opts->strategy, key, value);
	else if (!strcmp(key, "options.strategy-option")) {
		ALLOC_GROW(opts->xopts, opts->xopts_nr + 1, opts->xopts_alloc);
		opts->xopts[opts->xopts_nr++] = xstrdup(value);
	} else
		return error(_("Invalid key: %s"), key);

	if (!error_flag)
		return error(_("Invalid value for %s: %s"), key, value);

	return 0;
}

static void read_populate_opts(struct replay_opts **opts_ptr)
{
	const char *opts_file = git_path(SEQ_OPTS_FILE);

	if (!file_exists(opts_file))
		return;
	if (git_config_from_file(populate_opts_cb, opts_file, *opts_ptr) < 0)
		die(_("Malformed options sheet: %s"), opts_file);
}

static void walk_revs_populate_todo(struct commit_list **todo_list,
				struct replay_opts *opts)
{
	struct rev_info revs;
	struct commit *commit;
	struct commit_list **next;

	prepare_revs(&revs, opts);

	next = todo_list;
	while ((commit = get_revision(&revs)))
		next = commit_list_append(commit, next);
}

static int create_seq_dir(void)
{
	const char *seq_dir = git_path(SEQ_DIR);

	if (file_exists(seq_dir))
		return error(_("%s already exists."), seq_dir);
	else if (mkdir(seq_dir, 0777) < 0)
		die_errno(_("Could not create sequencer directory '%s'."), seq_dir);
	return 0;
}

static void save_head(const char *head)
{
	const char *head_file = git_path(SEQ_HEAD_FILE);
	static struct lock_file head_lock;
	struct strbuf buf = STRBUF_INIT;
	int fd;

	fd = hold_lock_file_for_update(&head_lock, head_file, LOCK_DIE_ON_ERROR);
	strbuf_addf(&buf, "%s\n", head);
	if (write_in_full(fd, buf.buf, buf.len) < 0)
		die_errno(_("Could not write to %s."), head_file);
	if (commit_lock_file(&head_lock) < 0)
		die(_("Error wrapping up %s."), head_file);
}

static void save_todo(struct commit_list *todo_list, struct replay_opts *opts)
{
	const char *todo_file = git_path(SEQ_TODO_FILE);
	static struct lock_file todo_lock;
	struct strbuf buf = STRBUF_INIT;
	int fd;

	fd = hold_lock_file_for_update(&todo_lock, todo_file, LOCK_DIE_ON_ERROR);
	if (format_todo(&buf, todo_list, opts) < 0)
		die(_("Could not format %s."), todo_file);
	if (write_in_full(fd, buf.buf, buf.len) < 0) {
		strbuf_release(&buf);
		die_errno(_("Could not write to %s."), todo_file);
	}
	if (commit_lock_file(&todo_lock) < 0) {
		strbuf_release(&buf);
		die(_("Error wrapping up %s."), todo_file);
	}
	strbuf_release(&buf);
}

static void save_opts(struct replay_opts *opts)
{
	const char *opts_file = git_path(SEQ_OPTS_FILE);

	if (opts->no_commit)
		git_config_set_in_file(opts_file, "options.no-commit", "true");
	if (opts->edit)
		git_config_set_in_file(opts_file, "options.edit", "true");
	if (opts->signoff)
		git_config_set_in_file(opts_file, "options.signoff", "true");
	if (opts->record_origin)
		git_config_set_in_file(opts_file, "options.record-origin", "true");
	if (opts->allow_ff)
		git_config_set_in_file(opts_file, "options.allow-ff", "true");
	if (opts->mainline) {
		struct strbuf buf = STRBUF_INIT;
		strbuf_addf(&buf, "%d", opts->mainline);
		git_config_set_in_file(opts_file, "options.mainline", buf.buf);
		strbuf_release(&buf);
	}
	if (opts->strategy)
		git_config_set_in_file(opts_file, "options.strategy", opts->strategy);
	if (opts->xopts) {
		int i;
		for (i = 0; i < opts->xopts_nr; i++)
			git_config_set_multivar_in_file(opts_file,
							"options.strategy-option",
							opts->xopts[i], "^$", 0);
	}
}

static int pick_commits(struct commit_list *todo_list, struct replay_opts *opts)
{
	struct commit_list *cur;
	int res;

	setenv(GIT_REFLOG_ACTION, action_name(opts), 0);
	if (opts->allow_ff)
		assert(!(opts->signoff || opts->no_commit ||
				opts->record_origin || opts->edit));
	read_and_refresh_cache(opts);

	for (cur = todo_list; cur; cur = cur->next) {
		save_todo(cur, opts);
		res = do_pick_commit(cur->item, opts);
		if (res) {
			if (!cur->next)
				/*
				 * An error was encountered while
				 * picking the last commit; the
				 * sequencer state is useless now --
				 * the user simply needs to resolve
				 * the conflict and commit
				 */
				remove_sequencer_state(0);
			return res;
		}
	}

	/*
	 * Sequence of picks finished successfully; cleanup by
	 * removing the .git/sequencer directory
	 */
	remove_sequencer_state(1);
	return 0;
}

static int pick_revisions(struct replay_opts *opts)
{
	struct commit_list *todo_list = NULL;
	unsigned char sha1[20];

	read_and_refresh_cache(opts);

	/*
	 * Decide what to do depending on the arguments; a fresh
	 * cherry-pick should be handled differently from an existing
	 * one that is being continued
	 */
	if (opts->subcommand == REPLAY_RESET) {
		remove_sequencer_state(1);
		return 0;
	} else if (opts->subcommand == REPLAY_CONTINUE) {
		if (!file_exists(git_path(SEQ_TODO_FILE)))
			goto error;
		read_populate_opts(&opts);
		read_populate_todo(&todo_list, opts);

		/* Verify that the conflict has been resolved */
		if (!index_differs_from("HEAD", 0))
			todo_list = todo_list->next;
	} else {
		/*
		 * Start a new cherry-pick/ revert sequence; but
		 * first, make sure that an existing one isn't in
		 * progress
		 */

		walk_revs_populate_todo(&todo_list, opts);
		if (create_seq_dir() < 0) {
			error(_("A cherry-pick or revert is in progress."));
			advise(_("Use --continue to continue the operation"));
			advise(_("or --reset to forget about it"));
			return -1;
		}
		if (get_sha1("HEAD", sha1)) {
			if (opts->action == REVERT)
				return error(_("Can't revert as initial commit"));
			return error(_("Can't cherry-pick into empty head"));
		}
		save_head(sha1_to_hex(sha1));
		save_opts(opts);
	}
	return pick_commits(todo_list, opts);
error:
	return error(_("No %s in progress"), action_name(opts));
}

int cmd_revert(int argc, const char **argv, const char *prefix)
{
	struct replay_opts opts;
	int res;

	memset(&opts, 0, sizeof(opts));
	if (isatty(0))
		opts.edit = 1;
	opts.action = REVERT;
	git_config(git_default_config, NULL);
	parse_args(argc, argv, &opts);
	res = pick_revisions(&opts);
	if (res < 0)
		die(_("revert failed"));
	return res;
}

int cmd_cherry_pick(int argc, const char **argv, const char *prefix)
{
	struct replay_opts opts;
	int res;

	memset(&opts, 0, sizeof(opts));
	opts.action = CHERRY_PICK;
	git_config(git_default_config, NULL);
	parse_args(argc, argv, &opts);
	res = pick_revisions(&opts);
	if (res < 0)
		die(_("cherry-pick failed"));
	return res;
}
