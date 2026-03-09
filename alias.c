#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "alias.h"
#include "config.h"
#include "gettext.h"
#include "strbuf.h"
#include "string-list.h"

struct config_alias_data {
	const char *alias;
	char *v;
	struct string_list *list;
};

static int config_alias_cb(const char *var, const char *value,
			   const struct config_context *ctx UNUSED, void *d)
{
	struct config_alias_data *data = d;
	const char *subsection, *key;
	size_t subsection_len;

	if (parse_config_key(var, "alias", &subsection, &subsection_len,
			     &key) < 0)
		return 0;

	/*
	 * Two config syntaxes:
	 * - alias.name = value   (without subsection, case-insensitive)
	 * - [alias "name"]
	 *       command = value  (with subsection, case-sensitive)
	 */
	if (subsection && strcmp(key, "command"))
		return 0;

	if (data->alias) {
		int match;

		if (subsection)
			match = (strlen(data->alias) == subsection_len &&
				 !strncmp(data->alias, subsection,
					  subsection_len));
		else
			match = !strcasecmp(data->alias, key);

		if (match) {
			FREE_AND_NULL(data->v);
			return git_config_string(&data->v,
						 var, value);
		}
	} else if (data->list) {
		struct string_list_item *item;

		if (!value)
			return config_error_nonbool(var);

		if (subsection)
			item = string_list_append_nodup(data->list,
				xmemdupz(subsection, subsection_len));
		else
			item = string_list_append(data->list, key);
		item->util = xstrdup(value);
	}

	return 0;
}

char *alias_lookup(const char *alias)
{
	struct config_alias_data data = { alias, NULL };

	read_early_config(the_repository, config_alias_cb, &data);

	return data.v;
}

void list_aliases(struct string_list *list)
{
	struct config_alias_data data = { NULL, NULL, list };

	read_early_config(the_repository, config_alias_cb, &data);
}

void quote_cmdline(struct strbuf *buf, const char **argv)
{
	for (const char **argp = argv; *argp; argp++) {
		if (argp != argv)
			strbuf_addch(buf, ' ');
		strbuf_addch(buf, '"');
		for (const char *p = *argp; *p; p++) {
			const char c = *p;

			if (c == '"' || c =='\\')
				strbuf_addch(buf, '\\');
			strbuf_addch(buf, c);
		}
		strbuf_addch(buf, '"');
	}
}

#define SPLIT_CMDLINE_BAD_ENDING 1
#define SPLIT_CMDLINE_UNCLOSED_QUOTE 2
#define SPLIT_CMDLINE_ARGC_OVERFLOW 3
static const char *split_cmdline_errors[] = {
	N_("cmdline ends with \\"),
	N_("unclosed quote"),
	N_("too many arguments"),
};

int split_cmdline(char *cmdline, const char ***argv)
{
	size_t src, dst, count = 0, size = 16;
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

	if (count >= INT_MAX) {
		FREE_AND_NULL(*argv);
		return -SPLIT_CMDLINE_ARGC_OVERFLOW;
	}

	ALLOC_GROW(*argv, count + 1, size);
	(*argv)[count] = NULL;

	return count;
}

const char *split_cmdline_strerror(int split_cmdline_errno)
{
	return split_cmdline_errors[-split_cmdline_errno - 1];
}
