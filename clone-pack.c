#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include <sys/wait.h>

static int quiet;
static const char clone_pack_usage[] = "git-clone-pack [-q] [--exec=<git-upload-pack>] [<host>:]<directory> [<heads>]*";
static const char *exec = "git-upload-pack";

static void clone_handshake(int fd[2], struct ref *ref)
{
	unsigned char sha1[20];

	while (ref) {
		packet_write(fd[1], "want %s\n", sha1_to_hex(ref->old_sha1));
		ref = ref->next;
	}
	packet_flush(fd[1]);

	/* We don't have nuttin' */
	packet_write(fd[1], "done\n");
	if (get_ack(fd[0], sha1))
		error("Huh! git-clone-pack got positive ack for %s", sha1_to_hex(sha1));
}

static int is_master(struct ref *ref)
{
	return !strcmp(ref->name, "refs/heads/master");
}

static void write_one_ref(struct ref *ref)
{
	char *path = git_path(ref->name);
	int fd;
	char *hex;

	if (safe_create_leading_directories(path))
		die("unable to create leading directory for %s", ref->name);
	fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0)
		die("unable to create ref %s", ref->name);
	hex = sha1_to_hex(ref->old_sha1);
	hex[40] = '\n';
	if (write(fd, hex, 41) != 41)
		die("unable to write ref %s", ref->name);
	close(fd);
}

static void write_refs(struct ref *ref)
{
	struct ref *head = NULL, *head_ptr, *master_ref;
	char *head_path;

	if (!strcmp(ref->name, "HEAD")) {
		head = ref;
		ref = ref->next;
	}
	head_ptr = NULL;
	master_ref = NULL;
	while (ref) {
		if (is_master(ref))
			master_ref = ref;
		if (head && !memcmp(ref->old_sha1, head->old_sha1, 20)) {
			if (!head_ptr || ref == master_ref)
				head_ptr = ref;
		}
		write_one_ref(ref);
		ref = ref->next;
	}
	if (!head)
		return;

	head_path = git_path("HEAD");
	if (!head_ptr) {
		/*
		 * If we had a master ref, and it wasn't HEAD, we need to undo the
		 * symlink, and write a standalone HEAD. Give a warning, because that's
		 * really really wrong.
		 */
		if (master_ref) {
			error("HEAD doesn't point to any refs! Making standalone HEAD");
			unlink(head_path);
		}
		write_one_ref(head);
		return;
	}

	/* We reset to the master branch if it's available */
	if (master_ref)
		return;

	/*
	 * Uhhuh. Other end didn't have master. We start HEAD off with
	 * the first branch with the same value.
	 */
	unlink(head_path);
	if (symlink(head_ptr->name, head_path) < 0)
		die("unable to link HEAD to %s", head_ptr->name);
}

static int clone_pack(int fd[2], int nr_match, char **match)
{
	struct ref *refs;
	int status;
	pid_t pid;

	get_remote_heads(fd[0], &refs, nr_match, match);
	if (!refs) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	clone_handshake(fd, refs);
	pid = fork();
	if (pid < 0)
		die("git-clone-pack: unable to fork off git-unpack-objects");
	if (!pid) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execlp("git-unpack-objects", "git-unpack-objects",
			quiet ? "-q" : NULL, NULL);
		die("git-unpack-objects exec failed");
	}
	close(fd[0]);
	close(fd[1]);
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			die("waiting for git-unpack-objects: %s", strerror(errno));
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code)
			die("git-unpack-objects died with error code %d", code);
		write_refs(refs);
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("git-unpack-objects died of signal %d", sig);
	}
	die("Sherlock Holmes! git-unpack-objects died of unnatural causes %d!", status);
}

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp("-q", arg)) {
				quiet = 1;
				continue;
			}
			if (!strncmp("--exec=", arg, 7)) {
				exec = arg + 7;
				continue;
			}
			usage(clone_pack_usage);
		}
		dest = arg;
		heads = argv + i + 1;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(clone_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = clone_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
