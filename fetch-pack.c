#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include <sys/wait.h>

static int quiet;
static int verbose;
static const char fetch_pack_usage[] =
"git-fetch-pack [-q] [-v] [--exec=upload-pack] [host:]directory <refs>...";
static const char *exec = "git-upload-pack";

static int find_common(int fd[2], unsigned char *result_sha1,
		       struct ref *refs)
{
	int fetching;
	static char line[1000];
	int count = 0, flushes = 0, retval;
	FILE *revs;

	revs = popen("git-rev-list $(git-rev-parse --all)", "r");
	if (!revs)
		die("unable to run 'git-rev-list'");

	fetching = 0;
	for ( ; refs ; refs = refs->next) {
		unsigned char *remote = refs->old_sha1;
		unsigned char *local = refs->new_sha1;

		if (!memcmp(remote, local, 20))
			continue;
		packet_write(fd[1], "want %s\n", sha1_to_hex(remote));
		fetching++;
	}
	packet_flush(fd[1]);
	if (!fetching)
		return 1;
	flushes = 1;
	retval = -1;
	while (fgets(line, sizeof(line), revs) != NULL) {
		unsigned char sha1[20];
		if (get_sha1_hex(line, sha1))
			die("git-fetch-pack: expected object name, got crud");
		packet_write(fd[1], "have %s\n", sha1_to_hex(sha1));
		if (verbose)
			fprintf(stderr, "have %s\n", sha1_to_hex(sha1));
		if (!(31 & ++count)) {
			packet_flush(fd[1]);
			flushes++;

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (count == 32)
				continue;
			if (get_ack(fd[0], result_sha1)) {
				flushes = 0;
				retval = 0;
				if (verbose)
					fprintf(stderr, "got ack\n");
				break;
			}
			flushes--;
		}
	}
	pclose(revs);
	packet_write(fd[1], "done\n");
	if (verbose)
		fprintf(stderr, "done\n");
	while (flushes) {
		flushes--;
		if (get_ack(fd[0], result_sha1)) {
			if (verbose)
				fprintf(stderr, "got ack\n");
			return 0;
		}
	}
	return retval;
}

static int everything_local(struct ref *refs)
{
	int retval;

	for (retval = 1; refs ; refs = refs->next) {
		const unsigned char *remote = refs->old_sha1;
		unsigned char local[20];

		if (read_ref(git_path("%s", refs->name), local) < 0 ||
		    memcmp(remote, local, 20)) {
			retval = 0;
			if (!verbose)
				continue;
			fprintf(stderr,
				"want %s (%s)\n", sha1_to_hex(remote),
				refs->name);
			continue;
		}

		memcpy(refs->new_sha1, local, 20);
		if (!verbose)
			continue;
		fprintf(stderr,
			"already have %s (%s)\n", sha1_to_hex(remote),
			refs->name);
	}
	return retval;
}

static int fetch_pack(int fd[2], int nr_match, char **match)
{
	struct ref *ref;
	unsigned char sha1[20];
	int status;
	pid_t pid;

	get_remote_heads(fd[0], &ref, nr_match, match, 1);
	if (!ref) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	if (everything_local(ref)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(fd, sha1, ref) < 0)
		fprintf(stderr, "warning: no common commits\n");
	pid = fork();
	if (pid < 0)
		die("git-fetch-pack: unable to fork off git-unpack-objects");
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
all_done:
		while (ref) {
			printf("%s %s\n",
			       sha1_to_hex(ref->old_sha1), ref->name);
			ref = ref->next;
		}
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
			if (!strncmp("--exec=", arg, 7)) {
				exec = arg + 7;
				continue;
			}
			if (!strcmp("-q", arg)) {
				quiet = 1;
				continue;
			}
			if (!strcmp("-v", arg)) {
				verbose = 1;
				continue;
			}
			usage(fetch_pack_usage);
		}
		dest = arg;
		heads = argv + i + 1;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(fetch_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = fetch_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
