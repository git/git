#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "commit.h"
#include "tag.h"
#include "exec_cmd.h"
#include "pack.h"
#include "sideband.h"
#include "fetch-pack.h"
#include "remote.h"
#include "run-command.h"
#include "connect.h"
#include "transport.h"
#include "version.h"
#include "prio-queue.h"
#include "sha1-array.h"

static int transfer_unpack_limit = -1;
static int fetch_unpack_limit = -1;
static int unpack_limit = 100;
static int prefer_ofs_delta = 1;
static int no_done;
static int fetch_fsck_objects = -1;
static int transfer_fsck_objects = -1;
static int agent_supported;
static struct lock_file shallow_lock;
static const char *alternate_shallow_file;

/* Remember to update object flag allocation in object.h */
#define COMPLETE	(1U << 0)
#define COMMON		(1U << 1)
#define COMMON_REF	(1U << 2)
#define SEEN		(1U << 3)
#define POPPED		(1U << 4)

static int marked;

/*
 * After sending this many "have"s if we do not get any new ACK , we
 * give up traversing our history.
 */
#define MAX_IN_VAIN 256

static struct prio_queue rev_list = { compare_commits_by_commit_date };
static int non_common_revs, multi_ack, use_sideband, allow_tip_sha1_in_want;

static void rev_list_push(struct commit *commit, int mark)
{
	if (!(commit->object.flags & mark)) {
		commit->object.flags |= mark;

		if (parse_commit(commit))
			return;

		prio_queue_put(&rev_list, commit);

		if (!(commit->object.flags & COMMON))
			non_common_revs++;
	}
}

static int rev_list_insert_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = deref_tag(parse_object(sha1), refname, 0);

	if (o && o->type == OBJ_COMMIT)
		rev_list_push((struct commit *)o, SEEN);

	return 0;
}

static int clear_marks(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = deref_tag(parse_object(sha1), refname, 0);

	if (o && o->type == OBJ_COMMIT)
		clear_commit_marks((struct commit *)o,
				   COMMON | COMMON_REF | SEEN | POPPED);
	return 0;
}

/*
   This function marks a rev and its ancestors as common.
   In some cases, it is desirable to mark only the ancestors (for example
   when only the server does not yet know that they are common).
*/

static void mark_common(struct commit *commit,
		int ancestors_only, int dont_parse)
{
	if (commit != NULL && !(commit->object.flags & COMMON)) {
		struct object *o = (struct object *)commit;

		if (!ancestors_only)
			o->flags |= COMMON;

		if (!(o->flags & SEEN))
			rev_list_push(commit, SEEN);
		else {
			struct commit_list *parents;

			if (!ancestors_only && !(o->flags & POPPED))
				non_common_revs--;
			if (!o->parsed && !dont_parse)
				if (parse_commit(commit))
					return;

			for (parents = commit->parents;
					parents;
					parents = parents->next)
				mark_common(parents->item, 0, dont_parse);
		}
	}
}

/*
  Get the next rev to send, ignoring the common.
*/

static const unsigned char *get_rev(void)
{
	struct commit *commit = NULL;

	while (commit == NULL) {
		unsigned int mark;
		struct commit_list *parents;

		if (rev_list.nr == 0 || non_common_revs == 0)
			return NULL;

		commit = prio_queue_get(&rev_list);
		parse_commit(commit);
		parents = commit->parents;

		commit->object.flags |= POPPED;
		if (!(commit->object.flags & COMMON))
			non_common_revs--;

		if (commit->object.flags & COMMON) {
			/* do not send "have", and ignore ancestors */
			commit = NULL;
			mark = COMMON | SEEN;
		} else if (commit->object.flags & COMMON_REF)
			/* send "have", and ignore ancestors */
			mark = COMMON | SEEN;
		else
			/* send "have", also for its ancestors */
			mark = SEEN;

		while (parents) {
			if (!(parents->item->object.flags & SEEN))
				rev_list_push(parents->item, mark);
			if (mark & COMMON)
				mark_common(parents->item, 1, 0);
			parents = parents->next;
		}
	}

	return commit->object.sha1;
}

enum ack_type {
	NAK = 0,
	ACK,
	ACK_continue,
	ACK_common,
	ACK_ready
};

static void consume_shallow_list(struct fetch_pack_args *args, int fd)
{
	if (args->stateless_rpc && args->depth > 0) {
		/* If we sent a depth we will get back "duplicate"
		 * shallow and unshallow commands every time there
		 * is a block of have lines exchanged.
		 */
		char *line;
		while ((line = packet_read_line(fd, NULL))) {
			if (starts_with(line, "shallow "))
				continue;
			if (starts_with(line, "unshallow "))
				continue;
			die("git fetch-pack: expected shallow list");
		}
	}
}

static enum ack_type get_ack(int fd, unsigned char *result_sha1)
{
	int len;
	char *line = packet_read_line(fd, &len);
	const char *arg;

	if (!len)
		die("git fetch-pack: expected ACK/NAK, got EOF");
	if (!strcmp(line, "NAK"))
		return NAK;
	if (skip_prefix(line, "ACK ", &arg)) {
		if (!get_sha1_hex(arg, result_sha1)) {
			arg += 40;
			len -= arg - line;
			if (len < 1)
				return ACK;
			if (strstr(arg, "continue"))
				return ACK_continue;
			if (strstr(arg, "common"))
				return ACK_common;
			if (strstr(arg, "ready"))
				return ACK_ready;
			return ACK;
		}
	}
	die("git fetch_pack: expected ACK/NAK, got '%s'", line);
}

static void send_request(struct fetch_pack_args *args,
			 int fd, struct strbuf *buf)
{
	if (args->stateless_rpc) {
		send_sideband(fd, -1, buf->buf, buf->len, LARGE_PACKET_MAX);
		packet_flush(fd);
	} else
		write_or_die(fd, buf->buf, buf->len);
}

static void insert_one_alternate_ref(const struct ref *ref, void *unused)
{
	rev_list_insert_ref(NULL, ref->old_sha1, 0, NULL);
}

#define INITIAL_FLUSH 16
#define PIPESAFE_FLUSH 32
#define LARGE_FLUSH 1024

static int next_flush(struct fetch_pack_args *args, int count)
{
	int flush_limit = args->stateless_rpc ? LARGE_FLUSH : PIPESAFE_FLUSH;

	if (count < flush_limit)
		count <<= 1;
	else
		count += flush_limit;
	return count;
}

static int find_common(struct fetch_pack_args *args,
		       int fd[2], unsigned char *result_sha1,
		       struct ref *refs)
{
	int fetching;
	int count = 0, flushes = 0, flush_at = INITIAL_FLUSH, retval;
	const unsigned char *sha1;
	unsigned in_vain = 0;
	int got_continue = 0;
	int got_ready = 0;
	struct strbuf req_buf = STRBUF_INIT;
	size_t state_len = 0;

	if (args->stateless_rpc && multi_ack == 1)
		die("--stateless-rpc requires multi_ack_detailed");
	if (marked)
		for_each_ref(clear_marks, NULL);
	marked = 1;

	for_each_ref(rev_list_insert_ref, NULL);
	for_each_alternate_ref(insert_one_alternate_ref, NULL);

	fetching = 0;
	for ( ; refs ; refs = refs->next) {
		unsigned char *remote = refs->old_sha1;
		const char *remote_hex;
		struct object *o;

		/*
		 * If that object is complete (i.e. it is an ancestor of a
		 * local ref), we tell them we have it but do not have to
		 * tell them about its ancestors, which they already know
		 * about.
		 *
		 * We use lookup_object here because we are only
		 * interested in the case we *know* the object is
		 * reachable and we have already scanned it.
		 */
		if (((o = lookup_object(remote)) != NULL) &&
				(o->flags & COMPLETE)) {
			continue;
		}

		remote_hex = sha1_to_hex(remote);
		if (!fetching) {
			struct strbuf c = STRBUF_INIT;
			if (multi_ack == 2)     strbuf_addstr(&c, " multi_ack_detailed");
			if (multi_ack == 1)     strbuf_addstr(&c, " multi_ack");
			if (no_done)            strbuf_addstr(&c, " no-done");
			if (use_sideband == 2)  strbuf_addstr(&c, " side-band-64k");
			if (use_sideband == 1)  strbuf_addstr(&c, " side-band");
			if (args->use_thin_pack) strbuf_addstr(&c, " thin-pack");
			if (args->no_progress)   strbuf_addstr(&c, " no-progress");
			if (args->include_tag)   strbuf_addstr(&c, " include-tag");
			if (prefer_ofs_delta)   strbuf_addstr(&c, " ofs-delta");
			if (agent_supported)    strbuf_addf(&c, " agent=%s",
							    git_user_agent_sanitized());
			packet_buf_write(&req_buf, "want %s%s\n", remote_hex, c.buf);
			strbuf_release(&c);
		} else
			packet_buf_write(&req_buf, "want %s\n", remote_hex);
		fetching++;
	}

	if (!fetching) {
		strbuf_release(&req_buf);
		packet_flush(fd[1]);
		return 1;
	}

	if (is_repository_shallow())
		write_shallow_commits(&req_buf, 1, NULL);
	if (args->depth > 0)
		packet_buf_write(&req_buf, "deepen %d", args->depth);
	packet_buf_flush(&req_buf);
	state_len = req_buf.len;

	if (args->depth > 0) {
		char *line;
		const char *arg;
		unsigned char sha1[20];

		send_request(args, fd[1], &req_buf);
		while ((line = packet_read_line(fd[0], NULL))) {
			if (skip_prefix(line, "shallow ", &arg)) {
				if (get_sha1_hex(arg, sha1))
					die("invalid shallow line: %s", line);
				register_shallow(sha1);
				continue;
			}
			if (skip_prefix(line, "unshallow ", &arg)) {
				if (get_sha1_hex(arg, sha1))
					die("invalid unshallow line: %s", line);
				if (!lookup_object(sha1))
					die("object not found: %s", line);
				/* make sure that it is parsed as shallow */
				if (!parse_object(sha1))
					die("error in object: %s", line);
				if (unregister_shallow(sha1))
					die("no shallow found: %s", line);
				continue;
			}
			die("expected shallow/unshallow, got %s", line);
		}
	} else if (!args->stateless_rpc)
		send_request(args, fd[1], &req_buf);

	if (!args->stateless_rpc) {
		/* If we aren't using the stateless-rpc interface
		 * we don't need to retain the headers.
		 */
		strbuf_setlen(&req_buf, 0);
		state_len = 0;
	}

	flushes = 0;
	retval = -1;
	while ((sha1 = get_rev())) {
		packet_buf_write(&req_buf, "have %s\n", sha1_to_hex(sha1));
		if (args->verbose)
			fprintf(stderr, "have %s\n", sha1_to_hex(sha1));
		in_vain++;
		if (flush_at <= ++count) {
			int ack;

			packet_buf_flush(&req_buf);
			send_request(args, fd[1], &req_buf);
			strbuf_setlen(&req_buf, state_len);
			flushes++;
			flush_at = next_flush(args, count);

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (!args->stateless_rpc && count == INITIAL_FLUSH)
				continue;

			consume_shallow_list(args, fd[0]);
			do {
				ack = get_ack(fd[0], result_sha1);
				if (args->verbose && ack)
					fprintf(stderr, "got ack %d %s\n", ack,
							sha1_to_hex(result_sha1));
				switch (ack) {
				case ACK:
					flushes = 0;
					multi_ack = 0;
					retval = 0;
					goto done;
				case ACK_common:
				case ACK_ready:
				case ACK_continue: {
					struct commit *commit =
						lookup_commit(result_sha1);
					if (!commit)
						die("invalid commit %s", sha1_to_hex(result_sha1));
					if (args->stateless_rpc
					 && ack == ACK_common
					 && !(commit->object.flags & COMMON)) {
						/* We need to replay the have for this object
						 * on the next RPC request so the peer knows
						 * it is in common with us.
						 */
						const char *hex = sha1_to_hex(result_sha1);
						packet_buf_write(&req_buf, "have %s\n", hex);
						state_len = req_buf.len;
					}
					mark_common(commit, 0, 1);
					retval = 0;
					in_vain = 0;
					got_continue = 1;
					if (ack == ACK_ready) {
						clear_prio_queue(&rev_list);
						got_ready = 1;
					}
					break;
					}
				}
			} while (ack);
			flushes--;
			if (got_continue && MAX_IN_VAIN < in_vain) {
				if (args->verbose)
					fprintf(stderr, "giving up\n");
				break; /* give up */
			}
		}
	}
done:
	if (!got_ready || !no_done) {
		packet_buf_write(&req_buf, "done\n");
		send_request(args, fd[1], &req_buf);
	}
	if (args->verbose)
		fprintf(stderr, "done\n");
	if (retval != 0) {
		multi_ack = 0;
		flushes++;
	}
	strbuf_release(&req_buf);

	if (!got_ready || !no_done)
		consume_shallow_list(args, fd[0]);
	while (flushes || multi_ack) {
		int ack = get_ack(fd[0], result_sha1);
		if (ack) {
			if (args->verbose)
				fprintf(stderr, "got ack (%d) %s\n", ack,
					sha1_to_hex(result_sha1));
			if (ack == ACK)
				return 0;
			multi_ack = 1;
			continue;
		}
		flushes--;
	}
	/* it is no error to fetch into a completely empty repo */
	return count ? retval : 0;
}

static struct commit_list *complete;

static int mark_complete(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = parse_object(sha1);

	while (o && o->type == OBJ_TAG) {
		struct tag *t = (struct tag *) o;
		if (!t->tagged)
			break; /* broken repository */
		o->flags |= COMPLETE;
		o = parse_object(t->tagged->sha1);
	}
	if (o && o->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *)o;
		if (!(commit->object.flags & COMPLETE)) {
			commit->object.flags |= COMPLETE;
			commit_list_insert(commit, &complete);
		}
	}
	return 0;
}

static void mark_recent_complete_commits(struct fetch_pack_args *args,
					 unsigned long cutoff)
{
	while (complete && cutoff <= complete->item->date) {
		if (args->verbose)
			fprintf(stderr, "Marking %s as complete\n",
				sha1_to_hex(complete->item->object.sha1));
		pop_most_recent_commit(&complete, COMPLETE);
	}
}

static void filter_refs(struct fetch_pack_args *args,
			struct ref **refs,
			struct ref **sought, int nr_sought)
{
	struct ref *newlist = NULL;
	struct ref **newtail = &newlist;
	struct ref *ref, *next;
	int i;

	i = 0;
	for (ref = *refs; ref; ref = next) {
		int keep = 0;
		next = ref->next;

		if (starts_with(ref->name, "refs/") &&
		    check_refname_format(ref->name, 0))
			; /* trash */
		else {
			while (i < nr_sought) {
				int cmp = strcmp(ref->name, sought[i]->name);
				if (cmp < 0)
					break; /* definitely do not have it */
				else if (cmp == 0) {
					keep = 1; /* definitely have it */
					sought[i]->matched = 1;
				}
				i++;
			}
		}

		if (!keep && args->fetch_all &&
		    (!args->depth || !starts_with(ref->name, "refs/tags/")))
			keep = 1;

		if (keep) {
			*newtail = ref;
			ref->next = NULL;
			newtail = &ref->next;
		} else {
			free(ref);
		}
	}

	/* Append unmatched requests to the list */
	if (allow_tip_sha1_in_want) {
		for (i = 0; i < nr_sought; i++) {
			ref = sought[i];
			if (ref->matched)
				continue;
			if (get_sha1_hex(ref->name, ref->old_sha1))
				continue;

			ref->matched = 1;
			*newtail = ref;
			ref->next = NULL;
			newtail = &ref->next;
		}
	}
	*refs = newlist;
}

static void mark_alternate_complete(const struct ref *ref, void *unused)
{
	mark_complete(NULL, ref->old_sha1, 0, NULL);
}

static int everything_local(struct fetch_pack_args *args,
			    struct ref **refs,
			    struct ref **sought, int nr_sought)
{
	struct ref *ref;
	int retval;
	unsigned long cutoff = 0;

	save_commit_buffer = 0;

	for (ref = *refs; ref; ref = ref->next) {
		struct object *o;

		if (!has_sha1_file(ref->old_sha1))
			continue;

		o = parse_object(ref->old_sha1);
		if (!o)
			continue;

		/* We already have it -- which may mean that we were
		 * in sync with the other side at some time after
		 * that (it is OK if we guess wrong here).
		 */
		if (o->type == OBJ_COMMIT) {
			struct commit *commit = (struct commit *)o;
			if (!cutoff || cutoff < commit->date)
				cutoff = commit->date;
		}
	}

	if (!args->depth) {
		for_each_ref(mark_complete, NULL);
		for_each_alternate_ref(mark_alternate_complete, NULL);
		commit_list_sort_by_date(&complete);
		if (cutoff)
			mark_recent_complete_commits(args, cutoff);
	}

	/*
	 * Mark all complete remote refs as common refs.
	 * Don't mark them common yet; the server has to be told so first.
	 */
	for (ref = *refs; ref; ref = ref->next) {
		struct object *o = deref_tag(lookup_object(ref->old_sha1),
					     NULL, 0);

		if (!o || o->type != OBJ_COMMIT || !(o->flags & COMPLETE))
			continue;

		if (!(o->flags & SEEN)) {
			rev_list_push((struct commit *)o, COMMON_REF | SEEN);

			mark_common((struct commit *)o, 1, 1);
		}
	}

	filter_refs(args, refs, sought, nr_sought);

	for (retval = 1, ref = *refs; ref ; ref = ref->next) {
		const unsigned char *remote = ref->old_sha1;
		unsigned char local[20];
		struct object *o;

		o = lookup_object(remote);
		if (!o || !(o->flags & COMPLETE)) {
			retval = 0;
			if (!args->verbose)
				continue;
			fprintf(stderr,
				"want %s (%s)\n", sha1_to_hex(remote),
				ref->name);
			continue;
		}

		hashcpy(ref->new_sha1, local);
		if (!args->verbose)
			continue;
		fprintf(stderr,
			"already have %s (%s)\n", sha1_to_hex(remote),
			ref->name);
	}
	return retval;
}

static int sideband_demux(int in, int out, void *data)
{
	int *xd = data;

	int ret = recv_sideband("fetch-pack", xd[0], out);
	close(out);
	return ret;
}

static int get_pack(struct fetch_pack_args *args,
		    int xd[2], char **pack_lockfile)
{
	struct async demux;
	const char *argv[22];
	char keep_arg[256];
	char hdr_arg[256];
	const char **av, *cmd_name;
	int do_keep = args->keep_pack;
	struct child_process cmd;
	int ret;

	memset(&demux, 0, sizeof(demux));
	if (use_sideband) {
		/* xd[] is talking with upload-pack; subprocess reads from
		 * xd[0], spits out band#2 to stderr, and feeds us band#1
		 * through demux->out.
		 */
		demux.proc = sideband_demux;
		demux.data = xd;
		demux.out = -1;
		if (start_async(&demux))
			die("fetch-pack: unable to fork off sideband"
			    " demultiplexer");
	}
	else
		demux.out = xd[0];

	memset(&cmd, 0, sizeof(cmd));
	cmd.argv = argv;
	av = argv;
	*hdr_arg = 0;
	if (!args->keep_pack && unpack_limit) {
		struct pack_header header;

		if (read_pack_header(demux.out, &header))
			die("protocol error: bad pack header");
		snprintf(hdr_arg, sizeof(hdr_arg),
			 "--pack_header=%"PRIu32",%"PRIu32,
			 ntohl(header.hdr_version), ntohl(header.hdr_entries));
		if (ntohl(header.hdr_entries) < unpack_limit)
			do_keep = 0;
		else
			do_keep = 1;
	}

	if (alternate_shallow_file) {
		*av++ = "--shallow-file";
		*av++ = alternate_shallow_file;
	}

	if (do_keep) {
		if (pack_lockfile)
			cmd.out = -1;
		*av++ = cmd_name = "index-pack";
		*av++ = "--stdin";
		if (!args->quiet && !args->no_progress)
			*av++ = "-v";
		if (args->use_thin_pack)
			*av++ = "--fix-thin";
		if (args->lock_pack || unpack_limit) {
			int s = sprintf(keep_arg,
					"--keep=fetch-pack %"PRIuMAX " on ", (uintmax_t) getpid());
			if (gethostname(keep_arg + s, sizeof(keep_arg) - s))
				strcpy(keep_arg + s, "localhost");
			*av++ = keep_arg;
		}
		if (args->check_self_contained_and_connected)
			*av++ = "--check-self-contained-and-connected";
	}
	else {
		*av++ = cmd_name = "unpack-objects";
		if (args->quiet || args->no_progress)
			*av++ = "-q";
		args->check_self_contained_and_connected = 0;
	}
	if (*hdr_arg)
		*av++ = hdr_arg;
	if (fetch_fsck_objects >= 0
	    ? fetch_fsck_objects
	    : transfer_fsck_objects >= 0
	    ? transfer_fsck_objects
	    : 0)
		*av++ = "--strict";
	*av++ = NULL;

	cmd.in = demux.out;
	cmd.git_cmd = 1;
	if (start_command(&cmd))
		die("fetch-pack: unable to fork off %s", cmd_name);
	if (do_keep && pack_lockfile) {
		*pack_lockfile = index_pack_lockfile(cmd.out);
		close(cmd.out);
	}

	if (!use_sideband)
		/* Closed by start_command() */
		xd[0] = -1;

	ret = finish_command(&cmd);
	if (!ret || (args->check_self_contained_and_connected && ret == 1))
		args->self_contained_and_connected =
			args->check_self_contained_and_connected &&
			ret == 0;
	else
		die("%s failed", cmd_name);
	if (use_sideband && finish_async(&demux))
		die("error in sideband demultiplexer");
	return 0;
}

static int cmp_ref_by_name(const void *a_, const void *b_)
{
	const struct ref *a = *((const struct ref **)a_);
	const struct ref *b = *((const struct ref **)b_);
	return strcmp(a->name, b->name);
}

static struct ref *do_fetch_pack(struct fetch_pack_args *args,
				 int fd[2],
				 const struct ref *orig_ref,
				 struct ref **sought, int nr_sought,
				 struct shallow_info *si,
				 char **pack_lockfile)
{
	struct ref *ref = copy_ref_list(orig_ref);
	unsigned char sha1[20];
	const char *agent_feature;
	int agent_len;

	sort_ref_list(&ref, ref_compare_name);
	qsort(sought, nr_sought, sizeof(*sought), cmp_ref_by_name);

	if (is_repository_shallow() && !server_supports("shallow"))
		die("Server does not support shallow clients");
	if (server_supports("multi_ack_detailed")) {
		if (args->verbose)
			fprintf(stderr, "Server supports multi_ack_detailed\n");
		multi_ack = 2;
		if (server_supports("no-done")) {
			if (args->verbose)
				fprintf(stderr, "Server supports no-done\n");
			if (args->stateless_rpc)
				no_done = 1;
		}
	}
	else if (server_supports("multi_ack")) {
		if (args->verbose)
			fprintf(stderr, "Server supports multi_ack\n");
		multi_ack = 1;
	}
	if (server_supports("side-band-64k")) {
		if (args->verbose)
			fprintf(stderr, "Server supports side-band-64k\n");
		use_sideband = 2;
	}
	else if (server_supports("side-band")) {
		if (args->verbose)
			fprintf(stderr, "Server supports side-band\n");
		use_sideband = 1;
	}
	if (server_supports("allow-tip-sha1-in-want")) {
		if (args->verbose)
			fprintf(stderr, "Server supports allow-tip-sha1-in-want\n");
		allow_tip_sha1_in_want = 1;
	}
	if (!server_supports("thin-pack"))
		args->use_thin_pack = 0;
	if (!server_supports("no-progress"))
		args->no_progress = 0;
	if (!server_supports("include-tag"))
		args->include_tag = 0;
	if (server_supports("ofs-delta")) {
		if (args->verbose)
			fprintf(stderr, "Server supports ofs-delta\n");
	} else
		prefer_ofs_delta = 0;

	if ((agent_feature = server_feature_value("agent", &agent_len))) {
		agent_supported = 1;
		if (args->verbose && agent_len)
			fprintf(stderr, "Server version is %.*s\n",
				agent_len, agent_feature);
	}

	if (everything_local(args, &ref, sought, nr_sought)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(args, fd, sha1, ref) < 0)
		if (!args->keep_pack)
			/* When cloning, it is not unusual to have
			 * no common commit.
			 */
			warning("no common commits");

	if (args->stateless_rpc)
		packet_flush(fd[1]);
	if (args->depth > 0)
		setup_alternate_shallow(&shallow_lock, &alternate_shallow_file,
					NULL);
	else if (si->nr_ours || si->nr_theirs)
		alternate_shallow_file = setup_temporary_shallow(si->shallow);
	else
		alternate_shallow_file = NULL;
	if (get_pack(args, fd, pack_lockfile))
		die("git fetch-pack: fetch failed.");

 all_done:
	return ref;
}

static int fetch_pack_config(const char *var, const char *value, void *cb)
{
	if (strcmp(var, "fetch.unpacklimit") == 0) {
		fetch_unpack_limit = git_config_int(var, value);
		return 0;
	}

	if (strcmp(var, "transfer.unpacklimit") == 0) {
		transfer_unpack_limit = git_config_int(var, value);
		return 0;
	}

	if (strcmp(var, "repack.usedeltabaseoffset") == 0) {
		prefer_ofs_delta = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "fetch.fsckobjects")) {
		fetch_fsck_objects = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "transfer.fsckobjects")) {
		transfer_fsck_objects = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, cb);
}

static void fetch_pack_setup(void)
{
	static int did_setup;
	if (did_setup)
		return;
	git_config(fetch_pack_config, NULL);
	if (0 <= transfer_unpack_limit)
		unpack_limit = transfer_unpack_limit;
	else if (0 <= fetch_unpack_limit)
		unpack_limit = fetch_unpack_limit;
	did_setup = 1;
}

static int remove_duplicates_in_refs(struct ref **ref, int nr)
{
	struct string_list names = STRING_LIST_INIT_NODUP;
	int src, dst;

	for (src = dst = 0; src < nr; src++) {
		struct string_list_item *item;
		item = string_list_insert(&names, ref[src]->name);
		if (item->util)
			continue; /* already have it */
		item->util = ref[src];
		if (src != dst)
			ref[dst] = ref[src];
		dst++;
	}
	for (src = dst; src < nr; src++)
		ref[src] = NULL;
	string_list_clear(&names, 0);
	return dst;
}

static void update_shallow(struct fetch_pack_args *args,
			   struct ref **sought, int nr_sought,
			   struct shallow_info *si)
{
	struct sha1_array ref = SHA1_ARRAY_INIT;
	int *status;
	int i;

	if (args->depth > 0 && alternate_shallow_file) {
		if (*alternate_shallow_file == '\0') { /* --unshallow */
			unlink_or_warn(git_path("shallow"));
			rollback_lock_file(&shallow_lock);
		} else
			commit_lock_file(&shallow_lock);
		return;
	}

	if (!si->shallow || !si->shallow->nr)
		return;

	if (args->cloning) {
		/*
		 * remote is shallow, but this is a clone, there are
		 * no objects in repo to worry about. Accept any
		 * shallow points that exist in the pack (iow in repo
		 * after get_pack() and reprepare_packed_git())
		 */
		struct sha1_array extra = SHA1_ARRAY_INIT;
		unsigned char (*sha1)[20] = si->shallow->sha1;
		for (i = 0; i < si->shallow->nr; i++)
			if (has_sha1_file(sha1[i]))
				sha1_array_append(&extra, sha1[i]);
		if (extra.nr) {
			setup_alternate_shallow(&shallow_lock,
						&alternate_shallow_file,
						&extra);
			commit_lock_file(&shallow_lock);
		}
		sha1_array_clear(&extra);
		return;
	}

	if (!si->nr_ours && !si->nr_theirs)
		return;

	remove_nonexistent_theirs_shallow(si);
	if (!si->nr_ours && !si->nr_theirs)
		return;
	for (i = 0; i < nr_sought; i++)
		sha1_array_append(&ref, sought[i]->old_sha1);
	si->ref = &ref;

	if (args->update_shallow) {
		/*
		 * remote is also shallow, .git/shallow may be updated
		 * so all refs can be accepted. Make sure we only add
		 * shallow roots that are actually reachable from new
		 * refs.
		 */
		struct sha1_array extra = SHA1_ARRAY_INIT;
		unsigned char (*sha1)[20] = si->shallow->sha1;
		assign_shallow_commits_to_refs(si, NULL, NULL);
		if (!si->nr_ours && !si->nr_theirs) {
			sha1_array_clear(&ref);
			return;
		}
		for (i = 0; i < si->nr_ours; i++)
			sha1_array_append(&extra, sha1[si->ours[i]]);
		for (i = 0; i < si->nr_theirs; i++)
			sha1_array_append(&extra, sha1[si->theirs[i]]);
		setup_alternate_shallow(&shallow_lock,
					&alternate_shallow_file,
					&extra);
		commit_lock_file(&shallow_lock);
		sha1_array_clear(&extra);
		sha1_array_clear(&ref);
		return;
	}

	/*
	 * remote is also shallow, check what ref is safe to update
	 * without updating .git/shallow
	 */
	status = xcalloc(nr_sought, sizeof(*status));
	assign_shallow_commits_to_refs(si, NULL, status);
	if (si->nr_ours || si->nr_theirs) {
		for (i = 0; i < nr_sought; i++)
			if (status[i])
				sought[i]->status = REF_STATUS_REJECT_SHALLOW;
	}
	free(status);
	sha1_array_clear(&ref);
}

struct ref *fetch_pack(struct fetch_pack_args *args,
		       int fd[], struct child_process *conn,
		       const struct ref *ref,
		       const char *dest,
		       struct ref **sought, int nr_sought,
		       struct sha1_array *shallow,
		       char **pack_lockfile)
{
	struct ref *ref_cpy;
	struct shallow_info si;

	fetch_pack_setup();
	if (nr_sought)
		nr_sought = remove_duplicates_in_refs(sought, nr_sought);

	if (!ref) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	prepare_shallow_info(&si, shallow);
	ref_cpy = do_fetch_pack(args, fd, ref, sought, nr_sought,
				&si, pack_lockfile);
	reprepare_packed_git();
	update_shallow(args, sought, nr_sought, &si);
	clear_shallow_info(&si);
	return ref_cpy;
}
