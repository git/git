/*
 * Builtin "git notes"
 *
 * Copyright (c) 2010 Johan Herland <johan@herland.net>
 *
 * Based on git-notes.sh by Johannes Schindelin,
 * and builtin-tag.c by Kristian Høgsberg and Carlos Rica.
 */

#include "cache.h"
#include "builtin.h"
#include "notes.h"
#include "blob.h"
#include "commit.h"
#include "refs.h"
#include "exec_cmd.h"
#include "run-command.h"
#include "parse-options.h"
#include "string-list.h"
#include "notes-merge.h"

static const char * const git_notes_usage[] = {
	"git notes [--ref <notes_ref>] [list [<object>]]",
	"git notes [--ref <notes_ref>] add [-f] [-m <msg> | -F <file> | (-c | -C) <object>] [<object>]",
	"git notes [--ref <notes_ref>] copy [-f] <from-object> <to-object>",
	"git notes [--ref <notes_ref>] append [-m <msg> | -F <file> | (-c | -C) <object>] [<object>]",
	"git notes [--ref <notes_ref>] edit [<object>]",
	"git notes [--ref <notes_ref>] show [<object>]",
	"git notes [--ref <notes_ref>] merge [-v | -q] [-s <strategy> ] <notes_ref>",
	"git notes merge --commit [-v | -q]",
	"git notes merge --abort [-v | -q]",
	"git notes [--ref <notes_ref>] remove [<object>...]",
	"git notes [--ref <notes_ref>] prune [-n | -v]",
	"git notes [--ref <notes_ref>] get-ref",
	NULL
};

static const char * const git_notes_list_usage[] = {
	"git notes [list [<object>]]",
	NULL
};

static const char * const git_notes_add_usage[] = {
	"git notes add [<options>] [<object>]",
	NULL
};

static const char * const git_notes_copy_usage[] = {
	"git notes copy [<options>] <from-object> <to-object>",
	"git notes copy --stdin [<from-object> <to-object>]...",
	NULL
};

static const char * const git_notes_append_usage[] = {
	"git notes append [<options>] [<object>]",
	NULL
};

static const char * const git_notes_edit_usage[] = {
	"git notes edit [<object>]",
	NULL
};

static const char * const git_notes_show_usage[] = {
	"git notes show [<object>]",
	NULL
};

static const char * const git_notes_merge_usage[] = {
	"git notes merge [<options>] <notes_ref>",
	"git notes merge --commit [<options>]",
	"git notes merge --abort [<options>]",
	NULL
};

static const char * const git_notes_remove_usage[] = {
	"git notes remove [<object>]",
	NULL
};

static const char * const git_notes_prune_usage[] = {
	"git notes prune [<options>]",
	NULL
};

static const char * const git_notes_get_ref_usage[] = {
	"git notes get-ref",
	NULL
};

static const char note_template[] =
	"\n"
	"#\n"
	"# Write/edit the notes for the following object:\n"
	"#\n";

struct msg_arg {
	int given;
	int use_editor;
	struct strbuf buf;
};

static int list_each_note(const unsigned char *object_sha1,
		const unsigned char *note_sha1, char *note_path,
		void *cb_data)
{
	printf("%s %s\n", sha1_to_hex(note_sha1), sha1_to_hex(object_sha1));
	return 0;
}

static void write_note_data(int fd, const unsigned char *sha1)
{
	unsigned long size;
	enum object_type type;
	char *buf = read_sha1_file(sha1, &type, &size);
	if (buf) {
		if (size)
			write_or_die(fd, buf, size);
		free(buf);
	}
}

static void write_commented_object(int fd, const unsigned char *object)
{
	const char *show_args[5] =
		{"show", "--stat", "--no-notes", sha1_to_hex(object), NULL};
	struct child_process show;
	struct strbuf buf = STRBUF_INIT;
	FILE *show_out;

	/* Invoke "git show --stat --no-notes $object" */
	memset(&show, 0, sizeof(show));
	show.argv = show_args;
	show.no_stdin = 1;
	show.out = -1;
	show.err = 0;
	show.git_cmd = 1;
	if (start_command(&show))
		die(_("unable to start 'show' for object '%s'"),
		    sha1_to_hex(object));

	/* Open the output as FILE* so strbuf_getline() can be used. */
	show_out = xfdopen(show.out, "r");
	if (show_out == NULL)
		die_errno(_("can't fdopen 'show' output fd"));

	/* Prepend "# " to each output line and write result to 'fd' */
	while (strbuf_getline(&buf, show_out, '\n') != EOF) {
		write_or_die(fd, "# ", 2);
		write_or_die(fd, buf.buf, buf.len);
		write_or_die(fd, "\n", 1);
	}
	strbuf_release(&buf);
	if (fclose(show_out))
		die_errno(_("failed to close pipe to 'show' for object '%s'"),
			  sha1_to_hex(object));
	if (finish_command(&show))
		die(_("failed to finish 'show' for object '%s'"),
		    sha1_to_hex(object));
}

static void create_note(const unsigned char *object, struct msg_arg *msg,
			int append_only, const unsigned char *prev,
			unsigned char *result)
{
	char *path = NULL;

	if (msg->use_editor || !msg->given) {
		int fd;

		/* write the template message before editing: */
		path = git_pathdup("NOTES_EDITMSG");
		fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			die_errno(_("could not create file '%s'"), path);

		if (msg->given)
			write_or_die(fd, msg->buf.buf, msg->buf.len);
		else if (prev && !append_only)
			write_note_data(fd, prev);
		write_or_die(fd, note_template, strlen(note_template));

		write_commented_object(fd, object);

		close(fd);
		strbuf_reset(&(msg->buf));

		if (launch_editor(path, &(msg->buf), NULL)) {
			die(_("Please supply the note contents using either -m" \
			    " or -F option"));
		}
		stripspace(&(msg->buf), 1);
	}

	if (prev && append_only) {
		/* Append buf to previous note contents */
		unsigned long size;
		enum object_type type;
		char *prev_buf = read_sha1_file(prev, &type, &size);

		strbuf_grow(&(msg->buf), size + 1);
		if (msg->buf.len && prev_buf && size)
			strbuf_insert(&(msg->buf), 0, "\n", 1);
		if (prev_buf && size)
			strbuf_insert(&(msg->buf), 0, prev_buf, size);
		free(prev_buf);
	}

	if (!msg->buf.len) {
		fprintf(stderr, _("Removing note for object %s\n"),
			sha1_to_hex(object));
		hashclr(result);
	} else {
		if (write_sha1_file(msg->buf.buf, msg->buf.len, blob_type, result)) {
			error(_("unable to write note object"));
			if (path)
				error(_("The note contents has been left in %s"),
				      path);
			exit(128);
		}
	}

	if (path) {
		unlink_or_warn(path);
		free(path);
	}
}

static int parse_msg_arg(const struct option *opt, const char *arg, int unset)
{
	struct msg_arg *msg = opt->value;

	strbuf_grow(&(msg->buf), strlen(arg) + 2);
	if (msg->buf.len)
		strbuf_addch(&(msg->buf), '\n');
	strbuf_addstr(&(msg->buf), arg);
	stripspace(&(msg->buf), 0);

	msg->given = 1;
	return 0;
}

static int parse_file_arg(const struct option *opt, const char *arg, int unset)
{
	struct msg_arg *msg = opt->value;

	if (msg->buf.len)
		strbuf_addch(&(msg->buf), '\n');
	if (!strcmp(arg, "-")) {
		if (strbuf_read(&(msg->buf), 0, 1024) < 0)
			die_errno(_("cannot read '%s'"), arg);
	} else if (strbuf_read_file(&(msg->buf), arg, 1024) < 0)
		die_errno(_("could not open or read '%s'"), arg);
	stripspace(&(msg->buf), 0);

	msg->given = 1;
	return 0;
}

static int parse_reuse_arg(const struct option *opt, const char *arg, int unset)
{
	struct msg_arg *msg = opt->value;
	char *buf;
	unsigned char object[20];
	enum object_type type;
	unsigned long len;

	if (msg->buf.len)
		strbuf_addch(&(msg->buf), '\n');

	if (get_sha1(arg, object))
		die(_("Failed to resolve '%s' as a valid ref."), arg);
	if (!(buf = read_sha1_file(object, &type, &len)) || !len) {
		free(buf);
		die(_("Failed to read object '%s'."), arg);;
	}
	strbuf_add(&(msg->buf), buf, len);
	free(buf);

	msg->given = 1;
	return 0;
}

static int parse_reedit_arg(const struct option *opt, const char *arg, int unset)
{
	struct msg_arg *msg = opt->value;
	msg->use_editor = 1;
	return parse_reuse_arg(opt, arg, unset);
}

void commit_notes(struct notes_tree *t, const char *msg)
{
	struct strbuf buf = STRBUF_INIT;
	unsigned char commit_sha1[20];

	if (!t)
		t = &default_notes_tree;
	if (!t->initialized || !t->ref || !*t->ref)
		die(_("Cannot commit uninitialized/unreferenced notes tree"));
	if (!t->dirty)
		return; /* don't have to commit an unchanged tree */

	/* Prepare commit message and reflog message */
	strbuf_addstr(&buf, msg);
	if (buf.buf[buf.len - 1] != '\n')
		strbuf_addch(&buf, '\n'); /* Make sure msg ends with newline */

	create_notes_commit(t, NULL, &buf, commit_sha1);
	strbuf_insert(&buf, 0, "notes: ", 7); /* commit message starts at index 7 */
	update_ref(buf.buf, t->ref, commit_sha1, NULL, 0, DIE_ON_ERR);

	strbuf_release(&buf);
}

combine_notes_fn parse_combine_notes_fn(const char *v)
{
	if (!strcasecmp(v, "overwrite"))
		return combine_notes_overwrite;
	else if (!strcasecmp(v, "ignore"))
		return combine_notes_ignore;
	else if (!strcasecmp(v, "concatenate"))
		return combine_notes_concatenate;
	else if (!strcasecmp(v, "cat_sort_uniq"))
		return combine_notes_cat_sort_uniq;
	else
		return NULL;
}

static int notes_rewrite_config(const char *k, const char *v, void *cb)
{
	struct notes_rewrite_cfg *c = cb;
	if (!prefixcmp(k, "notes.rewrite.") && !strcmp(k+14, c->cmd)) {
		c->enabled = git_config_bool(k, v);
		return 0;
	} else if (!c->mode_from_env && !strcmp(k, "notes.rewritemode")) {
		if (!v)
			config_error_nonbool(k);
		c->combine = parse_combine_notes_fn(v);
		if (!c->combine) {
			error(_("Bad notes.rewriteMode value: '%s'"), v);
			return 1;
		}
		return 0;
	} else if (!c->refs_from_env && !strcmp(k, "notes.rewriteref")) {
		/* note that a refs/ prefix is implied in the
		 * underlying for_each_glob_ref */
		if (!prefixcmp(v, "refs/notes/"))
			string_list_add_refs_by_glob(c->refs, v);
		else
			warning(_("Refusing to rewrite notes in %s"
				" (outside of refs/notes/)"), v);
		return 0;
	}

	return 0;
}


struct notes_rewrite_cfg *init_copy_notes_for_rewrite(const char *cmd)
{
	struct notes_rewrite_cfg *c = xmalloc(sizeof(struct notes_rewrite_cfg));
	const char *rewrite_mode_env = getenv(GIT_NOTES_REWRITE_MODE_ENVIRONMENT);
	const char *rewrite_refs_env = getenv(GIT_NOTES_REWRITE_REF_ENVIRONMENT);
	c->cmd = cmd;
	c->enabled = 1;
	c->combine = combine_notes_concatenate;
	c->refs = xcalloc(1, sizeof(struct string_list));
	c->refs->strdup_strings = 1;
	c->refs_from_env = 0;
	c->mode_from_env = 0;
	if (rewrite_mode_env) {
		c->mode_from_env = 1;
		c->combine = parse_combine_notes_fn(rewrite_mode_env);
		if (!c->combine)
			/* TRANSLATORS: The first %s is the name of the
			   environment variable, the second %s is its value */
			error(_("Bad %s value: '%s'"), GIT_NOTES_REWRITE_MODE_ENVIRONMENT,
					rewrite_mode_env);
	}
	if (rewrite_refs_env) {
		c->refs_from_env = 1;
		string_list_add_refs_from_colon_sep(c->refs, rewrite_refs_env);
	}
	git_config(notes_rewrite_config, c);
	if (!c->enabled || !c->refs->nr) {
		string_list_clear(c->refs, 0);
		free(c->refs);
		free(c);
		return NULL;
	}
	c->trees = load_notes_trees(c->refs);
	string_list_clear(c->refs, 0);
	free(c->refs);
	return c;
}

int copy_note_for_rewrite(struct notes_rewrite_cfg *c,
			  const unsigned char *from_obj, const unsigned char *to_obj)
{
	int ret = 0;
	int i;
	for (i = 0; c->trees[i]; i++)
		ret = copy_note(c->trees[i], from_obj, to_obj, 1, c->combine) || ret;
	return ret;
}

void finish_copy_notes_for_rewrite(struct notes_rewrite_cfg *c)
{
	int i;
	for (i = 0; c->trees[i]; i++) {
		commit_notes(c->trees[i], "Notes added by 'git notes copy'");
		free_notes(c->trees[i]);
	}
	free(c->trees);
	free(c);
}

static int notes_copy_from_stdin(int force, const char *rewrite_cmd)
{
	struct strbuf buf = STRBUF_INIT;
	struct notes_rewrite_cfg *c = NULL;
	struct notes_tree *t = NULL;
	int ret = 0;

	if (rewrite_cmd) {
		c = init_copy_notes_for_rewrite(rewrite_cmd);
		if (!c)
			return 0;
	} else {
		init_notes(NULL, NULL, NULL, 0);
		t = &default_notes_tree;
	}

	while (strbuf_getline(&buf, stdin, '\n') != EOF) {
		unsigned char from_obj[20], to_obj[20];
		struct strbuf **split;
		int err;

		split = strbuf_split(&buf, ' ');
		if (!split[0] || !split[1])
			die(_("Malformed input line: '%s'."), buf.buf);
		strbuf_rtrim(split[0]);
		strbuf_rtrim(split[1]);
		if (get_sha1(split[0]->buf, from_obj))
			die(_("Failed to resolve '%s' as a valid ref."), split[0]->buf);
		if (get_sha1(split[1]->buf, to_obj))
			die(_("Failed to resolve '%s' as a valid ref."), split[1]->buf);

		if (rewrite_cmd)
			err = copy_note_for_rewrite(c, from_obj, to_obj);
		else
			err = copy_note(t, from_obj, to_obj, force,
					combine_notes_overwrite);

		if (err) {
			error(_("Failed to copy notes from '%s' to '%s'"),
			      split[0]->buf, split[1]->buf);
			ret = 1;
		}

		strbuf_list_free(split);
	}

	if (!rewrite_cmd) {
		commit_notes(t, "Notes added by 'git notes copy'");
		free_notes(t);
	} else {
		finish_copy_notes_for_rewrite(c);
	}
	return ret;
}

static struct notes_tree *init_notes_check(const char *subcommand)
{
	struct notes_tree *t;
	init_notes(NULL, NULL, NULL, 0);
	t = &default_notes_tree;

	if (prefixcmp(t->ref, "refs/notes/"))
		die("Refusing to %s notes in %s (outside of refs/notes/)",
		    subcommand, t->ref);
	return t;
}

static int list(int argc, const char **argv, const char *prefix)
{
	struct notes_tree *t;
	unsigned char object[20];
	const unsigned char *note;
	int retval = -1;
	struct option options[] = {
		OPT_END()
	};

	if (argc)
		argc = parse_options(argc, argv, prefix, options,
				     git_notes_list_usage, 0);

	if (1 < argc) {
		error(_("too many parameters"));
		usage_with_options(git_notes_list_usage, options);
	}

	t = init_notes_check("list");
	if (argc) {
		if (get_sha1(argv[0], object))
			die(_("Failed to resolve '%s' as a valid ref."), argv[0]);
		note = get_note(t, object);
		if (note) {
			puts(sha1_to_hex(note));
			retval = 0;
		} else
			retval = error(_("No note found for object %s."),
				       sha1_to_hex(object));
	} else
		retval = for_each_note(t, 0, list_each_note, NULL);

	free_notes(t);
	return retval;
}

static int append_edit(int argc, const char **argv, const char *prefix);

static int add(int argc, const char **argv, const char *prefix)
{
	int retval = 0, force = 0;
	const char *object_ref;
	struct notes_tree *t;
	unsigned char object[20], new_note[20];
	char logmsg[100];
	const unsigned char *note;
	struct msg_arg msg = { 0, 0, STRBUF_INIT };
	struct option options[] = {
		{ OPTION_CALLBACK, 'm', "message", &msg, "msg",
			"note contents as a string", PARSE_OPT_NONEG,
			parse_msg_arg},
		{ OPTION_CALLBACK, 'F', "file", &msg, "file",
			"note contents in a file", PARSE_OPT_NONEG,
			parse_file_arg},
		{ OPTION_CALLBACK, 'c', "reedit-message", &msg, "object",
			"reuse and edit specified note object", PARSE_OPT_NONEG,
			parse_reedit_arg},
		{ OPTION_CALLBACK, 'C', "reuse-message", &msg, "object",
			"reuse specified note object", PARSE_OPT_NONEG,
			parse_reuse_arg},
		OPT__FORCE(&force, "replace existing notes"),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_notes_add_usage,
			     PARSE_OPT_KEEP_ARGV0);

	if (2 < argc) {
		error(_("too many parameters"));
		usage_with_options(git_notes_add_usage, options);
	}

	object_ref = argc > 1 ? argv[1] : "HEAD";

	if (get_sha1(object_ref, object))
		die(_("Failed to resolve '%s' as a valid ref."), object_ref);

	t = init_notes_check("add");
	note = get_note(t, object);

	if (note) {
		if (!force) {
			if (!msg.given) {
				/*
				 * Redirect to "edit" subcommand.
				 *
				 * We only end up here if none of -m/-F/-c/-C
				 * or -f are given. The original args are
				 * therefore still in argv[0-1].
				 */
				argv[0] = "edit";
				free_notes(t);
				return append_edit(argc, argv, prefix);
			}
			retval = error(_("Cannot add notes. Found existing notes "
				       "for object %s. Use '-f' to overwrite "
				       "existing notes"), sha1_to_hex(object));
			goto out;
		}
		fprintf(stderr, _("Overwriting existing notes for object %s\n"),
			sha1_to_hex(object));
	}

	create_note(object, &msg, 0, note, new_note);

	if (is_null_sha1(new_note))
		remove_note(t, object);
	else if (add_note(t, object, new_note, combine_notes_overwrite))
		die("BUG: combine_notes_overwrite failed");

	snprintf(logmsg, sizeof(logmsg), "Notes %s by 'git notes %s'",
		 is_null_sha1(new_note) ? "removed" : "added", "add");
	commit_notes(t, logmsg);
out:
	free_notes(t);
	strbuf_release(&(msg.buf));
	return retval;
}

static int copy(int argc, const char **argv, const char *prefix)
{
	int retval = 0, force = 0, from_stdin = 0;
	const unsigned char *from_note, *note;
	const char *object_ref;
	unsigned char object[20], from_obj[20];
	struct notes_tree *t;
	const char *rewrite_cmd = NULL;
	struct option options[] = {
		OPT__FORCE(&force, "replace existing notes"),
		OPT_BOOLEAN(0, "stdin", &from_stdin, "read objects from stdin"),
		OPT_STRING(0, "for-rewrite", &rewrite_cmd, "command",
			   "load rewriting config for <command> (implies "
			   "--stdin)"),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_notes_copy_usage,
			     0);

	if (from_stdin || rewrite_cmd) {
		if (argc) {
			error(_("too many parameters"));
			usage_with_options(git_notes_copy_usage, options);
		} else {
			return notes_copy_from_stdin(force, rewrite_cmd);
		}
	}

	if (argc < 2) {
		error(_("too few parameters"));
		usage_with_options(git_notes_copy_usage, options);
	}
	if (2 < argc) {
		error(_("too many parameters"));
		usage_with_options(git_notes_copy_usage, options);
	}

	if (get_sha1(argv[0], from_obj))
		die(_("Failed to resolve '%s' as a valid ref."), argv[0]);

	object_ref = 1 < argc ? argv[1] : "HEAD";

	if (get_sha1(object_ref, object))
		die(_("Failed to resolve '%s' as a valid ref."), object_ref);

	t = init_notes_check("copy");
	note = get_note(t, object);

	if (note) {
		if (!force) {
			retval = error(_("Cannot copy notes. Found existing "
				       "notes for object %s. Use '-f' to "
				       "overwrite existing notes"),
				       sha1_to_hex(object));
			goto out;
		}
		fprintf(stderr, _("Overwriting existing notes for object %s\n"),
			sha1_to_hex(object));
	}

	from_note = get_note(t, from_obj);
	if (!from_note) {
		retval = error(_("Missing notes on source object %s. Cannot "
			       "copy."), sha1_to_hex(from_obj));
		goto out;
	}

	if (add_note(t, object, from_note, combine_notes_overwrite))
		die("BUG: combine_notes_overwrite failed");
	commit_notes(t, "Notes added by 'git notes copy'");
out:
	free_notes(t);
	return retval;
}

static int append_edit(int argc, const char **argv, const char *prefix)
{
	const char *object_ref;
	struct notes_tree *t;
	unsigned char object[20], new_note[20];
	const unsigned char *note;
	char logmsg[100];
	const char * const *usage;
	struct msg_arg msg = { 0, 0, STRBUF_INIT };
	struct option options[] = {
		{ OPTION_CALLBACK, 'm', "message", &msg, "msg",
			"note contents as a string", PARSE_OPT_NONEG,
			parse_msg_arg},
		{ OPTION_CALLBACK, 'F', "file", &msg, "file",
			"note contents in a file", PARSE_OPT_NONEG,
			parse_file_arg},
		{ OPTION_CALLBACK, 'c', "reedit-message", &msg, "object",
			"reuse and edit specified note object", PARSE_OPT_NONEG,
			parse_reedit_arg},
		{ OPTION_CALLBACK, 'C', "reuse-message", &msg, "object",
			"reuse specified note object", PARSE_OPT_NONEG,
			parse_reuse_arg},
		OPT_END()
	};
	int edit = !strcmp(argv[0], "edit");

	usage = edit ? git_notes_edit_usage : git_notes_append_usage;
	argc = parse_options(argc, argv, prefix, options, usage,
			     PARSE_OPT_KEEP_ARGV0);

	if (2 < argc) {
		error(_("too many parameters"));
		usage_with_options(usage, options);
	}

	if (msg.given && edit)
		fprintf(stderr, _("The -m/-F/-c/-C options have been deprecated "
			"for the 'edit' subcommand.\n"
			"Please use 'git notes add -f -m/-F/-c/-C' instead.\n"));

	object_ref = 1 < argc ? argv[1] : "HEAD";

	if (get_sha1(object_ref, object))
		die(_("Failed to resolve '%s' as a valid ref."), object_ref);

	t = init_notes_check(argv[0]);
	note = get_note(t, object);

	create_note(object, &msg, !edit, note, new_note);

	if (is_null_sha1(new_note))
		remove_note(t, object);
	else if (add_note(t, object, new_note, combine_notes_overwrite))
		die("BUG: combine_notes_overwrite failed");

	snprintf(logmsg, sizeof(logmsg), "Notes %s by 'git notes %s'",
		 is_null_sha1(new_note) ? "removed" : "added", argv[0]);
	commit_notes(t, logmsg);
	free_notes(t);
	strbuf_release(&(msg.buf));
	return 0;
}

static int show(int argc, const char **argv, const char *prefix)
{
	const char *object_ref;
	struct notes_tree *t;
	unsigned char object[20];
	const unsigned char *note;
	int retval;
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_notes_show_usage,
			     0);

	if (1 < argc) {
		error(_("too many parameters"));
		usage_with_options(git_notes_show_usage, options);
	}

	object_ref = argc ? argv[0] : "HEAD";

	if (get_sha1(object_ref, object))
		die(_("Failed to resolve '%s' as a valid ref."), object_ref);

	t = init_notes_check("show");
	note = get_note(t, object);

	if (!note)
		retval = error(_("No note found for object %s."),
			       sha1_to_hex(object));
	else {
		const char *show_args[3] = {"show", sha1_to_hex(note), NULL};
		retval = execv_git_cmd(show_args);
	}
	free_notes(t);
	return retval;
}

static int merge_abort(struct notes_merge_options *o)
{
	int ret = 0;

	/*
	 * Remove .git/NOTES_MERGE_PARTIAL and .git/NOTES_MERGE_REF, and call
	 * notes_merge_abort() to remove .git/NOTES_MERGE_WORKTREE.
	 */

	if (delete_ref("NOTES_MERGE_PARTIAL", NULL, 0))
		ret += error("Failed to delete ref NOTES_MERGE_PARTIAL");
	if (delete_ref("NOTES_MERGE_REF", NULL, REF_NODEREF))
		ret += error("Failed to delete ref NOTES_MERGE_REF");
	if (notes_merge_abort(o))
		ret += error("Failed to remove 'git notes merge' worktree");
	return ret;
}

static int merge_commit(struct notes_merge_options *o)
{
	struct strbuf msg = STRBUF_INIT;
	unsigned char sha1[20], parent_sha1[20];
	struct notes_tree *t;
	struct commit *partial;
	struct pretty_print_context pretty_ctx;
	void *local_ref_to_free;
	int ret;

	/*
	 * Read partial merge result from .git/NOTES_MERGE_PARTIAL,
	 * and target notes ref from .git/NOTES_MERGE_REF.
	 */

	if (get_sha1("NOTES_MERGE_PARTIAL", sha1))
		die("Failed to read ref NOTES_MERGE_PARTIAL");
	else if (!(partial = lookup_commit_reference(sha1)))
		die("Could not find commit from NOTES_MERGE_PARTIAL.");
	else if (parse_commit(partial))
		die("Could not parse commit from NOTES_MERGE_PARTIAL.");

	if (partial->parents)
		hashcpy(parent_sha1, partial->parents->item->object.sha1);
	else
		hashclr(parent_sha1);

	t = xcalloc(1, sizeof(struct notes_tree));
	init_notes(t, "NOTES_MERGE_PARTIAL", combine_notes_overwrite, 0);

	o->local_ref = local_ref_to_free =
		resolve_refdup("NOTES_MERGE_REF", sha1, 0, NULL);
	if (!o->local_ref)
		die("Failed to resolve NOTES_MERGE_REF");

	if (notes_merge_commit(o, t, partial, sha1))
		die("Failed to finalize notes merge");

	/* Reuse existing commit message in reflog message */
	memset(&pretty_ctx, 0, sizeof(pretty_ctx));
	format_commit_message(partial, "%s", &msg, &pretty_ctx);
	strbuf_trim(&msg);
	strbuf_insert(&msg, 0, "notes: ", 7);
	update_ref(msg.buf, o->local_ref, sha1,
		   is_null_sha1(parent_sha1) ? NULL : parent_sha1,
		   0, DIE_ON_ERR);

	free_notes(t);
	strbuf_release(&msg);
	ret = merge_abort(o);
	free(local_ref_to_free);
	return ret;
}

static int merge(int argc, const char **argv, const char *prefix)
{
	struct strbuf remote_ref = STRBUF_INIT, msg = STRBUF_INIT;
	unsigned char result_sha1[20];
	struct notes_tree *t;
	struct notes_merge_options o;
	int do_merge = 0, do_commit = 0, do_abort = 0;
	int verbosity = 0, result;
	const char *strategy = NULL;
	struct option options[] = {
		OPT_GROUP("General options"),
		OPT__VERBOSITY(&verbosity),
		OPT_GROUP("Merge options"),
		OPT_STRING('s', "strategy", &strategy, "strategy",
			   "resolve notes conflicts using the given strategy "
			   "(manual/ours/theirs/union/cat_sort_uniq)"),
		OPT_GROUP("Committing unmerged notes"),
		{ OPTION_BOOLEAN, 0, "commit", &do_commit, NULL,
			"finalize notes merge by committing unmerged notes",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG },
		OPT_GROUP("Aborting notes merge resolution"),
		{ OPTION_BOOLEAN, 0, "abort", &do_abort, NULL,
			"abort notes merge",
			PARSE_OPT_NOARG | PARSE_OPT_NONEG },
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_notes_merge_usage, 0);

	if (strategy || do_commit + do_abort == 0)
		do_merge = 1;
	if (do_merge + do_commit + do_abort != 1) {
		error("cannot mix --commit, --abort or -s/--strategy");
		usage_with_options(git_notes_merge_usage, options);
	}

	if (do_merge && argc != 1) {
		error("Must specify a notes ref to merge");
		usage_with_options(git_notes_merge_usage, options);
	} else if (!do_merge && argc) {
		error("too many parameters");
		usage_with_options(git_notes_merge_usage, options);
	}

	init_notes_merge_options(&o);
	o.verbosity = verbosity + NOTES_MERGE_VERBOSITY_DEFAULT;

	if (do_abort)
		return merge_abort(&o);
	if (do_commit)
		return merge_commit(&o);

	o.local_ref = default_notes_ref();
	strbuf_addstr(&remote_ref, argv[0]);
	expand_notes_ref(&remote_ref);
	o.remote_ref = remote_ref.buf;

	if (strategy) {
		if (!strcmp(strategy, "manual"))
			o.strategy = NOTES_MERGE_RESOLVE_MANUAL;
		else if (!strcmp(strategy, "ours"))
			o.strategy = NOTES_MERGE_RESOLVE_OURS;
		else if (!strcmp(strategy, "theirs"))
			o.strategy = NOTES_MERGE_RESOLVE_THEIRS;
		else if (!strcmp(strategy, "union"))
			o.strategy = NOTES_MERGE_RESOLVE_UNION;
		else if (!strcmp(strategy, "cat_sort_uniq"))
			o.strategy = NOTES_MERGE_RESOLVE_CAT_SORT_UNIQ;
		else {
			error("Unknown -s/--strategy: %s", strategy);
			usage_with_options(git_notes_merge_usage, options);
		}
	}

	t = init_notes_check("merge");

	strbuf_addf(&msg, "notes: Merged notes from %s into %s",
		    remote_ref.buf, default_notes_ref());
	strbuf_add(&(o.commit_msg), msg.buf + 7, msg.len - 7); /* skip "notes: " */

	result = notes_merge(&o, t, result_sha1);

	if (result >= 0) /* Merge resulted (trivially) in result_sha1 */
		/* Update default notes ref with new commit */
		update_ref(msg.buf, default_notes_ref(), result_sha1, NULL,
			   0, DIE_ON_ERR);
	else { /* Merge has unresolved conflicts */
		/* Update .git/NOTES_MERGE_PARTIAL with partial merge result */
		update_ref(msg.buf, "NOTES_MERGE_PARTIAL", result_sha1, NULL,
			   0, DIE_ON_ERR);
		/* Store ref-to-be-updated into .git/NOTES_MERGE_REF */
		if (create_symref("NOTES_MERGE_REF", default_notes_ref(), NULL))
			die("Failed to store link to current notes ref (%s)",
			    default_notes_ref());
		printf("Automatic notes merge failed. Fix conflicts in %s and "
		       "commit the result with 'git notes merge --commit', or "
		       "abort the merge with 'git notes merge --abort'.\n",
		       git_path(NOTES_MERGE_WORKTREE));
	}

	free_notes(t);
	strbuf_release(&remote_ref);
	strbuf_release(&msg);
	return result < 0; /* return non-zero on conflicts */
}

#define IGNORE_MISSING 1

static int remove_one_note(struct notes_tree *t, const char *name, unsigned flag)
{
	int status;
	unsigned char sha1[20];
	if (get_sha1(name, sha1))
		return error(_("Failed to resolve '%s' as a valid ref."), name);
	status = remove_note(t, sha1);
	if (status)
		fprintf(stderr, _("Object %s has no note\n"), name);
	else
		fprintf(stderr, _("Removing note for object %s\n"), name);
	return (flag & IGNORE_MISSING) ? 0 : status;
}

static int remove_cmd(int argc, const char **argv, const char *prefix)
{
	unsigned flag = 0;
	int from_stdin = 0;
	struct option options[] = {
		OPT_BIT(0, "ignore-missing", &flag,
			"attempt to remove non-existent note is not an error",
			IGNORE_MISSING),
		OPT_BOOLEAN(0, "stdin", &from_stdin,
			    "read object names from the standard input"),
		OPT_END()
	};
	struct notes_tree *t;
	int retval = 0;

	argc = parse_options(argc, argv, prefix, options,
			     git_notes_remove_usage, 0);

	t = init_notes_check("remove");

	if (!argc && !from_stdin) {
		retval = remove_one_note(t, "HEAD", flag);
	} else {
		while (*argv) {
			retval |= remove_one_note(t, *argv, flag);
			argv++;
		}
	}
	if (from_stdin) {
		struct strbuf sb = STRBUF_INIT;
		while (strbuf_getwholeline(&sb, stdin, '\n') != EOF) {
			strbuf_rtrim(&sb);
			retval |= remove_one_note(t, sb.buf, flag);
		}
		strbuf_release(&sb);
	}
	if (!retval)
		commit_notes(t, "Notes removed by 'git notes remove'");
	free_notes(t);
	return retval;
}

static int prune(int argc, const char **argv, const char *prefix)
{
	struct notes_tree *t;
	int show_only = 0, verbose = 0;
	struct option options[] = {
		OPT__DRY_RUN(&show_only, "do not remove, show only"),
		OPT__VERBOSE(&verbose, "report pruned notes"),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_notes_prune_usage,
			     0);

	if (argc) {
		error(_("too many parameters"));
		usage_with_options(git_notes_prune_usage, options);
	}

	t = init_notes_check("prune");

	prune_notes(t, (verbose ? NOTES_PRUNE_VERBOSE : 0) |
		(show_only ? NOTES_PRUNE_VERBOSE|NOTES_PRUNE_DRYRUN : 0) );
	if (!show_only)
		commit_notes(t, "Notes removed by 'git notes prune'");
	free_notes(t);
	return 0;
}

static int get_ref(int argc, const char **argv, const char *prefix)
{
	struct option options[] = { OPT_END() };
	argc = parse_options(argc, argv, prefix, options,
			     git_notes_get_ref_usage, 0);

	if (argc) {
		error("too many parameters");
		usage_with_options(git_notes_get_ref_usage, options);
	}

	puts(default_notes_ref());
	return 0;
}

int cmd_notes(int argc, const char **argv, const char *prefix)
{
	int result;
	const char *override_notes_ref = NULL;
	struct option options[] = {
		OPT_STRING(0, "ref", &override_notes_ref, "notes_ref",
			   "use notes from <notes_ref>"),
		OPT_END()
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, git_notes_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (override_notes_ref) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_addstr(&sb, override_notes_ref);
		expand_notes_ref(&sb);
		setenv("GIT_NOTES_REF", sb.buf, 1);
		strbuf_release(&sb);
	}

	if (argc < 1 || !strcmp(argv[0], "list"))
		result = list(argc, argv, prefix);
	else if (!strcmp(argv[0], "add"))
		result = add(argc, argv, prefix);
	else if (!strcmp(argv[0], "copy"))
		result = copy(argc, argv, prefix);
	else if (!strcmp(argv[0], "append") || !strcmp(argv[0], "edit"))
		result = append_edit(argc, argv, prefix);
	else if (!strcmp(argv[0], "show"))
		result = show(argc, argv, prefix);
	else if (!strcmp(argv[0], "merge"))
		result = merge(argc, argv, prefix);
	else if (!strcmp(argv[0], "remove"))
		result = remove_cmd(argc, argv, prefix);
	else if (!strcmp(argv[0], "prune"))
		result = prune(argc, argv, prefix);
	else if (!strcmp(argv[0], "get-ref"))
		result = get_ref(argc, argv, prefix);
	else {
		result = error(_("Unknown subcommand: %s"), argv[0]);
		usage_with_options(git_notes_usage, options);
	}

	return result ? 1 : 0;
}
