#include "git-compat-util.h"
#include "config.h"
#include "gettext.h"
#include "hex.h"
#include "path.h"
#include "repository.h"
#include "strbuf.h"
#include "fsmonitor-ll.h"
#include "fsmonitor-ipc.h"
#include "fsmonitor-path-utils.h"

static GIT_PATH_FUNC(fsmonitor_ipc__get_default_path, "fsmonitor--daemon.ipc")

const char *fsmonitor_ipc__get_path(struct repository *r)
{
	static const char *ipc_path = NULL;
	git_SHA_CTX sha1ctx;
	char *sock_dir = NULL;
	struct strbuf ipc_file = STRBUF_INIT;
	unsigned char hash[GIT_MAX_RAWSZ];

	if (!r)
		BUG("No repository passed into fsmonitor_ipc__get_path");

	if (ipc_path)
		return ipc_path;


	/* By default the socket file is created in the .git directory */
	if (fsmonitor__is_fs_remote(r->gitdir) < 1) {
		ipc_path = fsmonitor_ipc__get_default_path();
		return ipc_path;
	}

	git_SHA1_Init(&sha1ctx);
	git_SHA1_Update(&sha1ctx, r->worktree, strlen(r->worktree));
	git_SHA1_Final(hash, &sha1ctx);

	repo_config_get_string(r, "fsmonitor.socketdir", &sock_dir);

	/* Create the socket file in either socketDir or $HOME */
	if (sock_dir && *sock_dir) {
		strbuf_addf(&ipc_file, "%s/.git-fsmonitor-%s",
					sock_dir, hash_to_hex(hash));
	} else {
		strbuf_addf(&ipc_file, "~/.git-fsmonitor-%s", hash_to_hex(hash));
	}
	free(sock_dir);

	ipc_path = interpolate_path(ipc_file.buf, 1);
	if (!ipc_path)
		die(_("Invalid path: %s"), ipc_file.buf);

	strbuf_release(&ipc_file);
	return ipc_path;
}
