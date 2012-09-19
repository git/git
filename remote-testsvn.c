#include "cache.h"
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

static int cmd_import(const char *line)
{
	int code;
	int dumpin_fd;
	unsigned int startrev = 0;
	struct argv_array svndump_argv = ARGV_ARRAY_INIT;
	struct child_process svndump_proc;

	if (dump_from_file) {
		dumpin_fd = open(url, O_RDONLY);
		if(dumpin_fd < 0)
			die_errno("Couldn't open svn dump file %s.", url);
	} else {
		memset(&svndump_proc, 0, sizeof(struct child_process));
		svndump_proc.out = -1;
		argv_array_push(&svndump_argv, "svnrdump");
		argv_array_push(&svndump_argv, "dump");
		argv_array_push(&svndump_argv, url);
		argv_array_pushf(&svndump_argv, "-r%u:HEAD", startrev);
		svndump_proc.argv = svndump_argv.argv;

		code = start_command(&svndump_proc);
		if (code)
			die("Unable to start %s, code %d", svndump_proc.argv[0], code);
		dumpin_fd = svndump_proc.out;
	}
	svndump_init_fd(dumpin_fd, STDIN_FILENO);
	svndump_read(url, private_ref);
	svndump_deinit();
	svndump_reset();

	close(dumpin_fd);
	if (!dump_from_file) {
		code = finish_command(&svndump_proc);
		if (code)
			warning("%s, returned %d", svndump_proc.argv[0], code);
		argv_array_clear(&svndump_argv);
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
		if (prefixcmp(batch_cmd->name, line->buf))
			die("Active %s batch interrupted by %s", batch_cmd->name, line->buf);
		/* buffer batch lines */
		string_list_append(&batchlines, line->buf);
		return 0;
	}

	for (p = input_command_list; p->name; p++) {
		if (!prefixcmp(line->buf, p->name) && (strlen(p->name) == line->len ||
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

int main(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT, url_sb = STRBUF_INIT,
			private_ref_sb = STRBUF_INIT;
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

	if (!prefixcmp(url_in, "file://")) {
		dump_from_file = 1;
		url = url_decode(url_in + sizeof("file://")-1);
	} else {
		dump_from_file = 0;
		end_url_with_slash(&url_sb, url_in);
		url = url_sb.buf;
	}

	strbuf_addf(&private_ref_sb, "refs/svn/%s/master", remote->name);
	private_ref = private_ref_sb.buf;

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
	return 0;
}
