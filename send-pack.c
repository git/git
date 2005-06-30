#include "cache.h"
#include "pkt-line.h"
#include <sys/wait.h>

static const char send_pack_usage[] = "git-send-pack [--exec=other] destination [heads]*";
static const char *exec = "git-receive-pack";

static int path_match(const char *path, int nr, char **match)
{
	int i;
	int pathlen = strlen(path);

	for (i = 0; i < nr; i++) {
		char *s = match[i];
		int len = strlen(s);

		if (!len || len > pathlen)
			continue;
		if (memcmp(path + pathlen - len, s, len))
			continue;
		if (pathlen > len && path[pathlen - len - 1] != '/')
			continue;
		*s = 0;
		return 1;
	}
	return 0;
}

struct ref {
	struct ref *next;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char name[0];
};

static void exec_pack_objects(void)
{
	static char *args[] = {
		"git-pack-objects",
		"--stdout",
		NULL
	};
	execvp("git-pack-objects", args);
	die("git-pack-objects exec failed (%s)", strerror(errno));
}

static void exec_rev_list(struct ref *refs)
{
	static char *args[1000];
	int i = 0;

	args[i++] = "git-rev-list";	/* 0 */
	args[i++] = "--objects";	/* 1 */
	while (refs) {
		char *buf = malloc(100);
		if (i > 900)
			die("git-rev-list environment overflow");
		args[i++] = buf;
		snprintf(buf, 50, "^%s", sha1_to_hex(refs->old_sha1));
		buf += 50;
		args[i++] = buf;
		snprintf(buf, 50, "%s", sha1_to_hex(refs->new_sha1));
		refs = refs->next;
	}
	args[i] = NULL;
	execvp("git-rev-list", args);
	die("git-rev-list exec failed (%s)", strerror(errno));
}

static void rev_list(int fd, struct ref *refs)
{
	int pipe_fd[2];
	pid_t pack_objects_pid;

	if (pipe(pipe_fd) < 0)
		die("rev-list setup: pipe failed");
	pack_objects_pid = fork();
	if (!pack_objects_pid) {
		dup2(pipe_fd[0], 0);
		dup2(fd, 1);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		close(fd);
		exec_pack_objects();
		die("pack-objects setup failed");
	}
	if (pack_objects_pid < 0)
		die("pack-objects fork failed");
	dup2(pipe_fd[1], 1);
	close(pipe_fd[0]);
	close(pipe_fd[1]);
	close(fd);
	exec_rev_list(refs);
}

static int pack_objects(int fd, struct ref *refs)
{
	pid_t rev_list_pid;

	rev_list_pid = fork();
	if (!rev_list_pid) {
		rev_list(fd, refs);
		die("rev-list setup failed");
	}
	if (rev_list_pid < 0)
		die("rev-list fork failed");
	/*
	 * We don't wait for the rev-list pipeline in the parent:
	 * we end up waiting for the other end instead
	 */
}

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

static int send_pack(int in, int out, int nr_match, char **match)
{
	struct ref *ref_list = NULL, **last_ref = &ref_list;
	struct ref *ref;

	for (;;) {
		unsigned char old_sha1[20];
		unsigned char new_sha1[20];
		static char buffer[1000];
		char *name;
		int len;

		len = packet_read_line(in, buffer, sizeof(buffer));
		if (!len)
			break;
		if (buffer[len-1] == '\n')
			buffer[--len] = 0;

		if (len < 42 || get_sha1_hex(buffer, old_sha1) || buffer[40] != ' ')
			die("protocol error: expected sha/ref, got '%s'", buffer);
		name = buffer + 41;
		if (nr_match && !path_match(name, nr_match, match))
			continue;
		if (read_ref(name, new_sha1) < 0)
			return error("no such local reference '%s'", name);
		if (!has_sha1_file(old_sha1))
			return error("remote '%s' points to object I don't have", name);
		if (!memcmp(old_sha1, new_sha1, 20)) {
			fprintf(stderr, "'%s' unchanged\n", name);
			continue;
		}
		ref = xmalloc(sizeof(*ref) + len - 40);
		memcpy(ref->old_sha1, old_sha1, 20);
		memcpy(ref->new_sha1, new_sha1, 20);
		memcpy(ref->name, buffer + 41, len - 40);
		ref->next = NULL;
		*last_ref = ref;
		last_ref = &ref->next;
	}

	for (ref = ref_list; ref; ref = ref->next) {
		char old_hex[60], *new_hex;
		strcpy(old_hex, sha1_to_hex(ref->old_sha1));
		new_hex = sha1_to_hex(ref->new_sha1);
		packet_write(out, "%s %s %s", old_hex, new_hex, ref->name);
		fprintf(stderr, "'%s': updating from %s to %s\n", ref->name, old_hex, new_hex);
	}
	
	packet_flush(out);
	if (ref_list)
		pack_objects(out, ref_list);
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
	pid_t pid;

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
	pid = fork();
	if (!pid) {
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
	return pid;
}

int main(int argc, char **argv)
{
	int i, nr_heads = 0;
	char *dest = NULL;
	char **heads = NULL;
	int fd[2], ret;
	pid_t pid;

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
	pid = setup_connection(fd, dest, heads);
	if (pid < 0)
		return 1;
	ret = send_pack(fd[0], fd[1], nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	waitpid(pid, NULL, 0);
	return ret;
}
