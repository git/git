#include "cache.h"
#include "hex.h"
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
	FILE *rev_list_in;
	struct check_connected_options defaults = CHECK_CONNECTED_INIT;
	const struct object_id *oid;
	int err = 0;
	struct packed_git *new_pack = NULL;
	struct transport *transport;
	size_t base_len;

	if (!opt)
		opt = &defaults;
	transport = opt->transport;

	oid = fn(cb_data);
	if (!oid) {
		if (opt->err_fd)
			close(opt->err_fd);
		return err;
	}

	if (transport && transport->smart_options &&
	    transport->smart_options->self_contained_and_connected &&
	    transport->pack_lockfiles.nr == 1 &&
	    strip_suffix(transport->pack_lockfiles.items[0].string,
			 ".keep", &base_len)) {
		struct strbuf idx_file = STRBUF_INIT;
		strbuf_add(&idx_file, transport->pack_lockfiles.items[0].string,
			   base_len);
		strbuf_addstr(&idx_file, ".idx");
		new_pack = add_packed_git(idx_file.buf, idx_file.len, 1);
		strbuf_release(&idx_file);
	}

	if (repo_has_promisor_remote(the_repository)) {
		/*
		 * For partial clones, we don't want to have to do a regular
		 * connectivity check because we have to enumerate and exclude
		 * all promisor objects (slow), and then the connectivity check
		 * itself becomes a no-op because in a partial clone every
		 * object is a promisor object. Instead, just make sure we
		 * received, in a promisor packfile, the objects pointed to by
		 * each wanted ref.
		 *
		 * Before checking for promisor packs, be sure we have the
		 * latest pack-files loaded into memory.
		 */
		reprepare_packed_git(the_repository);
		do {
			struct packed_git *p;

			for (p = get_all_packs(the_repository); p; p = p->next) {
				if (!p->pack_promisor)
					continue;
				if (find_pack_entry_one(oid->hash, p))
					goto promisor_pack_found;
			}
			/*
			 * Fallback to rev-list with oid and the rest of the
			 * object IDs provided by fn.
			 */
			goto no_promisor_pack_found;
promisor_pack_found:
			;
		} while ((oid = fn(cb_data)) != NULL);
		free(new_pack);
		return 0;
	}

no_promisor_pack_found:
	if (opt->shallow_file) {
		strvec_push(&rev_list.args, "--shallow-file");
		strvec_push(&rev_list.args, opt->shallow_file);
	}
	strvec_push(&rev_list.args,"rev-list");
	strvec_push(&rev_list.args, "--objects");
	strvec_push(&rev_list.args, "--stdin");
	if (repo_has_promisor_remote(the_repository))
		strvec_push(&rev_list.args, "--exclude-promisor-objects");
	if (!opt->is_deepening_fetch) {
		strvec_push(&rev_list.args, "--not");
		if (opt->exclude_hidden_refs_section)
			strvec_pushf(&rev_list.args, "--exclude-hidden=%s",
				     opt->exclude_hidden_refs_section);
		strvec_push(&rev_list.args, "--all");
	}
	strvec_push(&rev_list.args, "--quiet");
	strvec_push(&rev_list.args, "--alternate-refs");
	if (opt->progress)
		strvec_pushf(&rev_list.args, "--progress=%s",
			     _("Checking connectivity"));

	rev_list.git_cmd = 1;
	if (opt->env)
		strvec_pushv(&rev_list.env, opt->env);
	rev_list.in = -1;
	rev_list.no_stdout = 1;
	if (opt->err_fd)
		rev_list.err = opt->err_fd;
	else
		rev_list.no_stderr = opt->quiet;

	if (start_command(&rev_list)) {
		free(new_pack);
		return error(_("Could not run 'git rev-list'"));
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	rev_list_in = xfdopen(rev_list.in, "w");

	do {
		/*
		 * If index-pack already checked that:
		 * - there are no dangling pointers in the new pack
		 * - the pack is self contained
		 * Then if the updated ref is in the new pack, then we
		 * are sure the ref is good and not sending it to
		 * rev-list for verification.
		 */
		if (new_pack && find_pack_entry_one(oid->hash, new_pack))
			continue;

		if (fprintf(rev_list_in, "%s\n", oid_to_hex(oid)) < 0)
			break;
	} while ((oid = fn(cb_data)) != NULL);

	if (ferror(rev_list_in) || fflush(rev_list_in)) {
		if (errno != EPIPE && errno != EINVAL)
			error_errno(_("failed write to rev-list"));
		err = -1;
	}

	if (fclose(rev_list_in))
		err = error_errno(_("failed to close rev-list's stdin"));

	sigchain_pop(SIGPIPE);
	free(new_pack);
	return finish_command(&rev_list) || err;
}
