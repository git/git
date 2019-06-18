#include "cache.h"
#include "alias.h"
#include "config.h"
#include "string-list.h"

struct config_alias_data {
	const char *alias;
	char *v;
	struct string_list *list;
};

static int config_alias_cb(const char *key, const char *value, void *d)
{
	struct config_alias_data *data = d;
	const char *p;

	if (!skip_prefix(key, "alias.", &p))
		return 0;

	if (data->alias) {
		if (!strcasecmp(p, data->alias))
			return git_config_string((const char **)&data->v,
						 key, value);
	} else if (data->list) {
		string_list_append(data->list, p);
	}

	return 0;
}

char *alias_lookup(const char *alias)
{
	struct config_alias_data data = { alias, NULL };

	read_early_config(config_alias_cb, &data);

	return data.v;
}

void list_aliases(struct string_list *list)
{
	struct config_alias_data data = { NULL, NULL, list };

	read_early_config(config_alias_cb, &data);
}

#define SPLIT_CMDLINE_BAD_ENDING 1
#define SPLIT_CMDLINE_UNCLOSED_QUOTE 2
static const char *split_cmdline_errors[] = {
	N_("cmdline ends with \\"),
	N_("unclosed quote")
};

int split_cmdline(char *cmdline, const char ***argv)
{
	int src, dst, count = 0, size = 16;
	char quoted = 0;

	ALLOC_ARRAY(*argv, size);

	/* split alias_string */
	(*argv)[count++] = cmdline;
	for (src = dst = 0; cmdline[src];) {
		char c = cmdline[src];
		if (!quoted && isspace(c)) {
			cmdline[dst++] = 0;
			while (cmdline[++src]
					&& isspace(cmdline[src]))
				; /* skip */
			ALLOC_GROW(*argv, count + 1, size);
			(*argv)[count++] = cmdline + dst;
		} else if (!quoted && (c == '\'' || c == '"')) {
			quoted = c;
			src++;
		} else if (c == quoted) {
			quoted = 0;
			src++;
		} else {
			if (c == '\\' && quoted != '\'') {
				src++;
				c = cmdline[src];
				if (!c) {
					FREE_AND_NULL(*argv);
					return -SPLIT_CMDLINE_BAD_ENDING;
				}
			}
			cmdline[dst++] = c;
			src++;
		}
	}

	cmdline[dst] = 0;

	if (quoted) {
		FREE_AND_NULL(*argv);
		return -SPLIT_CMDLINE_UNCLOSED_QUOTE;
	}

	ALLOC_GROW(*argv, count + 1, size);
	(*argv)[count] = NULL;

	return count;
}

const char *split_cmdline_strerror(int split_cmdline_errno)
{
	return split_cmdline_errors[-split_cmdline_errno - 1];
}
