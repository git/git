#include "cache.h"
#include "pkt-line.h"

static const char send_pack_usage[] = "git-send-pack [--exec=other] destination [heads]*";

static const char *exec = "git-receive-pack";

struct ref {
	struct ref *next;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char name[0];
};

static struct ref *ref_list = NULL;

static int read_ref(const char *ref, unsigned char *sha1)
{
	int fd, ret;
	static char pathname[PATH_MAX];
	char buffer[60];
	const char *git_dir = gitenv(GIT_DIR_ENVIRONMENT) ? : DEFAULT_GIT_DIR_ENVIRONMENT;

	snprintf(pathname, sizeof(pathname), "%s/%s", git_dir, ref);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		return -1;
	ret = -1;
	if (read(fd, buffer, sizeof(buffer)) >= 40)
		ret = get_sha1_hex(buffer, sha1);
	close(fd);
	return ret;
}

static int send_pack(int in, int out)
{
	for (;;) {
		unsigned char old_sha1[20];
		unsigned char new_sha1[20];
		static char buffer[1000];
		char *name;
		struct ref *n;
		int len;

		len = packet_read_line(in, buffer, sizeof(buffer));
		if (!len)
			break;
		if (buffer[len-1] == '\n')
			buffer[--len] = 0;

		if (len < 42 || get_sha1_hex(buffer, old_sha1) || buffer[40] != ' ')
			die("protocol error: expected sha/ref, got '%s'", buffer);
		name = buffer + 41;
		if (read_ref(name, new_sha1) < 0) {
			fprintf(stderr, "no such local reference '%s'\n", name);
			continue;
		}
		if (!has_sha1_file(old_sha1)) {
			fprintf(stderr, "remote '%s' points to object I don't have\n", name);
			continue;
		}
		if (!memcmp(old_sha1, new_sha1, 20)) {
			fprintf(stderr, "'%s' unchanged\n", name);
		} else {
			char new_hex[60];
			strcpy(new_hex, sha1_to_hex(new_sha1));
			fprintf(stderr, "%s: updating from %s to %s\n", name, sha1_to_hex(old_sha1), new_hex);
		}
		n = xmalloc(sizeof(*n) + len - 40);
		memcpy(n->old_sha1, old_sha1, 20);
		memcpy(n->new_sha1, new_sha1, 20);
		memcpy(n->name, buffer + 41, len - 40);
		n->next = ref_list;
		ref_list = n;
	}
	packet_flush(out);
	close(out);
	return 0;
}

/*
 * First, make it shell-safe.  We do this by just disallowing any
 * special characters. Somebody who cares can do escaping and let
 * through the rest. But since we're doing to feed this to ssh as
 * a command line, we're going to be pretty damn anal for now.
 */
static char *shell_safe(char *url)
{
	char *n = url;
	unsigned char c;
	static const char flags[256] = {
		['0'...'9'] = 1,
		['a'...'z'] = 1,
		['A'...'Z'] = 1,
		['.'] = 1, ['/'] = 1,
		['-'] = 1, ['+'] = 1,
		[':'] = 1
	};

	while ((c = *n++) != 0) {
		if (flags[c] != 1)
			die("I don't like '%c'. Sue me.", c);
	}
	return url;
}

/*
 * Yeah, yeah, fixme. Need to pass in the heads etc.
 */
static int setup_connection(int fd[2], char *url, char **heads)
{
	char command[1024];
	const char *host, *path;
	char *colon;
	int pipefd[2][2];

	url = shell_safe(url);
	host = NULL;
	path = url;
	colon = strchr(url, ':');
	if (colon) {
		*colon = 0;
		host = url;
		path = colon+1;
	}
	snprintf(command, sizeof(command), "%s %s", exec, path);
	if (pipe(pipefd[0]) < 0 || pipe(pipefd[1]) < 0)
		die("unable to create pipe pair for communication");
	if (!fork()) {
		dup2(pipefd[1][0], 0);
		dup2(pipefd[0][1], 1);
		close(pipefd[0][0]);
		close(pipefd[0][1]);
		close(pipefd[1][0]);
		close(pipefd[1][1]);
		if (host)
			execlp("ssh", "ssh", host, command, NULL);
		else
			execlp("sh", "sh", "-c", command, NULL);
		die("exec failed");
	}		
	fd[0] = pipefd[0][0];
	fd[1] = pipefd[1][1];
	close(pipefd[0][1]);
	close(pipefd[1][0]);
	return 0;
}

int main(int argc, char **argv)
{
	int i, nr_heads = 0;
	char *dest = NULL;
	char **heads = NULL;
	int fd[2];

	argv++;
	for (i = 1; i < argc; i++) {
		char *arg = *argv++;

		if (*arg == '-') {
			if (!strncmp(arg, "--exec=", 7)) {
				exec = arg + 7;
				continue;
			}
			usage(send_pack_usage);
		}
		dest = arg;
		heads = argv;
		nr_heads = argc - i -1;
		break;
	}
	if (!dest)
		usage(send_pack_usage);
	if (setup_connection(fd, dest, heads))
		return 1;
	return send_pack(fd[0], fd[1]);
}
