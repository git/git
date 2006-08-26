#include <signal.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "tag.h"
#include "object.h"
#include "commit.h"
#include "exec_cmd.h"

static const char upload_pack_usage[] = "git-upload-pack [--strict] [--timeout=nn] <dir>";

#define THEY_HAVE (1U << 0)
#define OUR_REF (1U << 1)
#define WANTED (1U << 2)
static int multi_ack, nr_our_refs;
static int use_thin_pack;
static struct object_array have_obj;
static struct object_array want_obj;
static unsigned int timeout;
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

#define PACKET_MAX 1000
static ssize_t send_client_data(int fd, const char *data, ssize_t sz)
{
	ssize_t ssz;
	const char *p;

	if (!data) {
		if (!use_sideband)
			return 0;
		packet_flush(1);
	}

	if (!use_sideband) {
		if (fd == 3)
			/* emergency quit */
			fd = 2;
		if (fd == 2) {
			xwrite(fd, data, sz);
			return sz;
		}
		return safe_write(fd, data, sz);
	}
	p = data;
	ssz = sz;
	while (sz) {
		unsigned n;
		char hdr[5];

		n = sz;
		if (PACKET_MAX - 5 < n)
			n = PACKET_MAX - 5;
		sprintf(hdr, "%04x", n + 5);
		hdr[4] = fd;
		safe_write(1, hdr, 5);
		safe_write(1, p, n);
		p += n;
		sz -= n;
	}
	return ssz;
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
		int args;
		const char **argv;
		const char **p;
		char *buf;

		if (create_full_pack) {
			args = 10;
			use_thin_pack = 0; /* no point doing it */
		}
		else
			args = have_obj.nr + want_obj.nr + 5;
		p = xmalloc(args * sizeof(char *));
		argv = (const char **) p;
		buf = xmalloc(args * 45);

		dup2(lp_pipe[1], 1);
		close(0);
		close(lp_pipe[0]);
		close(lp_pipe[1]);
		*p++ = "rev-list";
		*p++ = use_thin_pack ? "--objects-edge" : "--objects";
		if (create_full_pack)
			*p++ = "--all";
		else {
			for (i = 0; i < want_obj.nr; i++) {
				struct object *o = want_obj.objects[i].item;
				*p++ = buf;
				memcpy(buf, sha1_to_hex(o->sha1), 41);
				buf += 41;
			}
		}
		if (!create_full_pack)
			for (i = 0; i < have_obj.nr; i++) {
				struct object *o = have_obj.objects[i].item;
				*p++ = buf;
				*buf++ = '^';
				memcpy(buf, sha1_to_hex(o->sha1), 41);
				buf += 41;
			}
		*p++ = NULL;
		execv_git_cmd(argv);
		die("git-upload-pack: unable to exec git-rev-list");
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
		execl_git_cmd("pack-objects", "--stdout", "--progress", NULL);
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
				sz = read(pu_pipe[0], cp,
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
				sz = read(pe_pipe[0], progress,
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
		send_client_data(1, NULL, 0);
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

	if (get_sha1_hex(hex, sha1))
		die("git-upload-pack: expected SHA1 object, got '%s'", hex);
	if (!has_sha1_file(sha1))
		return 0;

	o = lookup_object(sha1);
	if (!(o && o->parsed))
		o = parse_object(sha1);
	if (!o)
		die("oops (%s)", sha1_to_hex(sha1));
	if (o->type == OBJ_COMMIT) {
		struct commit_list *parents;
		if (o->flags & THEY_HAVE)
			return 0;
		o->flags |= THEY_HAVE;
		for (parents = ((struct commit*)o)->parents;
		     parents;
		     parents = parents->next)
			parents->item->object.flags |= THEY_HAVE;
	}
	add_object_array(o, NULL, &have_obj);
	return 1;
}

static int get_common_commits(void)
{
	static char line[1000];
	unsigned char sha1[20], last_sha1[20];
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
		if (!strncmp(line, "have ", 5)) {
			if (got_sha1(line+5, sha1) &&
			    (multi_ack || have_obj.nr == 1)) {
				packet_write(1, "ACK %s%s\n",
					     sha1_to_hex(sha1),
					     multi_ack ?  " continue" : "");
				if (multi_ack)
					hashcpy(last_sha1, sha1);
			}
			continue;
		}
		if (!strcmp(line, "done")) {
			if (have_obj.nr > 0) {
				if (multi_ack)
					packet_write(1, "ACK %s\n",
							sha1_to_hex(last_sha1));
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
	static char line[1000];
	int len;

	for (;;) {
		struct object *o;
		unsigned char sha1_buf[20];
		len = packet_read_line(0, line, sizeof(line));
		reset_timeout();
		if (!len)
			return;

		if (strncmp("want ", line, 5) ||
		    get_sha1_hex(line+5, sha1_buf))
			die("git-upload-pack: protocol error, "
			    "expected to get sha, not '%s'", line);
		if (strstr(line+45, "multi_ack"))
			multi_ack = 1;
		if (strstr(line+45, "thin-pack"))
			use_thin_pack = 1;
		if (strstr(line+45, "side-band"))
			use_sideband = 1;

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
}

static int send_ref(const char *refname, const unsigned char *sha1)
{
	static const char *capabilities = "multi_ack thin-pack side-band";
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
	head_ref(send_ref);
	for_each_ref(send_ref);
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
		if (!strncmp(arg, "--timeout=", 10)) {
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

	upload_pack();
	return 0;
}
