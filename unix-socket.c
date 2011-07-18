#include "cache.h"
#include "unix-socket.h"

static int unix_stream_socket(void)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		die_errno("unable to create socket");
	return fd;
}

static void unix_sockaddr_init(struct sockaddr_un *sa, const char *path)
{
	int size = strlen(path) + 1;
	if (size > sizeof(sa->sun_path))
		die("socket path is too long to fit in sockaddr");
	memset(sa, 0, sizeof(*sa));
	sa->sun_family = AF_UNIX;
	memcpy(sa->sun_path, path, size);
}

int unix_stream_connect(const char *path)
{
	int fd;
	struct sockaddr_un sa;

	unix_sockaddr_init(&sa, path);
	fd = unix_stream_socket();
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int unix_stream_listen(const char *path)
{
	int fd;
	struct sockaddr_un sa;

	unix_sockaddr_init(&sa, path);
	fd = unix_stream_socket();

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		unlink(path);
		if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			close(fd);
			return -1;
		}
	}

	if (listen(fd, 5) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}
