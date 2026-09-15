#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "odb.h"
#include "run-command.h"
#include "sigchain.h"
#include "connected.h"
#include "transport.h"
#include "packfile.h"
#include "promisor-remote.h"

static int promised_object_cb(const struct object_id *oid UNUSED,
			      struct object_info *oi UNUSED,
			      void *payload)
{
	bool *found = payload;
	*found = true;
	return 1;
}

/*
 * For partial clones, we don't want to have to do a regular connectivity check
 * because we have to enumerate and exclude all promisor objects (slow), and
 * then the connectivity check itself becomes a no-op because in a partial
 * clone every object is a promisor object. Instead, just make sure we
 * received, in a promisor packfile, the objects pointed to by each wanted ref.
 *
 * Before checking for promisor packs, be sure we have the latest pack-files
 * loaded into memory.
 *
 * Returns 1 when all object IDs have been found in promisor packs, in which
 * case we're fully connected and thus done. Returns 0 when we have found
 * objects in non-promisor packs, in which case we'll have to fall back to the
 * rev-list-based connectivity checks. Returns a negative error code on error.
 */
static int check_connected_promisor(oid_iterate_fn fn,
				    void *cb_data,
				    const struct object_id **oid)
{
	struct odb_for_each_object_options opts = {
		.flags = ODB_FOR_EACH_OBJECT_PROMISOR_ONLY,
		.prefix_hex_len = the_repository->hash_algo->hexsz,
	};
	int err;

	odb_reprepare(the_repository->objects);
	do {
		bool found = false;

		opts.prefix = *oid;

		err = odb_for_each_object_ext(the_repository->objects, NULL,
					      promised_object_cb, &found, &opts);
		if (err < 0)
			return err;

		/*
		 * We have found an object that is not part of a promisor pack,
		 * and thus we cannot skip the full connectivity check.
		 */
		if (!found)
			return 0;
	} while ((*oid = fn(cb_data)) != NULL);

	return 1;
}

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

	if (repo_has_promisor_remote(the_repository)) {
		err = check_connected_promisor(fn, cb_data, &oid);
		if (err) {
			if (opt->err_fd)
				close(opt->err_fd);
			if (err > 0)
				err = 0;
			return err;
		}
	}

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

	if (start_command(&rev_list))
		return error(_("Could not run 'git rev-list'"));

	sigchain_push(SIGPIPE, SIG_IGN);

	rev_list_in = xfdopen(rev_list.in, "w");

	if (transport && transport->smart_options &&
	    transport->smart_options->self_contained_and_connected &&
	    transport->pack_lockfiles.nr == 1 &&
	    strip_suffix(transport->pack_lockfiles.items[0].string,
			 ".keep", &base_len)) {
		struct strbuf idx_file = STRBUF_INIT;
		strbuf_add(&idx_file, transport->pack_lockfiles.items[0].string,
			   base_len);
		strbuf_addstr(&idx_file, ".idx");
		new_pack = add_packed_git(the_repository, idx_file.buf,
					  idx_file.len, 1);
		strbuf_release(&idx_file);
	}

	do {
		/*
		 * If index-pack already checked that:
		 * - there are no dangling pointers in the new pack
		 * - the pack is self contained
		 * Then if the updated ref is in the new pack, then we
		 * are sure the ref is good and not sending it to
		 * rev-list for verification.
		 */
		if (new_pack && find_pack_entry_one(oid, new_pack))
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
	if (new_pack) {
		close_pack(new_pack);
		free(new_pack);
	}
	return finish_command(&rev_list) || err;
}
