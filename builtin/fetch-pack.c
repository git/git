#include "builtin.h"
#include "pkt-line.h"
#include "fetch-pack.h"
#include "remote.h"
#include "connect.h"
#include "sha1-array.h"
#include "protocol.h"

static const char fetch_pack_usage[] =
"git fetch-pack [--all] [--stdin] [--quiet | -q] [--keep | -k] [--thin] "
"[--include-tag] [--upload-pack=<git-upload-pack>] [--depth=<n>] "
"[--no-progress] [--diag-url] [-v] [<host>:]<directory> [<refs>...]";

static void add_sought_entry(struct ref ***sought, int *nr, int *alloc,
			     const char *name)
{
	struct ref *ref;
	struct object_id oid;
	const char *p;

	if (!parse_oid_hex(name, &oid, &p)) {
		if (*p == ' ') {
			/* <oid> <ref>, find refname */
			name = p + 1;
		} else if (*p == '\0') {
			; /* <oid>, leave oid as name */
		} else {
			/* <ref>, clear cruft from oid */
			oidclr(&oid);
		}
	} else {
		/* <ref>, clear cruft from get_oid_hex */
		oidclr(&oid);
	}

	ref = alloc_ref(name);
	oidcpy(&ref->old_oid, &oid);
	(*nr)++;
	ALLOC_GROW(*sought, *nr, *alloc);
	(*sought)[*nr - 1] = ref;
}

int cmd_fetch_pack(int argc, const char **argv, const char *prefix)
{
	int i, ret;
	struct ref *ref = NULL;
	const char *dest = NULL;
	struct ref **sought = NULL;
	int nr_sought = 0, alloc_sought = 0;
	int fd[2];
	char *pack_lockfile = NULL;
	char **pack_lockfile_ptr = NULL;
	struct child_process *conn;
	struct fetch_pack_args args;
	struct oid_array shallow = OID_ARRAY_INIT;
	struct string_list deepen_not = STRING_LIST_INIT_DUP;
	struct packet_reader reader;

	fetch_if_missing = 0;

	register_allowed_protocol_version(protocol_v2);
	register_allowed_protocol_version(protocol_v1);
	register_allowed_protocol_version(protocol_v0);

	packet_trace_identity("fetch-pack");

	memset(&args, 0, sizeof(args));
	args.uploadpack = "git-upload-pack";

	for (i = 1; i < argc && *argv[i] == '-'; i++) {
		const char *arg = argv[i];

		if (skip_prefix(arg, "--upload-pack=", &arg)) {
			args.uploadpack = arg;
			continue;
		}
		if (skip_prefix(arg, "--exec=", &arg)) {
			args.uploadpack = arg;
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
		if (!strcmp("--diag-url", arg)) {
			args.diag_url = 1;
			continue;
		}
		if (!strcmp("-v", arg)) {
			args.verbose = 1;
			continue;
		}
		if (skip_prefix(arg, "--depth=", &arg)) {
			args.depth = strtol(arg, NULL, 0);
			continue;
		}
		if (skip_prefix(arg, "--shallow-since=", &arg)) {
			args.deepen_since = xstrdup(arg);
			continue;
		}
		if (skip_prefix(arg, "--shallow-exclude=", &arg)) {
			string_list_append(&deepen_not, arg);
			continue;
		}
		if (!strcmp(arg, "--deepen-relative")) {
			args.deepen_relative = 1;
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
		if (!strcmp("--check-self-contained-and-connected", arg)) {
			args.check_self_contained_and_connected = 1;
			continue;
		}
		if (!strcmp("--cloning", arg)) {
			args.cloning = 1;
			continue;
		}
		if (!strcmp("--update-shallow", arg)) {
			args.update_shallow = 1;
			continue;
		}
		if (!strcmp("--from-promisor", arg)) {
			args.from_promisor = 1;
			continue;
		}
		if (!strcmp("--no-dependents", arg)) {
			args.no_dependents = 1;
			continue;
		}
		if (skip_prefix(arg, ("--" CL_ARG__FILTER "="), &arg)) {
			parse_list_objects_filter(&args.filter_options, arg);
			continue;
		}
		if (!strcmp(arg, ("--no-" CL_ARG__FILTER))) {
			list_objects_filter_set_no_filter(&args.filter_options);
			continue;
		}
		usage(fetch_pack_usage);
	}
	if (deepen_not.nr)
		args.deepen_not = &deepen_not;

	if (i < argc)
		dest = argv[i++];
	else
		usage(fetch_pack_usage);

	/*
	 * Copy refs from cmdline to growable list, then append any
	 * refs from the standard input:
	 */
	for (; i < argc; i++)
		add_sought_entry(&sought, &nr_sought, &alloc_sought, argv[i]);
	if (args.stdin_refs) {
		if (args.stateless_rpc) {
			/* in stateless RPC mode we use pkt-line to read
			 * from stdin, until we get a flush packet
			 */
			for (;;) {
				char *line = packet_read_line(0, NULL);
				if (!line)
					break;
				add_sought_entry(&sought, &nr_sought,  &alloc_sought, line);
			}
		}
		else {
			/* read from stdin one ref per line, until EOF */
			struct strbuf line = STRBUF_INIT;
			while (strbuf_getline_lf(&line, stdin) != EOF)
				add_sought_entry(&sought, &nr_sought, &alloc_sought, line.buf);
			strbuf_release(&line);
		}
	}

	if (args.stateless_rpc) {
		conn = NULL;
		fd[0] = 0;
		fd[1] = 1;
	} else {
		int flags = args.verbose ? CONNECT_VERBOSE : 0;
		if (args.diag_url)
			flags |= CONNECT_DIAG_URL;
		conn = git_connect(fd, dest, args.uploadpack,
				   flags);
		if (!conn)
			return args.diag_url ? 0 : 1;
	}

	packet_reader_init(&reader, fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF);

	switch (discover_version(&reader)) {
	case protocol_v2:
		die("support for protocol v2 not implemented yet");
	case protocol_v1:
	case protocol_v0:
		get_remote_heads(&reader, &ref, 0, NULL, &shallow);
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	ref = fetch_pack(&args, fd, conn, ref, dest, sought, nr_sought,
			 &shallow, pack_lockfile_ptr, protocol_v0);
	if (pack_lockfile) {
		printf("lock %s\n", pack_lockfile);
		fflush(stdout);
	}
	if (args.check_self_contained_and_connected &&
	    args.self_contained_and_connected) {
		printf("connectivity-ok\n");
		fflush(stdout);
	}
	close(fd[0]);
	close(fd[1]);
	if (finish_connect(conn))
		return 1;

	ret = !ref;

	/*
	 * If the heads to pull were given, we should have consumed
	 * all of them by matching the remote.  Otherwise, 'git fetch
	 * remote no-such-ref' would silently succeed without issuing
	 * an error.
	 */
	ret |= report_unmatched_refs(sought, nr_sought);

	while (ref) {
		printf("%s %s\n",
		       oid_to_hex(&ref->old_oid), ref->name);
		ref = ref->next;
	}

	return ret;
}
