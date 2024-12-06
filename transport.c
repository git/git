#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "advice.h"
#include "config.h"
#include "environment.h"
#include "hex.h"
#include "transport.h"
#include "hook.h"
#include "pkt-line.h"
#include "fetch-pack.h"
#include "remote.h"
#include "connect.h"
#include "send-pack.h"
#include "bundle.h"
#include "gettext.h"
#include "refs.h"
#include "refspec.h"
#include "branch.h"
#include "url.h"
#include "submodule.h"
#include "string-list.h"
#include "oid-array.h"
#include "sigchain.h"
#include "trace2.h"
#include "transport-internal.h"
#include "protocol.h"
#include "object-name.h"
#include "color.h"
#include "bundle-uri.h"

static int transport_use_color = -1;
static char transport_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_RED		/* REJECTED */
};

enum color_transport {
	TRANSPORT_COLOR_RESET = 0,
	TRANSPORT_COLOR_REJECTED = 1
};

static int transport_color_config(void)
{
	const char *keys[] = {
		"color.transport.reset",
		"color.transport.rejected"
	}, *key = "color.transport";
	char *value;
	int i;
	static int initialized;

	if (initialized)
		return 0;
	initialized = 1;

	if (!git_config_get_string(key, &value))
		transport_use_color = git_config_colorbool(key, value);

	if (!want_color_stderr(transport_use_color))
		return 0;

	for (i = 0; i < ARRAY_SIZE(keys); i++)
		if (!git_config_get_string(keys[i], &value)) {
			if (!value)
				return config_error_nonbool(keys[i]);
			if (color_parse(value, transport_colors[i]) < 0)
				return -1;
		}

	return 0;
}

static const char *transport_get_color(enum color_transport ix)
{
	if (want_color_stderr(transport_use_color))
		return transport_colors[ix];
	return "";
}

static void set_upstreams(struct transport *transport, struct ref *refs,
	int pretend)
{
	struct ref *ref;
	for (ref = refs; ref; ref = ref->next) {
		const char *localname;
		const char *tmp;
		const char *remotename;
		int flag = 0;
		/*
		 * Check suitability for tracking. Must be successful /
		 * already up-to-date ref create/modify (not delete).
		 */
		if (ref->status != REF_STATUS_OK &&
			ref->status != REF_STATUS_UPTODATE)
			continue;
		if (!ref->peer_ref)
			continue;
		if (is_null_oid(&ref->new_oid))
			continue;

		/* Follow symbolic refs (mainly for HEAD). */
		localname = ref->peer_ref->name;
		remotename = ref->name;
		tmp = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
					      localname, RESOLVE_REF_READING,
					      NULL, &flag);
		if (tmp && flag & REF_ISSYMREF &&
			starts_with(tmp, "refs/heads/"))
			localname = tmp;

		/* Both source and destination must be local branches. */
		if (!localname || !starts_with(localname, "refs/heads/"))
			continue;
		if (!remotename || !starts_with(remotename, "refs/heads/"))
			continue;

		if (!pretend) {
			int flag = transport->verbose < 0 ? 0 : BRANCH_CONFIG_VERBOSE;
			install_branch_config(flag, localname + 11,
				transport->remote->name, remotename);
		} else if (transport->verbose >= 0)
			printf(_("Would set upstream of '%s' to '%s' of '%s'\n"),
				localname + 11, remotename + 11,
				transport->remote->name);
	}
}

struct bundle_transport_data {
	int fd;
	struct bundle_header header;
	unsigned get_refs_from_bundle_called : 1;
};

static void get_refs_from_bundle_inner(struct transport *transport)
{
	struct bundle_transport_data *data = transport->data;

	data->get_refs_from_bundle_called = 1;

	if (data->fd > 0)
		close(data->fd);
	data->fd = read_bundle_header(transport->url, &data->header);
	if (data->fd < 0)
		die(_("could not read bundle '%s'"), transport->url);

	transport->hash_algo = data->header.hash_algo;
}

static struct ref *get_refs_from_bundle(struct transport *transport,
					int for_push,
					struct transport_ls_refs_options *transport_options UNUSED)
{
	struct bundle_transport_data *data = transport->data;
	struct ref *result = NULL;
	int i;

	if (for_push)
		return NULL;

	get_refs_from_bundle_inner(transport);

	for (i = 0; i < data->header.references.nr; i++) {
		struct string_list_item *e = data->header.references.items + i;
		const char *name = e->string;
		struct ref *ref = alloc_ref(name);
		struct object_id *oid = e->util;
		oidcpy(&ref->old_oid, oid);
		ref->next = result;
		result = ref;
	}
	return result;
}

static int fetch_refs_from_bundle(struct transport *transport,
				  int nr_heads UNUSED,
				  struct ref **to_fetch UNUSED)
{
	struct bundle_transport_data *data = transport->data;
	struct strvec extra_index_pack_args = STRVEC_INIT;
	int ret;

	if (transport->progress)
		strvec_push(&extra_index_pack_args, "-v");

	if (!data->get_refs_from_bundle_called)
		get_refs_from_bundle_inner(transport);
	ret = unbundle(the_repository, &data->header, data->fd,
		       &extra_index_pack_args,
		       fetch_pack_fsck_objects() ? VERIFY_BUNDLE_FSCK : 0);
	transport->hash_algo = data->header.hash_algo;

	strvec_clear(&extra_index_pack_args);
	return ret;
}

static int close_bundle(struct transport *transport)
{
	struct bundle_transport_data *data = transport->data;
	if (data->fd > 0)
		close(data->fd);
	bundle_header_release(&data->header);
	free(data);
	return 0;
}

struct git_transport_data {
	struct git_transport_options options;
	struct child_process *conn;
	int fd[2];
	unsigned finished_handshake : 1;
	enum protocol_version version;
	struct oid_array extra_have;
	struct oid_array shallow;
};

static int set_git_option(struct git_transport_options *opts,
			  const char *name, const char *value)
{
	if (!strcmp(name, TRANS_OPT_UPLOADPACK)) {
		opts->uploadpack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_RECEIVEPACK)) {
		opts->receivepack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_THIN)) {
		opts->thin = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_FOLLOWTAGS)) {
		opts->followtags = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_KEEP)) {
		opts->keep = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_UPDATE_SHALLOW)) {
		opts->update_shallow = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_DEPTH)) {
		if (!value)
			opts->depth = 0;
		else {
			char *end;
			opts->depth = strtol(value, &end, 0);
			if (*end)
				die(_("transport: invalid depth option '%s'"), value);
		}
		return 0;
	} else if (!strcmp(name, TRANS_OPT_DEEPEN_SINCE)) {
		opts->deepen_since = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_DEEPEN_NOT)) {
		opts->deepen_not = (const struct string_list *)value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_DEEPEN_RELATIVE)) {
		opts->deepen_relative = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_FROM_PROMISOR)) {
		opts->from_promisor = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_LIST_OBJECTS_FILTER)) {
		list_objects_filter_die_if_populated(&opts->filter_options);
		parse_list_objects_filter(&opts->filter_options, value);
		return 0;
	} else if (!strcmp(name, TRANS_OPT_REFETCH)) {
		opts->refetch = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_REJECT_SHALLOW)) {
		opts->reject_shallow = !!value;
		return 0;
	}
	return 1;
}

static int connect_setup(struct transport *transport, int for_push)
{
	struct git_transport_data *data = transport->data;
	int flags = transport->verbose > 0 ? CONNECT_VERBOSE : 0;

	if (data->conn)
		return 0;

	switch (transport->family) {
	case TRANSPORT_FAMILY_ALL: break;
	case TRANSPORT_FAMILY_IPV4: flags |= CONNECT_IPV4; break;
	case TRANSPORT_FAMILY_IPV6: flags |= CONNECT_IPV6; break;
	}

	data->conn = git_connect(data->fd, transport->url,
				 for_push ?
					"git-receive-pack" :
					"git-upload-pack",
				 for_push ?
					data->options.receivepack :
					data->options.uploadpack,
				 flags);

	return 0;
}

static void die_if_server_options(struct transport *transport)
{
	if (!transport->server_options || !transport->server_options->nr)
		return;
	advise(_("see protocol.version in 'git help config' for more details"));
	die(_("server options require protocol version 2 or later"));
}

/*
 * Obtains the protocol version from the transport and writes it to
 * transport->data->version, first connecting if not already connected.
 *
 * If the protocol version is one that allows skipping the listing of remote
 * refs, and must_list_refs is 0, the listing of remote refs is skipped and
 * this function returns NULL. Otherwise, this function returns the list of
 * remote refs.
 */
static struct ref *handshake(struct transport *transport, int for_push,
			     struct transport_ls_refs_options *options,
			     int must_list_refs)
{
	struct git_transport_data *data = transport->data;
	struct ref *refs = NULL;
	struct packet_reader reader;
	size_t sid_len;
	const char *server_sid;

	connect_setup(transport, for_push);

	packet_reader_init(&reader, data->fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	data->version = discover_version(&reader);
	switch (data->version) {
	case protocol_v2:
		if ((!transport->server_options || !transport->server_options->nr) &&
		    transport->remote->server_options.nr)
			transport->server_options = &transport->remote->server_options;
		if (server_feature_v2("session-id", &server_sid))
			trace2_data_string("transfer", NULL, "server-sid", server_sid);
		if (must_list_refs)
			get_remote_refs(data->fd[1], &reader, &refs, for_push,
					options,
					transport->server_options,
					transport->stateless_rpc);
		break;
	case protocol_v1:
	case protocol_v0:
		die_if_server_options(transport);
		get_remote_heads(&reader, &refs,
				 for_push ? REF_NORMAL : 0,
				 &data->extra_have,
				 &data->shallow);
		server_sid = server_feature_value("session-id", &sid_len);
		if (server_sid) {
			char *sid = xstrndup(server_sid, sid_len);
			trace2_data_string("transfer", NULL, "server-sid", sid);
			free(sid);
		}
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}
	data->finished_handshake = 1;
	transport->hash_algo = reader.hash_algo;

	if (reader.line_peeked)
		BUG("buffer must be empty at the end of handshake()");

	return refs;
}

static struct ref *get_refs_via_connect(struct transport *transport, int for_push,
					struct transport_ls_refs_options *options)
{
	return handshake(transport, for_push, options, 1);
}

static int get_bundle_uri(struct transport *transport)
{
	struct git_transport_data *data = transport->data;
	struct packet_reader reader;
	int stateless_rpc = transport->stateless_rpc;

	if (!transport->bundles) {
		CALLOC_ARRAY(transport->bundles, 1);
		init_bundle_list(transport->bundles);
	}

	if (!data->finished_handshake) {
		struct ref *refs = handshake(transport, 0, NULL, 0);

		if (refs)
			free_refs(refs);
	}

	/*
	 * "Support" protocol v0 and v2 without bundle-uri support by
	 * silently degrading to a NOOP.
	 */
	if (!server_supports_v2("bundle-uri"))
		return 0;

	packet_reader_init(&reader, data->fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF);

	return get_remote_bundle_uri(data->fd[1], &reader,
				     transport->bundles, stateless_rpc);
}

static int fetch_refs_via_pack(struct transport *transport,
			       int nr_heads, struct ref **to_fetch)
{
	int ret = 0;
	struct git_transport_data *data = transport->data;
	struct ref *refs = NULL;
	struct fetch_pack_args args;
	struct ref *refs_tmp = NULL, **to_fetch_dup = NULL;

	memset(&args, 0, sizeof(args));
	args.uploadpack = data->options.uploadpack;
	args.keep_pack = data->options.keep;
	args.lock_pack = 1;
	args.use_thin_pack = data->options.thin;
	args.include_tag = data->options.followtags;
	args.verbose = (transport->verbose > 1);
	args.quiet = (transport->verbose < 0);
	args.no_progress = !transport->progress;
	args.depth = data->options.depth;
	args.deepen_since = data->options.deepen_since;
	args.deepen_not = data->options.deepen_not;
	args.deepen_relative = data->options.deepen_relative;
	args.check_self_contained_and_connected =
		data->options.check_self_contained_and_connected;
	args.cloning = transport->cloning;
	args.update_shallow = data->options.update_shallow;
	args.from_promisor = data->options.from_promisor;
	list_objects_filter_copy(&args.filter_options,
				 &data->options.filter_options);
	args.refetch = data->options.refetch;
	args.stateless_rpc = transport->stateless_rpc;
	args.server_options = transport->server_options;
	args.negotiation_tips = data->options.negotiation_tips;
	args.reject_shallow_remote = transport->smart_options->reject_shallow;

	if (!data->finished_handshake) {
		int i;
		int must_list_refs = 0;
		for (i = 0; i < nr_heads; i++) {
			if (!to_fetch[i]->exact_oid) {
				must_list_refs = 1;
				break;
			}
		}
		refs_tmp = handshake(transport, 0, NULL, must_list_refs);
	}

	if (data->version == protocol_unknown_version)
		BUG("unknown protocol version");
	else if (data->version <= protocol_v1)
		die_if_server_options(transport);

	if (data->options.acked_commits) {
		if (data->version < protocol_v2) {
			warning(_("--negotiate-only requires protocol v2"));
			ret = -1;
		} else if (!server_supports_feature("fetch", "wait-for-done", 0)) {
			warning(_("server does not support wait-for-done"));
			ret = -1;
		} else {
			negotiate_using_fetch(data->options.negotiation_tips,
					      transport->server_options,
					      transport->stateless_rpc,
					      data->fd,
					      data->options.acked_commits);
			ret = 0;
		}
		goto cleanup;
	}

	/*
	 * Create a shallow copy of `sought` so that we can free all of its entries.
	 * This is because `fetch_pack()` will modify the array to evict some
	 * entries, but won't free those.
	 */
	DUP_ARRAY(to_fetch_dup, to_fetch, nr_heads);
	to_fetch = to_fetch_dup;

	refs = fetch_pack(&args, data->fd,
			  refs_tmp ? refs_tmp : transport->remote_refs,
			  to_fetch, nr_heads, &data->shallow,
			  &transport->pack_lockfiles, data->version);

	data->finished_handshake = 0;
	data->options.self_contained_and_connected =
		args.self_contained_and_connected;
	data->options.connectivity_checked = args.connectivity_checked;

	if (!refs)
		ret = -1;
	if (report_unmatched_refs(to_fetch, nr_heads))
		ret = -1;

cleanup:
	close(data->fd[0]);
	if (data->fd[1] >= 0)
		close(data->fd[1]);
	if (finish_connect(data->conn))
		ret = -1;
	data->conn = NULL;

	free(to_fetch_dup);
	free_refs(refs_tmp);
	free_refs(refs);
	list_objects_filter_release(&args.filter_options);
	return ret;
}

static int push_had_errors(struct ref *ref)
{
	for (; ref; ref = ref->next) {
		switch (ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
		case REF_STATUS_OK:
			break;
		default:
			return 1;
		}
	}
	return 0;
}

int transport_refs_pushed(struct ref *ref)
{
	for (; ref; ref = ref->next) {
		switch(ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
			break;
		default:
			return 1;
		}
	}
	return 0;
}

static void update_one_tracking_ref(struct remote *remote, char *refname,
				    struct object_id *new_oid, int deletion,
				    int verbose)
{
	struct refspec_item rs;

	memset(&rs, 0, sizeof(rs));
	rs.src = refname;
	rs.dst = NULL;

	if (!remote_find_tracking(remote, &rs)) {
		if (verbose)
			fprintf(stderr, "updating local tracking ref '%s'\n", rs.dst);
		if (deletion)
			refs_delete_ref(get_main_ref_store(the_repository),
					NULL, rs.dst, NULL, 0);
		else
			refs_update_ref(get_main_ref_store(the_repository),
					"update by push", rs.dst, new_oid,
					NULL, 0, 0);
		free(rs.dst);
	}
}

void transport_update_tracking_ref(struct remote *remote, struct ref *ref, int verbose)
{
	char *refname;
	struct object_id *new_oid;
	struct ref_push_report *report;

	if (ref->status != REF_STATUS_OK && ref->status != REF_STATUS_UPTODATE)
		return;

	report = ref->report;
	if (!report)
		update_one_tracking_ref(remote, ref->name, &ref->new_oid,
					ref->deletion, verbose);
	else
		for (; report; report = report->next) {
			refname = report->ref_name ? (char *)report->ref_name : ref->name;
			new_oid = report->new_oid ? report->new_oid : &ref->new_oid;
			update_one_tracking_ref(remote, refname, new_oid,
						is_null_oid(new_oid), verbose);
		}
}

static void print_ref_status(char flag, const char *summary,
			     struct ref *to, struct ref *from, const char *msg,
			     struct ref_push_report *report,
			     int porcelain, int summary_width)
{
	const char *to_name;

	if (report && report->ref_name)
		to_name = report->ref_name;
	else
		to_name = to->name;

	if (porcelain) {
		if (from)
			fprintf(stdout, "%c\t%s:%s\t", flag, from->name, to_name);
		else
			fprintf(stdout, "%c\t:%s\t", flag, to_name);
		if (msg)
			fprintf(stdout, "%s (%s)\n", summary, msg);
		else
			fprintf(stdout, "%s\n", summary);
	} else {
		const char *red = "", *reset = "";
		if (push_had_errors(to)) {
			red = transport_get_color(TRANSPORT_COLOR_REJECTED);
			reset = transport_get_color(TRANSPORT_COLOR_RESET);
		}
		fprintf(stderr, " %s%c %-*s%s ", red, flag, summary_width,
			summary, reset);
		if (from)
			fprintf(stderr, "%s -> %s",
				prettify_refname(from->name),
				prettify_refname(to_name));
		else
			fputs(prettify_refname(to_name), stderr);
		if (msg) {
			fputs(" (", stderr);
			fputs(msg, stderr);
			fputc(')', stderr);
		}
		fputc('\n', stderr);
	}
}

static void print_ok_ref_status(struct ref *ref,
				struct ref_push_report *report,
				int porcelain, int summary_width)
{
	struct object_id *old_oid;
	struct object_id *new_oid;
	const char *ref_name;
	int forced_update;

	if (report && report->old_oid)
		old_oid = report->old_oid;
	else
		old_oid = &ref->old_oid;
	if (report && report->new_oid)
		new_oid = report->new_oid;
	else
		new_oid = &ref->new_oid;
	if (report && report->forced_update)
		forced_update = report->forced_update;
	else
		forced_update = ref->forced_update;
	if (report && report->ref_name)
		ref_name = report->ref_name;
	else
		ref_name = ref->name;

	if (ref->deletion)
		print_ref_status('-', "[deleted]", ref, NULL, NULL,
				 report, porcelain, summary_width);
	else if (is_null_oid(old_oid))
		print_ref_status('*',
				 (starts_with(ref_name, "refs/tags/")
				  ? "[new tag]"
				  : (starts_with(ref_name, "refs/heads/")
				     ? "[new branch]"
				     : "[new reference]")),
				 ref, ref->peer_ref, NULL,
				 report, porcelain, summary_width);
	else {
		struct strbuf quickref = STRBUF_INIT;
		char type;
		const char *msg;

		strbuf_add_unique_abbrev(&quickref, old_oid,
					 DEFAULT_ABBREV);
		if (forced_update) {
			strbuf_addstr(&quickref, "...");
			type = '+';
			msg = "forced update";
		} else {
			strbuf_addstr(&quickref, "..");
			type = ' ';
			msg = NULL;
		}
		strbuf_add_unique_abbrev(&quickref, new_oid,
					 DEFAULT_ABBREV);

		print_ref_status(type, quickref.buf, ref, ref->peer_ref, msg,
				 report, porcelain, summary_width);
		strbuf_release(&quickref);
	}
}

static int print_one_push_report(struct ref *ref, const char *dest, int count,
				 struct ref_push_report *report,
				 int porcelain, int summary_width)
{
	if (!count) {
		char *url = transport_anonymize_url(dest);
		fprintf(porcelain ? stdout : stderr, "To %s\n", url);
		free(url);
	}

	switch(ref->status) {
	case REF_STATUS_NONE:
		print_ref_status('X', "[no match]", ref, NULL, NULL,
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_NODELETE:
		print_ref_status('!', "[rejected]", ref, NULL,
				 "remote does not support deleting refs",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_UPTODATE:
		print_ref_status('=', "[up to date]", ref,
				 ref->peer_ref, NULL,
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_NONFASTFORWARD:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "non-fast-forward",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_ALREADY_EXISTS:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "already exists",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_FETCH_FIRST:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "fetch first",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_NEEDS_FORCE:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "needs force",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_STALE:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "stale info",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_REMOTE_UPDATED:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "remote ref updated since checkout",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REJECT_SHALLOW:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "new shallow roots not allowed",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_REMOTE_REJECT:
		print_ref_status('!', "[remote rejected]", ref,
				 ref->deletion ? NULL : ref->peer_ref,
				 ref->remote_status,
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_EXPECTING_REPORT:
		print_ref_status('!', "[remote failure]", ref,
				 ref->deletion ? NULL : ref->peer_ref,
				 "remote failed to report status",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_ATOMIC_PUSH_FAILED:
		print_ref_status('!', "[rejected]", ref, ref->peer_ref,
				 "atomic push failed",
				 report, porcelain, summary_width);
		break;
	case REF_STATUS_OK:
		print_ok_ref_status(ref, report, porcelain, summary_width);
		break;
	}

	return 1;
}

static int print_one_push_status(struct ref *ref, const char *dest, int count,
				 int porcelain, int summary_width)
{
	struct ref_push_report *report;
	int n = 0;

	if (!ref->report)
		return print_one_push_report(ref, dest, count,
					     NULL, porcelain, summary_width);

	for (report = ref->report; report; report = report->next)
		print_one_push_report(ref, dest, count + n++,
				      report, porcelain, summary_width);
	return n;
}

static int measure_abbrev(const struct object_id *oid, int sofar)
{
	char hex[GIT_MAX_HEXSZ + 1];
	int w = repo_find_unique_abbrev_r(the_repository, hex, oid,
					  DEFAULT_ABBREV);

	return (w < sofar) ? sofar : w;
}

int transport_summary_width(const struct ref *refs)
{
	int maxw = -1;

	for (; refs; refs = refs->next) {
		maxw = measure_abbrev(&refs->old_oid, maxw);
		maxw = measure_abbrev(&refs->new_oid, maxw);
	}
	if (maxw < 0)
		maxw = FALLBACK_DEFAULT_ABBREV;
	return (2 * maxw + 3);
}

void transport_print_push_status(const char *dest, struct ref *refs,
				  int verbose, int porcelain, unsigned int *reject_reasons)
{
	struct ref *ref;
	int n = 0;
	char *head;
	int summary_width = transport_summary_width(refs);

	if (transport_color_config() < 0)
		warning(_("could not parse transport.color.* config"));

	head = refs_resolve_refdup(get_main_ref_store(the_repository), "HEAD",
				   RESOLVE_REF_READING, NULL, NULL);

	if (verbose) {
		for (ref = refs; ref; ref = ref->next)
			if (ref->status == REF_STATUS_UPTODATE)
				n += print_one_push_status(ref, dest, n,
							   porcelain, summary_width);
	}

	for (ref = refs; ref; ref = ref->next)
		if (ref->status == REF_STATUS_OK)
			n += print_one_push_status(ref, dest, n,
						   porcelain, summary_width);

	*reject_reasons = 0;
	for (ref = refs; ref; ref = ref->next) {
		if (ref->status != REF_STATUS_NONE &&
		    ref->status != REF_STATUS_UPTODATE &&
		    ref->status != REF_STATUS_OK)
			n += print_one_push_status(ref, dest, n,
						   porcelain, summary_width);
		if (ref->status == REF_STATUS_REJECT_NONFASTFORWARD) {
			if (head != NULL && !strcmp(head, ref->name))
				*reject_reasons |= REJECT_NON_FF_HEAD;
			else
				*reject_reasons |= REJECT_NON_FF_OTHER;
		} else if (ref->status == REF_STATUS_REJECT_ALREADY_EXISTS) {
			*reject_reasons |= REJECT_ALREADY_EXISTS;
		} else if (ref->status == REF_STATUS_REJECT_FETCH_FIRST) {
			*reject_reasons |= REJECT_FETCH_FIRST;
		} else if (ref->status == REF_STATUS_REJECT_NEEDS_FORCE) {
			*reject_reasons |= REJECT_NEEDS_FORCE;
		} else if (ref->status == REF_STATUS_REJECT_REMOTE_UPDATED) {
			*reject_reasons |= REJECT_REF_NEEDS_UPDATE;
		}
	}
	free(head);
}

static int git_transport_push(struct transport *transport, struct ref *remote_refs, int flags)
{
	struct git_transport_data *data = transport->data;
	struct send_pack_args args;
	int ret = 0;

	if (transport_color_config() < 0)
		return -1;

	if (!data->finished_handshake)
		get_refs_via_connect(transport, 1, NULL);

	memset(&args, 0, sizeof(args));
	args.send_mirror = !!(flags & TRANSPORT_PUSH_MIRROR);
	args.force_update = !!(flags & TRANSPORT_PUSH_FORCE);
	args.use_thin_pack = data->options.thin;
	args.verbose = (transport->verbose > 0);
	args.quiet = (transport->verbose < 0);
	args.progress = transport->progress;
	args.dry_run = !!(flags & TRANSPORT_PUSH_DRY_RUN);
	args.porcelain = !!(flags & TRANSPORT_PUSH_PORCELAIN);
	args.atomic = !!(flags & TRANSPORT_PUSH_ATOMIC);
	args.push_options = transport->push_options;
	args.url = transport->url;

	if (flags & TRANSPORT_PUSH_CERT_ALWAYS)
		args.push_cert = SEND_PACK_PUSH_CERT_ALWAYS;
	else if (flags & TRANSPORT_PUSH_CERT_IF_ASKED)
		args.push_cert = SEND_PACK_PUSH_CERT_IF_ASKED;
	else
		args.push_cert = SEND_PACK_PUSH_CERT_NEVER;

	switch (data->version) {
	case protocol_v2:
		die(_("support for protocol v2 not implemented yet"));
		break;
	case protocol_v1:
	case protocol_v0:
		ret = send_pack(&args, data->fd, data->conn, remote_refs,
				&data->extra_have);
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	close(data->fd[1]);
	close(data->fd[0]);
	/*
	 * Atomic push may abort the connection early and close the pipe,
	 * which may cause an error for `finish_connect()`. Ignore this error
	 * for atomic git-push.
	 */
	if (ret || args.atomic)
		finish_connect(data->conn);
	else
		ret = finish_connect(data->conn);
	data->conn = NULL;
	data->finished_handshake = 0;

	return ret;
}

static int connect_git(struct transport *transport, const char *name,
		       const char *executable, int fd[2])
{
	struct git_transport_data *data = transport->data;
	data->conn = git_connect(data->fd, transport->url,
				 name, executable, 0);
	fd[0] = data->fd[0];
	fd[1] = data->fd[1];
	return 0;
}

static int disconnect_git(struct transport *transport)
{
	struct git_transport_data *data = transport->data;
	if (data->conn) {
		if (data->finished_handshake && !transport->stateless_rpc)
			packet_flush(data->fd[1]);
		close(data->fd[0]);
		if (data->fd[1] >= 0)
			close(data->fd[1]);
		finish_connect(data->conn);
	}

	if (data->options.negotiation_tips) {
		oid_array_clear(data->options.negotiation_tips);
		free(data->options.negotiation_tips);
	}
	list_objects_filter_release(&data->options.filter_options);
	oid_array_clear(&data->extra_have);
	oid_array_clear(&data->shallow);
	free(data);
	return 0;
}

static struct transport_vtable taken_over_vtable = {
	.get_refs_list	= get_refs_via_connect,
	.get_bundle_uri = get_bundle_uri,
	.fetch_refs	= fetch_refs_via_pack,
	.push_refs	= git_transport_push,
	.disconnect	= disconnect_git
};

void transport_take_over(struct transport *transport,
			 struct child_process *child)
{
	struct git_transport_data *data;

	if (!transport->smart_options)
		BUG("taking over transport requires non-NULL "
		    "smart_options field.");

	CALLOC_ARRAY(data, 1);
	data->options = *transport->smart_options;
	data->conn = child;
	data->fd[0] = data->conn->out;
	data->fd[1] = data->conn->in;
	data->finished_handshake = 0;
	transport->data = data;

	transport->vtable = &taken_over_vtable;
	transport->smart_options = &(data->options);

	transport->cannot_reuse = 1;
}

static int is_file(const char *url)
{
	struct stat buf;
	if (stat(url, &buf))
		return 0;
	return S_ISREG(buf.st_mode);
}

static int external_specification_len(const char *url)
{
	return strchr(url, ':') - url;
}

static const struct string_list *protocol_allow_list(void)
{
	static int enabled = -1;
	static struct string_list allowed = STRING_LIST_INIT_DUP;

	if (enabled < 0) {
		const char *v = getenv("GIT_ALLOW_PROTOCOL");
		if (v) {
			string_list_split(&allowed, v, ':', -1);
			string_list_sort(&allowed);
			enabled = 1;
		} else {
			enabled = 0;
		}
	}

	return enabled ? &allowed : NULL;
}

enum protocol_allow_config {
	PROTOCOL_ALLOW_NEVER = 0,
	PROTOCOL_ALLOW_USER_ONLY,
	PROTOCOL_ALLOW_ALWAYS
};

static enum protocol_allow_config parse_protocol_config(const char *key,
							const char *value)
{
	if (!strcasecmp(value, "always"))
		return PROTOCOL_ALLOW_ALWAYS;
	else if (!strcasecmp(value, "never"))
		return PROTOCOL_ALLOW_NEVER;
	else if (!strcasecmp(value, "user"))
		return PROTOCOL_ALLOW_USER_ONLY;

	die(_("unknown value for config '%s': %s"), key, value);
}

static enum protocol_allow_config get_protocol_config(const char *type)
{
	char *key = xstrfmt("protocol.%s.allow", type);
	char *value;

	/* first check the per-protocol config */
	if (!git_config_get_string(key, &value)) {
		enum protocol_allow_config ret =
			parse_protocol_config(key, value);
		free(key);
		free(value);
		return ret;
	}
	free(key);

	/* if defined, fallback to user-defined default for unknown protocols */
	if (!git_config_get_string("protocol.allow", &value)) {
		enum protocol_allow_config ret =
			parse_protocol_config("protocol.allow", value);
		free(value);
		return ret;
	}

	/* fallback to built-in defaults */
	/* known safe */
	if (!strcmp(type, "http") ||
	    !strcmp(type, "https") ||
	    !strcmp(type, "git") ||
	    !strcmp(type, "ssh"))
		return PROTOCOL_ALLOW_ALWAYS;

	/* known scary; err on the side of caution */
	if (!strcmp(type, "ext"))
		return PROTOCOL_ALLOW_NEVER;

	/* unknown; by default let them be used only directly by the user */
	return PROTOCOL_ALLOW_USER_ONLY;
}

int is_transport_allowed(const char *type, int from_user)
{
	const struct string_list *allow_list = protocol_allow_list();
	if (allow_list)
		return string_list_has_string(allow_list, type);

	switch (get_protocol_config(type)) {
	case PROTOCOL_ALLOW_ALWAYS:
		return 1;
	case PROTOCOL_ALLOW_NEVER:
		return 0;
	case PROTOCOL_ALLOW_USER_ONLY:
		if (from_user < 0)
			from_user = git_env_bool("GIT_PROTOCOL_FROM_USER", 1);
		return from_user;
	}

	BUG("invalid protocol_allow_config type");
}

int parse_transport_option(const char *var, const char *value,
			   struct string_list *transport_options)
{
	if (!value)
		return config_error_nonbool(var);
	if (!*value)
		string_list_clear(transport_options, 0);
	else
		string_list_append(transport_options, value);
	return 0;
}

void transport_check_allowed(const char *type)
{
	if (!is_transport_allowed(type, -1))
		die(_("transport '%s' not allowed"), type);
}

static struct transport_vtable bundle_vtable = {
	.get_refs_list	= get_refs_from_bundle,
	.fetch_refs	= fetch_refs_from_bundle,
	.disconnect	= close_bundle
};

static struct transport_vtable builtin_smart_vtable = {
	.get_refs_list	= get_refs_via_connect,
	.get_bundle_uri = get_bundle_uri,
	.fetch_refs	= fetch_refs_via_pack,
	.push_refs	= git_transport_push,
	.connect	= connect_git,
	.disconnect	= disconnect_git
};

struct transport *transport_get(struct remote *remote, const char *url)
{
	const char *helper;
	char *helper_to_free = NULL;
	const char *p;
	struct transport *ret = xcalloc(1, sizeof(*ret));

	ret->progress = isatty(2);
	string_list_init_dup(&ret->pack_lockfiles);

	CALLOC_ARRAY(ret->bundles, 1);
	init_bundle_list(ret->bundles);

	if (!remote)
		BUG("No remote provided to transport_get()");

	ret->got_remote_refs = 0;
	ret->remote = remote;
	helper = remote->foreign_vcs;

	if (!url)
		url = remote->url.v[0];
	ret->url = url;

	p = url;
	while (is_urlschemechar(p == url, *p))
		p++;
	if (starts_with(p, "::"))
		helper = helper_to_free = xstrndup(url, p - url);

	if (helper) {
		transport_helper_init(ret, helper);
		free(helper_to_free);
	} else if (starts_with(url, "rsync:")) {
		die(_("git-over-rsync is no longer supported"));
	} else if (url_is_local_not_ssh(url) && is_file(url) && is_bundle(url, 1)) {
		struct bundle_transport_data *data = xcalloc(1, sizeof(*data));
		bundle_header_init(&data->header);
		transport_check_allowed("file");
		ret->data = data;
		ret->vtable = &bundle_vtable;
		ret->smart_options = NULL;
	} else if (!is_url(url)
		|| starts_with(url, "file://")
		|| starts_with(url, "git://")
		|| starts_with(url, "ssh://")
		|| starts_with(url, "git+ssh://") /* deprecated - do not use */
		|| starts_with(url, "ssh+git://") /* deprecated - do not use */
		) {
		/*
		 * These are builtin smart transports; "allowed" transports
		 * will be checked individually in git_connect.
		 */
		struct git_transport_data *data = xcalloc(1, sizeof(*data));
		list_objects_filter_init(&data->options.filter_options);
		ret->data = data;
		ret->vtable = &builtin_smart_vtable;
		ret->smart_options = &(data->options);

		data->conn = NULL;
		data->finished_handshake = 0;
	} else {
		/* Unknown protocol in URL. Pass to external handler. */
		int len = external_specification_len(url);
		char *handler = xmemdupz(url, len);
		transport_helper_init(ret, handler);
		free(handler);
	}

	if (ret->smart_options) {
		ret->smart_options->thin = 1;
		ret->smart_options->uploadpack = "git-upload-pack";
		if (remote->uploadpack)
			ret->smart_options->uploadpack = remote->uploadpack;
		ret->smart_options->receivepack = "git-receive-pack";
		if (remote->receivepack)
			ret->smart_options->receivepack = remote->receivepack;
	}

	ret->hash_algo = &hash_algos[GIT_HASH_SHA1];

	return ret;
}

const struct git_hash_algo *transport_get_hash_algo(struct transport *transport)
{
	return transport->hash_algo;
}

int transport_set_option(struct transport *transport,
			 const char *name, const char *value)
{
	int git_reports = 1, protocol_reports = 1;

	if (transport->smart_options)
		git_reports = set_git_option(transport->smart_options,
					     name, value);

	if (transport->vtable->set_option)
		protocol_reports = transport->vtable->set_option(transport,
								 name, value);

	/* If either report is 0, report 0 (success). */
	if (!git_reports || !protocol_reports)
		return 0;
	/* If either reports -1 (invalid value), report -1. */
	if ((git_reports == -1) || (protocol_reports == -1))
		return -1;
	/* Otherwise if both report unknown, report unknown. */
	return 1;
}

void transport_set_verbosity(struct transport *transport, int verbosity,
	int force_progress)
{
	if (verbosity >= 1)
		transport->verbose = verbosity <= 3 ? verbosity : 3;
	if (verbosity < 0)
		transport->verbose = -1;

	/**
	 * Rules used to determine whether to report progress (processing aborts
	 * when a rule is satisfied):
	 *
	 *   . Report progress, if force_progress is 1 (ie. --progress).
	 *   . Don't report progress, if force_progress is 0 (ie. --no-progress).
	 *   . Don't report progress, if verbosity < 0 (ie. -q/--quiet ).
	 *   . Report progress if isatty(2) is 1.
	 **/
	if (force_progress >= 0)
		transport->progress = !!force_progress;
	else
		transport->progress = verbosity >= 0 && isatty(2);
}

static void die_with_unpushed_submodules(struct string_list *needs_pushing)
{
	int i;

	fprintf(stderr, _("The following submodule paths contain changes that can\n"
			"not be found on any remote:\n"));
	for (i = 0; i < needs_pushing->nr; i++)
		fprintf(stderr, "  %s\n", needs_pushing->items[i].string);
	fprintf(stderr, _("\nPlease try\n\n"
			  "	git push --recurse-submodules=on-demand\n\n"
			  "or cd to the path and use\n\n"
			  "	git push\n\n"
			  "to push them to a remote.\n\n"));

	string_list_clear(needs_pushing, 0);

	die(_("Aborting."));
}

static int run_pre_push_hook(struct transport *transport,
			     struct ref *remote_refs)
{
	int ret = 0, x;
	struct ref *r;
	struct child_process proc = CHILD_PROCESS_INIT;
	struct strbuf buf;
	const char *hook_path = find_hook(the_repository, "pre-push");

	if (!hook_path)
		return 0;

	strvec_push(&proc.args, hook_path);
	strvec_push(&proc.args, transport->remote->name);
	strvec_push(&proc.args, transport->url);

	proc.in = -1;
	proc.trace2_hook_name = "pre-push";

	if (start_command(&proc)) {
		finish_command(&proc);
		return -1;
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	strbuf_init(&buf, 256);

	for (r = remote_refs; r; r = r->next) {
		if (!r->peer_ref) continue;
		if (r->status == REF_STATUS_REJECT_NONFASTFORWARD) continue;
		if (r->status == REF_STATUS_REJECT_STALE) continue;
		if (r->status == REF_STATUS_REJECT_REMOTE_UPDATED) continue;
		if (r->status == REF_STATUS_UPTODATE) continue;

		strbuf_reset(&buf);
		strbuf_addf( &buf, "%s %s %s %s\n",
			 r->peer_ref->name, oid_to_hex(&r->new_oid),
			 r->name, oid_to_hex(&r->old_oid));

		if (write_in_full(proc.in, buf.buf, buf.len) < 0) {
			/* We do not mind if a hook does not read all refs. */
			if (errno != EPIPE)
				ret = -1;
			break;
		}
	}

	strbuf_release(&buf);

	x = close(proc.in);
	if (!ret)
		ret = x;

	sigchain_pop(SIGPIPE);

	x = finish_command(&proc);
	if (!ret)
		ret = x;

	return ret;
}

int transport_push(struct repository *r,
		   struct transport *transport,
		   struct refspec *rs, int flags,
		   unsigned int *reject_reasons)
{
	struct ref *remote_refs = NULL;
	struct ref *local_refs = NULL;
	int match_flags = MATCH_REFS_NONE;
	int verbose = (transport->verbose > 0);
	int quiet = (transport->verbose < 0);
	int porcelain = flags & TRANSPORT_PUSH_PORCELAIN;
	int pretend = flags & TRANSPORT_PUSH_DRY_RUN;
	int push_ret, err;
	int ret = -1;
	struct transport_ls_refs_options transport_options =
		TRANSPORT_LS_REFS_OPTIONS_INIT;

	*reject_reasons = 0;

	if (transport_color_config() < 0)
		goto done;

	if (!transport->vtable->push_refs)
		goto done;

	local_refs = get_local_heads();

	if (check_push_refs(local_refs, rs) < 0)
		goto done;

	refspec_ref_prefixes(rs, &transport_options.ref_prefixes);

	trace2_region_enter("transport_push", "get_refs_list", r);
	remote_refs = transport->vtable->get_refs_list(transport, 1,
						       &transport_options);
	trace2_region_leave("transport_push", "get_refs_list", r);

	transport_ls_refs_options_release(&transport_options);

	if (flags & TRANSPORT_PUSH_ALL)
		match_flags |= MATCH_REFS_ALL;
	if (flags & TRANSPORT_PUSH_MIRROR)
		match_flags |= MATCH_REFS_MIRROR;
	if (flags & TRANSPORT_PUSH_PRUNE)
		match_flags |= MATCH_REFS_PRUNE;
	if (flags & TRANSPORT_PUSH_FOLLOW_TAGS)
		match_flags |= MATCH_REFS_FOLLOW_TAGS;

	if (match_push_refs(local_refs, &remote_refs, rs, match_flags))
		goto done;

	if (transport->smart_options &&
	    transport->smart_options->cas &&
	    !is_empty_cas(transport->smart_options->cas))
		apply_push_cas(transport->smart_options->cas,
			       transport->remote, remote_refs);

	set_ref_status_for_push(remote_refs,
		flags & TRANSPORT_PUSH_MIRROR,
		flags & TRANSPORT_PUSH_FORCE);

	if (!(flags & TRANSPORT_PUSH_NO_HOOK))
		if (run_pre_push_hook(transport, remote_refs))
			goto done;

	if ((flags & (TRANSPORT_RECURSE_SUBMODULES_ON_DEMAND |
		      TRANSPORT_RECURSE_SUBMODULES_ONLY)) &&
	    !is_bare_repository()) {
		struct ref *ref = remote_refs;
		struct oid_array commits = OID_ARRAY_INIT;

		trace2_region_enter("transport_push", "push_submodules", r);
		for (; ref; ref = ref->next)
			if (!is_null_oid(&ref->new_oid))
				oid_array_append(&commits,
						  &ref->new_oid);

		if (!push_unpushed_submodules(r,
					      &commits,
					      transport->remote,
					      rs,
					      transport->push_options,
					      pretend)) {
			oid_array_clear(&commits);
			trace2_region_leave("transport_push", "push_submodules", r);
			die(_("failed to push all needed submodules"));
		}
		oid_array_clear(&commits);
		trace2_region_leave("transport_push", "push_submodules", r);
	}

	if (((flags & TRANSPORT_RECURSE_SUBMODULES_CHECK) ||
	     ((flags & (TRANSPORT_RECURSE_SUBMODULES_ON_DEMAND |
			TRANSPORT_RECURSE_SUBMODULES_ONLY)) &&
	      !pretend)) && !is_bare_repository()) {
		struct ref *ref = remote_refs;
		struct string_list needs_pushing = STRING_LIST_INIT_DUP;
		struct oid_array commits = OID_ARRAY_INIT;

		trace2_region_enter("transport_push", "check_submodules", r);
		for (; ref; ref = ref->next)
			if (!is_null_oid(&ref->new_oid))
				oid_array_append(&commits,
						  &ref->new_oid);

		if (find_unpushed_submodules(r,
					     &commits,
					     transport->remote->name,
					     &needs_pushing)) {
			oid_array_clear(&commits);
			trace2_region_leave("transport_push", "check_submodules", r);
			die_with_unpushed_submodules(&needs_pushing);
		}
		string_list_clear(&needs_pushing, 0);
		oid_array_clear(&commits);
		trace2_region_leave("transport_push", "check_submodules", r);
	}

	if (!(flags & TRANSPORT_RECURSE_SUBMODULES_ONLY)) {
		trace2_region_enter("transport_push", "push_refs", r);
		push_ret = transport->vtable->push_refs(transport, remote_refs, flags);
		trace2_region_leave("transport_push", "push_refs", r);
	} else
		push_ret = 0;
	err = push_had_errors(remote_refs);
	ret = push_ret | err;

	if (!quiet || err)
		transport_print_push_status(transport->url, remote_refs,
				verbose | porcelain, porcelain,
				reject_reasons);

	if (flags & TRANSPORT_PUSH_SET_UPSTREAM)
		set_upstreams(transport, remote_refs, pretend);

	if (!(flags & (TRANSPORT_PUSH_DRY_RUN |
		       TRANSPORT_RECURSE_SUBMODULES_ONLY))) {
		struct ref *ref;
		for (ref = remote_refs; ref; ref = ref->next)
			transport_update_tracking_ref(transport->remote, ref, verbose);
	}

	if (porcelain && !push_ret)
		puts("Done");
	else if (!quiet && !ret && !transport_refs_pushed(remote_refs))
		/* stable plumbing output; do not modify or localize */
		fprintf(stderr, "Everything up-to-date\n");

done:
	free_refs(local_refs);
	free_refs(remote_refs);
	return ret;
}

const struct ref *transport_get_remote_refs(struct transport *transport,
					    struct transport_ls_refs_options *transport_options)
{
	if (!transport->got_remote_refs) {
		transport->remote_refs =
			transport->vtable->get_refs_list(transport, 0,
							 transport_options);
		transport->got_remote_refs = 1;
	}

	return transport->remote_refs;
}

void transport_ls_refs_options_release(struct transport_ls_refs_options *opts)
{
	strvec_clear(&opts->ref_prefixes);
	free((char *)opts->unborn_head_target);
}

int transport_fetch_refs(struct transport *transport, struct ref *refs)
{
	int rc;
	int nr_heads = 0, nr_alloc = 0, nr_refs = 0;
	struct ref **heads = NULL;
	struct ref *rm;

	for (rm = refs; rm; rm = rm->next) {
		nr_refs++;
		if (rm->peer_ref &&
		    !is_null_oid(&rm->old_oid) &&
		    oideq(&rm->peer_ref->old_oid, &rm->old_oid))
			continue;
		ALLOC_GROW(heads, nr_heads + 1, nr_alloc);
		heads[nr_heads++] = rm;
	}

	if (!nr_heads) {
		/*
		 * When deepening of a shallow repository is requested,
		 * then local and remote refs are likely to still be equal.
		 * Just feed them all to the fetch method in that case.
		 * This condition shouldn't be met in a non-deepening fetch
		 * (see builtin/fetch.c:quickfetch()).
		 */
		ALLOC_ARRAY(heads, nr_refs);
		for (rm = refs; rm; rm = rm->next)
			heads[nr_heads++] = rm;
	}

	rc = transport->vtable->fetch_refs(transport, nr_heads, heads);

	free(heads);
	return rc;
}

int transport_get_remote_bundle_uri(struct transport *transport)
{
	int value = 0;
	const struct transport_vtable *vtable = transport->vtable;

	/* Check config only once. */
	if (transport->got_remote_bundle_uri)
		return 0;
	transport->got_remote_bundle_uri = 1;

	/*
	 * Don't request bundle-uri from the server unless configured to
	 * do so by the transfer.bundleURI=true config option.
	 */
	if (git_config_get_bool("transfer.bundleuri", &value) || !value)
		return 0;

	if (!transport->bundles->baseURI)
		transport->bundles->baseURI = xstrdup(transport->url);

	if (!vtable->get_bundle_uri)
		return error(_("bundle-uri operation not supported by protocol"));

	if (vtable->get_bundle_uri(transport) < 0)
		return error(_("could not retrieve server-advertised bundle-uri list"));
	return 0;
}

void transport_unlock_pack(struct transport *transport, unsigned int flags)
{
	int in_signal_handler = !!(flags & TRANSPORT_UNLOCK_PACK_IN_SIGNAL_HANDLER);
	int i;

	for (i = 0; i < transport->pack_lockfiles.nr; i++)
		if (in_signal_handler)
			unlink(transport->pack_lockfiles.items[i].string);
		else
			unlink_or_warn(transport->pack_lockfiles.items[i].string);
	if (!in_signal_handler)
		string_list_clear(&transport->pack_lockfiles, 0);
}

int transport_connect(struct transport *transport, const char *name,
		      const char *exec, int fd[2])
{
	if (transport->vtable->connect)
		return transport->vtable->connect(transport, name, exec, fd);
	else
		die(_("operation not supported by protocol"));
}

int transport_disconnect(struct transport *transport)
{
	int ret = 0;
	if (transport->vtable->disconnect)
		ret = transport->vtable->disconnect(transport);
	if (transport->got_remote_refs)
		free_refs((void *)transport->remote_refs);
	clear_bundle_list(transport->bundles);
	free(transport->bundles);
	free(transport);
	return ret;
}

/*
 * Strip username (and password) from a URL and return
 * it in a newly allocated string.
 */
char *transport_anonymize_url(const char *url)
{
	char *scheme_prefix, *anon_part;
	size_t anon_len, prefix_len = 0;

	anon_part = strchr(url, '@');
	if (url_is_local_not_ssh(url) || !anon_part)
		goto literal_copy;

	anon_len = strlen(++anon_part);
	scheme_prefix = strstr(url, "://");
	if (!scheme_prefix) {
		if (!strchr(anon_part, ':'))
			/* cannot be "me@there:/path/name" */
			goto literal_copy;
	} else {
		const char *cp;
		/* make sure scheme is reasonable */
		for (cp = url; cp < scheme_prefix; cp++) {
			switch (*cp) {
				/* RFC 1738 2.1 */
			case '+': case '.': case '-':
				break; /* ok */
			default:
				if (isalnum(*cp))
					break;
				/* it isn't */
				goto literal_copy;
			}
		}
		/* @ past the first slash does not count */
		cp = strchr(scheme_prefix + 3, '/');
		if (cp && cp < anon_part)
			goto literal_copy;
		prefix_len = scheme_prefix - url + 3;
	}
	return xstrfmt("%.*s%.*s", (int)prefix_len, url,
		       (int)anon_len, anon_part);
literal_copy:
	return xstrdup(url);
}
