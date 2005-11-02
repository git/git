
#include "cache.h"

#define MAXNAME (256)

static FILE *config_file;
static int config_linenr;
static int get_next_char(void)
{
	int c;
	FILE *f;

	c = '\n';
	if ((f = config_file) != NULL) {
		c = fgetc(f);
		if (c == '\r') {
			/* DOS like systems */
			c = fgetc(f);
			if (c != '\n') {
				ungetc(c, f);
				c = '\r';
			}
		}
		if (c == '\n')
			config_linenr++;
		if (c == EOF) {
			config_file = NULL;
			c = '\n';
		}
	}
	return c;
}

static char *parse_value(void)
{
	static char value[1024];
	int quote = 0, comment = 0, len = 0, space = 0;

	for (;;) {
		int c = get_next_char();
		if (len >= sizeof(value))
			return NULL;
		if (c == '\n') {
			if (quote)
				return NULL;
			value[len] = 0;
			return value;
		}
		if (comment)
			continue;
		if (isspace(c) && !quote) {
			space = 1;
			continue;
		}
		if (space) {
			if (len)
				value[len++] = ' ';
			space = 0;
		}
		if (c == '\\') {
			c = get_next_char();
			switch (c) {
			case '\n':
				continue;
			case 't':
				c = '\t';
				break;
			case 'b':
				c = '\b';
				break;
			case 'n':
				c = '\n';
				break;
			/* Some characters escape as themselves */
			case '\\': case '"':
				break;
			/* Reject unknown escape sequences */
			default:
				return NULL;
			}
			value[len++] = c;
			continue;
		}
		if (c == '"') {
			quote = 1-quote;
			continue;
		}
		if (!quote) {
			if (c == ';' || c == '#') {
				comment = 1;
				continue;
			}
		}
		value[len++] = c;
	}
}

static int get_value(config_fn_t fn, char *name, unsigned int len)
{
	int c;
	char *value;

	/* Get the full name */
	for (;;) {
		c = get_next_char();
		if (c == EOF)
			break;
		if (!isalnum(c))
			break;
		name[len++] = tolower(c);
		if (len >= MAXNAME)
			return -1;
	}
	name[len] = 0;
	while (c == ' ' || c == '\t')
		c = get_next_char();

	value = NULL;
	if (c != '\n') {
		if (c != '=')
			return -1;
		value = parse_value();
		if (!value)
			return -1;
	}
	return fn(name, value);
}

static int get_base_var(char *name)
{
	int baselen = 0;

	for (;;) {
		int c = get_next_char();
		if (c == EOF)
			return -1;
		if (c == ']')
			return baselen;
		if (!isalnum(c))
			return -1;
		if (baselen > MAXNAME / 2)
			return -1;
		name[baselen++] = tolower(c);
	}
}

static int git_parse_file(config_fn_t fn)
{
	int comment = 0;
	int baselen = 0;
	static char var[MAXNAME];

	for (;;) {
		int c = get_next_char();
		if (c == '\n') {
			/* EOF? */
			if (!config_file)
				return 0;
			comment = 0;
			continue;
		}
		if (comment || isspace(c))
			continue;
		if (c == '#' || c == ';') {
			comment = 1;
			continue;
		}
		if (c == '[') {
			baselen = get_base_var(var);
			if (baselen <= 0)
				break;
			var[baselen++] = '.';
			var[baselen] = 0;
			continue;
		}
		if (!isalpha(c))
			break;
		var[baselen] = tolower(c);
		if (get_value(fn, var, baselen+1) < 0)
			break;
	}
	die("bad config file line %d", config_linenr);
}

int git_config_int(const char *name, const char *value)
{
	if (value && *value) {
		char *end;
		int val = strtol(value, &end, 0);
		if (!*end)
			return val;
	}
	die("bad config value for '%s'", name);
}

int git_config_bool(const char *name, const char *value)
{
	if (!value)
		return 1;
	if (!*value)
		return 0;
	if (!strcasecmp(value, "true"))
		return 1;
	if (!strcasecmp(value, "false"))
		return 0;
	return git_config_int(name, value) != 0;
}

int git_default_config(const char *var, const char *value)
{
	/* This needs a better name */
	if (!strcmp(var, "core.filemode")) {
		trust_executable_bit = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "user.name")) {
		strncpy(git_default_name, value, sizeof(git_default_name));
		return 0;
	}

	if (!strcmp(var, "user.email")) {
		strncpy(git_default_email, value, sizeof(git_default_email));
		return 0;
	}

	/* Add other config variables here.. */
	return 0;
}

int git_config(config_fn_t fn)
{
	int ret;
	FILE *f = fopen(git_path("config"), "r");

	ret = -1;
	if (f) {
		config_file = f;
		config_linenr = 1;
		ret = git_parse_file(fn);
		fclose(f);
	}
	return ret;
}
