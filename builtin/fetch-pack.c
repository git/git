#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "gettext.h"
#include "hex.h"
#include "object-file.h"
#include "pkt-line.h"
#include "fetch-pack.h"
#include "remote.h"
#include "connect.h"
#include "oid-array.h"
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
			oidclr(&oid, the_repository->hash_algo);
		}
	} else {
		/* <ref>, clear cruft from get_oid_hex */
		oidclr(&oid, the_repository->hash_algo);
	}

	ref = alloc_ref(name);
	oidcpy(&ref->old_oid, &oid);
	(*nr)++;
	ALLOC_GROW(*sought, *nr, *alloc);
	(*sought)[*nr - 1] = ref;
}

int cmd_fetch_pack(int argc,
		   const char **argv,
		   const char *prefix UNUSED,
		   struct repository *repo UNUSED)
{
	int i, ret;
	struct ref *fetched_refs = NULL, *remote_refs = NULL;
	const char *dest = NULL;
	struct ref **sought = NULL;
	struct ref **sought_to_free = NULL;
	int nr_sought = 0, alloc_sought = 0;
	int fd[2];
	struct string_list pack_lockfiles = STRING_LIST_INIT_DUP;
	struct string_list *pack_lockfiles_ptr = NULL;
	struct child_process *conn;
	struct fetch_pack_args args;
	struct oid_array shallow = OID_ARRAY_INIT;
	struct string_list deepen_not = STRING_LIST_INIT_DUP;
	struct packet_reader reader;
	enum protocol_version version;

	fetch_if_missing = 0;

	packet_trace_identity("fetch-pack");

	memset(&args, 0, sizeof(args));
	list_objects_filter_init(&args.filter_options);
	args.uploadpack = "git-upload-pack";

	show_usage_if_asked(argc, argv, fetch_pack_usage);

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
			pack_lockfiles_ptr = &pack_lockfiles;
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
		if (!strcmp("--refetch", arg)) {
			args.refetch = 1;
			continue;
		}
		if (skip_prefix(arg, ("--filter="), &arg)) {
			parse_list_objects_filter(&args.filter_options, arg);
			continue;
		}
		if (!strcmp(arg, ("--no-filter"))) {
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
		conn = git_connect(fd, dest, "git-upload-pack",
				   args.uploadpack, flags);
		if (!conn)
			return args.diag_url ? 0 : 1;
	}

	packet_reader_init(&reader, fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	version = discover_version(&reader);
	switch (version) {
	case protocol_v2:
		get_remote_refs(fd[1], &reader, &remote_refs, 0, NULL, NULL,
				args.stateless_rpc);
		break;
	case protocol_v1:
	case protocol_v0:
		get_remote_heads(&reader, &remote_refs, 0, NULL, &shallow);
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	/*
	 * Create a shallow copy of `sought` so that we can free all of its entries.
	 * This is because `fetch_pack()` will modify the array to evict some
	 * entries, but won't free those.
	 */
	DUP_ARRAY(sought_to_free, sought, nr_sought);

	fetched_refs = fetch_pack(&args, fd, remote_refs, sought, nr_sought,
			 &shallow, pack_lockfiles_ptr, version);

	if (pack_lockfiles.nr) {
		int i;

		printf("lock %s\n", pack_lockfiles.items[0].string);
		fflush(stdout);
		for (i = 1; i < pack_lockfiles.nr; i++)
			warning(_("Lockfile created but not reported: %s"),
				pack_lockfiles.items[i].string);
	}
	if (args.check_self_contained_and_connected &&
	    args.self_contained_and_connected) {
		printf("connectivity-ok\n");
		fflush(stdout);
	}
	close(fd[0]);
	close(fd[1]);
	if (finish_connect(conn)) {
		ret = 1;
		goto cleanup;
	}

	ret = !fetched_refs;

	/*
	 * If the heads to pull were given, we should have consumed
	 * all of them by matching the remote.  Otherwise, 'git fetch
	 * remote no-such-ref' would silently succeed without issuing
	 * an error.
	 */
	ret |= report_unmatched_refs(sought, nr_sought);

	for (struct ref *ref = fetched_refs; ref; ref = ref->next)
		printf("%s %s\n",
		       oid_to_hex(&ref->old_oid), ref->name);

cleanup:
	for (size_t i = 0; i < nr_sought; i++)
		free_one_ref(sought_to_free[i]);
	free(sought_to_free);
	free(sought);
	free_refs(fetched_refs);
	free_refs(remote_refs);
	list_objects_filter_release(&args.filter_options);
	oid_array_clear(&shallow);
	string_list_clear(&pack_lockfiles, 0);
	return ret;
}
