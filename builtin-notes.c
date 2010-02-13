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

static const char * const git_notes_usage[] = {
	"git notes edit [-m <msg> | -F <file>] [<object>]",
	"git notes show [<object>]",
	NULL
};

static const char note_template[] =
	"\n"
	"#\n"
	"# Write/edit the notes for the following object:\n"
	"#\n";

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
		die("unable to start 'show' for object '%s'",
		    sha1_to_hex(object));

	/* Open the output as FILE* so strbuf_getline() can be used. */
	show_out = xfdopen(show.out, "r");
	if (show_out == NULL)
		die_errno("can't fdopen 'show' output fd");

	/* Prepend "# " to each output line and write result to 'fd' */
	while (strbuf_getline(&buf, show_out, '\n') != EOF) {
		write_or_die(fd, "# ", 2);
		write_or_die(fd, buf.buf, buf.len);
		write_or_die(fd, "\n", 1);
	}
	strbuf_release(&buf);
	if (fclose(show_out))
		die_errno("failed to close pipe to 'show' for object '%s'",
			  sha1_to_hex(object));
	if (finish_command(&show))
		die("failed to finish 'show' for object '%s'",
		    sha1_to_hex(object));
}

static void create_note(const unsigned char *object,
			struct strbuf *buf,
			int skip_editor,
			const unsigned char *prev,
			unsigned char *result)
{
	char *path = NULL;

	if (!skip_editor) {
		int fd;

		/* write the template message before editing: */
		path = git_pathdup("NOTES_EDITMSG");
		fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
		if (fd < 0)
			die_errno("could not create file '%s'", path);

		if (prev)
			write_note_data(fd, prev);
		write_or_die(fd, note_template, strlen(note_template));

		write_commented_object(fd, object);

		close(fd);

		if (launch_editor(path, buf, NULL)) {
			die("Please supply the note contents using either -m" \
			    " or -F option");
		}
	}

	stripspace(buf, 1);

	if (!buf->len) {
		fprintf(stderr, "Removing note for object %s\n",
			sha1_to_hex(object));
		hashclr(result);
	} else {
		if (write_sha1_file(buf->buf, buf->len, blob_type, result)) {
			error("unable to write note object");
			if (path)
				error("The note contents has been left in %s",
				      path);
			exit(128);
		}
	}

	if (path) {
		unlink_or_warn(path);
		free(path);
	}
}

struct msg_arg {
	int given;
	struct strbuf buf;
};

static int parse_msg_arg(const struct option *opt, const char *arg, int unset)
{
	struct msg_arg *msg = opt->value;

	if (!arg)
		return -1;
	if (msg->buf.len)
		strbuf_addstr(&(msg->buf), "\n\n");
	strbuf_addstr(&(msg->buf), arg);
	msg->given = 1;
	return 0;
}

int commit_notes(struct notes_tree *t, const char *msg)
{
	struct commit_list *parent;
	unsigned char tree_sha1[20], prev_commit[20], new_commit[20];
	struct strbuf buf = STRBUF_INIT;

	if (!t)
		t = &default_notes_tree;
	if (!t->initialized || !t->ref || !*t->ref)
		die("Cannot commit uninitialized/unreferenced notes tree");

	/* Prepare commit message and reflog message */
	strbuf_addstr(&buf, "notes: "); /* commit message starts at index 7 */
	strbuf_addstr(&buf, msg);
	if (buf.buf[buf.len - 1] != '\n')
		strbuf_addch(&buf, '\n'); /* Make sure msg ends with newline */

	/* Convert notes tree to tree object */
	if (write_notes_tree(t, tree_sha1))
		die("Failed to write current notes tree to database");

	/* Create new commit for the tree object */
	if (!read_ref(t->ref, prev_commit)) { /* retrieve parent commit */
		parent = xmalloc(sizeof(*parent));
		parent->item = lookup_commit(prev_commit);
		parent->next = NULL;
	} else {
		hashclr(prev_commit);
		parent = NULL;
	}
	if (commit_tree(buf.buf + 7, tree_sha1, parent, new_commit, NULL))
		die("Failed to commit notes tree to database");

	/* Update notes ref with new commit */
	update_ref(buf.buf, t->ref, new_commit, prev_commit, 0, DIE_ON_ERR);

	strbuf_release(&buf);
	return 0;
}

int cmd_notes(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	struct notes_tree *t;
	unsigned char object[20], new_note[20];
	const unsigned char *note;
	const char *object_ref, *logmsg;

	int edit = 0, show = 0;
	const char *msgfile = NULL;
	struct msg_arg msg = { 0, STRBUF_INIT };
	struct option options[] = {
		OPT_GROUP("Notes edit options"),
		OPT_CALLBACK('m', NULL, &msg, "msg",
			     "note contents as a string", parse_msg_arg),
		OPT_FILENAME('F', NULL, &msgfile, "note contents in a file"),
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_notes_usage, 0);

	if (argc && !strcmp(argv[0], "edit"))
		edit = 1;
	else if (argc && !strcmp(argv[0], "show"))
		show = 1;

	if (edit + show != 1)
		usage_with_options(git_notes_usage, options);

	object_ref = argc == 2 ? argv[1] : "HEAD";
	if (argc > 2) {
		error("too many parameters");
		usage_with_options(git_notes_usage, options);
	}

	if (get_sha1(object_ref, object))
		die("Failed to resolve '%s' as a valid ref.", object_ref);

	init_notes(NULL, NULL, NULL, 0);
	t = &default_notes_tree;

	if (prefixcmp(t->ref, "refs/notes/"))
		die("Refusing to %s notes in %s (outside of refs/notes/)",
		    edit ? "edit" : "show", t->ref);

	note = get_note(t, object);

	/* show command */

	if (show && !note) {
		error("No note found for object %s.", sha1_to_hex(object));
		return 1;
	} else if (show) {
		const char *show_args[3] = {"show", sha1_to_hex(note), NULL};
		return execv_git_cmd(show_args);
	}

	/* edit command */

	if (msg.given || msgfile) {
		if (msg.given && msgfile) {
			error("mixing -m and -F options is not allowed.");
			usage_with_options(git_notes_usage, options);
		}
		if (msg.given)
			strbuf_addbuf(&buf, &(msg.buf));
		else {
			if (!strcmp(msgfile, "-")) {
				if (strbuf_read(&buf, 0, 1024) < 0)
					die_errno("cannot read '%s'", msgfile);
			} else {
				if (strbuf_read_file(&buf, msgfile, 1024) < 0)
					die_errno("could not open or read '%s'",
						msgfile);
			}
		}
	}

	create_note(object, &buf, msg.given || msgfile, note, new_note);
	if (is_null_sha1(new_note)) {
		remove_note(t, object);
		logmsg = "Note removed by 'git notes edit'";
	} else {
		add_note(t, object, new_note, combine_notes_overwrite);
		logmsg = "Note added by 'git notes edit'";
	}
	commit_notes(t, logmsg);

	free_notes(t);
	strbuf_release(&buf);
	return 0;
}
