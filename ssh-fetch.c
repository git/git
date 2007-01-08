#ifndef COUNTERPART_ENV_NAME
#define COUNTERPART_ENV_NAME "GIT_SSH_UPLOAD"
#endif
#ifndef COUNTERPART_PROGRAM_NAME
#define COUNTERPART_PROGRAM_NAME "git-ssh-upload"
#endif
#ifndef MY_PROGRAM_NAME
#define MY_PROGRAM_NAME "git-ssh-fetch"
#endif

#include "cache.h"
#include "commit.h"
#include "rsh.h"
#include "fetch.h"
#include "refs.h"

static int fd_in;
static int fd_out;

static unsigned char remote_version;
static unsigned char local_version = 1;

static ssize_t force_write(int fd, void *buffer, size_t length)
{
	ssize_t ret = 0;
	while (ret < length) {
		ssize_t size = write(fd, (char *) buffer + ret, length - ret);
		if (size < 0) {
			return size;
		}
		if (size == 0) {
			return ret;
		}
		ret += size;
	}
	return ret;
}

static int prefetches;

static struct object_list *in_transit;
static struct object_list **end_of_transit = &in_transit;

void prefetch(unsigned char *sha1)
{
	char type = 'o';
	struct object_list *node;
	if (prefetches > 100) {
		fetch(in_transit->item->sha1);
	}
	node = xmalloc(sizeof(struct object_list));
	node->next = NULL;
	node->item = lookup_unknown_object(sha1);
	*end_of_transit = node;
	end_of_transit = &node->next;
	force_write(fd_out, &type, 1);
	force_write(fd_out, sha1, 20);
	prefetches++;
}

static char conn_buf[4096];
static size_t conn_buf_posn;

int fetch(unsigned char *sha1)
{
	int ret;
	signed char remote;
	struct object_list *temp;

	if (hashcmp(sha1, in_transit->item->sha1)) {
		/* we must have already fetched it to clean the queue */
		return has_sha1_file(sha1) ? 0 : -1;
	}
	prefetches--;
	temp = in_transit;
	in_transit = in_transit->next;
	if (!in_transit)
		end_of_transit = &in_transit;
	free(temp);

	if (conn_buf_posn) {
		remote = conn_buf[0];
		memmove(conn_buf, conn_buf + 1, --conn_buf_posn);
	} else {
		if (xread(fd_in, &remote, 1) < 1)
			return -1;
	}
	/* fprintf(stderr, "Got %d\n", remote); */
	if (remote < 0)
		return remote;
	ret = write_sha1_from_fd(sha1, fd_in, conn_buf, 4096, &conn_buf_posn);
	if (!ret)
		pull_say("got %s\n", sha1_to_hex(sha1));
	return ret;
}

static int get_version(void)
{
	char type = 'v';
	write(fd_out, &type, 1);
	write(fd_out, &local_version, 1);
	if (xread(fd_in, &remote_version, 1) < 1) {
		return error("Couldn't read version from remote end");
	}
	return 0;
}

int fetch_ref(char *ref, unsigned char *sha1)
{
	signed char remote;
	char type = 'r';
	write(fd_out, &type, 1);
	write(fd_out, ref, strlen(ref) + 1);

	if (read_in_full(fd_in, &remote, 1) != 1)
		return -1;
	if (remote < 0)
		return remote;
	if (read_in_full(fd_in, sha1, 20) != 20)
		return -1;
	return 0;
}

static const char ssh_fetch_usage[] =
  MY_PROGRAM_NAME
  " [-c] [-t] [-a] [-v] [--recover] [-w ref] commit-id url";
int main(int argc, char **argv)
{
	const char *write_ref = NULL;
	char *commit_id;
	char *url;
	int arg = 1;
	const char *prog;

	prog = getenv("GIT_SSH_PUSH");
	if (!prog) prog = "git-ssh-upload";

	setup_ident();
	setup_git_directory();
	git_config(git_default_config);

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			get_tree = 1;
		} else if (argv[arg][1] == 'c') {
			get_history = 1;
		} else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		} else if (argv[arg][1] == 'v') {
			get_verbosely = 1;
		} else if (argv[arg][1] == 'w') {
			write_ref = argv[arg + 1];
			arg++;
		} else if (!strcmp(argv[arg], "--recover")) {
			get_recover = 1;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage(ssh_fetch_usage);
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	if (setup_connection(&fd_in, &fd_out, prog, url, arg, argv + 1))
		return 1;

	if (get_version())
		return 1;

	if (pull(1, &commit_id, &write_ref, url))
		return 1;

	return 0;
}
