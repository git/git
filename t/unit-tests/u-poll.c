#include "unit-test.h"

#ifdef GIT_WINDOWS_NATIVE
static struct {
	int pipes[POLL_MAX_DESCRIPTORS + 1][2];
	size_t nr_pipes;
	int listener;
	int sockets[2];
	WSAEVENT event;
	int event_selected;
} poll_test;

static void open_pipes(struct pollfd *fds, size_t nr)
{
	size_t i;

	cl_assert(nr <= ARRAY_SIZE(poll_test.pipes));
	for (i = 0; i < nr; i++) {
		int *pipefd = poll_test.pipes[poll_test.nr_pipes];

		cl_assert_equal_i(pipe(pipefd), 0);
		poll_test.nr_pipes++;
		fds[i].fd = pipefd[0];
		fds[i].events = POLLIN;
	}
}

static void create_socket_pair(void)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	socklen_t address_length = sizeof(address);

	poll_test.listener = socket(AF_INET, SOCK_STREAM, 0);
	cl_assert(poll_test.listener >= 0);
	cl_assert_equal_i(bind(poll_test.listener,
			       (struct sockaddr *)&address,
			       sizeof(address)), 0);
	cl_assert_equal_i(getsockname(
				  (SOCKET)_get_osfhandle(poll_test.listener),
				  (struct sockaddr *)&address,
				  &address_length), 0);
	cl_assert_equal_i(listen(poll_test.listener, 1), 0);

	poll_test.sockets[0] = socket(AF_INET, SOCK_STREAM, 0);
	cl_assert(poll_test.sockets[0] >= 0);
	cl_assert_equal_i(connect(poll_test.sockets[0],
				  (struct sockaddr *)&address,
				  sizeof(address)), 0);

	poll_test.sockets[1] = accept(poll_test.listener, NULL, NULL);
	cl_assert(poll_test.sockets[1] >= 0);
	close(poll_test.listener);
	poll_test.listener = -1;
}
#endif

void test_poll__initialize(void)
{
#ifdef GIT_WINDOWS_NATIVE
	memset(&poll_test, 0, sizeof(poll_test));
	poll_test.listener = -1;
	poll_test.sockets[0] = -1;
	poll_test.sockets[1] = -1;
	poll_test.event = WSA_INVALID_EVENT;
#endif
}

void test_poll__cleanup(void)
{
#ifdef GIT_WINDOWS_NATIVE
	size_t i;

	if (poll_test.event_selected && poll_test.sockets[0] >= 0)
		WSAEventSelect(
			(SOCKET)_get_osfhandle(poll_test.sockets[0]), NULL, 0);
	if (poll_test.event != WSA_INVALID_EVENT)
		WSACloseEvent(poll_test.event);
	if (poll_test.listener >= 0)
		close(poll_test.listener);
	for (i = 0; i < ARRAY_SIZE(poll_test.sockets); i++)
		if (poll_test.sockets[i] >= 0)
			close(poll_test.sockets[i]);
	for (i = 0; i < poll_test.nr_pipes; i++) {
		close(poll_test.pipes[i][0]);
		close(poll_test.pipes[i][1]);
	}
#endif
}

void test_poll__limit(void)
{
#ifdef GIT_WINDOWS_NATIVE
	struct pollfd fds[POLL_MAX_DESCRIPTORS + 1] = { 0 };

	open_pipes(fds, ARRAY_SIZE(fds));
	cl_assert_equal_i(poll(fds, POLL_MAX_DESCRIPTORS, 0), 0);

	errno = 0;
	cl_assert_equal_i(poll(fds, ARRAY_SIZE(fds), 0), -1);
	cl_assert_equal_i(errno, EINVAL);
#else
	cl_skip();
#endif
}

void test_poll__sparse(void)
{
#ifdef GIT_WINDOWS_NATIVE
	struct pollfd fds[2 * POLL_MAX_DESCRIPTORS + 1] = { 0 };
	size_t i;

	open_pipes(fds, POLL_MAX_DESCRIPTORS);
	create_socket_pair();

	for (i = POLL_MAX_DESCRIPTORS; i > 0; i--) {
		fds[2 * i - 1] = fds[i - 1];
		fds[2 * i - 2].fd = -1;
	}
	fds[2 * POLL_MAX_DESCRIPTORS].fd = poll_test.sockets[0];
	fds[2 * POLL_MAX_DESCRIPTORS].events = POLLIN;

	cl_assert_equal_i(poll(fds, ARRAY_SIZE(fds), 0), 0);
#else
	cl_skip();
#endif
}

void test_poll__socket_cleanup(void)
{
#ifdef GIT_WINDOWS_NATIVE
	struct pollfd fds[POLL_MAX_DESCRIPTORS + 2] = { 0 };
	WSANETWORKEVENTS events;
	SOCKET socket_handle;

	create_socket_pair();
	socket_handle = (SOCKET)_get_osfhandle(poll_test.sockets[0]);
	poll_test.event = WSACreateEvent();
	cl_assert(poll_test.event != WSA_INVALID_EVENT);
	cl_assert_equal_i(WSAEventSelect(socket_handle, poll_test.event,
					FD_READ), 0);
	poll_test.event_selected = 1;

	fds[0].fd = poll_test.sockets[0];
	open_pipes(fds + 1, POLL_MAX_DESCRIPTORS + 1);

	errno = 0;
	cl_assert_equal_i(poll(fds, ARRAY_SIZE(fds), 0), -1);
	cl_assert_equal_i(errno, EINVAL);

	cl_assert_equal_i(send(
				  (SOCKET)_get_osfhandle(poll_test.sockets[1]),
				  "x", 1, 0), 1);
	cl_assert_equal_i(WaitForSingleObject(poll_test.event, 1000),
			  WAIT_OBJECT_0);
	cl_assert_equal_i(WSAEnumNetworkEvents(socket_handle, poll_test.event,
					      &events), 0);
#else
	cl_skip();
#endif
}
