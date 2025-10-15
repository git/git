#include "git-compat-util.h"
#include "midx.h"
#include "odb.h"
#include "packfile.h"
#include "repack.h"
#include "repository.h"
#include "run-command.h"

void prepare_pack_objects(struct child_process *cmd,
			  const struct pack_objects_args *args,
			  const char *out)
{
	strvec_push(&cmd->args, "pack-objects");
	if (args->window)
		strvec_pushf(&cmd->args, "--window=%s", args->window);
	if (args->window_memory)
		strvec_pushf(&cmd->args, "--window-memory=%s", args->window_memory);
	if (args->depth)
		strvec_pushf(&cmd->args, "--depth=%s", args->depth);
	if (args->threads)
		strvec_pushf(&cmd->args, "--threads=%s", args->threads);
	if (args->max_pack_size)
		strvec_pushf(&cmd->args, "--max-pack-size=%lu", args->max_pack_size);
	if (args->no_reuse_delta)
		strvec_pushf(&cmd->args, "--no-reuse-delta");
	if (args->no_reuse_object)
		strvec_pushf(&cmd->args, "--no-reuse-object");
	if (args->name_hash_version)
		strvec_pushf(&cmd->args, "--name-hash-version=%d", args->name_hash_version);
	if (args->path_walk)
		strvec_pushf(&cmd->args, "--path-walk");
	if (args->local)
		strvec_push(&cmd->args,  "--local");
	if (args->quiet)
		strvec_push(&cmd->args,  "--quiet");
	if (args->delta_base_offset)
		strvec_push(&cmd->args,  "--delta-base-offset");
	strvec_push(&cmd->args, out);
	cmd->git_cmd = 1;
	cmd->out = -1;
}

void pack_objects_args_release(struct pack_objects_args *args)
{
	free(args->window);
	free(args->window_memory);
	free(args->depth);
	free(args->threads);
	list_objects_filter_release(&args->filter_options);
}

void repack_remove_redundant_pack(struct repository *repo, const char *dir_name,
				  const char *base_name)
{
	struct strbuf buf = STRBUF_INIT;
	struct odb_source *source = repo->objects->sources;
	struct multi_pack_index *m = get_multi_pack_index(source);
	strbuf_addf(&buf, "%s.pack", base_name);
	if (m && source->local && midx_contains_pack(m, buf.buf))
		clear_midx_file(repo);
	strbuf_insertf(&buf, 0, "%s/", dir_name);
	unlink_pack_path(buf.buf, 1);
	strbuf_release(&buf);
}
