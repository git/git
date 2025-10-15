#include "git-compat-util.h"
#include "repack.h"

void pack_objects_args_release(struct pack_objects_args *args)
{
	free(args->window);
	free(args->window_memory);
	free(args->depth);
	free(args->threads);
	list_objects_filter_release(&args->filter_options);
}
