#include "cache.h"
#include "object-store.h"
#include "run-command.h"
#include "sigchain.h"
#include "connected.h"
#include "transport.h"
#include "packfile.h"
#include "promisor-remote.h"

/*
 * If we feed all the commits we want to verify to this command
 *
 *  $ git rev-list --objects --stdin --not --all
 *
 * and if it does not error out, that means everything reachable from
 * these commits locally exists and is connected to our existing refs.
 * Note that this does _not_ validate the individual objects.
 *
 * Returns 0 if everything is connected, non-zero otherwise.
 */
int check_connected(oid_iterate_fn fn, void *cb_data,
		    struct check_connected_options *opt)
{
	struct child_process rev_list = CHILD_PROCESS_INIT;
	struct check_connected_options defaults = CHECK_CONNECTED_INIT;
	char commit[GIT_MAX_HEXSZ + 1];
	struct object_id oid;
	int err = 0;
	struct packed_git *new_pack = NULL;
	struct transport *transport;
	size_t base_len;
	const unsigned hexsz = the_hash_algo->hexsz;

	if (!opt)
		opt = &defaults;
	transport = opt->transport;

	if (fn(cb_data, &oid)) {
		if (opt->err_fd)
			close(opt->err_fd);
		return err;
	}

	if (transport && transport->smart_options &&
	    transport->smart_options->self_contained_and_connected &&
	    transport->pack_lockfile &&
	    strip_suffix(transport->pack_lockfile, ".keep", &base_len)) {
		struct strbuf idx_file = STRBUF_INIT;
		strbuf_add(&idx_file, transport->pack_lockfile, base_len);
		strbuf_addstr(&idx_file, ".idx");
		new_pack = add_packed_git(idx_file.buf, idx_file.len, 1);
		strbuf_release(&idx_file);
	}

	if (opt->check_refs_only) {
		/*
		 * For partial clones, we don't want to have to do a regular
		 * connectivity check because we have to enumerate and exclude
		 * all promisor objects (slow), and then the connectivity check
		 * itself becomes a no-op because in a partial clone every
		 * object is a promisor object. Instead, just make sure we
		 * received the objects pointed to by each wanted ref.
		 */
		do {
			if (!repo_has_object_file(the_repository, &oid))
				return 1;
		} while (!fn(cb_data, &oid));
		return 0;
	}

	if (opt->shallow_file) {
		argv_array_push(&rev_list.args, "--shallow-file");
		argv_array_push(&rev_list.args, opt->shallow_file);
	}
	argv_array_push(&rev_list.args,"rev-list");
	argv_array_push(&rev_list.args, "--objects");
	argv_array_push(&rev_list.args, "--stdin");
	if (has_promisor_remote())
		argv_array_push(&rev_list.args, "--exclude-promisor-objects");
	if (!opt->is_deepening_fetch) {
		argv_array_push(&rev_list.args, "--not");
		argv_array_push(&rev_list.args, "--all");
	}
	argv_array_push(&rev_list.args, "--quiet");
	argv_array_push(&rev_list.args, "--alternate-refs");
	if (opt->progress)
		argv_array_pushf(&rev_list.args, "--progress=%s",
				 _("Checking connectivity"));

	rev_list.git_cmd = 1;
	rev_list.env = opt->env;
	rev_list.in = -1;
	rev_list.no_stdout = 1;
	if (opt->err_fd)
		rev_list.err = opt->err_fd;
	else
		rev_list.no_stderr = opt->quiet;

	if (start_command(&rev_list))
		return error(_("Could not run 'git rev-list'"));

	sigchain_push(SIGPIPE, SIG_IGN);

	commit[hexsz] = '\n';
	do {
		/*
		 * If index-pack already checked that:
		 * - there are no dangling pointers in the new pack
		 * - the pack is self contained
		 * Then if the updated ref is in the new pack, then we
		 * are sure the ref is good and not sending it to
		 * rev-list for verification.
		 */
		if (new_pack && find_pack_entry_one(oid.hash, new_pack))
			continue;

		memcpy(commit, oid_to_hex(&oid), hexsz);
		if (write_in_full(rev_list.in, commit, hexsz + 1) < 0) {
			if (errno != EPIPE && errno != EINVAL)
				error_errno(_("failed write to rev-list"));
			err = -1;
			break;
		}
	} while (!fn(cb_data, &oid));

	if (close(rev_list.in))
		err = error_errno(_("failed to close rev-list's stdin"));

	sigchain_pop(SIGPIPE);
	return finish_command(&rev_list) || err;
}
