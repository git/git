#include "builtin.h"
#include "commit.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "run-command.h"
#include "remote.h"
#include "connect.h"
#include "send-pack.h"
#include "quote.h"
#include "transport.h"
#include "version.h"
#include "sha1-array.h"
#include "gpg-interface.h"
#include "gettext.h"

static const char * const send_pack_usage[] = {
	N_("git send-pack [--all | --mirror] [--dry-run] [--force] "
	  "[--receive-pack=<git-receive-pack>] [--verbose] [--thin] [--atomic] "
	  "[<host>:]<directory> [<ref>...]\n"
	  "  --all and explicit <ref> specification are mutually exclusive."),
	NULL,
};

static struct send_pack_args args;

static void print_helper_status(struct ref *ref)
{
	struct strbuf buf = STRBUF_INIT;

	for (; ref; ref = ref->next) {
		const char *msg = NULL;
		const char *res;

		switch(ref->status) {
		case REF_STATUS_NONE:
			res = "error";
			msg = "no match";
			break;

		case REF_STATUS_OK:
			res = "ok";
			break;

		case REF_STATUS_UPTODATE:
			res = "ok";
			msg = "up to date";
			break;

		case REF_STATUS_REJECT_NONFASTFORWARD:
			res = "error";
			msg = "non-fast forward";
			break;

		case REF_STATUS_REJECT_FETCH_FIRST:
			res = "error";
			msg = "fetch first";
			break;

		case REF_STATUS_REJECT_NEEDS_FORCE:
			res = "error";
			msg = "needs force";
			break;

		case REF_STATUS_REJECT_STALE:
			res = "error";
			msg = "stale info";
			break;

		case REF_STATUS_REJECT_ALREADY_EXISTS:
			res = "error";
			msg = "already exists";
			break;

		case REF_STATUS_REJECT_NODELETE:
		case REF_STATUS_REMOTE_REJECT:
			res = "error";
			break;

		case REF_STATUS_EXPECTING_REPORT:
		default:
			continue;
		}

		strbuf_reset(&buf);
		strbuf_addf(&buf, "%s %s", res, ref->name);
		if (ref->remote_status)
			msg = ref->remote_status;
		if (msg) {
			strbuf_addch(&buf, ' ');
			quote_two_c_style(&buf, "", msg, 0);
		}
		strbuf_addch(&buf, '\n');

		write_or_die(1, buf.buf, buf.len);
	}
	strbuf_release(&buf);
}

static int send_pack_config(const char *k, const char *v, void *cb)
{
	git_gpg_config(k, v, NULL);

	if (!strcmp(k, "push.gpgsign")) {
		const char *value;
		if (!git_config_get_value("push.gpgsign", &value)) {
			switch (git_config_maybe_bool("push.gpgsign", value)) {
			case 0:
				args.push_cert = SEND_PACK_PUSH_CERT_NEVER;
				break;
			case 1:
				args.push_cert = SEND_PACK_PUSH_CERT_ALWAYS;
				break;
			default:
				if (value && !strcasecmp(value, "if-asked"))
					args.push_cert = SEND_PACK_PUSH_CERT_IF_ASKED;
				else
					return error("Invalid value for '%s'", k);
			}
		}
	}
	return 0;
}

int cmd_send_pack(int argc, const char **argv, const char *prefix)
{
	int i, nr_refspecs = 0;
	const char **refspecs = NULL;
	const char *remote_name = NULL;
	struct remote *remote = NULL;
	const char *dest = NULL;
	int fd[2];
	struct child_process *conn;
	struct sha1_array extra_have = SHA1_ARRAY_INIT;
	struct sha1_array shallow = SHA1_ARRAY_INIT;
	struct ref *remote_refs, *local_refs;
	int ret;
	int helper_status = 0;
	int send_all = 0;
	int verbose = 0;
	const char *receivepack = "git-receive-pack";
	unsigned dry_run = 0;
	unsigned send_mirror = 0;
	unsigned force_update = 0;
	unsigned quiet = 0;
	int push_cert = 0;
	unsigned use_thin_pack = 0;
	unsigned atomic = 0;
	unsigned stateless_rpc = 0;
	int flags;
	unsigned int reject_reasons;
	int progress = -1;
	int from_stdin = 0;
	struct push_cas_option cas = {0};

	struct option options[] = {
		OPT__VERBOSITY(&verbose),
		OPT_STRING(0, "receive-pack", &receivepack, "receive-pack", N_("receive pack program")),
		OPT_STRING(0, "exec", &receivepack, "receive-pack", N_("receive pack program")),
		OPT_STRING(0, "remote", &remote_name, "remote", N_("remote name")),
		OPT_BOOL(0, "all", &send_all, N_("push all refs")),
		OPT_BOOL('n' , "dry-run", &dry_run, N_("dry run")),
		OPT_BOOL(0, "mirror", &send_mirror, N_("mirror all refs")),
		OPT_BOOL('f', "force", &force_update, N_("force updates")),
		{ OPTION_CALLBACK,
		  0, "signed", &push_cert, "yes|no|if-asked", N_("GPG sign the push"),
		  PARSE_OPT_OPTARG, option_parse_push_signed },
		OPT_BOOL(0, "progress", &progress, N_("force progress reporting")),
		OPT_BOOL(0, "thin", &use_thin_pack, N_("use thin pack")),
		OPT_BOOL(0, "atomic", &atomic, N_("request atomic transaction on remote side")),
		OPT_BOOL(0, "stateless-rpc", &stateless_rpc, N_("use stateless RPC protocol")),
		OPT_BOOL(0, "stdin", &from_stdin, N_("read refs from stdin")),
		OPT_BOOL(0, "helper-status", &helper_status, N_("print status from remote helper")),
		{ OPTION_CALLBACK,
		  0, CAS_OPT_NAME, &cas, N_("refname>:<expect"),
		  N_("require old value of ref to be at this value"),
		  PARSE_OPT_OPTARG, parseopt_push_cas_option },
		OPT_END()
	};

	git_config(send_pack_config, NULL);
	argc = parse_options(argc, argv, prefix, options, send_pack_usage, 0);
	if (argc > 0) {
		dest = argv[0];
		refspecs = (const char **)(argv + 1);
		nr_refspecs = argc - 1;
	}

	if (!dest)
		usage_with_options(send_pack_usage, options);

	args.verbose = verbose;
	args.dry_run = dry_run;
	args.send_mirror = send_mirror;
	args.force_update = force_update;
	args.quiet = quiet;
	args.push_cert = push_cert;
	args.progress = progress;
	args.use_thin_pack = use_thin_pack;
	args.atomic = atomic;
	args.stateless_rpc = stateless_rpc;

	if (from_stdin) {
		struct argv_array all_refspecs = ARGV_ARRAY_INIT;

		for (i = 0; i < nr_refspecs; i++)
			argv_array_push(&all_refspecs, refspecs[i]);

		if (args.stateless_rpc) {
			const char *buf;
			while ((buf = packet_read_line(0, NULL)))
				argv_array_push(&all_refspecs, buf);
		} else {
			struct strbuf line = STRBUF_INIT;
			while (strbuf_getline(&line, stdin) != EOF)
				argv_array_push(&all_refspecs, line.buf);
			strbuf_release(&line);
		}

		refspecs = all_refspecs.argv;
		nr_refspecs = all_refspecs.argc;
	}

	/*
	 * --all and --mirror are incompatible; neither makes sense
	 * with any refspecs.
	 */
	if ((nr_refspecs > 0 && (send_all || args.send_mirror)) ||
	    (send_all && args.send_mirror))
		usage_with_options(send_pack_usage, options);

	if (remote_name) {
		remote = remote_get(remote_name);
		if (!remote_has_url(remote, dest)) {
			die("Destination %s is not a uri for %s",
			    dest, remote_name);
		}
	}

	if (progress == -1)
		progress = !args.quiet && isatty(2);
	args.progress = progress;

	if (args.stateless_rpc) {
		conn = NULL;
		fd[0] = 0;
		fd[1] = 1;
	} else {
		conn = git_connect(fd, dest, receivepack,
			args.verbose ? CONNECT_VERBOSE : 0);
	}

	get_remote_heads(fd[0], NULL, 0, &remote_refs, REF_NORMAL,
			 &extra_have, &shallow);

	transport_verify_remote_names(nr_refspecs, refspecs);

	local_refs = get_local_heads();

	flags = MATCH_REFS_NONE;

	if (send_all)
		flags |= MATCH_REFS_ALL;
	if (args.send_mirror)
		flags |= MATCH_REFS_MIRROR;

	/* match them up */
	if (match_push_refs(local_refs, &remote_refs, nr_refspecs, refspecs, flags))
		return -1;

	if (!is_empty_cas(&cas))
		apply_push_cas(&cas, remote, remote_refs);

	set_ref_status_for_push(remote_refs, args.send_mirror,
		args.force_update);

	ret = send_pack(&args, fd, conn, remote_refs, &extra_have);

	if (helper_status)
		print_helper_status(remote_refs);

	close(fd[1]);
	close(fd[0]);

	ret |= finish_connect(conn);

	if (!helper_status)
		transport_print_push_status(dest, remote_refs, args.verbose, 0, &reject_reasons);

	if (!args.dry_run && remote) {
		struct ref *ref;
		for (ref = remote_refs; ref; ref = ref->next)
			transport_update_tracking_ref(remote, ref, args.verbose);
	}

	if (!ret && !transport_refs_pushed(remote_refs))
		fprintf(stderr, "Everything up-to-date\n");

	return ret;
}
