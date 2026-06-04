#include "git-compat-util.h"
#include "simple-ipc.h"

#ifndef SUPPORTS_SIMPLE_IPC
/*
 * This source file should only be compiled when Simple IPC is supported.
 * See the top-level Makefile.
 */
#error SUPPORTS_SIMPLE_IPC not defined
#endif

int ipc_server_run(const char *path, const struct ipc_server_opts *opts,
		   ipc_server_application_cb *application_cb,
		   void *application_data)
{
	struct ipc_server_data *server_data = NULL;
	int ret;

	ret = ipc_server_init_async(&server_data, path, opts,
				    application_cb, application_data);
	if (ret)
		return ret;

	ipc_server_start_async(server_data);
	ret = ipc_server_await(server_data);

	ipc_server_free(server_data);

	return ret;
}
