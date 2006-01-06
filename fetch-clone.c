#include "cache.h"
#include <sys/wait.h>

static int finish_pack(const char *pack_tmp_name, const char *me)
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
		die("%s: unable to set up pipe", me);

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
		error("%s: unable to read from git-index-pack", me);
		err = 1;
	}
	close(pipe_fd[0]);

	for (;;) {
		int status, code;
		int retval = waitpid(pid, &status, 0);

		if (retval < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid failed (%s)", strerror(errno));
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

int receive_unpack_pack(int fd[2], const char *me, int quiet)
{
	int status;
	pid_t pid;

	pid = fork();
	if (pid < 0)
		die("%s: unable to fork off git-unpack-objects", me);
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
			die("waiting for git-unpack-objects: %s",
			    strerror(errno));
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code)
			die("git-unpack-objects died with error code %d",
			    code);
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("git-unpack-objects died of signal %d", sig);
	}
	die("git-unpack-objects died of unnatural causes %d", status);
}

int receive_keep_pack(int fd[2], const char *me)
{
	char tmpfile[PATH_MAX];
	int ofd, ifd;

	ifd = fd[0];
	snprintf(tmpfile, sizeof(tmpfile),
		 "%s/pack/tmp-XXXXXX", get_object_directory());
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
	return finish_pack(tmpfile, me);
}
