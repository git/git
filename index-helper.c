#include "cache.h"
#include "parse-options.h"
#include "sigchain.h"
#include "strbuf.h"
#include "exec_cmd.h"
#include "split-index.h"
#include "lockfile.h"
#include "cache.h"
#include "unix-socket.h"
#include "pkt-line.h"
#include "watchman-support.h"

struct shm {
	unsigned char sha1[20];
	void *shm;
	size_t size;
	pid_t pid;
};

static struct shm shm_index;
static struct shm shm_base_index;
static struct shm shm_watchman;
static int daemonized, to_verify = 1;

static void release_index_shm(struct shm *is)
{
	if (!is->shm)
		return;
	munmap(is->shm, is->size);
	unlink(git_path("shm-index-%s", sha1_to_hex(is->sha1)));
	is->shm = NULL;
}

static void release_watchman_shm(struct shm *is)
{
	if (!is->shm)
		return;
	munmap(is->shm, is->size);
	unlink(git_path("shm-watchman-%s-%" PRIuMAX,
			sha1_to_hex(is->sha1), (uintmax_t)is->pid));
	is->shm = NULL;
}

static void cleanup_shm(void)
{
	release_index_shm(&shm_index);
	release_index_shm(&shm_base_index);
	release_watchman_shm(&shm_watchman);
}

static void cleanup(void)
{
	if (daemonized)
		return;
	unlink(git_path("index-helper.sock"));
	cleanup_shm();
}

static void cleanup_on_signal(int sig)
{
	/*
	 * We ignore sigpipes -- that's just a client suddenly dying.
	 */
	if (sig == SIGPIPE)
		return;
	cleanup();
	sigchain_pop(sig);
	raise(sig);
}

static int shared_mmap_create(size_t size, void **new_mmap, const char *path)
{
	int fd = -1;
	int ret = -1;

	fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);

	if (fd < 0)
		goto done;

	if (ftruncate(fd, size))
		goto done;

	*new_mmap = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
			 fd, 0);

	if (*new_mmap == MAP_FAILED) {
		*new_mmap = NULL;
		goto done;
	}
	madvise(new_mmap, size, MADV_WILLNEED);

	ret = 0;
done:
	if (fd > 0)
		close(fd);
	return ret;
}

static void share_index(struct index_state *istate, struct shm *is)
{
	void *new_mmap;
	if (istate->mmap_size <= 20 ||
	    hashcmp(istate->sha1,
		    (unsigned char *)istate->mmap + istate->mmap_size - 20) ||
	    !hashcmp(istate->sha1, is->sha1))
		/* Nothing to do */
		return;
	if (shared_mmap_create(istate->mmap_size, &new_mmap,
			       git_path("shm-index-%s",
					sha1_to_hex(istate->sha1))) < 0) {
		warning("Failed to create shm-index file");
		exit(1);
	}


	release_index_shm(is);
	is->size = istate->mmap_size;
	is->shm = new_mmap;
	hashcpy(is->sha1, istate->sha1);

	memcpy(new_mmap, istate->mmap, istate->mmap_size - 20);

	/*
	 * The trailing hash must be written last after everything is
	 * written. It's the indication that the shared memory is now
	 * ready.
	 * The memory barrier here matches read-cache.c:try_shm.
	 */
	__sync_synchronize();

	hashcpy((unsigned char *)new_mmap + istate->mmap_size - 20, is->sha1);
}

static int verify_shm(void)
{
	int i;
	struct index_state istate;
	memset(&istate, 0, sizeof(istate));
	istate.always_verify_trailing_sha1 = 1;
	istate.to_shm = 1;
	i = read_index(&istate);
	if (i != the_index.cache_nr)
		goto done;
	for (; i < the_index.cache_nr; i++) {
		struct cache_entry *base, *ce;
		/* namelen is checked separately */
		const unsigned int ondisk_flags =
			CE_STAGEMASK | CE_VALID | CE_EXTENDED_FLAGS;
		unsigned int ce_flags, base_flags, ret;
		base = the_index.cache[i];
		ce = istate.cache[i];
		if (ce->ce_namelen != base->ce_namelen ||
		    strcmp(ce->name, base->name)) {
			warning("mismatch at entry %d", i);
			break;
		}
		ce_flags = ce->ce_flags;
		base_flags = base->ce_flags;
		/* only on-disk flags matter */
		ce->ce_flags   &= ondisk_flags;
		base->ce_flags &= ondisk_flags;
		ret = memcmp(&ce->ce_stat_data, &base->ce_stat_data,
			     offsetof(struct cache_entry, name) -
			     offsetof(struct cache_entry, ce_stat_data));
		ce->ce_flags = ce_flags;
		base->ce_flags = base_flags;
		if (ret) {
			warning("mismatch at entry %d", i);
			break;
		}
	}
done:
	discard_index(&istate);
	return i == the_index.cache_nr;
}

static void share_the_index(void)
{
	if (the_index.split_index && the_index.split_index->base)
		share_index(the_index.split_index->base, &shm_base_index);
	share_index(&the_index, &shm_index);
	if (to_verify && !verify_shm()) {
		cleanup_shm();
		discard_index(&the_index);
	}
}

static void set_socket_blocking_flag(int fd, int make_nonblocking)
{
	int flags;

	flags = fcntl(fd, F_GETFL, NULL);

	if (flags < 0)
		die(_("fcntl failed"));

	if (make_nonblocking)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags) < 0)
		die(_("fcntl failed"));
}

static void refresh(void)
{
	discard_index(&the_index);
	the_index.keep_mmap = 1;
	the_index.to_shm    = 1;
	if (read_cache() < 0)
		die(_("could not read index"));
	share_the_index();
}

#ifndef NO_MMAP

#ifdef USE_WATCHMAN
static void share_watchman(struct index_state *istate,
			   struct shm *is, pid_t pid)
{
	struct strbuf sb = STRBUF_INIT;
	void *shm;

	write_watchman_ext(&sb, istate);
	if (!shared_mmap_create(sb.len + 20, &shm,
				git_path("shm-watchman-%s-%" PRIuMAX,
					 sha1_to_hex(istate->sha1),
					 (uintmax_t)pid))) {
		is->size = sb.len + 20;
		is->shm = shm;
		is->pid = pid;
		hashcpy(is->sha1, istate->sha1);

		memcpy(shm, sb.buf, sb.len);
		hashcpy((unsigned char *)shm + is->size - 20, is->sha1);
	}
	strbuf_release(&sb);
}


static void prepare_with_watchman(pid_t pid)
{
	/*
	 * TODO: with the help of watchman, maybe we could detect if
	 * $GIT_DIR/index is updated.
	 */
	if (!verify_index(&the_index))
		refresh();

	if (check_watchman(&the_index))
		return;

	share_watchman(&the_index, &shm_watchman, pid);
}

static void prepare_index(pid_t pid)
{
	if (shm_index.shm == NULL)
		refresh();
	release_watchman_shm(&shm_watchman);
	if (the_index.last_update)
		prepare_with_watchman(pid);
}

#endif

static void reply_to_poke(int client_fd, const char *pid_buf)
{
	char *capabilities;
	struct strbuf sb = STRBUF_INIT;

#ifdef USE_WATCHMAN
	pid_t client_pid = strtoull(pid_buf, NULL, 10);

	prepare_index(client_pid);
#endif
	capabilities = strchr(pid_buf, ' ');

	if (capabilities && !strcmp(capabilities, " watchman"))
#ifdef USE_WATCHMAN
		packet_buf_write(&sb, "OK watchman");
#else
		packet_buf_write(&sb, "NAK watchman");
#endif
	else
		packet_buf_write(&sb, "OK");
	if (write_in_full(client_fd, sb.buf, sb.len) != sb.len)
		warning(_("client write failed"));
}

static void loop(int fd, int idle_in_seconds)
{
	assert(idle_in_seconds < INT_MAX / 1000);

	if (idle_in_seconds == 0)
		idle_in_seconds = -1;

	while (1) {
		struct pollfd pollfd;
		int result, client_fd;
		int flags;
		char buf[4096];
		int bytes_read;

		/* Wait for a request */
		pollfd.fd = fd;
		pollfd.events = POLLIN;
		result = poll(&pollfd, 1, idle_in_seconds * 1000);
		if (result < 0) {
			if (errno == EINTR)
				/*
				 * This can lead to an overlong keepalive,
				 * but that is better than a premature exit.
				 */
				continue;
			die_errno(_("poll() failed"));
		} else if (result == 0)
			/* timeout */
			break;

		client_fd = accept(fd, NULL, NULL);
		if (client_fd < 0)
			/*
			 * An error here is unlikely -- it probably
			 * indicates that the connecting process has
			 * already dropped the connection.
			 */
			continue;

		/*
		 * Our connection to the client is blocking since a client
		 * can always be killed by SIGINT or similar.
		 */
		set_socket_blocking_flag(client_fd, 0);

		flags = PACKET_READ_GENTLE_ON_EOF | PACKET_READ_CHOMP_NEWLINE;
		bytes_read = packet_read(client_fd, NULL, NULL, buf,
					 sizeof(buf), flags);

		if (bytes_read > 0) {
			/* ensure string termination */
			buf[bytes_read] = 0;
			if (!strcmp(buf, "refresh")) {
				refresh();
			} else if (starts_with(buf, "poke")) {
				if (buf[4] == ' ') {
					reply_to_poke(client_fd, buf + 5);
				} else {
					/*
					 * Just a poke to keep us
					 * alive, nothing to do.
					 */
				}
			} else if (!strcmp(buf, "die")) {
				break;
			} else {
				warning("BUG: Bogus command %s", buf);
			}
		} else {
			/*
			 * No command from client.  Probably it's just
			 * a liveness check or client error.  Just
			 * close up.
			 */
		}
		close(client_fd);
	}

	close(fd);
}

#else

static void loop(int fd, int idle_in_seconds)
{
	die(_("index-helper is not supported on this platform"));
}

#endif

static const char * const usage_text[] = {
	N_("git index-helper [options]"),
	NULL
};

static void request_kill(void)
{
	int fd = unix_stream_connect(git_path("index-helper.sock"));

	if (fd >= 0) {
		write_in_full(fd, "die", 4);
		close(fd);
	}

	/*
	 * The child will try to do this anyway, but we want to be
	 * ready to launch a new daemon immediately after this command
	 * returns.
	 */

	unlink(git_path("index-helper.sock"));
	return;
}

int main(int argc, char **argv)
{
	const char *prefix;
	int idle_in_seconds = 600, detach = 0, kill = 0, autorun = 0;
	int fd;
	int nongit;
	struct strbuf socket_path = STRBUF_INIT;
	struct option options[] = {
		OPT_INTEGER(0, "exit-after", &idle_in_seconds,
			    N_("exit if not used after some seconds")),
		OPT_BOOL(0, "strict", &to_verify,
			 "verify shared memory after creating"),
		OPT_BOOL(0, "detach", &detach, "detach the process"),
		OPT_BOOL(0, "kill", &kill, "request that existing index helper processes exit"),
		OPT_BOOL(0, "autorun", &autorun, "this is an automatic run of git index-helper, so certain errors can be solved by silently exiting"),
		OPT_END()
	};

	git_extract_argv0_path(argv[0]);
	git_setup_gettext();

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(usage_text, options);

	prefix = setup_git_directory_gently(&nongit);
	if (nongit) {
		if (autorun)
			exit(0);
		else
			die(_("not a git repository"));
	}

	if (parse_options(argc, (const char **)argv, prefix,
			  options, usage_text, 0))
		die(_("too many arguments"));

	if (kill) {
		if (detach)
			die(_("--kill doesn't want any other options"));
		request_kill();
		return 0;
	}

	/* check that no other copy is running */
	fd = unix_stream_connect(git_path("index-helper.sock"));
	if (fd > 0) {
		if (autorun)
			exit(0);
		else
			die(_("Already running"));
	}
	if (errno != ECONNREFUSED && errno != ENOENT) {
		if (autorun)
			return 0;
		else
			die_errno(_("Unexpected error checking socket"));
	}

	atexit(cleanup);
	sigchain_push_common(cleanup_on_signal);

	strbuf_git_path(&socket_path, "index-helper.sock");

	fd = unix_stream_listen(socket_path.buf);
	if (fd < 0)
		die_errno(_("could not set up index-helper socket"));

	if (detach) {
		FILE *fp = fopen(git_path("index-helper.log"), "a");
		if (!fp)
			die("failed to open %s for writing",
			    git_path("index-helper.log"));
		set_error_handle(fp);
	}

	if (detach && daemonize(&daemonized))
		die_errno(_("unable to detach"));

	loop(fd, idle_in_seconds);

	close(fd);
	return 0;
}
