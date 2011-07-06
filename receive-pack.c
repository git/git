#include "cache.h"
#include "pack.h"
#include "refs.h"
#include "pkt-line.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "commit.h"
#include "object.h"

static const char receive_pack_usage[] = "git-receive-pack <git-dir>";

static int deny_non_fast_forwards = 0;
static int receive_fsck_objects;
static int receive_unpack_limit = -1;
static int transfer_unpack_limit = -1;
static int unpack_limit = 100;
static int report_status;

static char capabilities[] = " report-status delete-refs ";
static int capabilities_sent;

static int receive_pack_config(const char *var, const char *value, void *cb)
{
	if (strcmp(var, "receive.denynonfastforwards") == 0) {
		deny_non_fast_forwards = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.unpacklimit") == 0) {
		receive_unpack_limit = git_config_int(var, value);
		return 0;
	}

	if (strcmp(var, "transfer.unpacklimit") == 0) {
		transfer_unpack_limit = git_config_int(var, value);
		return 0;
	}

	if (strcmp(var, "receive.fsckobjects") == 0) {
		receive_fsck_objects = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, cb);
}

static int show_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
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
	for_each_ref(show_ref, NULL);
	if (!capabilities_sent)
		show_ref("capabilities^{}", null_sha1, 0, NULL);

}

struct command {
	struct command *next;
	const char *error_string;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char ref_name[FLEX_ARRAY]; /* more */
};

static struct command *commands;

static const char pre_receive_hook[] = "hooks/pre-receive";
static const char post_receive_hook[] = "hooks/post-receive";

static int hook_status(int code, const char *hook_name)
{
	switch (code) {
	case 0:
		return 0;
	case -ERR_RUN_COMMAND_FORK:
		return error("hook fork failed");
	case -ERR_RUN_COMMAND_EXEC:
		return error("hook execute failed");
	case -ERR_RUN_COMMAND_PIPE:
		return error("hook pipe failed");
	case -ERR_RUN_COMMAND_WAITPID:
		return error("waitpid failed");
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		return error("waitpid is confused");
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		return error("%s died of signal", hook_name);
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		return error("%s died strangely", hook_name);
	default:
		error("%s exited with error code %d", hook_name, -code);
		return -code;
	}
}

static int run_hook(const char *hook_name)
{
	static char buf[sizeof(commands->old_sha1) * 2 + PATH_MAX + 4];
	struct command *cmd;
	struct child_process proc;
	const char *argv[2];
	int have_input = 0, code;

	for (cmd = commands; !have_input && cmd; cmd = cmd->next) {
		if (!cmd->error_string)
			have_input = 1;
	}

	if (!have_input || access(hook_name, X_OK) < 0)
		return 0;

	argv[0] = hook_name;
	argv[1] = NULL;

	memset(&proc, 0, sizeof(proc));
	proc.argv = argv;
	proc.in = -1;
	proc.stdout_to_stderr = 1;

	code = start_command(&proc);
	if (code)
		return hook_status(code, hook_name);
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!cmd->error_string) {
			size_t n = snprintf(buf, sizeof(buf), "%s %s %s\n",
				sha1_to_hex(cmd->old_sha1),
				sha1_to_hex(cmd->new_sha1),
				cmd->ref_name);
			if (write_in_full(proc.in, buf, n) != n)
				break;
		}
	}
	close(proc.in);
	return hook_status(finish_command(&proc), hook_name);
}

static int run_update_hook(struct command *cmd)
{
	static const char update_hook[] = "hooks/update";
	struct child_process proc;
	const char *argv[5];

	if (access(update_hook, X_OK) < 0)
		return 0;

	argv[0] = update_hook;
	argv[1] = cmd->ref_name;
	argv[2] = sha1_to_hex(cmd->old_sha1);
	argv[3] = sha1_to_hex(cmd->new_sha1);
	argv[4] = NULL;

	memset(&proc, 0, sizeof(proc));
	proc.argv = argv;
	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;

	return hook_status(run_command(&proc), update_hook);
}

static const char *update(struct command *cmd)
{
	const char *name = cmd->ref_name;
	unsigned char *old_sha1 = cmd->old_sha1;
	unsigned char *new_sha1 = cmd->new_sha1;
	struct ref_lock *lock;

	/* only refs/... are allowed */
	if (prefixcmp(name, "refs/") || check_ref_format(name + 5)) {
		error("refusing to create funny ref '%s' remotely", name);
		return "funny refname";
	}

	if (!is_null_sha1(new_sha1) && !has_sha1_file(new_sha1)) {
		error("unpack should have generated %s, "
		      "but I can't find it!", sha1_to_hex(new_sha1));
		return "bad pack";
	}
	if (deny_non_fast_forwards && !is_null_sha1(new_sha1) &&
	    !is_null_sha1(old_sha1) &&
	    !prefixcmp(name, "refs/heads/")) {
		struct object *old_object, *new_object;
		struct commit *old_commit, *new_commit;
		struct commit_list *bases, *ent;

		old_object = parse_object(old_sha1);
		new_object = parse_object(new_sha1);

		if (!old_object || !new_object ||
		    old_object->type != OBJ_COMMIT ||
		    new_object->type != OBJ_COMMIT) {
			error("bad sha1 objects for %s", name);
			return "bad ref";
		}
		old_commit = (struct commit *)old_object;
		new_commit = (struct commit *)new_object;
		bases = get_merge_bases(old_commit, new_commit, 1);
		for (ent = bases; ent; ent = ent->next)
			if (!hashcmp(old_sha1, ent->item->object.sha1))
				break;
		free_commit_list(bases);
		if (!ent) {
			error("denying non-fast forward %s"
			      " (you should pull first)", name);
			return "non-fast forward";
		}
	}
	if (run_update_hook(cmd)) {
		error("hook declined to update %s", name);
		return "hook declined";
	}

	if (is_null_sha1(new_sha1)) {
		if (!parse_object(old_sha1)) {
			warning ("Allowing deletion of corrupt ref.");
			old_sha1 = NULL;
		}
		if (delete_ref(name, old_sha1)) {
			error("failed to delete %s", name);
			return "failed to delete";
		}
		return NULL; /* good */
	}
	else {
		lock = lock_any_ref_for_update(name, old_sha1, 0);
		if (!lock) {
			error("failed to lock %s", name);
			return "failed to lock";
		}
		if (write_ref_sha1(lock, new_sha1, "push")) {
			return "failed to write"; /* error() already called */
		}
		return NULL; /* good */
	}
}

static char update_post_hook[] = "hooks/post-update";

static void run_update_post_hook(struct command *cmd)
{
	struct command *cmd_p;
	int argc;
	const char **argv;

	for (argc = 0, cmd_p = cmd; cmd_p; cmd_p = cmd_p->next) {
		if (cmd_p->error_string)
			continue;
		argc++;
	}
	if (!argc || access(update_post_hook, X_OK) < 0)
		return;
	argv = xmalloc(sizeof(*argv) * (2 + argc));
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
	run_command_v_opt(argv, RUN_COMMAND_NO_STDIN
		| RUN_COMMAND_STDOUT_TO_STDERR);
}

static void execute_commands(const char *unpacker_error)
{
	struct command *cmd = commands;

	if (unpacker_error) {
		while (cmd) {
			cmd->error_string = "n/a (unpacker error)";
			cmd = cmd->next;
		}
		return;
	}

	if (run_hook(pre_receive_hook)) {
		while (cmd) {
			cmd->error_string = "pre-receive hook declined";
			cmd = cmd->next;
		}
		return;
	}

	while (cmd) {
		cmd->error_string = update(cmd);
		cmd = cmd->next;
	}
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
		hashcpy(cmd->old_sha1, old_sha1);
		hashcpy(cmd->new_sha1, new_sha1);
		memcpy(cmd->ref_name, line + 82, len - 81);
		cmd->error_string = NULL;
		cmd->next = NULL;
		*p = cmd;
		p = &cmd->next;
	}
}

static const char *parse_pack_header(struct pack_header *hdr)
{
	switch (read_pack_header(0, hdr)) {
	case PH_ERROR_EOF:
		return "eof before pack header was fully read";

	case PH_ERROR_PACK_SIGNATURE:
		return "protocol error (pack signature mismatch detected)";

	case PH_ERROR_PROTOCOL:
		return "protocol error (pack version unsupported)";

	default:
		return "unknown error in parse_pack_header";

	case 0:
		return NULL;
	}
}

static const char *pack_lockfile;

static const char *unpack(void)
{
	struct pack_header hdr;
	const char *hdr_err;
	char hdr_arg[38];

	hdr_err = parse_pack_header(&hdr);
	if (hdr_err)
		return hdr_err;
	snprintf(hdr_arg, sizeof(hdr_arg),
			"--pack_header=%"PRIu32",%"PRIu32,
			ntohl(hdr.hdr_version), ntohl(hdr.hdr_entries));

	if (ntohl(hdr.hdr_entries) < unpack_limit) {
		int code, i = 0;
		const char *unpacker[4];
		unpacker[i++] = "unpack-objects";
		if (receive_fsck_objects)
			unpacker[i++] = "--strict";
		unpacker[i++] = hdr_arg;
		unpacker[i++] = NULL;
		code = run_command_v_opt(unpacker, RUN_GIT_CMD);
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
			return "unpacker exited with error code";
		}
	} else {
		const char *keeper[7];
		int s, status, i = 0;
		char keep_arg[256];
		struct child_process ip;

		s = sprintf(keep_arg, "--keep=receive-pack %i on ", getpid());
		if (gethostname(keep_arg + s, sizeof(keep_arg) - s))
			strcpy(keep_arg + s, "localhost");

		keeper[i++] = "index-pack";
		keeper[i++] = "--stdin";
		if (receive_fsck_objects)
			keeper[i++] = "--strict";
		keeper[i++] = "--fix-thin";
		keeper[i++] = hdr_arg;
		keeper[i++] = keep_arg;
		keeper[i++] = NULL;
		memset(&ip, 0, sizeof(ip));
		ip.argv = keeper;
		ip.out = -1;
		ip.git_cmd = 1;
		if (start_command(&ip))
			return "index-pack fork failed";
		pack_lockfile = index_pack_lockfile(ip.out);
		close(ip.out);
		status = finish_command(&ip);
		if (!status) {
			reprepare_packed_git();
			return NULL;
		}
		return "index-pack abnormal exit";
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

static int delete_only(struct command *cmd)
{
	while (cmd) {
		if (!is_null_sha1(cmd->new_sha1))
			return 0;
		cmd = cmd->next;
	}
	return 1;
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

	setup_path();

	if (!enter_repo(dir, 0))
		die("'%s': unable to chdir or not a git archive", dir);

	if (is_repository_shallow())
		die("attempt to push into a shallow repository");

	git_config(receive_pack_config, NULL);

	if (0 <= transfer_unpack_limit)
		unpack_limit = transfer_unpack_limit;
	else if (0 <= receive_unpack_limit)
		unpack_limit = receive_unpack_limit;

	write_head_info();

	/* EOF */
	packet_flush(1);

	read_head_info();
	if (commands) {
		const char *unpack_status = NULL;

		if (!delete_only(commands))
			unpack_status = unpack();
		execute_commands(unpack_status);
		if (pack_lockfile)
			unlink(pack_lockfile);
		if (report_status)
			report(unpack_status);
		run_hook(post_receive_hook);
		run_update_post_hook(commands);
	}
	return 0;
}
