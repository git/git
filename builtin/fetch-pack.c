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

static int transfer_unpack_limit = -1;
static int fetch_unpack_limit = -1;
static int unpack_limit = 100;
static int prefer_ofs_delta = 1;
static int fetch_fsck_objects = -1;
static int transfer_fsck_objects = -1;
static struct fetch_pack_args args = {
	/* .uploadpack = */ "git-upload-pack",
};

static const char fetch_pack_usage[] =
"git fetch-pack [--all] [--quiet|-q] [--keep|-k] [--thin] [--include-tag] [--upload-pack=<git-upload-pack>] [--depth=<n>] [--no-progress] [-v] [<host>:]<directory> [<refs>...]";

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

static struct commit_list *rev_list;
static int non_common_revs, multi_ack, use_sideband;

static void rev_list_push(struct commit *commit, int mark)
{
	if (!(commit->object.flags & mark)) {
		commit->object.flags |= mark;

		if (!(commit->object.parsed))
			if (parse_commit(commit))
				return;

		insert_by_date(commit, &rev_list);

		if (!(commit->object.flags & COMMON))
			non_common_revs++;
	}
}

static int rev_list_insert_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = deref_tag(parse_object(sha1), path, 0);

	if (o && o->type == OBJ_COMMIT)
		rev_list_push((struct commit *)o, SEEN);

	return 0;
}

static int clear_marks(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = deref_tag(parse_object(sha1), path, 0);

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

		if (rev_list == NULL || non_common_revs == 0)
			return NULL;

		commit = rev_list->item;
		if (!commit->object.parsed)
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

		rev_list = rev_list->next;
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

static void consume_shallow_list(int fd)
{
	if (args.stateless_rpc && args.depth > 0) {
		/* If we sent a depth we will get back "duplicate"
		 * shallow and unshallow commands every time there
		 * is a block of have lines exchanged.
		 */
		char line[1000];
		while (packet_read_line(fd, line, sizeof(line))) {
			if (!prefixcmp(line, "shallow "))
				continue;
			if (!prefixcmp(line, "unshallow "))
				continue;
			die("git fetch-pack: expected shallow list");
		}
	}
}

static enum ack_type get_ack(int fd, unsigned char *result_sha1)
{
	static char line[1000];
	int len = packet_read_line(fd, line, sizeof(line));

	if (!len)
		die("git fetch-pack: expected ACK/NAK, got EOF");
	if (line[len-1] == '\n')
		line[--len] = 0;
	if (!strcmp(line, "NAK"))
		return NAK;
	if (!prefixcmp(line, "ACK ")) {
		if (!get_sha1_hex(line+4, result_sha1)) {
			if (strstr(line+45, "continue"))
				return ACK_continue;
			if (strstr(line+45, "common"))
				return ACK_common;
			if (strstr(line+45, "ready"))
				return ACK_ready;
			return ACK;
		}
	}
	die("git fetch_pack: expected ACK/NAK, got '%s'", line);
}

static void send_request(int fd, struct strbuf *buf)
{
	if (args.stateless_rpc) {
		send_sideband(fd, -1, buf->buf, buf->len, LARGE_PACKET_MAX);
		packet_flush(fd);
	} else
		safe_write(fd, buf->buf, buf->len);
}

static int find_common(int fd[2], unsigned char *result_sha1,
		       struct ref *refs)
{
	int fetching;
	int count = 0, flushes = 0, retval;
	const unsigned char *sha1;
	unsigned in_vain = 0;
	int got_continue = 0;
	struct strbuf req_buf = STRBUF_INIT;
	size_t state_len = 0;

	if (args.stateless_rpc && multi_ack == 1)
		die("--stateless-rpc requires multi_ack_detailed");
	if (marked)
		for_each_ref(clear_marks, NULL);
	marked = 1;

	for_each_ref(rev_list_insert_ref, NULL);

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
			if (use_sideband == 2)  strbuf_addstr(&c, " side-band-64k");
			if (use_sideband == 1)  strbuf_addstr(&c, " side-band");
			if (args.use_thin_pack) strbuf_addstr(&c, " thin-pack");
			if (args.no_progress)   strbuf_addstr(&c, " no-progress");
			if (args.include_tag)   strbuf_addstr(&c, " include-tag");
			if (prefer_ofs_delta)   strbuf_addstr(&c, " ofs-delta");
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
		write_shallow_commits(&req_buf, 1);
	if (args.depth > 0)
		packet_buf_write(&req_buf, "deepen %d", args.depth);
	packet_buf_flush(&req_buf);
	state_len = req_buf.len;

	if (args.depth > 0) {
		char line[1024];
		unsigned char sha1[20];

		send_request(fd[1], &req_buf);
		while (packet_read_line(fd[0], line, sizeof(line))) {
			if (!prefixcmp(line, "shallow ")) {
				if (get_sha1_hex(line + 8, sha1))
					die("invalid shallow line: %s", line);
				register_shallow(sha1);
				continue;
			}
			if (!prefixcmp(line, "unshallow ")) {
				if (get_sha1_hex(line + 10, sha1))
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
	} else if (!args.stateless_rpc)
		send_request(fd[1], &req_buf);

	if (!args.stateless_rpc) {
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
		if (args.verbose)
			fprintf(stderr, "have %s\n", sha1_to_hex(sha1));
		in_vain++;
		if (!(31 & ++count)) {
			int ack;

			packet_buf_flush(&req_buf);
			send_request(fd[1], &req_buf);
			strbuf_setlen(&req_buf, state_len);
			flushes++;

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (!args.stateless_rpc && count == 32)
				continue;

			consume_shallow_list(fd[0]);
			do {
				ack = get_ack(fd[0], result_sha1);
				if (args.verbose && ack)
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
					if (args.stateless_rpc
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
					break;
					}
				}
			} while (ack);
			flushes--;
			if (got_continue && MAX_IN_VAIN < in_vain) {
				if (args.verbose)
					fprintf(stderr, "giving up\n");
				break; /* give up */
			}
		}
	}
done:
	packet_buf_write(&req_buf, "done\n");
	send_request(fd[1], &req_buf);
	if (args.verbose)
		fprintf(stderr, "done\n");
	if (retval != 0) {
		multi_ack = 0;
		flushes++;
	}
	strbuf_release(&req_buf);

	consume_shallow_list(fd[0]);
	while (flushes || multi_ack) {
		int ack = get_ack(fd[0], result_sha1);
		if (ack) {
			if (args.verbose)
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

static int mark_complete(const char *path, const unsigned char *sha1, int flag, void *cb_data)
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
		commit->object.flags |= COMPLETE;
		insert_by_date(commit, &complete);
	}
	return 0;
}

static void mark_recent_complete_commits(unsigned long cutoff)
{
	while (complete && cutoff <= complete->item->date) {
		if (args.verbose)
			fprintf(stderr, "Marking %s as complete\n",
				sha1_to_hex(complete->item->object.sha1));
		pop_most_recent_commit(&complete, COMPLETE);
	}
}

static void filter_refs(struct ref **refs, int nr_match, char **match)
{
	struct ref **return_refs;
	struct ref *newlist = NULL;
	struct ref **newtail = &newlist;
	struct ref *ref, *next;
	struct ref *fastarray[32];

	if (nr_match && !args.fetch_all) {
		if (ARRAY_SIZE(fastarray) < nr_match)
			return_refs = xcalloc(nr_match, sizeof(struct ref *));
		else {
			return_refs = fastarray;
			memset(return_refs, 0, sizeof(struct ref *) * nr_match);
		}
	}
	else
		return_refs = NULL;

	for (ref = *refs; ref; ref = next) {
		next = ref->next;
		if (!memcmp(ref->name, "refs/", 5) &&
		    check_ref_format(ref->name + 5))
			; /* trash */
		else if (args.fetch_all &&
			 (!args.depth || prefixcmp(ref->name, "refs/tags/") )) {
			*newtail = ref;
			ref->next = NULL;
			newtail = &ref->next;
			continue;
		}
		else {
			int order = path_match(ref->name, nr_match, match);
			if (order) {
				return_refs[order-1] = ref;
				continue; /* we will link it later */
			}
		}
		free(ref);
	}

	if (!args.fetch_all) {
		int i;
		for (i = 0; i < nr_match; i++) {
			ref = return_refs[i];
			if (ref) {
				*newtail = ref;
				ref->next = NULL;
				newtail = &ref->next;
			}
		}
		if (return_refs != fastarray)
			free(return_refs);
	}
	*refs = newlist;
}

static int everything_local(struct ref **refs, int nr_match, char **match)
{
	struct ref *ref;
	int retval;
	unsigned long cutoff = 0;

	save_commit_buffer = 0;

	for (ref = *refs; ref; ref = ref->next) {
		struct object *o;

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

	if (!args.depth) {
		for_each_ref(mark_complete, NULL);
		if (cutoff)
			mark_recent_complete_commits(cutoff);
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

	filter_refs(refs, nr_match, match);

	for (retval = 1, ref = *refs; ref ; ref = ref->next) {
		const unsigned char *remote = ref->old_sha1;
		unsigned char local[20];
		struct object *o;

		o = lookup_object(remote);
		if (!o || !(o->flags & COMPLETE)) {
			retval = 0;
			if (!args.verbose)
				continue;
			fprintf(stderr,
				"want %s (%s)\n", sha1_to_hex(remote),
				ref->name);
			continue;
		}

		hashcpy(ref->new_sha1, local);
		if (!args.verbose)
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

static int get_pack(int xd[2], char **pack_lockfile)
{
	struct async demux;
	const char *argv[20];
	char keep_arg[256];
	char hdr_arg[256];
	const char **av;
	int do_keep = args.keep_pack;
	struct child_process cmd;

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
	if (!args.keep_pack && unpack_limit) {
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

	if (do_keep) {
		if (pack_lockfile)
			cmd.out = -1;
		*av++ = "index-pack";
		*av++ = "--stdin";
		if (!args.quiet && !args.no_progress)
			*av++ = "-v";
		if (args.use_thin_pack)
			*av++ = "--fix-thin";
		if (args.lock_pack || unpack_limit) {
			int s = sprintf(keep_arg,
					"--keep=fetch-pack %"PRIuMAX " on ", (uintmax_t) getpid());
			if (gethostname(keep_arg + s, sizeof(keep_arg) - s))
				strcpy(keep_arg + s, "localhost");
			*av++ = keep_arg;
		}
	}
	else {
		*av++ = "unpack-objects";
		if (args.quiet)
			*av++ = "-q";
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
		die("fetch-pack: unable to fork off %s", argv[0]);
	if (do_keep && pack_lockfile) {
		*pack_lockfile = index_pack_lockfile(cmd.out);
		close(cmd.out);
	}

	if (finish_command(&cmd))
		die("%s failed", argv[0]);
	if (use_sideband && finish_async(&demux))
		die("error in sideband demultiplexer");
	return 0;
}

static struct ref *do_fetch_pack(int fd[2],
		const struct ref *orig_ref,
		int nr_match,
		char **match,
		char **pack_lockfile)
{
	struct ref *ref = copy_ref_list(orig_ref);
	unsigned char sha1[20];

	if (is_repository_shallow() && !server_supports("shallow"))
		die("Server does not support shallow clients");
	if (server_supports("multi_ack_detailed")) {
		if (args.verbose)
			fprintf(stderr, "Server supports multi_ack_detailed\n");
		multi_ack = 2;
	}
	else if (server_supports("multi_ack")) {
		if (args.verbose)
			fprintf(stderr, "Server supports multi_ack\n");
		multi_ack = 1;
	}
	if (server_supports("side-band-64k")) {
		if (args.verbose)
			fprintf(stderr, "Server supports side-band-64k\n");
		use_sideband = 2;
	}
	else if (server_supports("side-band")) {
		if (args.verbose)
			fprintf(stderr, "Server supports side-band\n");
		use_sideband = 1;
	}
	if (server_supports("ofs-delta")) {
		if (args.verbose)
			fprintf(stderr, "Server supports ofs-delta\n");
	} else
		prefer_ofs_delta = 0;
	if (everything_local(&ref, nr_match, match)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(fd, sha1, ref) < 0)
		if (!args.keep_pack)
			/* When cloning, it is not unusual to have
			 * no common commit.
			 */
			warning("no common commits");

	if (args.stateless_rpc)
		packet_flush(fd[1]);
	if (get_pack(fd, pack_lockfile))
		die("git fetch-pack: fetch failed.");

 all_done:
	return ref;
}

static int remove_duplicates(int nr_heads, char **heads)
{
	int src, dst;

	for (src = dst = 0; src < nr_heads; src++) {
		/* If heads[src] is different from any of
		 * heads[0..dst], push it in.
		 */
		int i;
		for (i = 0; i < dst; i++) {
			if (!strcmp(heads[i], heads[src]))
				break;
		}
		if (i < dst)
			continue;
		if (src != dst)
			heads[dst] = heads[src];
		dst++;
	}
	return dst;
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

static struct lock_file lock;

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

int cmd_fetch_pack(int argc, const char **argv, const char *prefix)
{
	int i, ret, nr_heads;
	struct ref *ref = NULL;
	char *dest = NULL, **heads;
	int fd[2];
	char *pack_lockfile = NULL;
	char **pack_lockfile_ptr = NULL;
	struct child_process *conn;

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
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
		dest = (char *)arg;
		heads = (char **)(argv + i + 1);
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(fetch_pack_usage);

	if (args.stateless_rpc) {
		conn = NULL;
		fd[0] = 0;
		fd[1] = 1;
	} else {
		conn = git_connect(fd, (char *)dest, args.uploadpack,
				   args.verbose ? CONNECT_VERBOSE : 0);
	}

	get_remote_heads(fd[0], &ref, 0, NULL, 0, NULL);

	ref = fetch_pack(&args, fd, conn, ref, dest,
		nr_heads, heads, pack_lockfile_ptr);
	if (pack_lockfile) {
		printf("lock %s\n", pack_lockfile);
		fflush(stdout);
	}
	close(fd[0]);
	close(fd[1]);
	if (finish_connect(conn))
		ref = NULL;
	ret = !ref;

	if (!ret && nr_heads) {
		/* If the heads to pull were given, we should have
		 * consumed all of them by matching the remote.
		 * Otherwise, 'git fetch remote no-such-ref' would
		 * silently succeed without issuing an error.
		 */
		for (i = 0; i < nr_heads; i++)
			if (heads[i] && heads[i][0]) {
				error("no such remote ref %s", heads[i]);
				ret = 1;
			}
	}
	while (ref) {
		printf("%s %s\n",
		       sha1_to_hex(ref->old_sha1), ref->name);
		ref = ref->next;
	}

	return ret;
}

struct ref *fetch_pack(struct fetch_pack_args *my_args,
		       int fd[], struct child_process *conn,
		       const struct ref *ref,
		const char *dest,
		int nr_heads,
		char **heads,
		char **pack_lockfile)
{
	struct stat st;
	struct ref *ref_cpy;

	fetch_pack_setup();
	if (&args != my_args)
		memcpy(&args, my_args, sizeof(args));
	if (args.depth > 0) {
		if (stat(git_path("shallow"), &st))
			st.st_mtime = 0;
	}

	if (heads && nr_heads)
		nr_heads = remove_duplicates(nr_heads, heads);
	if (!ref) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	ref_cpy = do_fetch_pack(fd, ref, nr_heads, heads, pack_lockfile);

	if (args.depth > 0) {
		struct cache_time mtime;
		struct strbuf sb = STRBUF_INIT;
		char *shallow = git_path("shallow");
		int fd;

		mtime.sec = st.st_mtime;
		mtime.nsec = ST_MTIME_NSEC(st);
		if (stat(shallow, &st)) {
			if (mtime.sec)
				die("shallow file was removed during fetch");
		} else if (st.st_mtime != mtime.sec
#ifdef USE_NSEC
				|| ST_MTIME_NSEC(st) != mtime.nsec
#endif
			  )
			die("shallow file was changed during fetch");

		fd = hold_lock_file_for_update(&lock, shallow,
					       LOCK_DIE_ON_ERROR);
		if (!write_shallow_commits(&sb, 0)
		 || write_in_full(fd, sb.buf, sb.len) != sb.len) {
			unlink_or_warn(shallow);
			rollback_lock_file(&lock);
		} else {
			commit_lock_file(&lock);
		}
		strbuf_release(&sb);
	}

	reprepare_packed_git();
	return ref_cpy;
}
