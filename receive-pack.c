#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "run-command.h"
#include <sys/wait.h>

static const char receive_pack_usage[] = "git-receive-pack <git-dir>";

static const char *unpacker[] = { "unpack-objects", NULL };

static int report_status;

static char capabilities[] = "report-status";
static int capabilities_sent;

static int show_ref(const char *path, const unsigned char *sha1)
{
	if (capabilities_sent)
		packet_write(1, "%s %s\n", sha1_to_hex(sha1), path);
	else
		packet_write(1, "%s %s%c%s\n",
			     sha1_to_hex(sha1), path, 0, capabilities);
	capabilities_sent = 1;
	return 0;
}

static void write_head_info(void)
{
	for_each_ref(show_ref);
	if (!capabilities_sent)
		show_ref("capabilities^{}", null_sha1);

}

struct command {
	struct command *next;
	const char *error_string;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char ref_name[FLEX_ARRAY]; /* more */
};

static struct command *commands;

static int is_all_zeroes(const char *hex)
{
	int i;
	for (i = 0; i < 40; i++)
		if (*hex++ != '0')
			return 0;
	return 1;
}

static int verify_old_ref(const char *name, char *hex_contents)
{
	int fd, ret;
	char buffer[60];

	if (is_all_zeroes(hex_contents))
		return 0;
	fd = open(name, O_RDONLY);
	if (fd < 0)
		return -1;
	ret = read(fd, buffer, 40);
	close(fd);
	if (ret != 40)
		return -1;
	if (memcmp(buffer, hex_contents, 40))
		return -1;
	return 0;
}

static char update_hook[] = "hooks/update";

static int run_update_hook(const char *refname,
			   char *old_hex, char *new_hex)
{
	int code;

	if (access(update_hook, X_OK) < 0)
		return 0;
	code = run_command(update_hook, refname, old_hex, new_hex, NULL);
	switch (code) {
	case 0:
		return 0;
	case -ERR_RUN_COMMAND_FORK:
		return error("hook fork failed");
	case -ERR_RUN_COMMAND_EXEC:
		return error("hook execute failed");
	case -ERR_RUN_COMMAND_WAITPID:
		return error("waitpid failed");
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		return error("waitpid is confused");
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		return error("%s died of signal", update_hook);
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		return error("%s died strangely", update_hook);
	default:
		error("%s exited with error code %d", update_hook, -code);
		return -code;
	}
}

static int update(struct command *cmd)
{
	const char *name = cmd->ref_name;
	unsigned char *old_sha1 = cmd->old_sha1;
	unsigned char *new_sha1 = cmd->new_sha1;
	char new_hex[60], *old_hex, *lock_name;
	int newfd, namelen, written;

	cmd->error_string = NULL;
	if (!strncmp(name, "refs/", 5) && check_ref_format(name + 5)) {
		cmd->error_string = "funny refname";
		return error("refusing to create funny ref '%s' locally",
			     name);
	}

	namelen = strlen(name);
	lock_name = xmalloc(namelen + 10);
	memcpy(lock_name, name, namelen);
	memcpy(lock_name + namelen, ".lock", 6);

	strcpy(new_hex, sha1_to_hex(new_sha1));
	old_hex = sha1_to_hex(old_sha1);
	if (!has_sha1_file(new_sha1)) {
		cmd->error_string = "bad pack";
		return error("unpack should have generated %s, "
			     "but I can't find it!", new_hex);
	}
	safe_create_leading_directories(lock_name);

	newfd = open(lock_name, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (newfd < 0) {
		cmd->error_string = "can't lock";
		return error("unable to create %s (%s)",
			     lock_name, strerror(errno));
	}

	/* Write the ref with an ending '\n' */
	new_hex[40] = '\n';
	new_hex[41] = 0;
	written = write(newfd, new_hex, 41);
	/* Remove the '\n' again */
	new_hex[40] = 0;

	close(newfd);
	if (written != 41) {
		unlink(lock_name);
		cmd->error_string = "can't write";
		return error("unable to write %s", lock_name);
	}
	if (verify_old_ref(name, old_hex) < 0) {
		unlink(lock_name);
		cmd->error_string = "raced";
		return error("%s changed during push", name);
	}
	if (run_update_hook(name, old_hex, new_hex)) {
		unlink(lock_name);
		cmd->error_string = "hook declined";
		return error("hook declined to update %s", name);
	}
	else if (rename(lock_name, name) < 0) {
		unlink(lock_name);
		cmd->error_string = "can't rename";
		return error("unable to replace %s", name);
	}
	else {
		fprintf(stderr, "%s: %s -> %s\n", name, old_hex, new_hex);
		return 0;
	}
}

static char update_post_hook[] = "hooks/post-update";

static void run_update_post_hook(struct command *cmd)
{
	struct command *cmd_p;
	int argc;
	const char **argv;

	if (access(update_post_hook, X_OK) < 0)
		return;
	for (argc = 1, cmd_p = cmd; cmd_p; cmd_p = cmd_p->next) {
		if (cmd_p->error_string)
			continue;
		argc++;
	}
	argv = xmalloc(sizeof(*argv) * (1 + argc));
	argv[0] = update_post_hook;

	for (argc = 1, cmd_p = cmd; cmd_p; cmd_p = cmd_p->next) {
		char *p;
		if (cmd_p->error_string)
			continue;
		p = xmalloc(strlen(cmd_p->ref_name) + 1);
		strcpy(p, cmd_p->ref_name);
		argv[argc] = p;
		argc++;
	}
	argv[argc] = NULL;
	run_command_v_opt(argc, argv, RUN_COMMAND_NO_STDIO);
}

/*
 * This gets called after(if) we've successfully
 * unpacked the data payload.
 */
static void execute_commands(void)
{
	struct command *cmd = commands;

	while (cmd) {
		update(cmd);
		cmd = cmd->next;
	}
	run_update_post_hook(commands);
}

static void read_head_info(void)
{
	struct command **p = &commands;
	for (;;) {
		static char line[1000];
		unsigned char old_sha1[20], new_sha1[20];
		struct command *cmd;
		char *refname;
		int len, reflen;

		len = packet_read_line(0, line, sizeof(line));
		if (!len)
			break;
		if (line[len-1] == '\n')
			line[--len] = 0;
		if (len < 83 ||
		    line[40] != ' ' ||
		    line[81] != ' ' ||
		    get_sha1_hex(line, old_sha1) ||
		    get_sha1_hex(line + 41, new_sha1))
			die("protocol error: expected old/new/ref, got '%s'",
			    line);

		refname = line + 82;
		reflen = strlen(refname);
		if (reflen + 82 < len) {
			if (strstr(refname + reflen + 1, "report-status"))
				report_status = 1;
		}
		cmd = xmalloc(sizeof(struct command) + len - 80);
		memcpy(cmd->old_sha1, old_sha1, 20);
		memcpy(cmd->new_sha1, new_sha1, 20);
		memcpy(cmd->ref_name, line + 82, len - 81);
		cmd->error_string = "n/a (unpacker error)";
		cmd->next = NULL;
		*p = cmd;
		p = &cmd->next;
	}
}

static const char *unpack(int *error_code)
{
	int code = run_command_v_opt(1, unpacker, RUN_GIT_CMD);

	*error_code = 0;
	switch (code) {
	case 0:
		return NULL;
	case -ERR_RUN_COMMAND_FORK:
		return "unpack fork failed";
	case -ERR_RUN_COMMAND_EXEC:
		return "unpack execute failed";
	case -ERR_RUN_COMMAND_WAITPID:
		return "waitpid failed";
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		return "waitpid is confused";
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		return "unpacker died of signal";
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		return "unpacker died strangely";
	default:
		*error_code = -code;
		return "unpacker exited with error code";
	}
}

static void report(const char *unpack_status)
{
	struct command *cmd;
	packet_write(1, "unpack %s\n",
		     unpack_status ? unpack_status : "ok");
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!cmd->error_string)
			packet_write(1, "ok %s\n",
				     cmd->ref_name);
		else
			packet_write(1, "ng %s %s\n",
				     cmd->ref_name, cmd->error_string);
	}
	packet_flush(1);
}

int main(int argc, char **argv)
{
	int i;
	char *dir = NULL;

	argv++;
	for (i = 1; i < argc; i++) {
		char *arg = *argv++;

		if (*arg == '-') {
			/* Do flag handling here */
			usage(receive_pack_usage);
		}
		if (dir)
			usage(receive_pack_usage);
		dir = arg;
	}
	if (!dir)
		usage(receive_pack_usage);

	if(!enter_repo(dir, 0))
		die("'%s': unable to chdir or not a git archive", dir);

	write_head_info();

	/* EOF */
	packet_flush(1);

	read_head_info();
	if (commands) {
		int code;
		const char *unpack_status = unpack(&code);
		if (!unpack_status)
			execute_commands();
		if (report_status)
			report(unpack_status);
	}
	return 0;
}
