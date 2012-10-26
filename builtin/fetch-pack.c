#include "builtin.h"
#include "pkt-line.h"
#include "fetch-pack.h"

static const char fetch_pack_usage[] =
"git fetch-pack [--all] [--stdin] [--quiet|-q] [--keep|-k] [--thin] "
"[--include-tag] [--upload-pack=<git-upload-pack>] [--depth=<n>] "
"[--no-progress] [-v] [<host>:]<directory> [<refs>...]";

int cmd_fetch_pack(int argc, const char **argv, const char *prefix)
{
	int i, ret;
	struct ref *ref = NULL;
	const char *dest = NULL;
	struct string_list sought = STRING_LIST_INIT_DUP;
	int fd[2];
	char *pack_lockfile = NULL;
	char **pack_lockfile_ptr = NULL;
	struct child_process *conn;
	struct fetch_pack_args args;

	packet_trace_identity("fetch-pack");

	memset(&args, 0, sizeof(args));
	args.uploadpack = "git-upload-pack";

	for (i = 1; i < argc && *argv[i] == '-'; i++) {
		const char *arg = argv[i];

		if (!prefixcmp(arg, "--upload-pack=")) {
			args.uploadpack = arg + 14;
			continue;
		}
		if (!prefixcmp(arg, "--exec=")) {
			args.uploadpack = arg + 7;
			continue;
		}
		if (!strcmp("--quiet", arg) || !strcmp("-q", arg)) {
			args.quiet = 1;
			continue;
		}
		if (!strcmp("--keep", arg) || !strcmp("-k", arg)) {
			args.lock_pack = args.keep_pack;
			args.keep_pack = 1;
			continue;
		}
		if (!strcmp("--thin", arg)) {
			args.use_thin_pack = 1;
			continue;
		}
		if (!strcmp("--include-tag", arg)) {
			args.include_tag = 1;
			continue;
		}
		if (!strcmp("--all", arg)) {
			args.fetch_all = 1;
			continue;
		}
		if (!strcmp("--stdin", arg)) {
			args.stdin_refs = 1;
			continue;
		}
		if (!strcmp("-v", arg)) {
			args.verbose = 1;
			continue;
		}
		if (!prefixcmp(arg, "--depth=")) {
			args.depth = strtol(arg + 8, NULL, 0);
			continue;
		}
		if (!strcmp("--no-progress", arg)) {
			args.no_progress = 1;
			continue;
		}
		if (!strcmp("--stateless-rpc", arg)) {
			args.stateless_rpc = 1;
			continue;
		}
		if (!strcmp("--lock-pack", arg)) {
			args.lock_pack = 1;
			pack_lockfile_ptr = &pack_lockfile;
			continue;
		}
		usage(fetch_pack_usage);
	}

	if (i < argc)
		dest = argv[i++];
	else
		usage(fetch_pack_usage);

	/*
	 * Copy refs from cmdline to growable list, then append any
	 * refs from the standard input:
	 */
	for (; i < argc; i++)
		string_list_append(&sought, xstrdup(argv[i]));
	if (args.stdin_refs) {
		if (args.stateless_rpc) {
			/* in stateless RPC mode we use pkt-line to read
			 * from stdin, until we get a flush packet
			 */
			static char line[1000];
			for (;;) {
				int n = packet_read_line(0, line, sizeof(line));
				if (!n)
					break;
				if (line[n-1] == '\n')
					n--;
				string_list_append(&sought, xmemdupz(line, n));
			}
		}
		else {
			/* read from stdin one ref per line, until EOF */
			struct strbuf line = STRBUF_INIT;
			while (strbuf_getline(&line, stdin, '\n') != EOF)
				string_list_append(&sought, strbuf_detach(&line, NULL));
			strbuf_release(&line);
		}
	}

	if (args.stateless_rpc) {
		conn = NULL;
		fd[0] = 0;
		fd[1] = 1;
	} else {
		conn = git_connect(fd, dest, args.uploadpack,
				   args.verbose ? CONNECT_VERBOSE : 0);
	}

	get_remote_heads(fd[0], &ref, 0, NULL);

	ref = fetch_pack(&args, fd, conn, ref, dest,
			 &sought, pack_lockfile_ptr);
	if (pack_lockfile) {
		printf("lock %s\n", pack_lockfile);
		fflush(stdout);
	}
	close(fd[0]);
	close(fd[1]);
	if (finish_connect(conn))
		return 1;

	ret = !ref || sought.nr;

	/*
	 * If the heads to pull were given, we should have consumed
	 * all of them by matching the remote.  Otherwise, 'git fetch
	 * remote no-such-ref' would silently succeed without issuing
	 * an error.
	 */
	for (i = 0; i < sought.nr; i++)
		error("no such remote ref %s", sought.items[i].string);
	while (ref) {
		printf("%s %s\n",
		       sha1_to_hex(ref->old_sha1), ref->name);
		ref = ref->next;
	}

	return ret;
}
