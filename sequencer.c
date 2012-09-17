#include "cache.h"
#include "sequencer.h"
#include "dir.h"
#include "object.h"
#include "commit.h"
#include "tag.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "utf8.h"
#include "cache-tree.h"
#include "diff.h"
#include "revision.h"
#include "rerere.h"
#include "merge-recursive.h"
#include "refs.h"
#include "argv-array.h"

#define GIT_REFLOG_ACTION "GIT_REFLOG_ACTION"

const char sign_off_header[] = "Signed-off-by: ";

static void remove_sequencer_state(void)
{
	struct strbuf seq_dir = STRBUF_INIT;

	strbuf_addf(&seq_dir, "%s", git_path(SEQ_DIR));
	remove_dir_recursively(&seq_dir, 0);
	strbuf_release(&seq_dir);
}

static const char *action_name(const struct replay_opts *opts)
{
	return opts->action == REPLAY_REVERT ? "revert" : "cherry-pick";
}

static char *get_encoding(const char *message);

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

static void write_cherry_pick_head(struct commit *commit, const char *pseudoref)
{
	const char *filename;
	int fd;
	struct strbuf buf = STRBUF_INIT;

	strbuf_addf(&buf, "%s\n", sha1_to_hex(commit->object.sha1));

	filename = git_path("%s", pseudoref);
	fd = open(filename, O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		die_errno(_("Could not open '%s' for writing"), filename);
	if (write_in_full(fd, buf.buf, buf.len) != buf.len || close(fd))
		die_errno(_("Could not write to '%s'"), filename);
	strbuf_release(&buf);
}

static void print_advice(int show_hint, struct replay_opts *opts)
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

	if (show_hint) {
		if (opts->no_commit)
			advise(_("after resolving the conflicts, mark the corrected paths\n"
				 "with 'git add <paths>' or 'git rm <paths>'"));
		else
			advise(_("after resolving the conflicts, mark the corrected paths\n"
				 "with 'git add <paths>' or 'git rm <paths>'\n"
				 "and commit the result with 'git commit'"));
	}
}

static void write_message(struct strbuf *msgbuf, const char *filename)
{
	static struct lock_file msg_file;

	int msg_fd = hold_lock_file_for_update(&msg_file, filename,
					       LOCK_DIE_ON_ERROR);
	if (write_in_full(msg_fd, msgbuf->buf, msgbuf->len) < 0)
		die_errno(_("Could not write to %s"), filename);
	strbuf_release(msgbuf);
	if (commit_lock_file(&msg_file) < 0)
		die(_("Error wrapping up %s"), filename);
}

static struct tree *empty_tree(void)
{
	return lookup_tree(EMPTY_TREE_SHA1_BIN);
}

static int error_dirty_index(struct replay_opts *opts)
{
	if (read_cache_unmerged())
		return error_resolve_conflict(action_name(opts));

	/* Different translation strings for cherry-pick and revert */
	if (opts->action == REPLAY_PICK)
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

	if (opts->signoff)
		append_signoff(msgbuf, 0);

	if (!clean) {
		int i;
		strbuf_addstr(msgbuf, "\nConflicts:\n");
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

static int is_index_unchanged(void)
{
	unsigned char head_sha1[20];
	struct commit *head_commit;

	if (!resolve_ref_unsafe("HEAD", head_sha1, 1, NULL))
		return error(_("Could not resolve HEAD commit\n"));

	head_commit = lookup_commit(head_sha1);

	/*
	 * If head_commit is NULL, check_commit, called from
	 * lookup_commit, would have indicated that head_commit is not
	 * a commit object already.  parse_commit() will return failure
	 * without further complaints in such a case.  Otherwise, if
	 * the commit is invalid, parse_commit() will complain.  So
	 * there is nothing for us to say here.  Just return failure.
	 */
	if (parse_commit(head_commit))
		return -1;

	if (!active_cache_tree)
		active_cache_tree = cache_tree();

	if (!cache_tree_fully_valid(active_cache_tree))
		if (cache_tree_update(active_cache_tree, active_cache,
				  active_nr, 0))
			return error(_("Unable to update cache tree\n"));

	return !hashcmp(active_cache_tree->sha1, head_commit->tree->object.sha1);
}

/*
 * If we are cherry-pick, and if the merge did not result in
 * hand-editing, we will hit this commit and inherit the original
 * author date and name.
 * If we are revert, or if our cherry-pick results in a hand merge,
 * we had better say that the current user is responsible for that.
 */
static int run_git_commit(const char *defmsg, struct replay_opts *opts,
			  int allow_empty)
{
	struct argv_array array;
	int rc;

	argv_array_init(&array);
	argv_array_push(&array, "commit");
	argv_array_push(&array, "-n");

	if (opts->signoff)
		argv_array_push(&array, "-s");
	if (!opts->edit) {
		argv_array_push(&array, "-F");
		argv_array_push(&array, defmsg);
	}

	if (allow_empty)
		argv_array_push(&array, "--allow-empty");

	if (opts->allow_empty_message)
		argv_array_push(&array, "--allow-empty-message");

	rc = run_command_v_opt(array.argv, RUN_GIT_CMD);
	argv_array_clear(&array);
	return rc;
}

static int is_original_commit_empty(struct commit *commit)
{
	const unsigned char *ptree_sha1;

	if (parse_commit(commit))
		return error(_("Could not parse commit %s\n"),
			     sha1_to_hex(commit->object.sha1));
	if (commit->parents) {
		struct commit *parent = commit->parents->item;
		if (parse_commit(parent))
			return error(_("Could not parse parent commit %s\n"),
				sha1_to_hex(parent->object.sha1));
		ptree_sha1 = parent->tree->object.sha1;
	} else {
		ptree_sha1 = EMPTY_TREE_SHA1_BIN; /* commit is root */
	}

	return !hashcmp(ptree_sha1, commit->tree->object.sha1);
}

/*
 * Do we run "git commit" with "--allow-empty"?
 */
static int allow_empty(struct replay_opts *opts, struct commit *commit)
{
	int index_unchanged, empty_commit;

	/*
	 * Three cases:
	 *
	 * (1) we do not allow empty at all and error out.
	 *
	 * (2) we allow ones that were initially empty, but
	 * forbid the ones that become empty;
	 *
	 * (3) we allow both.
	 */
	if (!opts->allow_empty)
		return 0; /* let "git commit" barf as necessary */

	index_unchanged = is_index_unchanged();
	if (index_unchanged < 0)
		return index_unchanged;
	if (!index_unchanged)
		return 0; /* we do not have to say --allow-empty */

	if (opts->keep_redundant_commits)
		return 1;

	empty_commit = is_original_commit_empty(commit);
	if (empty_commit < 0)
		return empty_commit;
	if (!empty_commit)
		return 0;
	else
		return 1;
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

	if (opts->action == REPLAY_REVERT) {
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
	}

	if (!opts->strategy || !strcmp(opts->strategy, "recursive") || opts->action == REPLAY_REVERT) {
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

	/*
	 * If the merge was clean or if it failed due to conflict, we write
	 * CHERRY_PICK_HEAD for the subsequent invocation of commit to use.
	 * However, if the merge did not even start, then we don't want to
	 * write it at all.
	 */
	if (opts->action == REPLAY_PICK && !opts->no_commit && (res == 0 || res == 1))
		write_cherry_pick_head(commit, "CHERRY_PICK_HEAD");
	if (opts->action == REPLAY_REVERT && ((opts->no_commit && res == 0) || res == 1))
		write_cherry_pick_head(commit, "REVERT_HEAD");

	if (res) {
		error(opts->action == REPLAY_REVERT
		      ? _("could not revert %s... %s")
		      : _("could not apply %s... %s"),
		      find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV),
		      msg.subject);
		print_advice(res == 1, opts);
		rerere(opts->allow_rerere_auto);
	} else {
		int allow = allow_empty(opts, commit);
		if (allow < 0)
			return allow;
		if (!opts->no_commit)
			res = run_git_commit(defmsg, opts, allow);
	}

	free_message(&msg);
	free(defmsg);

	return res;
}

static void prepare_revs(struct replay_opts *opts)
{
	/*
	 * picking (but not reverting) ranges (but not individual revisions)
	 * should be done in reverse
	 */
	if (opts->action == REPLAY_PICK && !opts->revs->no_walk)
		opts->revs->reverse ^= 1;

	if (prepare_revision_walk(opts->revs))
		die(_("revision walk setup failed"));

	if (!opts->revs->commits)
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

static int format_todo(struct strbuf *buf, struct commit_list *todo_list,
		struct replay_opts *opts)
{
	struct commit_list *cur = NULL;
	const char *sha1_abbrev = NULL;
	const char *action_str = opts->action == REPLAY_REVERT ? "revert" : "pick";
	const char *subject;
	int subject_len;

	for (cur = todo_list; cur; cur = cur->next) {
		sha1_abbrev = find_unique_abbrev(cur->item->object.sha1, DEFAULT_ABBREV);
		subject_len = find_commit_subject(cur->item->buffer, &subject);
		strbuf_addf(buf, "%s %s %.*s\n", action_str, sha1_abbrev,
			subject_len, subject);
	}
	return 0;
}

static struct commit *parse_insn_line(char *bol, char *eol, struct replay_opts *opts)
{
	unsigned char commit_sha1[20];
	enum replay_action action;
	char *end_of_object_name;
	int saved, status, padding;

	if (!prefixcmp(bol, "pick")) {
		action = REPLAY_PICK;
		bol += strlen("pick");
	} else if (!prefixcmp(bol, "revert")) {
		action = REPLAY_REVERT;
		bol += strlen("revert");
	} else
		return NULL;

	/* Eat up extra spaces/ tabs before object name */
	padding = strspn(bol, " \t");
	if (!padding)
		return NULL;
	bol += padding;

	end_of_object_name = bol + strcspn(bol, " \t\n");
	saved = *end_of_object_name;
	*end_of_object_name = '\0';
	status = get_sha1(bol, commit_sha1);
	*end_of_object_name = saved;

	/*
	 * Verify that the action matches up with the one in
	 * opts; we don't support arbitrary instructions
	 */
	if (action != opts->action) {
		const char *action_str;
		action_str = action == REPLAY_REVERT ? "revert" : "cherry-pick";
		error(_("Cannot %s during a %s"), action_str, action_name(opts));
		return NULL;
	}

	if (status < 0)
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
		char *eol = strchrnul(p, '\n');
		commit = parse_insn_line(p, eol, opts);
		if (!commit)
			return error(_("Could not parse line %d."), i);
		next = commit_list_append(commit, next);
		p = *eol ? eol + 1 : eol;
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
		die_errno(_("Could not open %s"), todo_file);
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
	struct commit *commit;
	struct commit_list **next;

	prepare_revs(opts);

	next = todo_list;
	while ((commit = get_revision(opts->revs)))
		next = commit_list_append(commit, next);
}

static int create_seq_dir(void)
{
	const char *seq_dir = git_path(SEQ_DIR);

	if (file_exists(seq_dir)) {
		error(_("a cherry-pick or revert is already in progress"));
		advise(_("try \"git cherry-pick (--continue | --quit | --abort)\""));
		return -1;
	}
	else if (mkdir(seq_dir, 0777) < 0)
		die_errno(_("Could not create sequencer directory %s"), seq_dir);
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
		die_errno(_("Could not write to %s"), head_file);
	if (commit_lock_file(&head_lock) < 0)
		die(_("Error wrapping up %s."), head_file);
}

static int reset_for_rollback(const unsigned char *sha1)
{
	const char *argv[4];	/* reset --merge <arg> + NULL */
	argv[0] = "reset";
	argv[1] = "--merge";
	argv[2] = sha1_to_hex(sha1);
	argv[3] = NULL;
	return run_command_v_opt(argv, RUN_GIT_CMD);
}

static int rollback_single_pick(void)
{
	unsigned char head_sha1[20];

	if (!file_exists(git_path("CHERRY_PICK_HEAD")) &&
	    !file_exists(git_path("REVERT_HEAD")))
		return error(_("no cherry-pick or revert in progress"));
	if (read_ref_full("HEAD", head_sha1, 0, NULL))
		return error(_("cannot resolve HEAD"));
	if (is_null_sha1(head_sha1))
		return error(_("cannot abort from a branch yet to be born"));
	return reset_for_rollback(head_sha1);
}

static int sequencer_rollback(struct replay_opts *opts)
{
	const char *filename;
	FILE *f;
	unsigned char sha1[20];
	struct strbuf buf = STRBUF_INIT;

	filename = git_path(SEQ_HEAD_FILE);
	f = fopen(filename, "r");
	if (!f && errno == ENOENT) {
		/*
		 * There is no multiple-cherry-pick in progress.
		 * If CHERRY_PICK_HEAD or REVERT_HEAD indicates
		 * a single-cherry-pick in progress, abort that.
		 */
		return rollback_single_pick();
	}
	if (!f)
		return error(_("cannot open %s: %s"), filename,
						strerror(errno));
	if (strbuf_getline(&buf, f, '\n')) {
		error(_("cannot read %s: %s"), filename, ferror(f) ?
			strerror(errno) : _("unexpected end of file"));
		fclose(f);
		goto fail;
	}
	fclose(f);
	if (get_sha1_hex(buf.buf, sha1) || buf.buf[40] != '\0') {
		error(_("stored pre-cherry-pick HEAD file '%s' is corrupt"),
			filename);
		goto fail;
	}
	if (reset_for_rollback(sha1))
		goto fail;
	remove_sequencer_state();
	strbuf_release(&buf);
	return 0;
fail:
	strbuf_release(&buf);
	return -1;
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
		die_errno(_("Could not write to %s"), todo_file);
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
		if (res)
			return res;
	}

	/*
	 * Sequence of picks finished successfully; cleanup by
	 * removing the .git/sequencer directory
	 */
	remove_sequencer_state();
	return 0;
}

static int continue_single_pick(void)
{
	const char *argv[] = { "commit", NULL };

	if (!file_exists(git_path("CHERRY_PICK_HEAD")) &&
	    !file_exists(git_path("REVERT_HEAD")))
		return error(_("no cherry-pick or revert in progress"));
	return run_command_v_opt(argv, RUN_GIT_CMD);
}

static int sequencer_continue(struct replay_opts *opts)
{
	struct commit_list *todo_list = NULL;

	if (!file_exists(git_path(SEQ_TODO_FILE)))
		return continue_single_pick();
	read_populate_opts(&opts);
	read_populate_todo(&todo_list, opts);

	/* Verify that the conflict has been resolved */
	if (file_exists(git_path("CHERRY_PICK_HEAD")) ||
	    file_exists(git_path("REVERT_HEAD"))) {
		int ret = continue_single_pick();
		if (ret)
			return ret;
	}
	if (index_differs_from("HEAD", 0))
		return error_dirty_index(opts);
	todo_list = todo_list->next;
	return pick_commits(todo_list, opts);
}

static int single_pick(struct commit *cmit, struct replay_opts *opts)
{
	setenv(GIT_REFLOG_ACTION, action_name(opts), 0);
	return do_pick_commit(cmit, opts);
}

int sequencer_pick_revisions(struct replay_opts *opts)
{
	struct commit_list *todo_list = NULL;
	unsigned char sha1[20];

	if (opts->subcommand == REPLAY_NONE)
		assert(opts->revs);

	read_and_refresh_cache(opts);

	/*
	 * Decide what to do depending on the arguments; a fresh
	 * cherry-pick should be handled differently from an existing
	 * one that is being continued
	 */
	if (opts->subcommand == REPLAY_REMOVE_STATE) {
		remove_sequencer_state();
		return 0;
	}
	if (opts->subcommand == REPLAY_ROLLBACK)
		return sequencer_rollback(opts);
	if (opts->subcommand == REPLAY_CONTINUE)
		return sequencer_continue(opts);

	/*
	 * If we were called as "git cherry-pick <commit>", just
	 * cherry-pick/revert it, set CHERRY_PICK_HEAD /
	 * REVERT_HEAD, and don't touch the sequencer state.
	 * This means it is possible to cherry-pick in the middle
	 * of a cherry-pick sequence.
	 */
	if (opts->revs->cmdline.nr == 1 &&
	    opts->revs->cmdline.rev->whence == REV_CMD_REV &&
	    opts->revs->no_walk &&
	    !opts->revs->cmdline.rev->flags) {
		struct commit *cmit;
		if (prepare_revision_walk(opts->revs))
			die(_("revision walk setup failed"));
		cmit = get_revision(opts->revs);
		if (!cmit || get_revision(opts->revs))
			die("BUG: expected exactly one commit from walk");
		return single_pick(cmit, opts);
	}

	/*
	 * Start a new cherry-pick/ revert sequence; but
	 * first, make sure that an existing one isn't in
	 * progress
	 */

	walk_revs_populate_todo(&todo_list, opts);
	if (create_seq_dir() < 0)
		return -1;
	if (get_sha1("HEAD", sha1)) {
		if (opts->action == REPLAY_REVERT)
			return error(_("Can't revert as initial commit"));
		return error(_("Can't cherry-pick into empty head"));
	}
	save_head(sha1_to_hex(sha1));
	save_opts(opts);
	return pick_commits(todo_list, opts);
}

static int ends_rfc2822_footer(struct strbuf *sb, int ignore_footer)
{
	int ch;
	int hit = 0;
	int i, j, k;
	int len = sb->len - ignore_footer;
	int first = 1;
	const char *buf = sb->buf;

	for (i = len - 1; i > 0; i--) {
		if (hit && buf[i] == '\n')
			break;
		hit = (buf[i] == '\n');
	}

	while (i < len - 1 && buf[i] == '\n')
		i++;

	for (; i < len; i = k) {
		for (k = i; k < len && buf[k] != '\n'; k++)
			; /* do nothing */
		k++;

		if ((buf[k] == ' ' || buf[k] == '\t') && !first)
			continue;

		first = 0;

		for (j = 0; i + j < len; j++) {
			ch = buf[i + j];
			if (ch == ':')
				break;
			if (isalnum(ch) ||
			    (ch == '-'))
				continue;
			return 0;
		}
	}
	return 1;
}

void append_signoff(struct strbuf *msgbuf, int ignore_footer)
{
	struct strbuf sob = STRBUF_INIT;
	int i;

	strbuf_addstr(&sob, sign_off_header);
	strbuf_addstr(&sob, fmt_name(getenv("GIT_COMMITTER_NAME"),
				getenv("GIT_COMMITTER_EMAIL")));
	strbuf_addch(&sob, '\n');
	for (i = msgbuf->len - 1 - ignore_footer; i > 0 && msgbuf->buf[i - 1] != '\n'; i--)
		; /* do nothing */
	if (prefixcmp(msgbuf->buf + i, sob.buf)) {
		if (!i || !ends_rfc2822_footer(msgbuf, ignore_footer))
			strbuf_splice(msgbuf, msgbuf->len - ignore_footer, 0, "\n", 1);
		strbuf_splice(msgbuf, msgbuf->len - ignore_footer, 0, sob.buf, sob.len);
	}
	strbuf_release(&sob);
}
