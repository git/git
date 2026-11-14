#include "git-compat-util.h"
#include "repack.h"
#include "repository.h"
#include "run-command.h"
#include "string-list.h"
#include "hex.h"
#include "packfile.h"
#include "list-objects-filter-options.h"
#include "list-objects-filter.h"
#include "odb.h"
#include "promisor-remote.h"

int write_filtered_pack(const struct write_pack_opts *opts,
			struct existing_packs *existing,
			struct string_list *names)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct string_list_item *item;
	FILE *in;
	int ret;
	const char *caret;
	const char *pack_prefix = write_pack_opts_pack_prefix(opts);

	prepare_pack_objects(&cmd, opts->po_args, opts->destination);

	strvec_push(&cmd.args, "--stdin-packs");

	for_each_string_list_item(item, &existing->kept_packs)
		strvec_pushf(&cmd.args, "--keep-pack=%s", item->string);

	cmd.in = -1;

	ret = start_command(&cmd);
	if (ret)
		return ret;

	/*
	 * Here 'names' contains only the pack(s) that were just
	 * written, which is exactly the packs we want to keep. Also
	 * 'existing_kept_packs' already contains the packs in
	 * 'keep_pack_list'.
	 */
	in = xfdopen(cmd.in, "w");
	for_each_string_list_item(item, names)
		fprintf(in, "^%s-%s.pack\n", pack_prefix, item->string);
	for_each_string_list_item(item, &existing->non_kept_packs)
		fprintf(in, "%s.pack\n", item->string);
	for_each_string_list_item(item, &existing->cruft_packs)
		fprintf(in, "%s.pack\n", item->string);
	caret = opts->po_args->pack_kept_objects ? "" : "^";
	for_each_string_list_item(item, &existing->kept_packs)
		fprintf(in, "%s%s.pack\n", caret, item->string);
	fclose(in);

	return finish_pack_objects_cmd(existing->repo->hash_algo, opts, &cmd,
				       names);
}

struct collect_cb_data {
	struct repository *repo;
	struct oidset *set;
};

static int collect_promisor_blob(const struct object_id *oid,
				 struct object_info *oi UNUSED,
				 void *cb_data)
{
	struct collect_cb_data *data = cb_data;
	struct object_info info = OBJECT_INFO_INIT;
	enum object_type type;

	info.typep = &type;

	/*
	 * Use OBJECT_INFO_SKIP_FETCH_OBJECT to avoid triggering a
	 * lazy fetch while collecting promisor blobs.
	 */
	if (odb_read_object_info_extended(data->repo->objects, oid, &info,
			OBJECT_INFO_SKIP_FETCH_OBJECT) < 0)
		return 0;

	if (type == OBJ_BLOB)
		oidset_insert(data->set, oid);

	return 0;
}

int enumerate_promisor_blobs(struct repository *repo,
			     const struct list_objects_filter_options *filter,
			     struct oidset *to_drop)
{
	struct oidset all_promisor_blobs = OIDSET_INIT;
	struct collect_cb_data cb = {
		.repo = repo,
		.set = &all_promisor_blobs
	};
	int ret = 0;

	/*
	 * The caller (cmd_repack) is responsible for validating that a
	 * blob:limit filter and a promisor remote are present before
	 * calling this function.
	 *
	 * Walk only promisor objects. Every object visited here is a
	 * promisor object, so it is recoverable from the promisor remote
	 * as long as the remote still has it, the same assumption the rest
	 * of partial clone relies on.
	 *
	 * We do not use write_filtered_pack() here because git repack
	 * routes promisor objects through repack_promisor_objects()
	 * before the filter machinery runs, so the filtered pack never
	 * contains promisor blobs. Direct enumeration via
	 * ODB_FOR_EACH_OBJECT_PROMISOR_ONLY is the correct approach.
	 */
	ret = odb_for_each_object(repo->objects, NULL,
			collect_promisor_blob, &cb,
			ODB_FOR_EACH_OBJECT_PROMISOR_ONLY);
	if (ret)
		goto cleanup;

	/*
	 * Apply the filter to find which blobs exceed the threshold.
	 * The caller has to_drop and is responsible for clearing it.
	 */
	ret = list_objects_filter__filter_oidset(repo,
		filter,
		&all_promisor_blobs,
		to_drop);

cleanup:
	oidset_clear(&all_promisor_blobs);
	return ret;
}
