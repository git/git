#include "cache.h"

static const char *alias_key;
static char *alias_val;

static int alias_lookup_cb(const char *k, const char *v, void *cb)
{
	if (!prefixcmp(k, "alias.") && !strcmp(k+6, alias_key)) {
		if (!v)
			return config_error_nonbool(k);
		alias_val = xstrdup(v);
		return 0;
	}
	return 0;
}

char *alias_lookup(const char *alias)
{
	alias_key = alias;
	alias_val = NULL;
	git_config(alias_lookup_cb, NULL);
	return alias_val;
}

#define SPLIT_CMDLINE_BAD_ENDING 1
#define SPLIT_CMDLINE_UNCLOSED_QUOTE 2
static const char *split_cmdline_errors[] = {
	"cmdline ends with \\",
	"unclosed quote"
};

int split_cmdline(char *cmdline, const char ***argv)
{
	int src, dst, count = 0, size = 16;
	char quoted = 0;

	*argv = xmalloc(sizeof(char *) * size);

	/* split alias_string */
	(*argv)[count++] = cmdline;
	for (src = dst = 0; cmdline[src];) {
		char c = cmdline[src];
		if (!quoted && isspace(c)) {
			cmdline[dst++] = 0;
			while (cmdline[++src]
					&& isspace(cmdline[src]))
				; /* skip */
			ALLOC_GROW(*argv, count+1, size);
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
					free(*argv);
					*argv = NULL;
					return -SPLIT_CMDLINE_BAD_ENDING;
				}
			}
			cmdline[dst++] = c;
			src++;
		}
	}

	cmdline[dst] = 0;

	if (quoted) {
		free(*argv);
		*argv = NULL;
		return -SPLIT_CMDLINE_UNCLOSED_QUOTE;
	}

	ALLOC_GROW(*argv, count+1, size);
	(*argv)[count] = NULL;

	return count;
}

const char *split_cmdline_strerror(int split_cmdline_errno) {
	return split_cmdline_errors[-split_cmdline_errno-1];
}
