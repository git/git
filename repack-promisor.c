#include "git-compat-util.h"
#include "repack.h"
#include "hex.h"
#include "pack.h"
#include "packfile.h"
#include "path.h"
#include "repository.h"
#include "run-command.h"

struct write_oid_context {
	struct child_process *cmd;
	const struct git_hash_algo *algop;
};

/*
 * Write oid to the given struct child_process's stdin, starting it first if
 * necessary.
 */
static int write_oid(const struct object_id *oid,
		     struct packed_git *pack UNUSED,
		     uint32_t pos UNUSED, void *data)
{
	struct write_oid_context *ctx = data;
	struct child_process *cmd = ctx->cmd;

	if (cmd->in == -1) {
		if (start_command(cmd))
			die(_("could not start pack-objects to repack promisor objects"));
	}

	if (write_in_full(cmd->in, oid_to_hex(oid), ctx->algop->hexsz) < 0 ||
	    write_in_full(cmd->in, "\n", 1) < 0)
		die(_("failed to feed promisor objects to pack-objects"));
	return 0;
}

void repack_promisor_objects(struct repository *repo,
			     const struct pack_objects_args *args,
			     struct string_list *names, const char *packtmp)
{
	struct write_oid_context ctx;
	struct child_process cmd = CHILD_PROCESS_INIT;
	FILE *out;
	struct strbuf line = STRBUF_INIT;

	prepare_pack_objects(&cmd, args, packtmp);
	cmd.in = -1;

	/*
	 * NEEDSWORK: Giving pack-objects only the OIDs without any ordering
	 * hints may result in suboptimal deltas in the resulting pack. See if
	 * the OIDs can be sent with fake paths such that pack-objects can use a
	 * {type -> existing pack order} ordering when computing deltas instead
	 * of a {type -> size} ordering, which may produce better deltas.
	 */
	ctx.cmd = &cmd;
	ctx.algop = repo->hash_algo;
	for_each_packed_object(repo, write_oid, &ctx,
			       FOR_EACH_OBJECT_PROMISOR_ONLY);

	if (cmd.in == -1) {
		/* No packed objects; cmd was never started */
		child_process_clear(&cmd);
		return;
	}

	close(cmd.in);

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;
		char *promisor_name;

		if (line.len != repo->hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only from pack-objects."));
		item = string_list_append(names, line.buf);

		/*
		 * pack-objects creates the .pack and .idx files, but not the
		 * .promisor file. Create the .promisor file, which is empty.
		 *
		 * NEEDSWORK: fetch-pack sometimes generates non-empty
		 * .promisor files containing the ref names and associated
		 * hashes at the point of generation of the corresponding
		 * packfile, but this would not preserve their contents. Maybe
		 * concatenate the contents of all .promisor files instead of
		 * just creating a new empty file.
		 */
		promisor_name = mkpathdup("%s-%s.promisor", packtmp,
					  line.buf);
		write_promisor_file(promisor_name, NULL, 0);

		item->util = generated_pack_populate(item->string, packtmp);

		free(promisor_name);
	}

	fclose(out);
	if (finish_command(&cmd))
		die(_("could not finish pack-objects to repack promisor objects"));
	strbuf_release(&line);
}
