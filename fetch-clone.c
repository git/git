#include "cache.h"
#include "exec_cmd.h"
#include "pkt-line.h"
#include <sys/wait.h>
#include <sys/time.h>

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
		die("%s: unable to fork off git-index-pack", me);
	if (!pid) {
		close(0);
		dup2(pipe_fd[1], 1);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		execl_git_cmd("index-pack", "-o", idx, pack_tmp_name, NULL);
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

static pid_t setup_sideband(int sideband, const char *me, int fd[2], int xd[2])
{
	pid_t side_pid;

	if (!sideband) {
		fd[0] = xd[0];
		fd[1] = xd[1];
		return 0;
	}
	/* xd[] is talking with upload-pack; subprocess reads from
	 * xd[0], spits out band#2 to stderr, and feeds us band#1
	 * through our fd[0].
	 */
	if (pipe(fd) < 0)
		die("%s: unable to set up pipe", me);
	side_pid = fork();
	if (side_pid < 0)
		die("%s: unable to fork off sideband demultiplexer", me);
	if (!side_pid) {
		/* subprocess */
		close(fd[0]);
		if (xd[0] != xd[1])
			close(xd[1]);
		while (1) {
			char buf[1024];
			int len = packet_read_line(xd[0], buf, sizeof(buf));
			if (len == 0)
				break;
			if (len < 1)
				die("%s: protocol error: no band designator",
				    me);
			len--;
			switch (buf[0] & 0xFF) {
			case 3:
				safe_write(2, buf+1, len);
				fprintf(stderr, "\n");
				exit(1);
			case 2:
				/* color sideband */
				safe_write(2, "\033[44;37;1m", 10);
				safe_write(2, buf+1, len);
				safe_write(2, "\033[m", 3);
				continue;
			case 1:
				safe_write(fd[1], buf+1, len);
				continue;
			default:
				die("%s: protocol error: bad band #%d",
				    me, (buf[0] & 0xFF));
			}
		}
		exit(0);
	}
	close(xd[0]);
	close(fd[1]);
	fd[1] = xd[1];
	return side_pid;
}

int receive_unpack_pack(int xd[2], const char *me, int quiet, int sideband)
{
	int status;
	pid_t pid, side_pid;
	int fd[2];

	side_pid = setup_sideband(sideband, me, fd, xd);
	pid = fork();
	if (pid < 0)
		die("%s: unable to fork off git-unpack-objects", me);
	if (!pid) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execl_git_cmd("unpack-objects", quiet ? "-q" : NULL, NULL);
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

/*
 * We average out the download speed over this many "events", where
 * an event is a minimum of about half a second. That way, we get
 * a reasonably stable number.
 */
#define NR_AVERAGE (4)

/*
 * A "binary msec" is a power-of-two-msec, aka 1/1024th of a second.
 * Keeping the time in that format means that "bytes / msecs" means
 * the same as kB/s (modulo rounding).
 *
 * 1000512 is a magic number (usecs in a second, rounded up by half
 * of 1024, to make "rounding" come out right ;)
 */
#define usec_to_binarymsec(x) ((int)(x) / (1000512 >> 10))

int receive_keep_pack(int xd[2], const char *me, int quiet, int sideband)
{
	char tmpfile[PATH_MAX];
	int ofd, ifd, fd[2];
	unsigned long total;
	static struct timeval prev_tv;
	struct average {
		unsigned long bytes;
		unsigned long time;
	} download[NR_AVERAGE] = { {0, 0}, };
	unsigned long avg_bytes, avg_time;
	int idx = 0;

	setup_sideband(sideband, me, fd, xd);

	ifd = fd[0];
	snprintf(tmpfile, sizeof(tmpfile),
		 "%s/pack/tmp-XXXXXX", get_object_directory());
	ofd = mkstemp(tmpfile);
	if (ofd < 0)
		return error("unable to create temporary file %s", tmpfile);

	gettimeofday(&prev_tv, NULL);
	total = 0;
	avg_bytes = 0;
	avg_time = 0;
	while (1) {
		char buf[8192];
		ssize_t sz, wsz, pos;
		sz = read(ifd, buf, sizeof(buf));
		if (sz == 0)
			break;
		if (sz < 0) {
			if (errno != EINTR && errno != EAGAIN) {
				error("error reading pack (%s)", strerror(errno));
				close(ofd);
				unlink(tmpfile);
				return -1;
			}
			sz = 0;
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
		total += sz;
		if (!quiet) {
			static unsigned long last;
			struct timeval tv;
			unsigned long diff = total - last;
			/* not really "msecs", but a power-of-two millisec (1/1024th of a sec) */
			unsigned long msecs;

			gettimeofday(&tv, NULL);
			msecs = tv.tv_sec - prev_tv.tv_sec;
			msecs <<= 10;
			msecs += usec_to_binarymsec(tv.tv_usec - prev_tv.tv_usec);

			if (msecs > 500) {
				prev_tv = tv;
				last = total;

				/* Update averages ..*/
				avg_bytes += diff;
				avg_time += msecs;
				avg_bytes -= download[idx].bytes;
				avg_time -= download[idx].time;
				download[idx].bytes = diff;
				download[idx].time = msecs;
				idx++;
				if (idx >= NR_AVERAGE)
					idx = 0;

				fprintf(stderr, "%4lu.%03luMB  (%lu kB/s)      \r",
					total >> 20,
					1000*((total >> 10) & 1023)>>10,
					avg_bytes / avg_time );
			}
		}
	}
	close(ofd);
	return finish_pack(tmpfile, me);
}
