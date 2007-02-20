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

static const char upload_pack_usage[] = "git-upload-pack [--strict] [--timeout=nn] <dir>";

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
static int use_thin_pack, use_ofs_delta;
static struct object_array have_obj;
static struct object_array want_obj;
static unsigned int timeout;
/* 0 for no sideband,
 * otherwise maximum packet size (up to 65520 bytes).
 */
static int use_sideband;

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

FILE *pack_pipe = NULL;
static void show_commit(struct commit *commit)
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

static void show_object(struct object_array_entry *p)
{
	/* An object with name "foo\n0000000..." can be used to
	 * confuse downstream git-pack-objects very badly.
	 */
	const char *ep = strchr(p->name, '\n');
	if (ep) {
		fprintf(pack_pipe, "%s %.*s\n", sha1_to_hex(p->item->sha1),
		       (int) (ep - p->name),
		       p->name);
	}
	else
		fprintf(pack_pipe, "%s %s\n",
				sha1_to_hex(p->item->sha1), p->name);
}

static void show_edge(struct commit *commit)
{
	fprintf(pack_pipe, "-%s\n", sha1_to_hex(commit->object.sha1));
}

static void create_pack_file(void)
{
	/* Pipes between rev-list to pack-objects, pack-objects to us
	 * and pack-objects error stream for progress bar.
	 */
	int lp_pipe[2], pu_pipe[2], pe_pipe[2];
	pid_t pid_rev_list, pid_pack_objects;
	int create_full_pack = (nr_our_refs == want_obj.nr && !have_obj.nr);
	char data[8193], progress[128];
	char abort_msg[] = "aborting due to possible repository "
		"corruption on the remote side.";
	int buffered = -1;

	if (pipe(lp_pipe) < 0)
		die("git-upload-pack: unable to create pipe");
	pid_rev_list = fork();
	if (pid_rev_list < 0)
		die("git-upload-pack: unable to fork git-rev-list");

	if (!pid_rev_list) {
		int i;
		struct rev_info revs;

		pack_pipe = fdopen(lp_pipe[1], "w");

		if (create_full_pack)
			use_thin_pack = 0; /* no point doing it */
		init_revisions(&revs, NULL);
		revs.tag_objects = 1;
		revs.tree_objects = 1;
		revs.blob_objects = 1;
		if (use_thin_pack)
			revs.edge_hint = 1;

		if (create_full_pack) {
			const char *args[] = {"rev-list", "--all", NULL};
			setup_revisions(2, args, &revs, NULL);
		} else {
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
		}
		prepare_revision_walk(&revs);
		mark_edges_uninteresting(revs.commits, &revs, show_edge);
		traverse_commit_list(&revs, show_commit, show_object);
		exit(0);
	}

	if (pipe(pu_pipe) < 0)
		die("git-upload-pack: unable to create pipe");
	if (pipe(pe_pipe) < 0)
		die("git-upload-pack: unable to create pipe");
	pid_pack_objects = fork();
	if (pid_pack_objects < 0) {
		/* daemon sets things up to ignore TERM */
		kill(pid_rev_list, SIGKILL);
		die("git-upload-pack: unable to fork git-pack-objects");
	}
	if (!pid_pack_objects) {
		dup2(lp_pipe[0], 0);
		dup2(pu_pipe[1], 1);
		dup2(pe_pipe[1], 2);

		close(lp_pipe[0]);
		close(lp_pipe[1]);
		close(pu_pipe[0]);
		close(pu_pipe[1]);
		close(pe_pipe[0]);
		close(pe_pipe[1]);
		execl_git_cmd("pack-objects", "--stdout", "--progress",
			      use_ofs_delta ? "--delta-base-offset" : NULL,
			      NULL);
		kill(pid_rev_list, SIGKILL);
		die("git-upload-pack: unable to exec git-pack-objects");
	}

	close(lp_pipe[0]);
	close(lp_pipe[1]);

	/* We read from pe_pipe[0] to capture stderr output for
	 * progress bar, and pu_pipe[0] to capture the pack data.
	 */
	close(pe_pipe[1]);
	close(pu_pipe[1]);

	while (1) {
		const char *who;
		struct pollfd pfd[2];
		pid_t pid;
		int status;
		ssize_t sz;
		int pe, pu, pollsize;

		reset_timeout();

		pollsize = 0;
		pe = pu = -1;

		if (0 <= pu_pipe[0]) {
			pfd[pollsize].fd = pu_pipe[0];
			pfd[pollsize].events = POLLIN;
			pu = pollsize;
			pollsize++;
		}
		if (0 <= pe_pipe[0]) {
			pfd[pollsize].fd = pe_pipe[0];
			pfd[pollsize].events = POLLIN;
			pe = pollsize;
			pollsize++;
		}

		if (pollsize) {
			if (poll(pfd, pollsize, -1) < 0) {
				if (errno != EINTR) {
					error("poll failed, resuming: %s",
					      strerror(errno));
					sleep(1);
				}
				continue;
			}
			if (0 <= pu && (pfd[pu].revents & (POLLIN|POLLHUP))) {
				/* Data ready; we keep the last byte
				 * to ourselves in case we detect
				 * broken rev-list, so that we can
				 * leave the stream corrupted.  This
				 * is unfortunate -- unpack-objects
				 * would happily accept a valid pack
				 * data with trailing garbage, so
				 * appending garbage after we pass all
				 * the pack data is not good enough to
				 * signal breakage to downstream.
				 */
				char *cp = data;
				ssize_t outsz = 0;
				if (0 <= buffered) {
					*cp++ = buffered;
					outsz++;
				}
				sz = xread(pu_pipe[0], cp,
					  sizeof(data) - outsz);
				if (0 < sz)
						;
				else if (sz == 0) {
					close(pu_pipe[0]);
					pu_pipe[0] = -1;
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
			if (0 <= pe && (pfd[pe].revents & (POLLIN|POLLHUP))) {
				/* Status ready; we ship that in the side-band
				 * or dump to the standard error.
				 */
				sz = xread(pe_pipe[0], progress,
					  sizeof(progress));
				if (0 < sz)
					send_client_data(2, progress, sz);
				else if (sz == 0) {
					close(pe_pipe[0]);
					pe_pipe[0] = -1;
				}
				else
					goto fail;
			}
		}

		/* See if the children are still there */
		if (pid_rev_list || pid_pack_objects) {
			pid = waitpid(-1, &status, WNOHANG);
			if (!pid)
				continue;
			who = ((pid == pid_rev_list) ? "git-rev-list" :
			       (pid == pid_pack_objects) ? "git-pack-objects" :
			       NULL);
			if (!who) {
				if (pid < 0) {
					error("git-upload-pack: %s",
					      strerror(errno));
					goto fail;
				}
				error("git-upload-pack: we weren't "
				      "waiting for %d", pid);
				continue;
			}
			if (!WIFEXITED(status) || WEXITSTATUS(status) > 0) {
				error("git-upload-pack: %s died with error.",
				      who);
				goto fail;
			}
			if (pid == pid_rev_list)
				pid_rev_list = 0;
			if (pid == pid_pack_objects)
				pid_pack_objects = 0;
			if (pid_rev_list || pid_pack_objects)
				continue;
		}

		/* both died happily */
		if (pollsize)
			continue;

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
	}
 fail:
	if (pid_pack_objects)
		kill(pid_pack_objects, SIGKILL);
	if (pid_rev_list)
		kill(pid_rev_list, SIGKILL);
	send_client_data(3, abort_msg, sizeof(abort_msg));
	die("git-upload-pack: %s", abort_msg);
}

static int got_sha1(char *hex, unsigned char *sha1)
{
	struct object *o;
	int we_knew_they_have = 0;

	if (get_sha1_hex(hex, sha1))
		die("git-upload-pack: expected SHA1 object, got '%s'", hex);
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

	insert_by_date(want, &work);
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
				insert_by_date(parent, &work);
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
	char hex[41], last_hex[41];
	int len;

	track_object_refs = 0;
	save_commit_buffer = 0;

	for(;;) {
		len = packet_read_line(0, line, sizeof(line));
		reset_timeout();

		if (!len) {
			if (have_obj.nr == 0 || multi_ack)
				packet_write(1, "NAK\n");
			continue;
		}
		len = strip(line, len);
		if (!prefixcmp(line, "have ")) {
			switch (got_sha1(line+5, sha1)) {
			case -1: /* they have what we do not */
				if (multi_ack && ok_to_give_up())
					packet_write(1, "ACK %s continue\n",
						     sha1_to_hex(sha1));
				break;
			default:
				memcpy(hex, sha1_to_hex(sha1), 41);
				if (multi_ack) {
					const char *msg = "ACK %s continue\n";
					packet_write(1, msg, hex);
					memcpy(last_hex, hex, 41);
				}
				else if (have_obj.nr == 1)
					packet_write(1, "ACK %s\n", hex);
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
		die("git-upload-pack: expected SHA1 list, got '%s'", line);
	}
}

static void receive_needs(void)
{
	struct object_array shallows = {0, 0, NULL};
	static char line[1000];
	int len, depth = 0;

	for (;;) {
		struct object *o;
		unsigned char sha1_buf[20];
		len = packet_read_line(0, line, sizeof(line));
		reset_timeout();
		if (!len)
			break;

		if (!(-prefixcmp(line, "shallow "))) {
			unsigned char sha1[20];
			struct object *object;
			use_thin_pack = 0;
			if (get_sha1(line + 8, sha1))
				die("invalid shallow line: %s", line);
			object = parse_object(sha1);
			if (!object)
				die("did not find object for %s", line);
			object->flags |= CLIENT_SHALLOW;
			add_object_array(object, NULL, &shallows);
			continue;
		}
		if (!(-prefixcmp(line, "deepen "))) {
			char *end;
			use_thin_pack = 0;
			depth = strtol(line + 7, &end, 0);
			if (end == line + 7 || depth <= 0)
				die("Invalid deepen: %s", line);
			continue;
		}
		if ((-prefixcmp(line, "want ")) ||
		    get_sha1_hex(line+5, sha1_buf))
			die("git-upload-pack: protocol error, "
			    "expected to get sha, not '%s'", line);
		if (strstr(line+45, "multi_ack"))
			multi_ack = 1;
		if (strstr(line+45, "thin-pack"))
			use_thin_pack = 1;
		if (strstr(line+45, "ofs-delta"))
			use_ofs_delta = 1;
		if (strstr(line+45, "side-band-64k"))
			use_sideband = LARGE_PACKET_MAX;
		else if (strstr(line+45, "side-band"))
			use_sideband = DEFAULT_PACKET_MAX;

		/* We have sent all our refs already, and the other end
		 * should have chosen out of them; otherwise they are
		 * asking for nonsense.
		 *
		 * Hmph.  We may later want to allow "want" line that
		 * asks for something like "master~10" (symbolic)...
		 * would it make sense?  I don't know.
		 */
		o = lookup_object(sha1_buf);
		if (!o || !(o->flags & OUR_REF))
			die("git-upload-pack: not our ref %s", line+5);
		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			add_object_array(o, NULL, &want_obj);
		}
	}
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
				parse_commit((struct commit *)object);
				parents = ((struct commit *)object)->parents;
				while (parents) {
					add_object_array(&parents->item->object,
							NULL, &want_obj);
					parents = parents->next;
				}
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
	free(shallows.objects);
}

static int send_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	static const char *capabilities = "multi_ack thin-pack side-band"
		" side-band-64k ofs-delta shallow";
	struct object *o = parse_object(sha1);

	if (!o)
		die("git-upload-pack: cannot find object %s:", sha1_to_hex(sha1));

	if (capabilities)
		packet_write(1, "%s %s%c%s\n", sha1_to_hex(sha1), refname,
			0, capabilities);
	else
		packet_write(1, "%s %s\n", sha1_to_hex(sha1), refname);
	capabilities = NULL;
	if (!(o->flags & OUR_REF)) {
		o->flags |= OUR_REF;
		nr_our_refs++;
	}
	if (o->type == OBJ_TAG) {
		o = deref_tag(o, refname, 0);
		packet_write(1, "%s %s^{}\n", sha1_to_hex(o->sha1), refname);
	}
	return 0;
}

static void upload_pack(void)
{
	reset_timeout();
	head_ref(send_ref, NULL);
	for_each_ref(send_ref, NULL);
	packet_flush(1);
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

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "--strict")) {
			strict = 1;
			continue;
		}
		if (!prefixcmp(arg, "--timeout=")) {
			timeout = atoi(arg+10);
			continue;
		}
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
	}
	
	if (i != argc-1)
		usage(upload_pack_usage);
	dir = argv[i];

	if (!enter_repo(dir, strict))
		die("'%s': unable to chdir or not a git archive", dir);
	if (is_repository_shallow())
		die("attempt to fetch/clone from a shallow repository");
	upload_pack();
	return 0;
}
