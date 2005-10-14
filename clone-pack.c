#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include <sys/wait.h>

static int quiet;
static int keep_pack;
static const char clone_pack_usage[] =
"git-clone-pack [-q] [--keep] [--exec=<git-upload-pack>] [<host>:]<directory> [<heads>]*";
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
	char *path = git_path("%s", ref->name);
	int fd;
	char *hex;

	if (!strncmp(ref->name, "refs/", 5) &&
	    check_ref_format(ref->name + 5)) {
		error("refusing to create funny ref '%s' locally", ref->name);
		return;
	}

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

	/* Upload-pack must report HEAD first */
	if (!strcmp(ref->name, "HEAD")) {
		head = ref;
		ref = ref->next;
	}
	head_ptr = NULL;
	master_ref = NULL;
	while (ref) {
		if (is_master(ref))
			master_ref = ref;
		if (head &&
		    !memcmp(ref->old_sha1, head->old_sha1, 20) &&
		    !strncmp(ref->name, "refs/heads/",11) &&
		    (!head_ptr || ref == master_ref))
			head_ptr = ref;

		write_one_ref(ref);
		ref = ref->next;
	}
	if (!head) {
		fprintf(stderr, "No HEAD in remote.\n");
		return;
	}

	head_path = strdup(git_path("HEAD"));
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
		free(head_path);
		return;
	}

	/* We reset to the master branch if it's available */
	if (master_ref)
		return;

	fprintf(stderr, "Setting HEAD to %s\n", head_ptr->name);

	/*
	 * Uhhuh. Other end didn't have master. We start HEAD off with
	 * the first branch with the same value.
	 */
	if (create_symref(head_path, head_ptr->name) < 0)
		die("unable to link HEAD to %s", head_ptr->name);
	free(head_path);
}

static int clone_by_unpack(int fd[2])
{
	int status;
	pid_t pid;

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
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("git-unpack-objects died of signal %d", sig);
	}
	die("Sherlock Holmes! git-unpack-objects died of unnatural causes %d!", status);
}

static int finish_pack(const char *pack_tmp_name)
{
	int pipe_fd[2];
	pid_t pid;
	char idx[PATH_MAX];
	char final[PATH_MAX];
	char hash[41];
	unsigned char sha1[20];
	char *cp;
	int err = 0;

	if (pipe(pipe_fd) < 0)
		die("git-clone-pack: unable to set up pipe");

	strcpy(idx, pack_tmp_name); /* ".git/objects/pack-XXXXXX" */
	cp = strrchr(idx, '/');
	memcpy(cp, "/pidx", 5);

	pid = fork();
	if (pid < 0)
		die("git-clone-pack: unable to fork off git-index-pack");
	if (!pid) {
		close(0);
		dup2(pipe_fd[1], 1);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		execlp("git-index-pack","git-index-pack",
		       "-o", idx, pack_tmp_name, NULL);
		error("cannot exec git-index-pack <%s> <%s>",
		      idx, pack_tmp_name);
		exit(1);
	}
	close(pipe_fd[1]);
	if (read(pipe_fd[0], hash, 40) != 40) {
		error("git-clone-pack: unable to read from git-index-pack");
		err = 1;
	}
	close(pipe_fd[0]);

	for (;;) {
		int status, code;
		int retval = waitpid(pid, &status, 0);

		if (retval < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid failed (%s)", strerror(retval));
			goto error_die;
		}
		if (WIFSIGNALED(status)) {
			int sig = WTERMSIG(status);
			error("git-index-pack died of signal %d", sig);
			goto error_die;
		}
		if (!WIFEXITED(status)) {
			error("git-index-pack died of unnatural causes %d",
			      status);
			goto error_die;
		}
		code = WEXITSTATUS(status);
		if (code) {
			error("git-index-pack died with error code %d", code);
			goto error_die;
		}
		if (err)
			goto error_die;
		break;
	}
	hash[40] = 0;
	if (get_sha1_hex(hash, sha1)) {
		error("git-index-pack reported nonsense '%s'", hash);
		goto error_die;
	}
	/* Now we have pack in pack_tmp_name[], and
	 * idx in idx[]; rename them to their final names.
	 */
	snprintf(final, sizeof(final),
		 "%s/pack/pack-%s.pack", get_object_directory(), hash);
	move_temp_to_file(pack_tmp_name, final);
	chmod(final, 0444);
	snprintf(final, sizeof(final),
		 "%s/pack/pack-%s.idx", get_object_directory(), hash);
	move_temp_to_file(idx, final);
	chmod(final, 0444);
	return 0;

 error_die:
	unlink(idx);
	unlink(pack_tmp_name);
	exit(1);
}

static int clone_without_unpack(int fd[2])
{
	char tmpfile[PATH_MAX];
	int ofd, ifd;

	ifd = fd[0];
	snprintf(tmpfile, sizeof(tmpfile),
		 "%s/pack-XXXXXX", get_object_directory());
	ofd = mkstemp(tmpfile);
	if (ofd < 0)
		return error("unable to create temporary file %s", tmpfile);

	while (1) {
		char buf[8192];
		ssize_t sz, wsz, pos;
		sz = read(ifd, buf, sizeof(buf));
		if (sz == 0)
			break;
		if (sz < 0) {
			error("error reading pack (%s)", strerror(errno));
			close(ofd);
			unlink(tmpfile);
			return -1;
		}
		pos = 0;
		while (pos < sz) {
			wsz = write(ofd, buf + pos, sz - pos);
			if (wsz < 0) {
				error("error writing pack (%s)",
				      strerror(errno));
				close(ofd);
				unlink(tmpfile);
				return -1;
			}
			pos += wsz;
		}
	}
	close(ofd);
	return finish_pack(tmpfile);
}

static int clone_pack(int fd[2], int nr_match, char **match)
{
	struct ref *refs;
	int status;

	get_remote_heads(fd[0], &refs, nr_match, match);
	if (!refs) {
		packet_flush(fd[1]);
		die("no matching remote head");
	}
	clone_handshake(fd, refs);

	if (keep_pack)
		status = clone_without_unpack(fd);
	else
		status = clone_by_unpack(fd);

	if (!status)
		write_refs(refs);
	return status;
}

static int clone_options(const char *var, const char *value)
{
	if (!strcmp("clone.keeppack", var)) {
		keep_pack = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("clone.quiet", var)) {
		quiet = git_config_bool(var, value);
		return 0;
	}
	/*
	 * Put other local option parsing for this program
	 * here ...
	 */

	/* Fall back on the default ones */
	return git_default_config(var, value);
}

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;

	git_config(clone_options);
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
			if (!strcmp("--keep", arg)) {
				keep_pack = 1;
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
