#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "strbuf.h"
#include "trace2.h"
#include "trace2/tr2_cfg.h"
#include "trace2/tr2_sysenv.h"
#include "wildmatch.h"

static struct strbuf **tr2_cfg_patterns;
static int tr2_cfg_count_patterns;
static int tr2_cfg_loaded;

static struct strbuf **tr2_cfg_env_vars;
static int tr2_cfg_env_vars_count;
static int tr2_cfg_env_vars_loaded;

/*
 * Parse a string containing a comma-delimited list of config keys
 * or wildcard patterns into a list of strbufs.
 */
static int tr2_cfg_load_patterns(void)
{
	struct strbuf **s;
	const char *envvar;

	if (tr2_cfg_loaded)
		return tr2_cfg_count_patterns;
	tr2_cfg_loaded = 1;

	envvar = tr2_sysenv_get(TR2_SYSENV_CFG_PARAM);
	if (!envvar || !*envvar)
		return tr2_cfg_count_patterns;

	tr2_cfg_patterns = strbuf_split_buf(envvar, strlen(envvar), ',', -1);
	for (s = tr2_cfg_patterns; *s; s++) {
		strbuf_trim_trailing_ch(*s, ',');
		strbuf_trim_trailing_newline(*s);
		strbuf_trim(*s);
	}

	tr2_cfg_count_patterns = s - tr2_cfg_patterns;
	return tr2_cfg_count_patterns;
}

void tr2_cfg_free_patterns(void)
{
	if (tr2_cfg_patterns)
		strbuf_list_free(tr2_cfg_patterns);
	tr2_cfg_count_patterns = 0;
	tr2_cfg_loaded = 0;
}

/*
 * Parse a string containing a comma-delimited list of environment variable
 * names into a list of strbufs.
 */
static int tr2_load_env_vars(void)
{
	struct strbuf **s;
	const char *varlist;

	if (tr2_cfg_env_vars_loaded)
		return tr2_cfg_env_vars_count;
	tr2_cfg_env_vars_loaded = 1;

	varlist = tr2_sysenv_get(TR2_SYSENV_ENV_VARS);
	if (!varlist || !*varlist)
		return tr2_cfg_env_vars_count;

	tr2_cfg_env_vars = strbuf_split_buf(varlist, strlen(varlist), ',', -1);
	for (s = tr2_cfg_env_vars; *s; s++) {
		strbuf_trim_trailing_ch(*s, ',');
		strbuf_trim_trailing_newline(*s);
		strbuf_trim(*s);
	}

	tr2_cfg_env_vars_count = s - tr2_cfg_env_vars;
	return tr2_cfg_env_vars_count;
}

void tr2_cfg_free_env_vars(void)
{
	if (tr2_cfg_env_vars)
		strbuf_list_free(tr2_cfg_env_vars);
	tr2_cfg_env_vars_count = 0;
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
	struct strbuf **s;
	struct tr2_cfg_data *data = (struct tr2_cfg_data *)d;

	for (s = tr2_cfg_patterns; *s; s++) {
		struct strbuf *buf = *s;
		int wm = wildmatch(buf->buf, key, WM_CASEFOLD);
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
	struct strbuf **s;

	kvi_from_param(&kvi);
	if (tr2_load_env_vars() <= 0)
		return;

	for (s = tr2_cfg_env_vars; *s; s++) {
		struct strbuf *buf = *s;
		const char *val = getenv(buf->buf);
		if (val && *val)
			trace2_def_param_fl(file, line, buf->buf, val, &kvi);
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
