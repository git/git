#include "test-tool.h"
#include "git-compat-util.h"
#include "strbuf.h"

#ifdef GIT_WINDOWS_NATIVE
static const char *usage_string = "<pipe-filename>";

#define TEST_BUFSIZE (4096)

int cmd__windows_named_pipe(int argc, const char **argv)
{
	const char *filename;
	struct strbuf pathname = STRBUF_INIT;
	int err;
	HANDLE h;
	BOOL connected;
	char buf[TEST_BUFSIZE + 1];

	if (argc < 2)
		goto print_usage;
	filename = argv[1];
	if (strpbrk(filename, "/\\"))
		goto print_usage;
	strbuf_addf(&pathname, "//./pipe/%s", filename);

	/*
	 * Create a single instance of the server side of the named pipe.
	 * This will allow exactly one client instance to connect to it.
	 */
	h = CreateNamedPipeA(
		pathname.buf,
		PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		TEST_BUFSIZE, TEST_BUFSIZE, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		err = err_win_to_posix(GetLastError());
		fprintf(stderr, "CreateNamedPipe failed: %s\n",
			strerror(err));
		return err;
	}

	connected = ConnectNamedPipe(h, NULL)
		? TRUE
		: (GetLastError() == ERROR_PIPE_CONNECTED);
	if (!connected) {
		err = err_win_to_posix(GetLastError());
		fprintf(stderr, "ConnectNamedPipe failed: %s\n",
			strerror(err));
		CloseHandle(h);
		return err;
	}

	while (1) {
		DWORD nbr;
		BOOL success = ReadFile(h, buf, TEST_BUFSIZE, &nbr, NULL);
		if (!success || nbr == 0)
			break;
		buf[nbr] = 0;

		write(1, buf, nbr);
	}

	DisconnectNamedPipe(h);
	CloseHandle(h);
	return 0;

print_usage:
	fprintf(stderr, "usage: %s %s\n", argv[0], usage_string);
	return 1;
}
#endif
