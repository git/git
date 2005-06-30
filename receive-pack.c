#include "cache.h"
#include <sys/wait.h>

static const char receive_pack_usage[] = "git-receive-pack [--unpack=executable] <git-dir> [heads]";

static const char *unpacker = "git-unpack-objects";

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

static void safe_write(int fd, const void *buf, unsigned n)
{
	while (n) {
		int ret = write(fd, buf, n);
		if (ret > 0) {
			buf += ret;
			n -= ret;
			continue;
		}
		if (!ret)
			die("write error (disk full?)");
		if (errno == EAGAIN || errno == EINTR)
			continue;
		die("write error (%s)", strerror(errno));
	}
}

/*
 * If we buffered things up above (we don't, but we should),
 * we'd flush it here
 */
static void flush_safe(int fd)
{
}

/*
 * Write a packetized stream, where each line is preceded by
 * its length (including the header) as a 4-byte hex number.
 * A length of 'zero' means end of stream (and a length of 1-3
 * would be an error). 
 */
#define hex(a) (hexchar[(a) & 15])
static void packet_write(const char *fmt, ...)
{
	static char buffer[1000];
	static char hexchar[] = "0123456789abcdef";
	va_list args;
	unsigned n;

	va_start(args, fmt);
	n = vsnprintf(buffer + 4, sizeof(buffer) - 4, fmt, args);
	va_end(args);
	if (n >= sizeof(buffer)-4)
		die("protocol error: impossibly long line");
	n += 4;
	buffer[0] = hex(n >> 12);
	buffer[1] = hex(n >> 8);
	buffer[2] = hex(n >> 4);
	buffer[3] = hex(n);
	safe_write(1, buffer, n);
}

static void show_ref(const char *path, unsigned char *sha1)
{
	packet_write("%s %s\n", sha1_to_hex(sha1), path);
}

static int read_ref(const char *path, unsigned char *sha1)
{
	int ret = -1;
	int fd = open(path, O_RDONLY);

	if (fd >= 0) {
		char buffer[60];
		if (read(fd, buffer, sizeof(buffer)) >= 40)
			ret = get_sha1_hex(buffer, sha1);
		close(fd);
	}
	return ret;
}

static void write_head_info(const char *base, int nr, char **match)
{
	DIR *dir = opendir(base);

	if (dir) {
		struct dirent *de;
		int baselen = strlen(base);
		char *path = xmalloc(baselen + 257);
		memcpy(path, base, baselen);

		while ((de = readdir(dir)) != NULL) {
			char sha1[20];
			struct stat st;
			int namelen;

			if (de->d_name[0] == '.')
				continue;
			namelen = strlen(de->d_name);
			if (namelen > 255)
				continue;
			memcpy(path + baselen, de->d_name, namelen+1);
			if (lstat(path, &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode)) {
				path[baselen + namelen] = '/';
				path[baselen + namelen + 1] = 0;
				write_head_info(path, nr, match);
				continue;
			}
			if (read_ref(path, sha1) < 0)
				continue;
			if (nr && !path_match(path, nr, match))
				continue;
			show_ref(path, sha1);
		}
		free(path);
		closedir(dir);
	}
}

/*
 * This is all pretty stupid, but we use this packetized line
 * format to make a streaming format possible without ever
 * over-running the read buffers. That way we'll never read
 * into what might be the pack data (which should go to another
 * process entirely).
 *
 * The writing side could use stdio, but since the reading
 * side can't, we stay with pure read/write interfaces.
 */
static void safe_read(int fd, void *buffer, unsigned size)
{
	int n = 0;

	while (n < size) {
		int ret = read(0, buffer + n, size - n);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			die("read error (%s)", strerror(errno));
		}
		if (!ret)
			die("unexpected EOF");
		n += ret;
	}
}

static int safe_read_line(char *buffer, unsigned size)
{
	int n, len;

	safe_read(0, buffer, 4);

	len = 0;
	for (n = 0; n < 4; n++) {
		unsigned char c = buffer[n];
		len <<= 4;
		if (c >= '0' && c <= '9') {
			len += c - '0';
			continue;
		}
		if (c >= 'a' && c <= 'f') {
			len += c - 'a' + 10;
			continue;
		}
		if (c >= 'A' && c <= 'F') {
			len += c - 'A' + 10;
			continue;
		}
		die("protocol error: bad line length character");
	}
	if (!len)
		return 0;
	if (len < 4 || len >= size)
		die("protocol error: bad line length %d", len);
	safe_read(0, buffer + 4, len - 4);
	buffer[len] = 0;
	return len;
}

struct line {
	struct line *next;
	char data[0];
};

struct line *commands = NULL;

/*
 * This gets called after(if) we've successfully
 * unpacked the data payload.
 */
static void execute_commands(void)
{
	struct line *line = commands;

	while (line) {
		printf("%s", line->data);
		line = line->next;
	}
}

static void read_head_info(void)
{
	struct line **p = &commands;
	for (;;) {
		static char line[1000];
		int len = safe_read_line(line, sizeof(line));
		struct line *n;
		if (!len)
			break;
		n = xmalloc(sizeof(struct line) + len);
		n->next = NULL;
		memcpy(n->data, line + 4, len - 3);
		*p = n;
		p = &n->next;
	}
}

static void unpack(void)
{
	pid_t pid = fork();

	if (pid < 0)
		die("unpack fork failed");
	if (!pid) {
		char *const envp[] = { "GIT_DIR=.", NULL };
		execle(unpacker, unpacker, NULL, envp);
		die("unpack execute failed");
	}

	for (;;) {
		int status, code;
		int retval = waitpid(pid, &status, 0);

		if (retval < 0) {
			if (errno == EINTR)
				continue;
			die("waitpid failed (%s)", strerror(retval));
		}
		if (retval != pid)
			die("waitpid is confused");
		if (WIFSIGNALED(status))
			die("%s died of signal %d", unpacker, WTERMSIG(status));
		if (!WIFEXITED(status))
			die("%s died out of really strange complications", unpacker);
		code = WEXITSTATUS(status);
		if (code)
			die("%s exited with error code %d", unpacker, code);
		return;
	}
}

int main(int argc, char **argv)
{
	int i, nr_heads = 0;
	const char *dir = NULL;
	char **heads = NULL;

	argv++;
	for (i = 1; i < argc; i++) {
		const char *arg = *argv++;

		if (*arg == '-') {
			if (!strncmp(arg, "--unpack=", 9)) {
				unpacker = arg+9;
				continue;
			}
			/* Do flag handling here */
			usage(receive_pack_usage);
		}
		dir = arg;
		heads = argv;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dir)
		usage(receive_pack_usage);

	/* chdir to the directory. If that fails, try appending ".git" */
	if (chdir(dir) < 0) {
		static char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s.git", dir);
		if (chdir(path) < 0)
			die("unable to cd to %s", dir);
	}

	/* If we have a ".git" directory, chdir to it */
	chdir(".git");

	if (access("objects", X_OK) < 0 || access("refs/heads", X_OK) < 0)
		die("%s doesn't appear to be a git directory", dir);
	write_head_info("refs/", nr_heads, heads);

	/* EOF */
	safe_write(1, "0000", 4);
	flush_safe(1);

	read_head_info();
	unpack();
	execute_commands();
	return 0;
}
