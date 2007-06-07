#include "cache.h"
#include "builtin.h"
#include "object.h"
#include "commit.h"
#include "tag.h"
#include "wt-status.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "utf8.h"

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

static const char *revert_usage = "git-revert [--edit | --no-edit] [-n] <commit-ish>";

static const char *cherry_pick_usage = "git-cherry-pick [--edit] [-n] [-r] [-x] <commit-ish>";

static int edit;
static int replay;
enum { REVERT, CHERRY_PICK } action;
static int no_commit;
static struct commit *commit;
static int needed_deref;

static const char *me;

#define GIT_REFLOG_ACTION "GIT_REFLOG_ACTION"

static void parse_options(int argc, const char **argv)
{
	const char *usage_str = action == REVERT ?
		revert_usage : cherry_pick_usage;
	unsigned char sha1[20];
	const char *arg;
	int i;

	if (argc < 2)
		usage(usage_str);

	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "-n") || !strcmp(arg, "--no-commit"))
			no_commit = 1;
		else if (!strcmp(arg, "-e") || !strcmp(arg, "--edit"))
			edit = 1;
		else if (!strcmp(arg, "--no-edit"))
			edit = 0;
		else if (!strcmp(arg, "-x") || !strcmp(arg, "--i-really-want-"
				"to-expose-my-private-commit-object-name"))
			replay = 0;
		else if (strcmp(arg, "-r"))
			usage(usage_str);
	}
	if (i != argc - 1)
		usage(usage_str);
	arg = argv[argc - 1];
	if (get_sha1(arg, sha1))
		die ("Cannot find '%s'", arg);
	commit = (struct commit *)parse_object(sha1);
	if (!commit)
		die ("Could not find %s", sha1_to_hex(sha1));
	if (commit->object.type == OBJ_TAG) {
		commit = (struct commit *)
			deref_tag((struct object *)commit, arg, strlen(arg));
		needed_deref = 1;
	}
	if (commit->object.type != OBJ_COMMIT)
		die ("'%s' does not point to a commit", arg);
}

static char *get_oneline(const char *message)
{
	char *result;
	const char *p = message, *abbrev, *eol;
	int abbrev_len, oneline_len;

	if (!p)
		die ("Could not read commit message of %s",
				sha1_to_hex(commit->object.sha1));
	while (*p && (*p != '\n' || p[1] != '\n'))
		p++;

	if (*p) {
		p += 2;
		for (eol = p + 1; *eol && *eol != '\n'; eol++)
			; /* do nothing */
	} else
		eol = p;
	abbrev = find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV);
	abbrev_len = strlen(abbrev);
	oneline_len = eol - p;
	result = xmalloc(abbrev_len + 5 + oneline_len);
	memcpy(result, abbrev, abbrev_len);
	memcpy(result + abbrev_len, "... ", 4);
	memcpy(result + abbrev_len + 4, p, oneline_len);
	result[abbrev_len + 4 + oneline_len] = '\0';
	return result;
}

static char *get_encoding(const char *message)
{
	const char *p = message, *eol;

	if (!p)
		die ("Could not read commit message of %s",
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

struct lock_file msg_file;
static int msg_fd;

static void add_to_msg(const char *string)
{
	int len = strlen(string);
	if (write_in_full(msg_fd, string, len) < 0)
		die ("Could not write to MERGE_MSG");
}

static void add_message_to_msg(const char *message)
{
	const char *p = message;
	while (*p && (*p != '\n' || p[1] != '\n'))
		p++;

	if (!*p)
		add_to_msg(sha1_to_hex(commit->object.sha1));

	p += 2;
	add_to_msg(p);
	return;
}

static void set_author_ident_env(const char *message)
{
	const char *p = message;
	if (!p)
		die ("Could not read commit message of %s",
				sha1_to_hex(commit->object.sha1));
	while (*p && *p != '\n') {
		const char *eol;

		for (eol = p; *eol && *eol != '\n'; eol++)
			; /* do nothing */
		if (!prefixcmp(p, "author ")) {
			char *line, *pend, *email, *timestamp;

			p += 7;
			line = xmalloc(eol + 1 - p);
			memcpy(line, p, eol - p);
			line[eol - p] = '\0';
			email = strchr(line, '<');
			if (!email)
				die ("Could not extract author email from %s",
					sha1_to_hex(commit->object.sha1));
			if (email == line)
				pend = line;
			else
				for (pend = email; pend != line + 1 &&
						isspace(pend[-1]); pend--);
					; /* do nothing */
			*pend = '\0';
			email++;
			timestamp = strchr(email, '>');
			if (!timestamp)
				die ("Could not extract author email from %s",
					sha1_to_hex(commit->object.sha1));
			*timestamp = '\0';
			for (timestamp++; *timestamp && isspace(*timestamp);
					timestamp++)
				; /* do nothing */
			setenv("GIT_AUTHOR_NAME", line, 1);
			setenv("GIT_AUTHOR_EMAIL", email, 1);
			setenv("GIT_AUTHOR_DATE", timestamp, 1);
			free(line);
			return;
		}
		p = eol;
		if (*p == '\n')
			p++;
	}
	die ("No author information found in %s",
			sha1_to_hex(commit->object.sha1));
}

static int merge_recursive(const char *base_sha1,
		const char *head_sha1, const char *head_name,
		const char *next_sha1, const char *next_name)
{
	char buffer[256];
	const char *argv[6];

	sprintf(buffer, "GITHEAD_%s", head_sha1);
	setenv(buffer, head_name, 1);
	sprintf(buffer, "GITHEAD_%s", next_sha1);
	setenv(buffer, next_name, 1);

	/*
	 * This three way merge is an interesting one.  We are at
	 * $head, and would want to apply the change between $commit
	 * and $prev on top of us (when reverting), or the change between
	 * $prev and $commit on top of us (when cherry-picking or replaying).
	 */
	argv[0] = "merge-recursive";
	argv[1] = base_sha1;
	argv[2] = "--";
	argv[3] = head_sha1;
	argv[4] = next_sha1;
	argv[5] = NULL;

	return run_command_v_opt(argv, RUN_COMMAND_NO_STDIN | RUN_GIT_CMD);
}

static int revert_or_cherry_pick(int argc, const char **argv)
{
	unsigned char head[20];
	struct commit *base, *next;
	int i;
	char *oneline, *reencoded_message = NULL;
	const char *message, *encoding;
	const char *defmsg = xstrdup(git_path("MERGE_MSG"));

	git_config(git_default_config);
	me = action == REVERT ? "revert" : "cherry-pick";
	setenv(GIT_REFLOG_ACTION, me, 0);
	parse_options(argc, argv);

	/* this is copied from the shell script, but it's never triggered... */
	if (action == REVERT && replay)
		die("revert is incompatible with replay");

	if (no_commit) {
		/*
		 * We do not intend to commit immediately.  We just want to
		 * merge the differences in.
		 */
		if (write_tree(head, 0, NULL))
			die ("Your index file is unmerged.");
	} else {
		struct wt_status s;

		if (get_sha1("HEAD", head))
			die ("You do not have a valid HEAD");
		wt_status_prepare(&s);
		if (s.commitable || s.workdir_dirty)
			die ("Dirty index: cannot %s", me);
		discard_cache();
	}

	if (!commit->parents)
		die ("Cannot %s a root commit", me);
	if (commit->parents->next)
		die ("Cannot %s a multi-parent commit.", me);
	if (!(message = commit->buffer))
		die ("Cannot get commit message for %s",
				sha1_to_hex(commit->object.sha1));

	/*
	 * "commit" is an existing commit.  We would want to apply
	 * the difference it introduces since its first parent "prev"
	 * on top of the current HEAD if we are cherry-pick.  Or the
	 * reverse of it if we are revert.
	 */

	msg_fd = hold_lock_file_for_update(&msg_file, defmsg, 1);

	encoding = get_encoding(message);
	if (!encoding)
		encoding = "utf-8";
	if (!git_commit_encoding)
		git_commit_encoding = "utf-8";
	if ((reencoded_message = reencode_string(message,
					git_commit_encoding, encoding)))
		message = reencoded_message;

	oneline = get_oneline(message);

	if (action == REVERT) {
		char *oneline_body = strchr(oneline, ' ');

		base = commit;
		next = commit->parents->item;
		add_to_msg("Revert \"");
		add_to_msg(oneline_body + 1);
		add_to_msg("\"\n\nThis reverts commit ");
		add_to_msg(sha1_to_hex(commit->object.sha1));
		add_to_msg(".\n");
	} else {
		base = commit->parents->item;
		next = commit;
		set_author_ident_env(message);
		add_message_to_msg(message);
		if (!replay) {
			add_to_msg("(cherry picked from commit ");
			add_to_msg(sha1_to_hex(commit->object.sha1));
			add_to_msg(")\n");
		}
	}
	if (needed_deref) {
		add_to_msg("(original 'git ");
		add_to_msg(me);
		add_to_msg("' arguments: ");
		for (i = 0; i < argc; i++) {
			if (i)
				add_to_msg(" ");
			add_to_msg(argv[i]);
		}
		add_to_msg(")\n");
	}

	if (merge_recursive(sha1_to_hex(base->object.sha1),
				sha1_to_hex(head), "HEAD",
				sha1_to_hex(next->object.sha1), oneline) ||
			write_tree(head, 0, NULL)) {
		add_to_msg("\nConflicts:\n\n");
		read_cache();
		for (i = 0; i < active_nr;) {
			struct cache_entry *ce = active_cache[i++];
			if (ce_stage(ce)) {
				add_to_msg("\t");
				add_to_msg(ce->name);
				add_to_msg("\n");
				while (i < active_nr && !strcmp(ce->name,
						active_cache[i]->name))
					i++;
			}
		}
		if (close(msg_fd) || commit_lock_file(&msg_file) < 0)
			die ("Error wrapping up %s", defmsg);
		fprintf(stderr, "Automatic %s failed.  "
			"After resolving the conflicts,\n"
			"mark the corrected paths with 'git-add <paths>'\n"
			"and commit the result.\n", me);
		if (action == CHERRY_PICK) {
			fprintf(stderr, "When commiting, use the option "
				"'-c %s' to retain authorship and message.\n",
				find_unique_abbrev(commit->object.sha1,
					DEFAULT_ABBREV));
		}
		exit(1);
	}
	if (close(msg_fd) || commit_lock_file(&msg_file) < 0)
		die ("Error wrapping up %s", defmsg);
	fprintf(stderr, "Finished one %s.\n", me);

	/*
	 *
	 * If we are cherry-pick, and if the merge did not result in
	 * hand-editing, we will hit this commit and inherit the original
	 * author date and name.
	 * If we are revert, or if our cherry-pick results in a hand merge,
	 * we had better say that the current user is responsible for that.
	 */

	if (!no_commit) {
		if (edit)
			return execl_git_cmd("commit", "-n", NULL);
		else
			return execl_git_cmd("commit", "-n", "-F", defmsg, NULL);
	}
	if (reencoded_message)
		free(reencoded_message);

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
	replay = 1;
	action = CHERRY_PICK;
	return revert_or_cherry_pick(argc, argv);
}
