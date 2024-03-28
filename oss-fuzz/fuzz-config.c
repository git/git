#include "git-compat-util.h"
#include "config.h"

int LLVMFuzzerTestOneInput(const uint8_t *, size_t);
static int config_parser_callback(const char *, const char *,
					const struct config_context *, void *);

static int config_parser_callback(const char *key, const char *value,
					const struct config_context *ctx UNUSED,
					void *data UNUSED)
{
	/*
	 * Visit every byte of memory we are given to make sure the parser
	 * gave it to us appropriately. We need to unconditionally return 0,
	 * but we also want to prevent the strlen from being optimized away.
	 */
	size_t c = strlen(key);

	if (value)
		c += strlen(value);
	return c == SIZE_MAX;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, const size_t size)
{
	struct config_options config_opts = { 0 };

	config_opts.error_action = CONFIG_ERROR_SILENT;
	git_config_from_mem(config_parser_callback, CONFIG_ORIGIN_BLOB,
				"fuzztest-config", (const char *)data, size, NULL,
				CONFIG_SCOPE_UNKNOWN, &config_opts);
	return 0;
}
