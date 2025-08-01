#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "strbuf.h"
#include "trace2.h"
#include "trace2/tr2_cfg.h"
#include "trace2/tr2_sysenv.h"
#include "wildmatch.h"

static struct string_list tr2_cfg_patterns = STRING_LIST_INIT_DUP;
static int tr2_cfg_loaded;

static struct string_list tr2_cfg_env_vars = STRING_LIST_INIT_DUP;
static int tr2_cfg_env_vars_loaded;

/*
 * Parse a string containing a comma-delimited list of config keys
 * or wildcard patterns into a string list.
 */
static size_t tr2_cfg_load_patterns(void)
{
	const char *envvar;

	if (tr2_cfg_loaded)
		return tr2_cfg_patterns.nr;
	tr2_cfg_loaded = 1;

	envvar = tr2_sysenv_get(TR2_SYSENV_CFG_PARAM);
	if (!envvar || !*envvar)
		return tr2_cfg_patterns.nr;

	string_list_split_f(&tr2_cfg_patterns, envvar, ",", -1,
			    STRING_LIST_SPLIT_TRIM);
	return tr2_cfg_patterns.nr;
}

void tr2_cfg_free_patterns(void)
{
	if (tr2_cfg_patterns.nr)
		string_list_clear(&tr2_cfg_patterns, 0);
	tr2_cfg_loaded = 0;
}

/*
 * Parse a string containing a comma-delimited list of environment variable
 * names into a string list.
 */
static size_t tr2_load_env_vars(void)
{
	const char *varlist;

	if (tr2_cfg_env_vars_loaded)
		return tr2_cfg_env_vars.nr;
	tr2_cfg_env_vars_loaded = 1;

	varlist = tr2_sysenv_get(TR2_SYSENV_ENV_VARS);
	if (!varlist || !*varlist)
		return tr2_cfg_env_vars.nr;

	string_list_split_f(&tr2_cfg_env_vars, varlist, ",", -1,
			    STRING_LIST_SPLIT_TRIM);
	return tr2_cfg_env_vars.nr;
}

void tr2_cfg_free_env_vars(void)
{
	if (tr2_cfg_env_vars.nr)
		string_list_clear(&tr2_cfg_env_vars, 0);
	tr2_cfg_env_vars_loaded = 0;
}

struct tr2_cfg_data {
	const char *file;
	int line;
};

/*
 * See if the given config key matches any of our patterns of interest.
 */
static int tr2_cfg_cb(const char *key, const char *value,
		      const struct config_context *ctx, void *d)
{
	struct string_list_item *item;
	struct tr2_cfg_data *data = (struct tr2_cfg_data *)d;

	for_each_string_list_item(item, &tr2_cfg_patterns) {
		int wm = wildmatch(item->string, key, WM_CASEFOLD);
		if (wm == WM_MATCH) {
			trace2_def_param_fl(data->file, data->line, key, value,
					    ctx->kvi);
			return 0;
		}
	}

	return 0;
}

void tr2_cfg_list_config_fl(const char *file, int line)
{
	struct tr2_cfg_data data = { file, line };

	if (tr2_cfg_load_patterns() > 0)
		read_early_config(the_repository, tr2_cfg_cb, &data);
}

void tr2_list_env_vars_fl(const char *file, int line)
{
	struct key_value_info kvi = KVI_INIT;
	struct string_list_item *item;

	kvi_from_param(&kvi);
	if (tr2_load_env_vars() <= 0)
		return;

	for_each_string_list_item(item, &tr2_cfg_env_vars) {
		const char *val = getenv(item->string);
		if (val && *val)
			trace2_def_param_fl(file, line, item->string, val, &kvi);
	}
}

void tr2_cfg_set_fl(const char *file, int line, const char *key,
		    const char *value)
{
	struct key_value_info kvi = KVI_INIT;
	struct config_context ctx = {
		.kvi = &kvi,
	};
	struct tr2_cfg_data data = { file, line };

	if (tr2_cfg_load_patterns() > 0)
		tr2_cfg_cb(key, value, &ctx, &data);
}
