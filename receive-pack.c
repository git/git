#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "run-command.h"
#include <sys/wait.h>

static const char receive_pack_usage[] = "git-receive-pack <git-dir>";

static const char unpacker[] = "git-unpack-objects";

static int show_ref(const char *path, const unsigned char *sha1)
{
	packet_write(1, "%s %s\n", sha1_to_hex(sha1), path);
	return 0;
}

static void write_head_info(void)
{
	for_each_ref(show_ref);
}

struct command {
	struct command *next;
	unsigned char updated;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char ref_name[0];
};

static struct command *commands = NULL;

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
		die("hook fork failed");
	case -ERR_RUN_COMMAND_EXEC:
		die("hook execute failed");
	case -ERR_RUN_COMMAND_WAITPID:
		die("waitpid failed");
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		die("waitpid is confused");
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		fprintf(stderr, "%s died of signal", update_hook);
		return -1;
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		die("%s died strangely", update_hook);
	default:
		error("%s exited with error code %d", update_hook, -code);
		return -code;
	}
}

static int update(const char *name,
		  unsigned char *old_sha1, unsigned char *new_sha1)
{
	char new_hex[60], *old_hex, *lock_name;
	int newfd, namelen, written;

	namelen = strlen(name);
	lock_name = xmalloc(namelen + 10);
	memcpy(lock_name, name, namelen);
	memcpy(lock_name + namelen, ".lock", 6);

	strcpy(new_hex, sha1_to_hex(new_sha1));
	old_hex = sha1_to_hex(old_sha1);
	if (!has_sha1_file(new_sha1))
		return error("unpack should have generated %s, "
			     "but I can't find it!", new_hex);

	safe_create_leading_directories(lock_name);

	newfd = open(lock_name, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (newfd < 0)
		return error("unable to create %s (%s)",
			     lock_name, strerror(errno));

	/* Write the ref with an ending '\n' */
	new_hex[40] = '\n';
	new_hex[41] = 0;
	written = write(newfd, new_hex, 41);
	/* Remove the '\n' again */
	new_hex[40] = 0;

	close(newfd);
	if (written != 41) {
		unlink(lock_name);
		return error("unable to write %s", lock_name);
	}
	if (verify_old_ref(name, old_hex) < 0) {
		unlink(lock_name);
		return error("%s changed during push", name);
	}
	if (run_update_hook(name, old_hex, new_hex)) {
		unlink(lock_name);
		return error("hook declined to update %s\n", name);
	}
	else if (rename(lock_name, name) < 0) {
		unlink(lock_name);
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
	char **argv;

	if (access(update_post_hook, X_OK) < 0)
		return;
	for (argc = 1, cmd_p = cmd; cmd_p; cmd_p = cmd_p->next) {
		if (!cmd_p->updated)
			continue;
		argc++;
	}
	argv = xmalloc(sizeof(*argv) * (1 + argc));
	argv[0] = update_post_hook;

	for (argc = 1, cmd_p = cmd; cmd_p; cmd_p = cmd_p->next) {
		if (!cmd_p->updated)
			continue;
		argv[argc] = xmalloc(strlen(cmd_p->ref_name) + 1);
		strcpy(argv[argc], cmd_p->ref_name);
		argc++;
	}
	argv[argc] = NULL;
	run_command_v(argc, argv);
}

/*
 * This gets called after(if) we've successfully
 * unpacked the data payload.
 */
static void execute_commands(void)
{
	struct command *cmd = commands;

	while (cmd) {
		cmd->updated = !update(cmd->ref_name,
				       cmd->old_sha1, cmd->new_sha1);
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
		int len;

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
			die("protocol error: expected old/new/ref, got '%s'", line);
		cmd = xmalloc(sizeof(struct command) + len - 80);
		memcpy(cmd->old_sha1, old_sha1, 20);
		memcpy(cmd->new_sha1, new_sha1, 20);
		memcpy(cmd->ref_name, line + 82, len - 81);
		cmd->next = NULL;
		*p = cmd;
		p = &cmd->next;
	}
}

static void unpack(void)
{
	int code = run_command(unpacker, NULL);
	switch (code) {
	case 0:
		return;
	case -ERR_RUN_COMMAND_FORK:
		die("unpack fork failed");
	case -ERR_RUN_COMMAND_EXEC:
		die("unpack execute failed");
	case -ERR_RUN_COMMAND_WAITPID:
		die("waitpid failed");
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		die("waitpid is confused");
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		die("%s died of signal", unpacker);
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		die("%s died strangely", unpacker);
	default:
		die("%s exited with error code %d", unpacker, -code);
	}
}

int main(int argc, char **argv)
{
	int i;
	const char *dir = NULL;

	argv++;
	for (i = 1; i < argc; i++) {
		const char *arg = *argv++;

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

	/* chdir to the directory. If that fails, try appending ".git" */
	if (chdir(dir) < 0) {
		if (chdir(mkpath("%s.git", dir)) < 0)
			die("unable to cd to %s", dir);
	}

	/* If we have a ".git" directory, chdir to it */
	chdir(".git");
	setenv("GIT_DIR", ".", 1);

	if (access("objects", X_OK) < 0 || access("refs/heads", X_OK) < 0)
		die("%s doesn't appear to be a git directory", dir);
	write_head_info();

	/* EOF */
	packet_flush(1);

	read_head_info();
	if (commands) {
		unpack();
		execute_commands();
	}
	return 0;
}
