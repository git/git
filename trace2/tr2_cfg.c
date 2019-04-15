#include "cache.h"
#include "config.h"
#include "trace2/tr2_cfg.h"
#include "trace2/tr2_sysenv.h"

static struct strbuf **tr2_cfg_patterns;
static int tr2_cfg_count_patterns;
static int tr2_cfg_loaded;

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
		struct strbuf *buf = *s;

		if (buf->len && buf->buf[buf->len - 1] == ',')
			strbuf_setlen(buf, buf->len - 1);
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

struct tr2_cfg_data {
	const char *file;
	int line;
};

/*
 * See if the given config key matches any of our patterns of interest.
 */
static int tr2_cfg_cb(const char *key, const char *value, void *d)
{
	struct strbuf **s;
	struct tr2_cfg_data *data = (struct tr2_cfg_data *)d;

	for (s = tr2_cfg_patterns; *s; s++) {
		struct strbuf *buf = *s;
		int wm = wildmatch(buf->buf, key, WM_CASEFOLD);
		if (wm == WM_MATCH) {
			trace2_def_param_fl(data->file, data->line, key, value);
			return 0;
		}
	}

	return 0;
}

void tr2_cfg_list_config_fl(const char *file, int line)
{
	struct tr2_cfg_data data = { file, line };

	if (tr2_cfg_load_patterns() > 0)
		read_early_config(tr2_cfg_cb, &data);
}

void tr2_cfg_set_fl(const char *file, int line, const char *key,
		    const char *value)
{
	struct tr2_cfg_data data = { file, line };

	if (tr2_cfg_load_patterns() > 0)
		tr2_cfg_cb(key, value, &data);
}
