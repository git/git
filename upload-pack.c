#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "tag.h"
#include "object.h"
#include "commit.h"
#include "exec_cmd.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "run-command.h"
#include "sigchain.h"

static const char upload_pack_usage[] = "git upload-pack [--strict] [--timeout=<n>] <dir>";

/* bits #0..7 in revision.h, #8..10 in commit.c */
#define THEY_HAVE	(1u << 11)
#define OUR_REF		(1u << 12)
#define WANTED		(1u << 13)
#define COMMON_KNOWN	(1u << 14)
#define REACHABLE	(1u << 15)

#define SHALLOW		(1u << 16)
#define NOT_SHALLOW	(1u << 17)
#define CLIENT_SHALLOW	(1u << 18)

static unsigned long oldest_have;

static int multi_ack, nr_our_refs;
static int no_done;
static int use_thin_pack, use_ofs_delta, use_include_tag;
static int no_progress, daemon_mode;
static int shallow_nr;
static struct object_array have_obj;
static struct object_array want_obj;
static struct object_array extra_edge_obj;
static unsigned int timeout;
/* 0 for no sideband,
 * otherwise maximum packet size (up to 65520 bytes).
 */
static int use_sideband;
static int debug_fd;
static int advertise_refs;
static int stateless_rpc;

static void reset_timeout(void)
{
	alarm(timeout);
}

static int strip(char *line, int len)
{
	if (len && line[len-1] == '\n')
		line[--len] = 0;
	return len;
}

static ssize_t send_client_data(int fd, const char *data, ssize_t sz)
{
	if (use_sideband)
		return send_sideband(1, fd, data, sz, use_sideband);
	if (fd == 3)
		/* emergency quit */
		fd = 2;
	if (fd == 2) {
		/* XXX: are we happy to lose stuff here? */
		xwrite(fd, data, sz);
		return sz;
	}
	return safe_write(fd, data, sz);
}

static FILE *pack_pipe = NULL;
static void show_commit(struct commit *commit, void *data)
{
	if (commit->object.flags & BOUNDARY)
		fputc('-', pack_pipe);
	if (fputs(sha1_to_hex(commit->object.sha1), pack_pipe) < 0)
		die("broken output pipe");
	fputc('\n', pack_pipe);
	fflush(pack_pipe);
	free(commit->buffer);
	commit->buffer = NULL;
}

static void show_object(struct object *obj, const struct name_path *path, const char *component)
{
	show_object_with_name(pack_pipe, obj, path, component);
}

static void show_edge(struct commit *commit)
{
	fprintf(pack_pipe, "-%s\n", sha1_to_hex(commit->object.sha1));
}

static int do_rev_list(int in, int out, void *user_data)
{
	int i;
	struct rev_info revs;

	pack_pipe = xfdopen(out, "w");
	init_revisions(&revs, NULL);
	revs.tag_objects = 1;
	revs.tree_objects = 1;
	revs.blob_objects = 1;
	if (use_thin_pack)
		revs.edge_hint = 1;

	for (i = 0; i < want_obj.nr; i++) {
		struct object *o = want_obj.objects[i].item;
		/* why??? */
		o->flags &= ~UNINTERESTING;
		add_pending_object(&revs, o, NULL);
	}
	for (i = 0; i < have_obj.nr; i++) {
		struct object *o = have_obj.objects[i].item;
		o->flags |= UNINTERESTING;
		add_pending_object(&revs, o, NULL);
	}
	setup_revisions(0, NULL, &revs, NULL);
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	mark_edges_uninteresting(revs.commits, &revs, show_edge);
	if (use_thin_pack)
		for (i = 0; i < extra_edge_obj.nr; i++)
			fprintf(pack_pipe, "-%s\n", sha1_to_hex(
					extra_edge_obj.objects[i].item->sha1));
	traverse_commit_list(&revs, show_commit, show_object, NULL);
	fflush(pack_pipe);
	fclose(pack_pipe);
	return 0;
}

static void create_pack_file(void)
{
	struct async rev_list;
	struct child_process pack_objects;
	int create_full_pack = (nr_our_refs == want_obj.nr && !have_obj.nr);
	char data[8193], progress[128];
	char abort_msg[] = "aborting due to possible repository "
		"corruption on the remote side.";
	int buffered = -1;
	ssize_t sz;
	const char *argv[10];
	int arg = 0;

	argv[arg++] = "pack-objects";
	if (!shallow_nr) {
		argv[arg++] = "--revs";
		if (create_full_pack)
			argv[arg++] = "--all";
		else if (use_thin_pack)
			argv[arg++] = "--thin";
	}

	argv[arg++] = "--stdout";
	if (!no_progress)
		argv[arg++] = "--progress";
	if (use_ofs_delta)
		argv[arg++] = "--delta-base-offset";
	if (use_include_tag)
		argv[arg++] = "--include-tag";
	argv[arg++] = NULL;

	memset(&pack_objects, 0, sizeof(pack_objects));
	pack_objects.in = -1;
	pack_objects.out = -1;
	pack_objects.err = -1;
	pack_objects.git_cmd = 1;
	pack_objects.argv = argv;

	if (start_command(&pack_objects))
		die("git upload-pack: unable to fork git-pack-objects");

	if (shallow_nr) {
		memset(&rev_list, 0, sizeof(rev_list));
		rev_list.proc = do_rev_list;
		rev_list.out = pack_objects.in;
		if (start_async(&rev_list))
			die("git upload-pack: unable to fork git-rev-list");
	}
	else {
		FILE *pipe_fd = xfdopen(pack_objects.in, "w");
		if (!create_full_pack) {
			int i;
			for (i = 0; i < want_obj.nr; i++)
				fprintf(pipe_fd, "%s\n", sha1_to_hex(want_obj.objects[i].item->sha1));
			fprintf(pipe_fd, "--not\n");
			for (i = 0; i < have_obj.nr; i++)
				fprintf(pipe_fd, "%s\n", sha1_to_hex(have_obj.objects[i].item->sha1));
		}

		fprintf(pipe_fd, "\n");
		fflush(pipe_fd);
		fclose(pipe_fd);
	}


	/* We read from pack_objects.err to capture stderr output for
	 * progress bar, and pack_objects.out to capture the pack data.
	 */

	while (1) {
		struct pollfd pfd[2];
		int pe, pu, pollsize;

		reset_timeout();

		pollsize = 0;
		pe = pu = -1;

		if (0 <= pack_objects.out) {
			pfd[pollsize].fd = pack_objects.out;
			pfd[pollsize].events = POLLIN;
			pu = pollsize;
			pollsize++;
		}
		if (0 <= pack_objects.err) {
			pfd[pollsize].fd = pack_objects.err;
			pfd[pollsize].events = POLLIN;
			pe = pollsize;
			pollsize++;
		}

		if (!pollsize)
			break;

		if (poll(pfd, pollsize, -1) < 0) {
			if (errno != EINTR) {
				error("poll failed, resuming: %s",
				      strerror(errno));
				sleep(1);
			}
			continue;
		}
		if (0 <= pe && (pfd[pe].revents & (POLLIN|POLLHUP))) {
			/* Status ready; we ship that in the side-band
			 * or dump to the standard error.
			 */
			sz = xread(pack_objects.err, progress,
				  sizeof(progress));
			if (0 < sz)
				send_client_data(2, progress, sz);
			else if (sz == 0) {
				close(pack_objects.err);
				pack_objects.err = -1;
			}
			else
				goto fail;
			/* give priority to status messages */
			continue;
		}
		if (0 <= pu && (pfd[pu].revents & (POLLIN|POLLHUP))) {
			/* Data ready; we keep the last byte to ourselves
			 * in case we detect broken rev-list, so that we
			 * can leave the stream corrupted.  This is
			 * unfortunate -- unpack-objects would happily
			 * accept a valid packdata with trailing garbage,
			 * so appending garbage after we pass all the
			 * pack data is not good enough to signal
			 * breakage to downstream.
			 */
			char *cp = data;
			ssize_t outsz = 0;
			if (0 <= buffered) {
				*cp++ = buffered;
				outsz++;
			}
			sz = xread(pack_objects.out, cp,
				  sizeof(data) - outsz);
			if (0 < sz)
				;
			else if (sz == 0) {
				close(pack_objects.out);
				pack_objects.out = -1;
			}
			else
				goto fail;
			sz += outsz;
			if (1 < sz) {
				buffered = data[sz-1] & 0xFF;
				sz--;
			}
			else
				buffered = -1;
			sz = send_client_data(1, data, sz);
			if (sz < 0)
				goto fail;
		}
	}

	if (finish_command(&pack_objects)) {
		error("git upload-pack: git-pack-objects died with error.");
		goto fail;
	}
	if (shallow_nr && finish_async(&rev_list))
		goto fail;	/* error was already reported */

	/* flush the data */
	if (0 <= buffered) {
		data[0] = buffered;
		sz = send_client_data(1, data, 1);
		if (sz < 0)
			goto fail;
		fprintf(stderr, "flushed.\n");
	}
	if (use_sideband)
		packet_flush(1);
	return;

 fail:
	send_client_data(3, abort_msg, sizeof(abort_msg));
	die("git upload-pack: %s", abort_msg);
}

static int got_sha1(char *hex, unsigned char *sha1)
{
	struct object *o;
	int we_knew_they_have = 0;

	if (get_sha1_hex(hex, sha1))
		die("git upload-pack: expected SHA1 object, got '%s'", hex);
	if (!has_sha1_file(sha1))
		return -1;

	o = lookup_object(sha1);
	if (!(o && o->parsed))
		o = parse_object(sha1);
	if (!o)
		die("oops (%s)", sha1_to_hex(sha1));
	if (o->type == OBJ_COMMIT) {
		struct commit_list *parents;
		struct commit *commit = (struct commit *)o;
		if (o->flags & THEY_HAVE)
			we_knew_they_have = 1;
		else
			o->flags |= THEY_HAVE;
		if (!oldest_have || (commit->date < oldest_have))
			oldest_have = commit->date;
		for (parents = commit->parents;
		     parents;
		     parents = parents->next)
			parents->item->object.flags |= THEY_HAVE;
	}
	if (!we_knew_they_have) {
		add_object_array(o, NULL, &have_obj);
		return 1;
	}
	return 0;
}

static int reachable(struct commit *want)
{
	struct commit_list *work = NULL;

	commit_list_insert_by_date(want, &work);
	while (work) {
		struct commit_list *list = work->next;
		struct commit *commit = work->item;
		free(work);
		work = list;

		if (commit->object.flags & THEY_HAVE) {
			want->object.flags |= COMMON_KNOWN;
			break;
		}
		if (!commit->object.parsed)
			parse_object(commit->object.sha1);
		if (commit->object.flags & REACHABLE)
			continue;
		commit->object.flags |= REACHABLE;
		if (commit->date < oldest_have)
			continue;
		for (list = commit->parents; list; list = list->next) {
			struct commit *parent = list->item;
			if (!(parent->object.flags & REACHABLE))
				commit_list_insert_by_date(parent, &work);
		}
	}
	want->object.flags |= REACHABLE;
	clear_commit_marks(want, REACHABLE);
	free_commit_list(work);
	return (want->object.flags & COMMON_KNOWN);
}

static int ok_to_give_up(void)
{
	int i;

	if (!have_obj.nr)
		return 0;

	for (i = 0; i < want_obj.nr; i++) {
		struct object *want = want_obj.objects[i].item;

		if (want->flags & COMMON_KNOWN)
			continue;
		want = deref_tag(want, "a want line", 0);
		if (!want || want->type != OBJ_COMMIT) {
			/* no way to tell if this is reachable by
			 * looking at the ancestry chain alone, so
			 * leave a note to ourselves not to worry about
			 * this object anymore.
			 */
			want_obj.objects[i].item->flags |= COMMON_KNOWN;
			continue;
		}
		if (!reachable((struct commit *)want))
			return 0;
	}
	return 1;
}

static int get_common_commits(void)
{
	static char line[1000];
	unsigned char sha1[20];
	char last_hex[41];
	int got_common = 0;
	int got_other = 0;
	int sent_ready = 0;

	save_commit_buffer = 0;

	for (;;) {
		int len = packet_read_line(0, line, sizeof(line));
		reset_timeout();

		if (!len) {
			if (multi_ack == 2 && got_common
			    && !got_other && ok_to_give_up()) {
				sent_ready = 1;
				packet_write(1, "ACK %s ready\n", last_hex);
			}
			if (have_obj.nr == 0 || multi_ack)
				packet_write(1, "NAK\n");

			if (no_done && sent_ready) {
				packet_write(1, "ACK %s\n", last_hex);
				return 0;
			}
			if (stateless_rpc)
				exit(0);
			got_common = 0;
			got_other = 0;
			continue;
		}
		strip(line, len);
		if (!prefixcmp(line, "have ")) {
			switch (got_sha1(line+5, sha1)) {
			case -1: /* they have what we do not */
				got_other = 1;
				if (multi_ack && ok_to_give_up()) {
					const char *hex = sha1_to_hex(sha1);
					if (multi_ack == 2) {
						sent_ready = 1;
						packet_write(1, "ACK %s ready\n", hex);
					} else
						packet_write(1, "ACK %s continue\n", hex);
				}
				break;
			default:
				got_common = 1;
				memcpy(last_hex, sha1_to_hex(sha1), 41);
				if (multi_ack == 2)
					packet_write(1, "ACK %s common\n", last_hex);
				else if (multi_ack)
					packet_write(1, "ACK %s continue\n", last_hex);
				else if (have_obj.nr == 1)
					packet_write(1, "ACK %s\n", last_hex);
				break;
			}
			continue;
		}
		if (!strcmp(line, "done")) {
			if (have_obj.nr > 0) {
				if (multi_ack)
					packet_write(1, "ACK %s\n", last_hex);
				return 0;
			}
			packet_write(1, "NAK\n");
			return -1;
		}
		die("git upload-pack: expected SHA1 list, got '%s'", line);
	}
}

static void check_non_tip(void)
{
	static const char *argv[] = {
		"rev-list", "--stdin", NULL,
	};
	static struct child_process cmd;
	struct object *o;
	char namebuf[42]; /* ^ + SHA-1 + LF */
	int i;

	/* In the normal in-process case non-tip request can never happen */
	if (!stateless_rpc)
		goto error;

	cmd.argv = argv;
	cmd.git_cmd = 1;
	cmd.no_stderr = 1;
	cmd.in = -1;
	cmd.out = -1;

	if (start_command(&cmd))
		goto error;

	/*
	 * If rev-list --stdin encounters an unknown commit, it
	 * terminates, which will cause SIGPIPE in the write loop
	 * below.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);

	namebuf[0] = '^';
	namebuf[41] = '\n';
	for (i = get_max_object_index(); 0 < i; ) {
		o = get_indexed_object(--i);
		if (!o)
			continue;
		if (!(o->flags & OUR_REF))
			continue;
		memcpy(namebuf + 1, sha1_to_hex(o->sha1), 40);
		if (write_in_full(cmd.in, namebuf, 42) < 0)
			goto error;
	}
	namebuf[40] = '\n';
	for (i = 0; i < want_obj.nr; i++) {
		o = want_obj.objects[i].item;
		if (o->flags & OUR_REF)
			continue;
		memcpy(namebuf, sha1_to_hex(o->sha1), 40);
		if (write_in_full(cmd.in, namebuf, 41) < 0)
			goto error;
	}
	close(cmd.in);

	sigchain_pop(SIGPIPE);

	/*
	 * The commits out of the rev-list are not ancestors of
	 * our ref.
	 */
	i = read_in_full(cmd.out, namebuf, 1);
	if (i)
		goto error;
	close(cmd.out);

	/*
	 * rev-list may have died by encountering a bad commit
	 * in the history, in which case we do want to bail out
	 * even when it showed no commit.
	 */
	if (finish_command(&cmd))
		goto error;

	/* All the non-tip ones are ancestors of what we advertised */
	return;

error:
	/* Pick one of them (we know there at least is one) */
	for (i = 0; i < want_obj.nr; i++) {
		o = want_obj.objects[i].item;
		if (!(o->flags & OUR_REF))
			die("git upload-pack: not our ref %s",
			    sha1_to_hex(o->sha1));
	}
}

static void receive_needs(void)
{
	struct object_array shallows = OBJECT_ARRAY_INIT;
	static char line[1000];
	int len, depth = 0;
	int has_non_tip = 0;

	shallow_nr = 0;
	if (debug_fd)
		write_str_in_full(debug_fd, "#S\n");
	for (;;) {
		struct object *o;
		unsigned char sha1_buf[20];
		len = packet_read_line(0, line, sizeof(line));
		reset_timeout();
		if (!len)
			break;
		if (debug_fd)
			write_in_full(debug_fd, line, len);

		if (!prefixcmp(line, "shallow ")) {
			unsigned char sha1[20];
			struct object *object;
			if (get_sha1(line + 8, sha1))
				die("invalid shallow line: %s", line);
			object = parse_object(sha1);
			if (!object)
				die("did not find object for %s", line);
			object->flags |= CLIENT_SHALLOW;
			add_object_array(object, NULL, &shallows);
			continue;
		}
		if (!prefixcmp(line, "deepen ")) {
			char *end;
			depth = strtol(line + 7, &end, 0);
			if (end == line + 7 || depth <= 0)
				die("Invalid deepen: %s", line);
			continue;
		}
		if (prefixcmp(line, "want ") ||
		    get_sha1_hex(line+5, sha1_buf))
			die("git upload-pack: protocol error, "
			    "expected to get sha, not '%s'", line);
		if (strstr(line+45, "multi_ack_detailed"))
			multi_ack = 2;
		else if (strstr(line+45, "multi_ack"))
			multi_ack = 1;
		if (strstr(line+45, "no-done"))
			no_done = 1;
		if (strstr(line+45, "thin-pack"))
			use_thin_pack = 1;
		if (strstr(line+45, "ofs-delta"))
			use_ofs_delta = 1;
		if (strstr(line+45, "side-band-64k"))
			use_sideband = LARGE_PACKET_MAX;
		else if (strstr(line+45, "side-band"))
			use_sideband = DEFAULT_PACKET_MAX;
		if (strstr(line+45, "no-progress"))
			no_progress = 1;
		if (strstr(line+45, "include-tag"))
			use_include_tag = 1;

		o = lookup_object(sha1_buf);
		if (!o)
			die("git upload-pack: not our ref %s",
			    sha1_to_hex(sha1_buf));
		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			if (!(o->flags & OUR_REF))
				has_non_tip = 1;
			add_object_array(o, NULL, &want_obj);
		}
	}
	if (debug_fd)
		write_str_in_full(debug_fd, "#E\n");

	/*
	 * We have sent all our refs already, and the other end
	 * should have chosen out of them. When we are operating
	 * in the stateless RPC mode, however, their choice may
	 * have been based on the set of older refs advertised
	 * by another process that handled the initial request.
	 */
	if (has_non_tip)
		check_non_tip();

	if (!use_sideband && daemon_mode)
		no_progress = 1;

	if (depth == 0 && shallows.nr == 0)
		return;
	if (depth > 0) {
		struct commit_list *result, *backup;
		int i;
		backup = result = get_shallow_commits(&want_obj, depth,
			SHALLOW, NOT_SHALLOW);
		while (result) {
			struct object *object = &result->item->object;
			if (!(object->flags & (CLIENT_SHALLOW|NOT_SHALLOW))) {
				packet_write(1, "shallow %s",
						sha1_to_hex(object->sha1));
				register_shallow(object->sha1);
				shallow_nr++;
			}
			result = result->next;
		}
		free_commit_list(backup);
		for (i = 0; i < shallows.nr; i++) {
			struct object *object = shallows.objects[i].item;
			if (object->flags & NOT_SHALLOW) {
				struct commit_list *parents;
				packet_write(1, "unshallow %s",
					sha1_to_hex(object->sha1));
				object->flags &= ~CLIENT_SHALLOW;
				/* make sure the real parents are parsed */
				unregister_shallow(object->sha1);
				object->parsed = 0;
				if (parse_commit((struct commit *)object))
					die("invalid commit");
				parents = ((struct commit *)object)->parents;
				while (parents) {
					add_object_array(&parents->item->object,
							NULL, &want_obj);
					parents = parents->next;
				}
				add_object_array(object, NULL, &extra_edge_obj);
			}
			/* make sure commit traversal conforms to client */
			register_shallow(object->sha1);
		}
		packet_flush(1);
	} else
		if (shallows.nr > 0) {
			int i;
			for (i = 0; i < shallows.nr; i++)
				register_shallow(shallows.objects[i].item->sha1);
		}

	shallow_nr += shallows.nr;
	free(shallows.objects);
}

static int send_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	static const char *capabilities = "multi_ack thin-pack side-band"
		" side-band-64k ofs-delta shallow no-progress"
		" include-tag multi_ack_detailed";
	struct object *o = parse_object(sha1);
	const char *refname_nons = strip_namespace(refname);

	if (!o)
		die("git upload-pack: cannot find object %s:", sha1_to_hex(sha1));

	if (capabilities)
		packet_write(1, "%s %s%c%s%s\n", sha1_to_hex(sha1), refname_nons,
			     0, capabilities,
			     stateless_rpc ? " no-done" : "");
	else
		packet_write(1, "%s %s\n", sha1_to_hex(sha1), refname_nons);
	capabilities = NULL;
	if (!(o->flags & OUR_REF)) {
		o->flags |= OUR_REF;
		nr_our_refs++;
	}
	if (o->type == OBJ_TAG) {
		o = deref_tag(o, refname, 0);
		if (o)
			packet_write(1, "%s %s^{}\n", sha1_to_hex(o->sha1), refname_nons);
	}
	return 0;
}

static int mark_our_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *o = parse_object(sha1);
	if (!o)
		die("git upload-pack: cannot find object %s:", sha1_to_hex(sha1));
	if (!(o->flags & OUR_REF)) {
		o->flags |= OUR_REF;
		nr_our_refs++;
	}
	return 0;
}

static void upload_pack(void)
{
	if (advertise_refs || !stateless_rpc) {
		reset_timeout();
		head_ref_namespaced(send_ref, NULL);
		for_each_namespaced_ref(send_ref, NULL);
		packet_flush(1);
	} else {
		head_ref_namespaced(mark_our_ref, NULL);
		for_each_namespaced_ref(mark_our_ref, NULL);
	}
	if (advertise_refs)
		return;

	receive_needs();
	if (want_obj.nr) {
		get_common_commits();
		create_pack_file();
	}
}

int main(int argc, char **argv)
{
	char *dir;
	int i;
	int strict = 0;

	packet_trace_identity("upload-pack");
	git_extract_argv0_path(argv[0]);
	read_replace_refs = 0;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "--advertise-refs")) {
			advertise_refs = 1;
			continue;
		}
		if (!strcmp(arg, "--stateless-rpc")) {
			stateless_rpc = 1;
			continue;
		}
		if (!strcmp(arg, "--strict")) {
			strict = 1;
			continue;
		}
		if (!prefixcmp(arg, "--timeout=")) {
			timeout = atoi(arg+10);
			daemon_mode = 1;
			continue;
		}
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
	}

	if (i != argc-1)
		usage(upload_pack_usage);

	setup_path();

	dir = argv[i];

	if (!enter_repo(dir, strict))
		die("'%s' does not appear to be a git repository", dir);
	if (is_repository_shallow())
		die("attempt to fetch/clone from a shallow repository");
	if (getenv("GIT_DEBUG_SEND_PACK"))
		debug_fd = atoi(getenv("GIT_DEBUG_SEND_PACK"));
	upload_pack();
	return 0;
}
