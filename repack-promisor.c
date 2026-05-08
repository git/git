#include "git-compat-util.h"
#include "repack.h"
#include "hash.h"
#include "hex.h"
#include "odb.h"
#include "pack.h"
#include "packfile.h"
#include "path.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "strmap.h"
#include "strvec.h"

struct write_oid_context {
	struct child_process *cmd;
	const struct git_hash_algo *algop;
};

/*
 * Write oid to the given struct child_process's stdin, starting it first if
 * necessary.
 */
static int write_oid(const struct object_id *oid,
		     struct object_info *oi UNUSED,
		     void *data)
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

/*
 * Go through all .promisor files contained in repo (excluding those whose name
 * appears in not_repacked_basenames, which acts as a ignorelist), and copies
 * their content inside the destination file "<packtmp>-<dest_hex>.promisor".
 * Each line of a never repacked .promisor file is: "<oid> <ref>" (as described
 * in the write_promisor_file() function).
 * After a repack, the copied lines will be: "<oid> <ref> <time>", where <time>
 * is the time (in Unix time) at which the .promisor file was last modified.
 * Only the lines whose <oid> is present inside "<packtmp>-<dest_hex>.idx" will
 * be copied.
 * The contents of all .promisor files are assumed to be correctly formed.
 */
static void copy_promisor_content(struct repository *repo,
					      const char *dest_hex,
					      const char *packtmp,
					      struct strset *not_repacked_basenames)
{
	char *dest_idx_name;
	char *dest_promisor_name;
	FILE *dest;
	struct strset dest_content = STRSET_INIT;
	struct strbuf dest_to_write = STRBUF_INIT;
	struct strbuf source_promisor_name = STRBUF_INIT;
	struct strbuf line = STRBUF_INIT;
	struct object_id dest_oid;
	struct packed_git *dest_pack, *p;
	int err;

	dest_idx_name = mkpathdup("%s-%s.idx", packtmp, dest_hex);
	get_oid_hex_algop(dest_hex, &dest_oid, repo->hash_algo);
	dest_pack = parse_pack_index(repo, dest_oid.hash, dest_idx_name);
	if (!dest_pack)
		BUG("parse_pack_index() failed.");

	/* Open the .promisor dest file, and fill dest_content with its content */
	dest_promisor_name = mkpathdup("%s-%s.promisor", packtmp, dest_hex);
	dest = xfopen(dest_promisor_name, "r+");
	while (strbuf_getline(&line, dest) != EOF)
		strset_add(&dest_content, line.buf);

	repo_for_each_pack(repo, p) {
		FILE *source;
		struct stat source_stat;

		if (!p->pack_promisor)
			continue;

		if (not_repacked_basenames &&
			strset_contains(not_repacked_basenames, pack_basename(p)))
			continue;

		strbuf_reset(&source_promisor_name);
		strbuf_addstr(&source_promisor_name, p->pack_name);
		strbuf_strip_suffix(&source_promisor_name, ".pack");
		strbuf_addstr(&source_promisor_name, ".promisor");

		if (stat(source_promisor_name.buf, &source_stat))
			die(_("File not found: %s"), source_promisor_name.buf);

		source = xfopen(source_promisor_name.buf, "r");

		while (strbuf_getline(&line, source) != EOF) {
			struct string_list line_sections = STRING_LIST_INIT_DUP;
			struct object_id oid;

			/* Split line into <oid>, <ref> and <time> (if <time> exists) */
			string_list_split(&line_sections, line.buf, " ", 3);

			/* Ignore the lines where <oid> doesn't appear in the dest_pack */
			get_oid_hex_algop(line_sections.items[0].string, &oid, repo->hash_algo);
			if (!find_pack_entry_one(&oid, dest_pack)) {
				string_list_clear(&line_sections, 0);
				continue;
			}

			/* If <time> doesn't exist, retrieve it and add it to line */
			if (line_sections.nr < 3)
				strbuf_addf(&line, " %" PRItime,
					    (timestamp_t)source_stat.st_mtime);

			/*
			 * Add the finalized line to dest_to_write and dest_content if it
			 * wasn't already present inside dest_content
			 */
			if (strset_add(&dest_content, line.buf)) {
				strbuf_addbuf(&dest_to_write, &line);
				strbuf_addch(&dest_to_write, '\n');
			}

			string_list_clear(&line_sections, 0);
		}

		err = ferror(source);
		err |= fclose(source);
		if (err)
			die(_("Could not read '%s' promisor file"), source_promisor_name.buf);
	}

	/* If dest_to_write is not empty, then there are new lines to append */
	if (dest_to_write.len) {
		if (fseek(dest, 0L, SEEK_END))
			die_errno(_("fseek failed"));
		fprintf(dest, "%s", dest_to_write.buf);
	}

	err = ferror(dest);
	err |= fclose(dest);
	if (err)
		die(_("Could not write '%s' promisor file"), dest_promisor_name);

	close_pack_index(dest_pack);
	free(dest_pack);
	free(dest_idx_name);
	free(dest_promisor_name);
	strset_clear(&dest_content);
	strbuf_release(&dest_to_write);
	strbuf_release(&source_promisor_name);
	strbuf_release(&line);
}

static void finish_repacking_promisor_objects(struct repository *repo,
					      struct child_process *cmd,
					      struct string_list *names,
					      const char *packtmp,
					      struct strset *not_repacked_basenames)
{
	struct strbuf line = STRBUF_INIT;
	FILE *out;

	close(cmd->in);

	out = xfdopen(cmd->out, "r");
	while (strbuf_getline_lf(&line, out) != EOF) {
		struct string_list_item *item;
		char *promisor_name;

		if (line.len != repo->hash_algo->hexsz)
			die(_("repack: Expecting full hex object ID lines only from pack-objects."));
		item = string_list_append(names, line.buf);

		/*
		 * pack-objects creates the .pack and .idx files, but not the
		 * .promisor file. Create the .promisor file.
		 */
		promisor_name = mkpathdup("%s-%s.promisor", packtmp,
					  line.buf);
		write_promisor_file(promisor_name, NULL, 0);

		/* Now let's fill the content of the newly created .promisor file */
		copy_promisor_content(repo, line.buf, packtmp, not_repacked_basenames);

		item->util = generated_pack_populate(item->string, packtmp);

		free(promisor_name);
	}

	fclose(out);
	if (finish_command(cmd))
		die(_("could not finish pack-objects to repack promisor objects"));
	strbuf_release(&line);
}

void repack_promisor_objects(struct repository *repo,
			     const struct pack_objects_args *args,
			     struct string_list *names, const char *packtmp)
{
	struct write_oid_context ctx;
	struct child_process cmd = CHILD_PROCESS_INIT;

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
	odb_for_each_object(repo->objects, NULL, write_oid, &ctx,
			    ODB_FOR_EACH_OBJECT_PROMISOR_ONLY);

	if (cmd.in == -1) {
		/* No packed objects; cmd was never started */
		child_process_clear(&cmd);
		return;
	}

	finish_repacking_promisor_objects(repo, &cmd, names, packtmp, NULL);
}

void pack_geometry_repack_promisors(struct repository *repo,
				    const struct pack_objects_args *args,
				    const struct pack_geometry *geometry,
				    struct string_list *names,
				    const char *packtmp)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	FILE *in;
	struct strset not_repacked_basenames = STRSET_INIT;

	if (!geometry->promisor_split)
		return;

	prepare_pack_objects(&cmd, args, packtmp);
	strvec_push(&cmd.args, "--stdin-packs");
	cmd.in = -1;
	if (start_command(&cmd))
		die(_("could not start pack-objects to repack promisor packs"));

	in = xfdopen(cmd.in, "w");
	for (size_t i = 0; i < geometry->promisor_split; i++)
		fprintf(in, "%s\n", pack_basename(geometry->promisor_pack[i]));
	for (size_t i = geometry->promisor_split; i < geometry->promisor_pack_nr; i++) {
		const char *name = pack_basename(geometry->promisor_pack[i]);
		fprintf(in, "^%s\n", name);
		strset_add(&not_repacked_basenames, name);
	}
	fclose(in);

	finish_repacking_promisor_objects(repo, &cmd, names, packtmp,
			strset_get_size(&not_repacked_basenames) ? &not_repacked_basenames : NULL);

	strset_clear(&not_repacked_basenames);
}
