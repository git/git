#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "commit.h"
#include "date.h"
#include "gettext.h"
#include "hex.h"
#include "object-store-ll.h"
#include "pkt-line.h"
#include "sideband.h"
#include "run-command.h"
#include "remote.h"
#include "connect.h"
#include "send-pack.h"
#include "transport.h"
#include "version.h"
#include "oid-array.h"
#include "gpg-interface.h"
#include "shallow.h"
#include "parse-options.h"
#include "trace2.h"
#include "write-or-die.h"

int option_parse_push_signed(const struct option *opt,
			     const char *arg, int unset)
{
	if (unset) {
		*(int *)(opt->value) = SEND_PACK_PUSH_CERT_NEVER;
		return 0;
	}
	switch (git_parse_maybe_bool(arg)) {
	case 1:
		*(int *)(opt->value) = SEND_PACK_PUSH_CERT_ALWAYS;
		return 0;
	case 0:
		*(int *)(opt->value) = SEND_PACK_PUSH_CERT_NEVER;
		return 0;
	}
	if (!strcasecmp("if-asked", arg)) {
		*(int *)(opt->value) = SEND_PACK_PUSH_CERT_IF_ASKED;
		return 0;
	}
	die("bad %s argument: %s", opt->long_name, arg);
}

static void feed_object(const struct object_id *oid, FILE *fh, int negative)
{
	if (negative &&
	    !repo_has_object_file_with_flags(the_repository, oid,
					     OBJECT_INFO_SKIP_FETCH_OBJECT |
					     OBJECT_INFO_QUICK))
		return;

	if (negative)
		putc('^', fh);
	fputs(oid_to_hex(oid), fh);
	putc('\n', fh);
}

/*
 * Make a pack stream and spit it out into file descriptor fd
 */
static int pack_objects(int fd, struct ref *refs, struct oid_array *advertised,
			struct oid_array *negotiated,
			struct send_pack_args *args)
{
	/*
	 * The child becomes pack-objects --revs; we feed
	 * the revision parameters to it via its stdin and
	 * let its stdout go back to the other end.
	 */
	struct child_process po = CHILD_PROCESS_INIT;
	FILE *po_in;
	int rc;

	trace2_region_enter("send_pack", "pack_objects", the_repository);
	strvec_push(&po.args, "pack-objects");
	strvec_push(&po.args, "--all-progress-implied");
	strvec_push(&po.args, "--revs");
	strvec_push(&po.args, "--stdout");
	if (args->use_thin_pack)
		strvec_push(&po.args, "--thin");
	if (args->use_ofs_delta)
		strvec_push(&po.args, "--delta-base-offset");
	if (args->quiet || !args->progress)
		strvec_push(&po.args, "-q");
	if (args->progress)
		strvec_push(&po.args, "--progress");
	if (is_repository_shallow(the_repository))
		strvec_push(&po.args, "--shallow");
	if (args->disable_bitmaps)
		strvec_push(&po.args, "--no-use-bitmap-index");
	po.in = -1;
	po.out = args->stateless_rpc ? -1 : fd;
	po.git_cmd = 1;
	po.clean_on_exit = 1;
	if (start_command(&po))
		die_errno("git pack-objects failed");

	/*
	 * We feed the pack-objects we just spawned with revision
	 * parameters by writing to the pipe.
	 */
	po_in = xfdopen(po.in, "w");
	for (size_t i = 0; i < advertised->nr; i++)
		feed_object(&advertised->oid[i], po_in, 1);
	for (size_t i = 0; i < negotiated->nr; i++)
		feed_object(&negotiated->oid[i], po_in, 1);

	while (refs) {
		if (!is_null_oid(&refs->old_oid))
			feed_object(&refs->old_oid, po_in, 1);
		if (!is_null_oid(&refs->new_oid))
			feed_object(&refs->new_oid, po_in, 0);
		refs = refs->next;
	}

	fflush(po_in);
	if (ferror(po_in))
		die_errno("error writing to pack-objects");
	fclose(po_in);

	if (args->stateless_rpc) {
		char *buf = xmalloc(LARGE_PACKET_MAX);
		while (1) {
			ssize_t n = xread(po.out, buf, LARGE_PACKET_MAX);
			if (n <= 0)
				break;
			send_sideband(fd, -1, buf, n, LARGE_PACKET_MAX);
		}
		free(buf);
		close(po.out);
		po.out = -1;
	}

	rc = finish_command(&po);
	if (rc) {
		/*
		 * For a normal non-zero exit, we assume pack-objects wrote
		 * something useful to stderr. For death by signal, though,
		 * we should mention it to the user. The exception is SIGPIPE
		 * (141), because that's a normal occurrence if the remote end
		 * hangs up (and we'll report that by trying to read the unpack
		 * status).
		 */
		if (rc > 128 && rc != 141)
			error("pack-objects died of signal %d", rc - 128);
		trace2_region_leave("send_pack", "pack_objects", the_repository);
		return -1;
	}
	trace2_region_leave("send_pack", "pack_objects", the_repository);
	return 0;
}

static int receive_unpack_status(struct packet_reader *reader)
{
	if (packet_reader_read(reader) != PACKET_READ_NORMAL)
		return error(_("unexpected flush packet while reading remote unpack status"));
	if (!skip_prefix(reader->line, "unpack ", &reader->line))
		return error(_("unable to parse remote unpack status: %s"), reader->line);
	if (strcmp(reader->line, "ok"))
		return error(_("remote unpack failed: %s"), reader->line);
	return 0;
}

static int receive_status(struct packet_reader *reader, struct ref *refs)
{
	struct ref *hint;
	int ret;
	struct ref_push_report *report = NULL;
	int new_report = 0;
	int once = 0;

	trace2_region_enter("send_pack", "receive_status", the_repository);
	hint = NULL;
	ret = receive_unpack_status(reader);
	while (1) {
		struct object_id old_oid, new_oid;
		const char *head;
		const char *refname;
		char *p;
		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;
		head = reader->line;
		p = strchr(head, ' ');
		if (!p) {
			error("invalid status line from remote: %s", reader->line);
			ret = -1;
			break;
		}
		*p++ = '\0';

		if (!strcmp(head, "option")) {
			const char *key, *val;

			if (!hint || !(report || new_report)) {
				if (!once++)
					error("'option' without a matching 'ok/ng' directive");
				ret = -1;
				continue;
			}
			if (new_report) {
				if (!hint->report) {
					CALLOC_ARRAY(hint->report, 1);
					report = hint->report;
				} else {
					report = hint->report;
					while (report->next)
						report = report->next;
					CALLOC_ARRAY(report->next, 1);
					report = report->next;
				}
				new_report = 0;
			}
			key = p;
			p = strchr(key, ' ');
			if (p)
				*p++ = '\0';
			val = p;
			if (!strcmp(key, "refname"))
				report->ref_name = xstrdup_or_null(val);
			else if (!strcmp(key, "old-oid") && val &&
				 !parse_oid_hex(val, &old_oid, &val))
				report->old_oid = oiddup(&old_oid);
			else if (!strcmp(key, "new-oid") && val &&
				 !parse_oid_hex(val, &new_oid, &val))
				report->new_oid = oiddup(&new_oid);
			else if (!strcmp(key, "forced-update"))
				report->forced_update = 1;
			continue;
		}

		report = NULL;
		new_report = 0;
		if (strcmp(head, "ok") && strcmp(head, "ng")) {
			error("invalid ref status from remote: %s", head);
			ret = -1;
			break;
		}
		refname = p;
		p = strchr(refname, ' ');
		if (p)
			*p++ = '\0';
		/* first try searching at our hint, falling back to all refs */
		if (hint)
			hint = find_ref_by_name(hint, refname);
		if (!hint)
			hint = find_ref_by_name(refs, refname);
		if (!hint) {
			warning("remote reported status on unknown ref: %s",
				refname);
			continue;
		}
		if (hint->status != REF_STATUS_EXPECTING_REPORT &&
		    hint->status != REF_STATUS_OK &&
		    hint->status != REF_STATUS_REMOTE_REJECT) {
			warning("remote reported status on unexpected ref: %s",
				refname);
			continue;
		}
		if (!strcmp(head, "ng")) {
			hint->status = REF_STATUS_REMOTE_REJECT;
			if (p)
				hint->remote_status = xstrdup(p);
			else
				hint->remote_status = xstrdup("failed");
		} else {
			hint->status = REF_STATUS_OK;
			hint->remote_status = xstrdup_or_null(p);
			new_report = 1;
		}
	}
	trace2_region_leave("send_pack", "receive_status", the_repository);
	return ret;
}

static int sideband_demux(int in UNUSED, int out, void *data)
{
	int *fd = data, ret;
	if (async_with_fork())
		close(fd[1]);
	ret = recv_sideband("send-pack", fd[0], out);
	close(out);
	return ret;
}

static int advertise_shallow_grafts_cb(const struct commit_graft *graft, void *cb)
{
	struct strbuf *sb = cb;
	if (graft->nr_parent == -1)
		packet_buf_write(sb, "shallow %s\n", oid_to_hex(&graft->oid));
	return 0;
}

static void advertise_shallow_grafts_buf(struct strbuf *sb)
{
	if (!is_repository_shallow(the_repository))
		return;
	for_each_commit_graft(advertise_shallow_grafts_cb, sb);
}

#define CHECK_REF_NO_PUSH -1
#define CHECK_REF_STATUS_REJECTED -2
#define CHECK_REF_UPTODATE -3
static int check_to_send_update(const struct ref *ref, const struct send_pack_args *args)
{
	if (!ref->peer_ref && !args->send_mirror)
		return CHECK_REF_NO_PUSH;

	/* Check for statuses set by set_ref_status_for_push() */
	switch (ref->status) {
	case REF_STATUS_REJECT_NONFASTFORWARD:
	case REF_STATUS_REJECT_ALREADY_EXISTS:
	case REF_STATUS_REJECT_FETCH_FIRST:
	case REF_STATUS_REJECT_NEEDS_FORCE:
	case REF_STATUS_REJECT_STALE:
	case REF_STATUS_REJECT_REMOTE_UPDATED:
	case REF_STATUS_REJECT_NODELETE:
		return CHECK_REF_STATUS_REJECTED;
	case REF_STATUS_UPTODATE:
		return CHECK_REF_UPTODATE;

	default:
	case REF_STATUS_EXPECTING_REPORT:
		/* already passed checks on the local side */
	case REF_STATUS_OK:
		/* of course this is OK */
		return 0;
	}
}

/*
 * the beginning of the next line, or the end of buffer.
 *
 * NEEDSWORK: perhaps move this to git-compat-util.h or somewhere and
 * convert many similar uses found by "git grep -A4 memchr".
 */
static const char *next_line(const char *line, size_t len)
{
	const char *nl = memchr(line, '\n', len);
	if (!nl)
		return line + len; /* incomplete line */
	return nl + 1;
}

static int generate_push_cert(struct strbuf *req_buf,
			      const struct ref *remote_refs,
			      struct send_pack_args *args,
			      const char *cap_string,
			      const char *push_cert_nonce)
{
	const struct ref *ref;
	struct string_list_item *item;
	char *signing_key_id = get_signing_key_id();
	char *signing_key = get_signing_key();
	const char *cp, *np;
	struct strbuf cert = STRBUF_INIT;
	int update_seen = 0;

	strbuf_addstr(&cert, "certificate version 0.1\n");
	strbuf_addf(&cert, "pusher %s ", signing_key_id);
	datestamp(&cert);
	strbuf_addch(&cert, '\n');
	if (args->url && *args->url) {
		char *anon_url = transport_anonymize_url(args->url);
		strbuf_addf(&cert, "pushee %s\n", anon_url);
		free(anon_url);
	}
	if (push_cert_nonce[0])
		strbuf_addf(&cert, "nonce %s\n", push_cert_nonce);
	if (args->push_options)
		for_each_string_list_item(item, args->push_options)
			strbuf_addf(&cert, "push-option %s\n", item->string);
	strbuf_addstr(&cert, "\n");

	for (ref = remote_refs; ref; ref = ref->next) {
		if (check_to_send_update(ref, args) < 0)
			continue;
		update_seen = 1;
		strbuf_addf(&cert, "%s %s %s\n",
			    oid_to_hex(&ref->old_oid),
			    oid_to_hex(&ref->new_oid),
			    ref->name);
	}
	if (!update_seen)
		goto free_return;

	if (sign_buffer(&cert, &cert, signing_key))
		die(_("failed to sign the push certificate"));

	packet_buf_write(req_buf, "push-cert%c%s", 0, cap_string);
	for (cp = cert.buf; cp < cert.buf + cert.len; cp = np) {
		np = next_line(cp, cert.buf + cert.len - cp);
		packet_buf_write(req_buf,
				 "%.*s", (int)(np - cp), cp);
	}
	packet_buf_write(req_buf, "push-cert-end\n");

free_return:
	free(signing_key_id);
	free(signing_key);
	strbuf_release(&cert);
	return update_seen;
}

#define NONCE_LEN_LIMIT 256

static void reject_invalid_nonce(const char *nonce, int len)
{
	int i = 0;

	if (NONCE_LEN_LIMIT <= len)
		die("the receiving end asked to sign an invalid nonce <%.*s>",
		    len, nonce);

	for (i = 0; i < len; i++) {
		int ch = nonce[i] & 0xFF;
		if (isalnum(ch) ||
		    ch == '-' || ch == '.' ||
		    ch == '/' || ch == '+' ||
		    ch == '=' || ch == '_')
			continue;
		die("the receiving end asked to sign an invalid nonce <%.*s>",
		    len, nonce);
	}
}

static void get_commons_through_negotiation(const char *url,
					    const struct ref *remote_refs,
					    struct oid_array *commons)
{
	struct child_process child = CHILD_PROCESS_INIT;
	const struct ref *ref;
	int len = the_hash_algo->hexsz + 1; /* hash + NL */
	int nr_negotiation_tip = 0;

	child.git_cmd = 1;
	child.no_stdin = 1;
	child.out = -1;
	strvec_pushl(&child.args, "fetch", "--negotiate-only", NULL);
	for (ref = remote_refs; ref; ref = ref->next) {
		if (!is_null_oid(&ref->new_oid)) {
			strvec_pushf(&child.args, "--negotiation-tip=%s",
				     oid_to_hex(&ref->new_oid));
			nr_negotiation_tip++;
		}
	}
	strvec_push(&child.args, url);

	if (!nr_negotiation_tip) {
		child_process_clear(&child);
		return;
	}

	if (start_command(&child))
		die(_("send-pack: unable to fork off fetch subprocess"));

	do {
		char hex_hash[GIT_MAX_HEXSZ + 1];
		int read_len = read_in_full(child.out, hex_hash, len);
		struct object_id oid;
		const char *end;

		if (!read_len)
			break;
		if (read_len != len)
			die("invalid length read %d", read_len);
		if (parse_oid_hex(hex_hash, &oid, &end) || *end != '\n')
			die("invalid hash");
		oid_array_append(commons, &oid);
	} while (1);

	if (finish_command(&child)) {
		/*
		 * The information that push negotiation provides is useful but
		 * not mandatory.
		 */
		warning(_("push negotiation failed; proceeding anyway with push"));
	}
}

int send_pack(struct send_pack_args *args,
	      int fd[], struct child_process *conn,
	      struct ref *remote_refs,
	      struct oid_array *extra_have)
{
	struct oid_array commons = OID_ARRAY_INIT;
	int in = fd[0];
	int out = fd[1];
	struct strbuf req_buf = STRBUF_INIT;
	struct strbuf cap_buf = STRBUF_INIT;
	struct ref *ref;
	int need_pack_data = 0;
	int allow_deleting_refs = 0;
	int status_report = 0;
	int use_sideband = 0;
	int quiet_supported = 0;
	int agent_supported = 0;
	int advertise_sid = 0;
	int push_negotiate = 0;
	int use_atomic = 0;
	int atomic_supported = 0;
	int use_push_options = 0;
	int push_options_supported = 0;
	int object_format_supported = 0;
	unsigned cmds_sent = 0;
	int ret;
	struct async demux;
	char *push_cert_nonce = NULL;
	struct packet_reader reader;
	int use_bitmaps;

	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n"
			"Perhaps you should specify a branch.\n");
		ret = 0;
		goto out;
	}

	git_config_get_bool("push.negotiate", &push_negotiate);
	if (push_negotiate) {
		trace2_region_enter("send_pack", "push_negotiate", the_repository);
		get_commons_through_negotiation(args->url, remote_refs, &commons);
		trace2_region_leave("send_pack", "push_negotiate", the_repository);
	}

	if (!git_config_get_bool("push.usebitmaps", &use_bitmaps))
		args->disable_bitmaps = !use_bitmaps;

	git_config_get_bool("transfer.advertisesid", &advertise_sid);

	/* Does the other end support the reporting? */
	if (server_supports("report-status-v2"))
		status_report = 2;
	else if (server_supports("report-status"))
		status_report = 1;
	if (server_supports("delete-refs"))
		allow_deleting_refs = 1;
	if (server_supports("ofs-delta"))
		args->use_ofs_delta = 1;
	if (server_supports("side-band-64k"))
		use_sideband = 1;
	if (server_supports("quiet"))
		quiet_supported = 1;
	if (server_supports("agent"))
		agent_supported = 1;
	if (!server_supports("session-id"))
		advertise_sid = 0;
	if (server_supports("no-thin"))
		args->use_thin_pack = 0;
	if (server_supports("atomic"))
		atomic_supported = 1;
	if (server_supports("push-options"))
		push_options_supported = 1;

	if (!server_supports_hash(the_hash_algo->name, &object_format_supported))
		die(_("the receiving end does not support this repository's hash algorithm"));

	if (args->push_cert != SEND_PACK_PUSH_CERT_NEVER) {
		size_t len;
		const char *nonce = server_feature_value("push-cert", &len);

		if (nonce) {
			reject_invalid_nonce(nonce, len);
			push_cert_nonce = xmemdupz(nonce, len);
		} else if (args->push_cert == SEND_PACK_PUSH_CERT_ALWAYS) {
			die(_("the receiving end does not support --signed push"));
		} else if (args->push_cert == SEND_PACK_PUSH_CERT_IF_ASKED) {
			warning(_("not sending a push certificate since the"
				  " receiving end does not support --signed"
				  " push"));
		}
	}

	if (args->atomic && !atomic_supported)
		die(_("the receiving end does not support --atomic push"));

	use_atomic = atomic_supported && args->atomic;

	if (args->push_options && !push_options_supported)
		die(_("the receiving end does not support push options"));

	use_push_options = push_options_supported && args->push_options;

	if (status_report == 1)
		strbuf_addstr(&cap_buf, " report-status");
	else if (status_report == 2)
		strbuf_addstr(&cap_buf, " report-status-v2");
	if (use_sideband)
		strbuf_addstr(&cap_buf, " side-band-64k");
	if (quiet_supported && (args->quiet || !args->progress))
		strbuf_addstr(&cap_buf, " quiet");
	if (use_atomic)
		strbuf_addstr(&cap_buf, " atomic");
	if (use_push_options)
		strbuf_addstr(&cap_buf, " push-options");
	if (object_format_supported)
		strbuf_addf(&cap_buf, " object-format=%s", the_hash_algo->name);
	if (agent_supported)
		strbuf_addf(&cap_buf, " agent=%s", git_user_agent_sanitized());
	if (advertise_sid)
		strbuf_addf(&cap_buf, " session-id=%s", trace2_session_id());

	/*
	 * NEEDSWORK: why does delete-refs have to be so specific to
	 * send-pack machinery that set_ref_status_for_push() cannot
	 * set this bit for us???
	 */
	for (ref = remote_refs; ref; ref = ref->next)
		if (ref->deletion && !allow_deleting_refs)
			ref->status = REF_STATUS_REJECT_NODELETE;

	/*
	 * Clear the status for each ref and see if we need to send
	 * the pack data.
	 */
	for (ref = remote_refs; ref; ref = ref->next) {
		switch (check_to_send_update(ref, args)) {
		case 0: /* no error */
			break;
		case CHECK_REF_STATUS_REJECTED:
			/*
			 * When we know the server would reject a ref update if
			 * we were to send it and we're trying to send the refs
			 * atomically, abort the whole operation.
			 */
			if (use_atomic) {
				reject_atomic_push(remote_refs, args->send_mirror);
				error("atomic push failed for ref %s. status: %d",
				      ref->name, ref->status);
				ret = args->porcelain ? 0 : -1;
				goto out;
			}
			/* else fallthrough */
		default:
			continue;
		}
		if (!ref->deletion)
			need_pack_data = 1;

		if (args->dry_run || !status_report)
			ref->status = REF_STATUS_OK;
		else
			ref->status = REF_STATUS_EXPECTING_REPORT;
	}

	if (!args->dry_run)
		advertise_shallow_grafts_buf(&req_buf);

	/*
	 * Finally, tell the other end!
	 */
	if (!args->dry_run && push_cert_nonce) {
		cmds_sent = generate_push_cert(&req_buf, remote_refs, args,
					       cap_buf.buf, push_cert_nonce);
		trace2_printf("Generated push certificate");
	} else if (!args->dry_run) {
		for (ref = remote_refs; ref; ref = ref->next) {
			char *old_hex, *new_hex;

			if (check_to_send_update(ref, args) < 0)
				continue;

			old_hex = oid_to_hex(&ref->old_oid);
			new_hex = oid_to_hex(&ref->new_oid);
			if (!cmds_sent) {
				packet_buf_write(&req_buf,
						 "%s %s %s%c%s",
						 old_hex, new_hex, ref->name, 0,
						 cap_buf.buf);
				cmds_sent = 1;
			} else {
				packet_buf_write(&req_buf, "%s %s %s",
						 old_hex, new_hex, ref->name);
			}
		}
	}

	if (use_push_options) {
		struct string_list_item *item;

		packet_buf_flush(&req_buf);
		for_each_string_list_item(item, args->push_options)
			packet_buf_write(&req_buf, "%s", item->string);
	}

	if (args->stateless_rpc) {
		if (!args->dry_run && (cmds_sent || is_repository_shallow(the_repository))) {
			packet_buf_flush(&req_buf);
			send_sideband(out, -1, req_buf.buf, req_buf.len, LARGE_PACKET_MAX);
		}
	} else {
		write_or_die(out, req_buf.buf, req_buf.len);
		packet_flush(out);
	}

	if (use_sideband && cmds_sent) {
		memset(&demux, 0, sizeof(demux));
		demux.proc = sideband_demux;
		demux.data = fd;
		demux.out = -1;
		demux.isolate_sigpipe = 1;
		if (start_async(&demux))
			die("send-pack: unable to fork off sideband demultiplexer");
		in = demux.out;
	}

	packet_reader_init(&reader, in, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	if (need_pack_data && cmds_sent) {
		if (pack_objects(out, remote_refs, extra_have, &commons, args) < 0) {
			if (args->stateless_rpc)
				close(out);
			if (git_connection_is_socket(conn))
				shutdown(fd[0], SHUT_WR);

			/*
			 * Do not even bother with the return value; we know we
			 * are failing, and just want the error() side effects,
			 * as well as marking refs with their remote status (if
			 * we get one).
			 */
			if (status_report)
				receive_status(&reader, remote_refs);

			if (use_sideband) {
				close(demux.out);
				finish_async(&demux);
			}
			fd[1] = -1;

			ret = -1;
			goto out;
		}
		if (!args->stateless_rpc)
			/* Closed by pack_objects() via start_command() */
			fd[1] = -1;
	}
	if (args->stateless_rpc && cmds_sent)
		packet_flush(out);

	if (status_report && cmds_sent)
		ret = receive_status(&reader, remote_refs);
	else
		ret = 0;
	if (args->stateless_rpc)
		packet_flush(out);

	if (use_sideband && cmds_sent) {
		close(demux.out);
		if (finish_async(&demux)) {
			error("error in sideband demultiplexer");
			ret = -1;
		}
	}

	if (ret < 0)
		goto out;

	if (args->porcelain) {
		ret = 0;
		goto out;
	}

	for (ref = remote_refs; ref; ref = ref->next) {
		switch (ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
		case REF_STATUS_OK:
			break;
		default:
			ret = -1;
			goto out;
		}
	}

	ret = 0;

out:
	oid_array_clear(&commons);
	strbuf_release(&req_buf);
	strbuf_release(&cap_buf);
	free(push_cert_nonce);
	return ret;
}
