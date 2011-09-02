#include "builtin.h"
#include "pack.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "commit.h"
#include "object.h"
#include "remote.h"
#include "transport.h"
#include "string-list.h"
#include "sha1-array.h"
#include "connected.h"

static const char receive_pack_usage[] = "git receive-pack <git-dir>";

enum deny_action {
	DENY_UNCONFIGURED,
	DENY_IGNORE,
	DENY_WARN,
	DENY_REFUSE
};

static int deny_deletes;
static int deny_non_fast_forwards;
static enum deny_action deny_current_branch = DENY_UNCONFIGURED;
static enum deny_action deny_delete_current = DENY_UNCONFIGURED;
static int receive_fsck_objects;
static int receive_unpack_limit = -1;
static int transfer_unpack_limit = -1;
static int unpack_limit = 100;
static int report_status;
static int use_sideband;
static int prefer_ofs_delta = 1;
static int auto_update_server_info;
static int auto_gc = 1;
static const char *head_name;
static int sent_capabilities;

static enum deny_action parse_deny_action(const char *var, const char *value)
{
	if (value) {
		if (!strcasecmp(value, "ignore"))
			return DENY_IGNORE;
		if (!strcasecmp(value, "warn"))
			return DENY_WARN;
		if (!strcasecmp(value, "refuse"))
			return DENY_REFUSE;
	}
	if (git_config_bool(var, value))
		return DENY_REFUSE;
	return DENY_IGNORE;
}

static int receive_pack_config(const char *var, const char *value, void *cb)
{
	if (strcmp(var, "receive.denydeletes") == 0) {
		deny_deletes = git_config_bool(var, value);
		return 0;
	}

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

	if (!strcmp(var, "receive.denycurrentbranch")) {
		deny_current_branch = parse_deny_action(var, value);
		return 0;
	}

	if (strcmp(var, "receive.denydeletecurrent") == 0) {
		deny_delete_current = parse_deny_action(var, value);
		return 0;
	}

	if (strcmp(var, "repack.usedeltabaseoffset") == 0) {
		prefer_ofs_delta = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.updateserverinfo") == 0) {
		auto_update_server_info = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.autogc") == 0) {
		auto_gc = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, cb);
}

static int show_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	if (sent_capabilities)
		packet_write(1, "%s %s\n", sha1_to_hex(sha1), path);
	else
		packet_write(1, "%s %s%c%s%s\n",
			     sha1_to_hex(sha1), path, 0,
			     " report-status delete-refs side-band-64k",
			     prefer_ofs_delta ? " ofs-delta" : "");
	sent_capabilities = 1;
	return 0;
}

static void write_head_info(void)
{
	for_each_ref(show_ref, NULL);
	if (!sent_capabilities)
		show_ref("capabilities^{}", null_sha1, 0, NULL);

}

struct command {
	struct command *next;
	const char *error_string;
	unsigned int skip_update;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char ref_name[FLEX_ARRAY]; /* more */
};

static const char pre_receive_hook[] = "hooks/pre-receive";
static const char post_receive_hook[] = "hooks/post-receive";

static void rp_error(const char *err, ...) __attribute__((format (printf, 1, 2)));
static void rp_warning(const char *err, ...) __attribute__((format (printf, 1, 2)));

static void report_message(const char *prefix, const char *err, va_list params)
{
	int sz = strlen(prefix);
	char msg[4096];

	strncpy(msg, prefix, sz);
	sz += vsnprintf(msg + sz, sizeof(msg) - sz, err, params);
	if (sz > (sizeof(msg) - 1))
		sz = sizeof(msg) - 1;
	msg[sz++] = '\n';

	if (use_sideband)
		send_sideband(1, 2, msg, sz, use_sideband);
	else
		xwrite(2, msg, sz);
}

static void rp_warning(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	report_message("warning: ", err, params);
	va_end(params);
}

static void rp_error(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	report_message("error: ", err, params);
	va_end(params);
}

static int copy_to_sideband(int in, int out, void *arg)
{
	char data[128];
	while (1) {
		ssize_t sz = xread(in, data, sizeof(data));
		if (sz <= 0)
			break;
		send_sideband(1, 2, data, sz, use_sideband);
	}
	close(in);
	return 0;
}

static int run_receive_hook(struct command *commands, const char *hook_name)
{
	static char buf[sizeof(commands->old_sha1) * 2 + PATH_MAX + 4];
	struct command *cmd;
	struct child_process proc;
	struct async muxer;
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

	if (use_sideband) {
		memset(&muxer, 0, sizeof(muxer));
		muxer.proc = copy_to_sideband;
		muxer.in = -1;
		code = start_async(&muxer);
		if (code)
			return code;
		proc.err = muxer.in;
	}

	code = start_command(&proc);
	if (code) {
		if (use_sideband)
			finish_async(&muxer);
		return code;
	}

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
	if (use_sideband)
		finish_async(&muxer);
	return finish_command(&proc);
}

static int run_update_hook(struct command *cmd)
{
	static const char update_hook[] = "hooks/update";
	const char *argv[5];
	struct child_process proc;
	int code;

	if (access(update_hook, X_OK) < 0)
		return 0;

	argv[0] = update_hook;
	argv[1] = cmd->ref_name;
	argv[2] = sha1_to_hex(cmd->old_sha1);
	argv[3] = sha1_to_hex(cmd->new_sha1);
	argv[4] = NULL;

	memset(&proc, 0, sizeof(proc));
	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;
	proc.err = use_sideband ? -1 : 0;
	proc.argv = argv;

	code = start_command(&proc);
	if (code)
		return code;
	if (use_sideband)
		copy_to_sideband(proc.err, -1, NULL);
	return finish_command(&proc);
}

static int is_ref_checked_out(const char *ref)
{
	if (is_bare_repository())
		return 0;

	if (!head_name)
		return 0;
	return !strcmp(head_name, ref);
}

static char *refuse_unconfigured_deny_msg[] = {
	"By default, updating the current branch in a non-bare repository",
	"is denied, because it will make the index and work tree inconsistent",
	"with what you pushed, and will require 'git reset --hard' to match",
	"the work tree to HEAD.",
	"",
	"You can set 'receive.denyCurrentBranch' configuration variable to",
	"'ignore' or 'warn' in the remote repository to allow pushing into",
	"its current branch; however, this is not recommended unless you",
	"arranged to update its work tree to match what you pushed in some",
	"other way.",
	"",
	"To squelch this message and still keep the default behaviour, set",
	"'receive.denyCurrentBranch' configuration variable to 'refuse'."
};

static void refuse_unconfigured_deny(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(refuse_unconfigured_deny_msg); i++)
		rp_error("%s", refuse_unconfigured_deny_msg[i]);
}

static char *refuse_unconfigured_deny_delete_current_msg[] = {
	"By default, deleting the current branch is denied, because the next",
	"'git clone' won't result in any file checked out, causing confusion.",
	"",
	"You can set 'receive.denyDeleteCurrent' configuration variable to",
	"'warn' or 'ignore' in the remote repository to allow deleting the",
	"current branch, with or without a warning message.",
	"",
	"To squelch this message, you can set it to 'refuse'."
};

static void refuse_unconfigured_deny_delete_current(void)
{
	int i;
	for (i = 0;
	     i < ARRAY_SIZE(refuse_unconfigured_deny_delete_current_msg);
	     i++)
		rp_error("%s", refuse_unconfigured_deny_delete_current_msg[i]);
}

static const char *update(struct command *cmd)
{
	const char *name = cmd->ref_name;
	unsigned char *old_sha1 = cmd->old_sha1;
	unsigned char *new_sha1 = cmd->new_sha1;
	struct ref_lock *lock;

	/* only refs/... are allowed */
	if (prefixcmp(name, "refs/") || check_ref_format(name + 5)) {
		rp_error("refusing to create funny ref '%s' remotely", name);
		return "funny refname";
	}

	if (is_ref_checked_out(name)) {
		switch (deny_current_branch) {
		case DENY_IGNORE:
			break;
		case DENY_WARN:
			rp_warning("updating the current branch");
			break;
		case DENY_REFUSE:
		case DENY_UNCONFIGURED:
			rp_error("refusing to update checked out branch: %s", name);
			if (deny_current_branch == DENY_UNCONFIGURED)
				refuse_unconfigured_deny();
			return "branch is currently checked out";
		}
	}

	if (!is_null_sha1(new_sha1) && !has_sha1_file(new_sha1)) {
		error("unpack should have generated %s, "
		      "but I can't find it!", sha1_to_hex(new_sha1));
		return "bad pack";
	}

	if (!is_null_sha1(old_sha1) && is_null_sha1(new_sha1)) {
		if (deny_deletes && !prefixcmp(name, "refs/heads/")) {
			rp_error("denying ref deletion for %s", name);
			return "deletion prohibited";
		}

		if (!strcmp(name, head_name)) {
			switch (deny_delete_current) {
			case DENY_IGNORE:
				break;
			case DENY_WARN:
				rp_warning("deleting the current branch");
				break;
			case DENY_REFUSE:
			case DENY_UNCONFIGURED:
				if (deny_delete_current == DENY_UNCONFIGURED)
					refuse_unconfigured_deny_delete_current();
				rp_error("refusing to delete the current branch: %s", name);
				return "deletion of the current branch prohibited";
			}
		}
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
			rp_error("denying non-fast-forward %s"
				 " (you should pull first)", name);
			return "non-fast-forward";
		}
	}
	if (run_update_hook(cmd)) {
		rp_error("hook declined to update %s", name);
		return "hook declined";
	}

	if (is_null_sha1(new_sha1)) {
		if (!parse_object(old_sha1)) {
			rp_warning("Allowing deletion of corrupt ref.");
			old_sha1 = NULL;
		}
		if (delete_ref(name, old_sha1, 0)) {
			rp_error("failed to delete %s", name);
			return "failed to delete";
		}
		return NULL; /* good */
	}
	else {
		lock = lock_any_ref_for_update(name, old_sha1, 0);
		if (!lock) {
			rp_error("failed to lock %s", name);
			return "failed to lock";
		}
		if (write_ref_sha1(lock, new_sha1, "push")) {
			return "failed to write"; /* error() already called */
		}
		return NULL; /* good */
	}
}

static char update_post_hook[] = "hooks/post-update";

static void run_update_post_hook(struct command *commands)
{
	struct command *cmd;
	int argc;
	const char **argv;
	struct child_process proc;

	for (argc = 0, cmd = commands; cmd; cmd = cmd->next) {
		if (cmd->error_string)
			continue;
		argc++;
	}
	if (!argc || access(update_post_hook, X_OK) < 0)
		return;
	argv = xmalloc(sizeof(*argv) * (2 + argc));
	argv[0] = update_post_hook;

	for (argc = 1, cmd = commands; cmd; cmd = cmd->next) {
		char *p;
		if (cmd->error_string)
			continue;
		p = xmalloc(strlen(cmd->ref_name) + 1);
		strcpy(p, cmd->ref_name);
		argv[argc] = p;
		argc++;
	}
	argv[argc] = NULL;

	memset(&proc, 0, sizeof(proc));
	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;
	proc.err = use_sideband ? -1 : 0;
	proc.argv = argv;

	if (!start_command(&proc)) {
		if (use_sideband)
			copy_to_sideband(proc.err, -1, NULL);
		finish_command(&proc);
	}
}

static void check_aliased_update(struct command *cmd, struct string_list *list)
{
	struct string_list_item *item;
	struct command *dst_cmd;
	unsigned char sha1[20];
	char cmd_oldh[41], cmd_newh[41], dst_oldh[41], dst_newh[41];
	int flag;

	const char *dst_name = resolve_ref(cmd->ref_name, sha1, 0, &flag);

	if (!(flag & REF_ISSYMREF))
		return;

	if ((item = string_list_lookup(list, dst_name)) == NULL)
		return;

	cmd->skip_update = 1;

	dst_cmd = (struct command *) item->util;

	if (!hashcmp(cmd->old_sha1, dst_cmd->old_sha1) &&
	    !hashcmp(cmd->new_sha1, dst_cmd->new_sha1))
		return;

	dst_cmd->skip_update = 1;

	strcpy(cmd_oldh, find_unique_abbrev(cmd->old_sha1, DEFAULT_ABBREV));
	strcpy(cmd_newh, find_unique_abbrev(cmd->new_sha1, DEFAULT_ABBREV));
	strcpy(dst_oldh, find_unique_abbrev(dst_cmd->old_sha1, DEFAULT_ABBREV));
	strcpy(dst_newh, find_unique_abbrev(dst_cmd->new_sha1, DEFAULT_ABBREV));
	rp_error("refusing inconsistent update between symref '%s' (%s..%s) and"
		 " its target '%s' (%s..%s)",
		 cmd->ref_name, cmd_oldh, cmd_newh,
		 dst_cmd->ref_name, dst_oldh, dst_newh);

	cmd->error_string = dst_cmd->error_string =
		"inconsistent aliased update";
}

static void check_aliased_updates(struct command *commands)
{
	struct command *cmd;
	struct string_list ref_list = STRING_LIST_INIT_NODUP;

	for (cmd = commands; cmd; cmd = cmd->next) {
		struct string_list_item *item =
			string_list_append(&ref_list, cmd->ref_name);
		item->util = (void *)cmd;
	}
	sort_string_list(&ref_list);

	for (cmd = commands; cmd; cmd = cmd->next)
		check_aliased_update(cmd, &ref_list);

	string_list_clear(&ref_list, 0);
}

static int command_singleton_iterator(void *cb_data, unsigned char sha1[20])
{
	struct command **cmd_list = cb_data;
	struct command *cmd = *cmd_list;

	if (!cmd)
		return -1; /* end of list */
	*cmd_list = NULL; /* this returns only one */
	hashcpy(sha1, cmd->new_sha1);
	return 0;
}

static void set_connectivity_errors(struct command *commands)
{
	struct command *cmd;

	for (cmd = commands; cmd; cmd = cmd->next) {
		struct command *singleton = cmd;
		if (!check_everything_connected(command_singleton_iterator,
						0, &singleton))
			continue;
		cmd->error_string = "missing necessary objects";
	}
}

static int iterate_receive_command_list(void *cb_data, unsigned char sha1[20])
{
	struct command **cmd_list = cb_data;
	struct command *cmd = *cmd_list;

	if (!cmd)
		return -1; /* end of list */
	*cmd_list = cmd->next;
	hashcpy(sha1, cmd->new_sha1);
	return 0;
}

static void execute_commands(struct command *commands, const char *unpacker_error)
{
	struct command *cmd;
	unsigned char sha1[20];

	if (unpacker_error) {
		for (cmd = commands; cmd; cmd = cmd->next)
			cmd->error_string = "n/a (unpacker error)";
		return;
	}

	cmd = commands;
	if (check_everything_connected(iterate_receive_command_list,
				       0, &cmd))
		set_connectivity_errors(commands);

	if (run_receive_hook(commands, pre_receive_hook)) {
		for (cmd = commands; cmd; cmd = cmd->next)
			cmd->error_string = "pre-receive hook declined";
		return;
	}

	check_aliased_updates(commands);

	head_name = resolve_ref("HEAD", sha1, 0, NULL);

	for (cmd = commands; cmd; cmd = cmd->next)
		if (!cmd->skip_update)
			cmd->error_string = update(cmd);
}

static struct command *read_head_info(void)
{
	struct command *commands = NULL;
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
			if (strstr(refname + reflen + 1, "side-band-64k"))
				use_sideband = LARGE_PACKET_MAX;
		}
		cmd = xcalloc(1, sizeof(struct command) + len - 80);
		hashcpy(cmd->old_sha1, old_sha1);
		hashcpy(cmd->new_sha1, new_sha1);
		memcpy(cmd->ref_name, line + 82, len - 81);
		*p = cmd;
		p = &cmd->next;
	}
	return commands;
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
		if (!code)
			return NULL;
		return "unpack-objects abnormal exit";
	} else {
		const char *keeper[7];
		int s, status, i = 0;
		char keep_arg[256];
		struct child_process ip;

		s = sprintf(keep_arg, "--keep=receive-pack %"PRIuMAX" on ", (uintmax_t) getpid());
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
		status = start_command(&ip);
		if (status) {
			return "index-pack fork failed";
		}
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

static void report(struct command *commands, const char *unpack_status)
{
	struct command *cmd;
	struct strbuf buf = STRBUF_INIT;

	packet_buf_write(&buf, "unpack %s\n",
			 unpack_status ? unpack_status : "ok");
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!cmd->error_string)
			packet_buf_write(&buf, "ok %s\n",
					 cmd->ref_name);
		else
			packet_buf_write(&buf, "ng %s %s\n",
					 cmd->ref_name, cmd->error_string);
	}
	packet_buf_flush(&buf);

	if (use_sideband)
		send_sideband(1, 1, buf.buf, buf.len, use_sideband);
	else
		safe_write(1, buf.buf, buf.len);
	strbuf_release(&buf);
}

static int delete_only(struct command *commands)
{
	struct command *cmd;
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!is_null_sha1(cmd->new_sha1))
			return 0;
	}
	return 1;
}

static void add_one_alternate_sha1(const unsigned char sha1[20], void *unused)
{
	add_extra_ref(".have", sha1, 0);
}

static void collect_one_alternate_ref(const struct ref *ref, void *data)
{
	struct sha1_array *sa = data;
	sha1_array_append(sa, ref->old_sha1);
}

static void add_alternate_refs(void)
{
	struct sha1_array sa = SHA1_ARRAY_INIT;
	for_each_alternate_ref(collect_one_alternate_ref, &sa);
	sha1_array_for_each_unique(&sa, add_one_alternate_sha1, NULL);
	sha1_array_clear(&sa);
}

int cmd_receive_pack(int argc, const char **argv, const char *prefix)
{
	int advertise_refs = 0;
	int stateless_rpc = 0;
	int i;
	char *dir = NULL;
	struct command *commands;

	packet_trace_identity("receive-pack");

	argv++;
	for (i = 1; i < argc; i++) {
		const char *arg = *argv++;

		if (*arg == '-') {
			if (!strcmp(arg, "--advertise-refs")) {
				advertise_refs = 1;
				continue;
			}
			if (!strcmp(arg, "--stateless-rpc")) {
				stateless_rpc = 1;
				continue;
			}

			usage(receive_pack_usage);
		}
		if (dir)
			usage(receive_pack_usage);
		dir = xstrdup(arg);
	}
	if (!dir)
		usage(receive_pack_usage);

	setup_path();

	if (!enter_repo(dir, 0))
		die("'%s' does not appear to be a git repository", dir);

	if (is_repository_shallow())
		die("attempt to push into a shallow repository");

	git_config(receive_pack_config, NULL);

	if (0 <= transfer_unpack_limit)
		unpack_limit = transfer_unpack_limit;
	else if (0 <= receive_unpack_limit)
		unpack_limit = receive_unpack_limit;

	if (advertise_refs || !stateless_rpc) {
		add_alternate_refs();
		write_head_info();
		clear_extra_refs();

		/* EOF */
		packet_flush(1);
	}
	if (advertise_refs)
		return 0;

	if ((commands = read_head_info()) != NULL) {
		const char *unpack_status = NULL;

		if (!delete_only(commands))
			unpack_status = unpack();
		execute_commands(commands, unpack_status);
		if (pack_lockfile)
			unlink_or_warn(pack_lockfile);
		if (report_status)
			report(commands, unpack_status);
		run_receive_hook(commands, post_receive_hook);
		run_update_post_hook(commands);
		if (auto_gc) {
			const char *argv_gc_auto[] = {
				"gc", "--auto", "--quiet", NULL,
			};
			run_command_v_opt(argv_gc_auto, RUN_GIT_CMD);
		}
		if (auto_update_server_info)
			update_server_info(0);
	}
	if (use_sideband)
		packet_flush(1);
	return 0;
}
