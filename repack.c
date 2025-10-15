#include "git-compat-util.h"
#include "repack.h"
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
