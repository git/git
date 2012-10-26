#include "builtin.h"
#include "commit.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "run-command.h"
#include "remote.h"
#include "send-pack.h"
#include "quote.h"
#include "transport.h"
#include "version.h"

static int feed_object(const unsigned char *sha1, int fd, int negative)
{
	char buf[42];

	if (negative && !has_sha1_file(sha1))
		return 1;

	memcpy(buf + negative, sha1_to_hex(sha1), 40);
	if (negative)
		buf[0] = '^';
	buf[40 + negative] = '\n';
	return write_or_whine(fd, buf, 41 + negative, "send-pack: send refs");
}

/*
 * Make a pack stream and spit it out into file descriptor fd
 */
static int pack_objects(int fd, struct ref *refs, struct extra_have_objects *extra, struct send_pack_args *args)
{
	/*
	 * The child becomes pack-objects --revs; we feed
	 * the revision parameters to it via its stdin and
	 * let its stdout go back to the other end.
	 */
	const char *argv[] = {
		"pack-objects",
		"--all-progress-implied",
		"--revs",
		"--stdout",
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	};
	struct child_process po;
	int i;

	i = 4;
	if (args->use_thin_pack)
		argv[i++] = "--thin";
	if (args->use_ofs_delta)
		argv[i++] = "--delta-base-offset";
	if (args->quiet || !args->progress)
		argv[i++] = "-q";
	if (args->progress)
		argv[i++] = "--progress";
	memset(&po, 0, sizeof(po));
	po.argv = argv;
	po.in = -1;
	po.out = args->stateless_rpc ? -1 : fd;
	po.git_cmd = 1;
	if (start_command(&po))
		die_errno("git pack-objects failed");

	/*
	 * We feed the pack-objects we just spawned with revision
	 * parameters by writing to the pipe.
	 */
	for (i = 0; i < extra->nr; i++)
		if (!feed_object(extra->array[i], po.in, 1))
			break;

	while (refs) {
		if (!is_null_sha1(refs->old_sha1) &&
		    !feed_object(refs->old_sha1, po.in, 1))
			break;
		if (!is_null_sha1(refs->new_sha1) &&
		    !feed_object(refs->new_sha1, po.in, 0))
			break;
		refs = refs->next;
	}

	close(po.in);

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

	if (finish_command(&po))
		return -1;
	return 0;
}

static int receive_status(int in, struct ref *refs)
{
	struct ref *hint;
	char line[1000];
	int ret = 0;
	int len = packet_read_line(in, line, sizeof(line));
	if (len < 10 || memcmp(line, "unpack ", 7))
		return error("did not receive remote status");
	if (memcmp(line, "unpack ok\n", 10)) {
		char *p = line + strlen(line) - 1;
		if (*p == '\n')
			*p = '\0';
		error("unpack failed: %s", line + 7);
		ret = -1;
	}
	hint = NULL;
	while (1) {
		char *refname;
		char *msg;
		len = packet_read_line(in, line, sizeof(line));
		if (!len)
			break;
		if (len < 3 ||
		    (memcmp(line, "ok ", 3) && memcmp(line, "ng ", 3))) {
			fprintf(stderr, "protocol error: %s\n", line);
			ret = -1;
			break;
		}

		line[strlen(line)-1] = '\0';
		refname = line + 3;
		msg = strchr(refname, ' ');
		if (msg)
			*msg++ = '\0';

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
		if (hint->status != REF_STATUS_EXPECTING_REPORT) {
			warning("remote reported status on unexpected ref: %s",
					refname);
			continue;
		}

		if (line[0] == 'o' && line[1] == 'k')
			hint->status = REF_STATUS_OK;
		else {
			hint->status = REF_STATUS_REMOTE_REJECT;
			ret = -1;
		}
		if (msg)
			hint->remote_status = xstrdup(msg);
		/* start our next search from the next ref */
		hint = hint->next;
	}
	return ret;
}

static int sideband_demux(int in, int out, void *data)
{
	int *fd = data, ret;
#ifdef NO_PTHREADS
	close(fd[1]);
#endif
	ret = recv_sideband("send-pack", fd[0], out);
	close(out);
	return ret;
}

int send_pack(struct send_pack_args *args,
	      int fd[], struct child_process *conn,
	      struct ref *remote_refs,
	      struct extra_have_objects *extra_have)
{
	int in = fd[0];
	int out = fd[1];
	struct strbuf req_buf = STRBUF_INIT;
	struct ref *ref;
	int new_refs;
	int allow_deleting_refs = 0;
	int status_report = 0;
	int use_sideband = 0;
	int quiet_supported = 0;
	int agent_supported = 0;
	unsigned cmds_sent = 0;
	int ret;
	struct async demux;

	/* Does the other end support the reporting? */
	if (server_supports("report-status"))
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

	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n"
			"Perhaps you should specify a branch such as 'master'.\n");
		return 0;
	}

	/*
	 * Finally, tell the other end!
	 */
	new_refs = 0;
	for (ref = remote_refs; ref; ref = ref->next) {
		if (!ref->peer_ref && !args->send_mirror)
			continue;

		/* Check for statuses set by set_ref_status_for_push() */
		switch (ref->status) {
		case REF_STATUS_REJECT_NONFASTFORWARD:
		case REF_STATUS_UPTODATE:
			continue;
		default:
			; /* do nothing */
		}

		if (ref->deletion && !allow_deleting_refs) {
			ref->status = REF_STATUS_REJECT_NODELETE;
			continue;
		}

		if (!ref->deletion)
			new_refs++;

		if (args->dry_run) {
			ref->status = REF_STATUS_OK;
		} else {
			char *old_hex = sha1_to_hex(ref->old_sha1);
			char *new_hex = sha1_to_hex(ref->new_sha1);
			int quiet = quiet_supported && (args->quiet || !args->progress);

			if (!cmds_sent && (status_report || use_sideband ||
					   quiet || agent_supported)) {
				packet_buf_write(&req_buf,
						 "%s %s %s%c%s%s%s%s%s",
						 old_hex, new_hex, ref->name, 0,
						 status_report ? " report-status" : "",
						 use_sideband ? " side-band-64k" : "",
						 quiet ? " quiet" : "",
						 agent_supported ? " agent=" : "",
						 agent_supported ? git_user_agent_sanitized() : ""
						);
			}
			else
				packet_buf_write(&req_buf, "%s %s %s",
						 old_hex, new_hex, ref->name);
			ref->status = status_report ?
				REF_STATUS_EXPECTING_REPORT :
				REF_STATUS_OK;
			cmds_sent++;
		}
	}

	if (args->stateless_rpc) {
		if (!args->dry_run && cmds_sent) {
			packet_buf_flush(&req_buf);
			send_sideband(out, -1, req_buf.buf, req_buf.len, LARGE_PACKET_MAX);
		}
	} else {
		safe_write(out, req_buf.buf, req_buf.len);
		packet_flush(out);
	}
	strbuf_release(&req_buf);

	if (use_sideband && cmds_sent) {
		memset(&demux, 0, sizeof(demux));
		demux.proc = sideband_demux;
		demux.data = fd;
		demux.out = -1;
		if (start_async(&demux))
			die("send-pack: unable to fork off sideband demultiplexer");
		in = demux.out;
	}

	if (new_refs && cmds_sent) {
		if (pack_objects(out, remote_refs, extra_have, args) < 0) {
			for (ref = remote_refs; ref; ref = ref->next)
				ref->status = REF_STATUS_NONE;
			if (args->stateless_rpc)
				close(out);
			if (git_connection_is_socket(conn))
				shutdown(fd[0], SHUT_WR);
			if (use_sideband)
				finish_async(&demux);
			return -1;
		}
	}
	if (args->stateless_rpc && cmds_sent)
		packet_flush(out);

	if (status_report && cmds_sent)
		ret = receive_status(in, remote_refs);
	else
		ret = 0;
	if (args->stateless_rpc)
		packet_flush(out);

	if (use_sideband && cmds_sent) {
		if (finish_async(&demux)) {
			error("error in sideband demultiplexer");
			ret = -1;
		}
		close(demux.out);
	}

	if (ret < 0)
		return ret;

	if (args->porcelain)
		return 0;

	for (ref = remote_refs; ref; ref = ref->next) {
		switch (ref->status) {
		case REF_STATUS_NONE:
		case REF_STATUS_UPTODATE:
		case REF_STATUS_OK:
			break;
		default:
			return -1;
		}
	}
	return 0;
}
