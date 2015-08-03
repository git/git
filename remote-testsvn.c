#include "cache.h"
#include "refs.h"
#include "remote.h"
#include "strbuf.h"
#include "url.h"
#include "exec_cmd.h"
#include "run-command.h"
#include "vcs-svn/svndump.h"
#include "notes.h"
#include "argv-array.h"

static const char *url;
static int dump_from_file;
static const char *private_ref;
static const char *remote_ref = "refs/heads/master";
static const char *marksfilename, *notes_ref;
struct rev_note { unsigned int rev_nr; };

static int cmd_capabilities(const char *line);
static int cmd_import(const char *line);
static int cmd_list(const char *line);

typedef int (*input_command_handler)(const char *);
struct input_command_entry {
	const char *name;
	input_command_handler fn;
	unsigned char batchable;	/* whether the command starts or is part of a batch */
};

static const struct input_command_entry input_command_list[] = {
	{ "capabilities", cmd_capabilities, 0 },
	{ "import", cmd_import, 1 },
	{ "list", cmd_list, 0 },
	{ NULL, NULL }
};

static int cmd_capabilities(const char *line)
{
	printf("import\n");
	printf("bidi-import\n");
	printf("refspec %s:%s\n\n", remote_ref, private_ref);
	fflush(stdout);
	return 0;
}

static void terminate_batch(void)
{
	/* terminate a current batch's fast-import stream */
	printf("done\n");
	fflush(stdout);
}

/* NOTE: 'ref' refers to a git reference, while 'rev' refers to a svn revision. */
static char *read_ref_note(const unsigned char sha1[20])
{
	const unsigned char *note_sha1;
	char *msg = NULL;
	unsigned long msglen;
	enum object_type type;

	init_notes(NULL, notes_ref, NULL, 0);
	if (!(note_sha1 = get_note(NULL, sha1)))
		return NULL;	/* note tree not found */
	if (!(msg = read_sha1_file(note_sha1, &type, &msglen)))
		error("Empty notes tree. %s", notes_ref);
	else if (!msglen || type != OBJ_BLOB) {
		error("Note contains unusable content. "
			"Is something else using this notes tree? %s", notes_ref);
		free(msg);
		msg = NULL;
	}
	free_notes(NULL);
	return msg;
}

static int parse_rev_note(const char *msg, struct rev_note *res)
{
	const char *key, *value, *end;
	size_t len;

	while (*msg) {
		end = strchrnul(msg, '\n');
		len = end - msg;

		key = "Revision-number: ";
		if (starts_with(msg, key)) {
			long i;
			char *end;
			value = msg + strlen(key);
			i = strtol(value, &end, 0);
			if (end == value || i < 0 || i > UINT32_MAX)
				return -1;
			res->rev_nr = i;
			return 0;
		}
		msg += len + 1;
	}
	/* didn't find it */
	return -1;
}

static int note2mark_cb(const unsigned char *object_sha1,
		const unsigned char *note_sha1, char *note_path,
		void *cb_data)
{
	FILE *file = (FILE *)cb_data;
	char *msg;
	unsigned long msglen;
	enum object_type type;
	struct rev_note note;

	if (!(msg = read_sha1_file(note_sha1, &type, &msglen)) ||
			!msglen || type != OBJ_BLOB) {
		free(msg);
		return 1;
	}
	if (parse_rev_note(msg, &note))
		return 2;
	if (fprintf(file, ":%d %s\n", note.rev_nr, sha1_to_hex(object_sha1)) < 1)
		return 3;
	return 0;
}

static void regenerate_marks(void)
{
	int ret;
	FILE *marksfile = fopen(marksfilename, "w+");

	if (!marksfile)
		die_errno("Couldn't create mark file %s.", marksfilename);
	ret = for_each_note(NULL, 0, note2mark_cb, marksfile);
	if (ret)
		die("Regeneration of marks failed, returned %d.", ret);
	fclose(marksfile);
}

static void check_or_regenerate_marks(int latestrev)
{
	FILE *marksfile;
	struct strbuf sb = STRBUF_INIT;
	struct strbuf line = STRBUF_INIT;
	int found = 0;

	if (latestrev < 1)
		return;

	init_notes(NULL, notes_ref, NULL, 0);
	marksfile = fopen(marksfilename, "r");
	if (!marksfile) {
		regenerate_marks();
		marksfile = fopen(marksfilename, "r");
		if (!marksfile)
			die_errno("cannot read marks file %s!", marksfilename);
		fclose(marksfile);
	} else {
		strbuf_addf(&sb, ":%d ", latestrev);
		while (strbuf_getline(&line, marksfile, '\n') != EOF) {
			if (starts_with(line.buf, sb.buf)) {
				found++;
				break;
			}
		}
		fclose(marksfile);
		if (!found)
			regenerate_marks();
	}
	free_notes(NULL);
	strbuf_release(&sb);
	strbuf_release(&line);
}

static int cmd_import(const char *line)
{
	int code;
	int dumpin_fd;
	char *note_msg;
	unsigned char head_sha1[20];
	unsigned int startrev;
	struct child_process svndump_proc = CHILD_PROCESS_INIT;
	const char *command = "svnrdump";

	if (read_ref(private_ref, head_sha1))
		startrev = 0;
	else {
		note_msg = read_ref_note(head_sha1);
		if(note_msg == NULL) {
			warning("No note found for %s.", private_ref);
			startrev = 0;
		} else {
			struct rev_note note = { 0 };
			if (parse_rev_note(note_msg, &note))
				die("Revision number couldn't be parsed from note.");
			startrev = note.rev_nr + 1;
			free(note_msg);
		}
	}
	check_or_regenerate_marks(startrev - 1);

	if (dump_from_file) {
		dumpin_fd = open(url, O_RDONLY);
		if(dumpin_fd < 0)
			die_errno("Couldn't open svn dump file %s.", url);
	} else {
		svndump_proc.out = -1;
		argv_array_push(&svndump_proc.args, command);
		argv_array_push(&svndump_proc.args, "dump");
		argv_array_push(&svndump_proc.args, url);
		argv_array_pushf(&svndump_proc.args, "-r%u:HEAD", startrev);

		code = start_command(&svndump_proc);
		if (code)
			die("Unable to start %s, code %d", command, code);
		dumpin_fd = svndump_proc.out;
	}
	/* setup marks file import/export */
	printf("feature import-marks-if-exists=%s\n"
			"feature export-marks=%s\n", marksfilename, marksfilename);

	svndump_init_fd(dumpin_fd, STDIN_FILENO);
	svndump_read(url, private_ref, notes_ref);
	svndump_deinit();
	svndump_reset();

	close(dumpin_fd);
	if (!dump_from_file) {
		code = finish_command(&svndump_proc);
		if (code)
			warning("%s, returned %d", command, code);
	}

	return 0;
}

static int cmd_list(const char *line)
{
	printf("? %s\n\n", remote_ref);
	fflush(stdout);
	return 0;
}

static int do_command(struct strbuf *line)
{
	const struct input_command_entry *p = input_command_list;
	static struct string_list batchlines = STRING_LIST_INIT_DUP;
	static const struct input_command_entry *batch_cmd;
	/*
	 * commands can be grouped together in a batch.
	 * Batches are ended by \n. If no batch is active the program ends.
	 * During a batch all lines are buffered and passed to the handler function
	 * when the batch is terminated.
	 */
	if (line->len == 0) {
		if (batch_cmd) {
			struct string_list_item *item;
			for_each_string_list_item(item, &batchlines)
				batch_cmd->fn(item->string);
			terminate_batch();
			batch_cmd = NULL;
			string_list_clear(&batchlines, 0);
			return 0;	/* end of the batch, continue reading other commands. */
		}
		return 1;	/* end of command stream, quit */
	}
	if (batch_cmd) {
		if (!starts_with(batch_cmd->name, line->buf))
			die("Active %s batch interrupted by %s", batch_cmd->name, line->buf);
		/* buffer batch lines */
		string_list_append(&batchlines, line->buf);
		return 0;
	}

	for (p = input_command_list; p->name; p++) {
		if (starts_with(line->buf, p->name) && (strlen(p->name) == line->len ||
				line->buf[strlen(p->name)] == ' ')) {
			if (p->batchable) {
				batch_cmd = p;
				string_list_append(&batchlines, line->buf);
				return 0;
			}
			return p->fn(line->buf);
		}
	}
	die("Unknown command '%s'\n", line->buf);
	return 0;
}

int main(int argc, char **argv)
{
	struct strbuf buf = STRBUF_INIT, url_sb = STRBUF_INIT,
			private_ref_sb = STRBUF_INIT, marksfilename_sb = STRBUF_INIT,
			notes_ref_sb = STRBUF_INIT;
	static struct remote *remote;
	const char *url_in;

	git_extract_argv0_path(argv[0]);
	setup_git_directory();
	if (argc < 2 || argc > 3) {
		usage("git-remote-svn <remote-name> [<url>]");
		return 1;
	}

	remote = remote_get(argv[1]);
	url_in = (argc == 3) ? argv[2] : remote->url[0];

	if (starts_with(url_in, "file://")) {
		dump_from_file = 1;
		url = url_decode(url_in + sizeof("file://")-1);
	} else {
		dump_from_file = 0;
		end_url_with_slash(&url_sb, url_in);
		url = url_sb.buf;
	}

	strbuf_addf(&private_ref_sb, "refs/svn/%s/master", remote->name);
	private_ref = private_ref_sb.buf;

	strbuf_addf(&notes_ref_sb, "refs/notes/%s/revs", remote->name);
	notes_ref = notes_ref_sb.buf;

	strbuf_addf(&marksfilename_sb, "%s/info/fast-import/remote-svn/%s.marks",
		get_git_dir(), remote->name);
	marksfilename = marksfilename_sb.buf;

	while (1) {
		if (strbuf_getline(&buf, stdin, '\n') == EOF) {
			if (ferror(stdin))
				die("Error reading command stream");
			else
				die("Unexpected end of command stream");
		}
		if (do_command(&buf))
			break;
		strbuf_reset(&buf);
	}

	strbuf_release(&buf);
	strbuf_release(&url_sb);
	strbuf_release(&private_ref_sb);
	strbuf_release(&notes_ref_sb);
	strbuf_release(&marksfilename_sb);
	return 0;
}
