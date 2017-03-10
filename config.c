/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Johannes Schindelin, 2005
 *
 */
#include "cache.h"
#include "lockfile.h"
#include "exec_cmd.h"
#include "strbuf.h"
#include "quote.h"
#include "hashmap.h"
#include "string-list.h"
#include "utf8.h"

struct config_source {
	struct config_source *prev;
	union {
		FILE *file;
		struct config_buf {
			const char *buf;
			size_t len;
			size_t pos;
		} buf;
	} u;
	enum config_origin_type origin_type;
	const char *name;
	const char *path;
	int die_on_error;
	int linenr;
	int eof;
	struct strbuf value;
	struct strbuf var;

	int (*do_fgetc)(struct config_source *c);
	int (*do_ungetc)(int c, struct config_source *conf);
	long (*do_ftell)(struct config_source *c);
};

/*
 * These variables record the "current" config source, which
 * can be accessed by parsing callbacks.
 *
 * The "cf" variable will be non-NULL only when we are actually parsing a real
 * config source (file, blob, cmdline, etc).
 *
 * The "current_config_kvi" variable will be non-NULL only when we are feeding
 * cached config from a configset into a callback.
 *
 * They should generally never be non-NULL at the same time. If they are both
 * NULL, then we aren't parsing anything (and depending on the function looking
 * at the variables, it's either a bug for it to be called in the first place,
 * or it's a function which can be reused for non-config purposes, and should
 * fall back to some sane behavior).
 */
static struct config_source *cf;
static struct key_value_info *current_config_kvi;

/*
 * Similar to the variables above, this gives access to the "scope" of the
 * current value (repo, global, etc). For cached values, it can be found via
 * the current_config_kvi as above. During parsing, the current value can be
 * found in this variable. It's not part of "cf" because it transcends a single
 * file (i.e., a file included from .git/config is still in "repo" scope).
 */
static enum config_scope current_parsing_scope;

static int core_compression_seen;
static int pack_compression_seen;
static int zlib_compression_seen;

/*
 * Default config_set that contains key-value pairs from the usual set of config
 * config files (i.e repo specific .git/config, user wide ~/.gitconfig, XDG
 * config file and the global /etc/gitconfig)
 */
static struct config_set the_config_set;

static int config_file_fgetc(struct config_source *conf)
{
	return getc_unlocked(conf->u.file);
}

static int config_file_ungetc(int c, struct config_source *conf)
{
	return ungetc(c, conf->u.file);
}

static long config_file_ftell(struct config_source *conf)
{
	return ftell(conf->u.file);
}


static int config_buf_fgetc(struct config_source *conf)
{
	if (conf->u.buf.pos < conf->u.buf.len)
		return conf->u.buf.buf[conf->u.buf.pos++];

	return EOF;
}

static int config_buf_ungetc(int c, struct config_source *conf)
{
	if (conf->u.buf.pos > 0) {
		conf->u.buf.pos--;
		if (conf->u.buf.buf[conf->u.buf.pos] != c)
			die("BUG: config_buf can only ungetc the same character");
		return c;
	}

	return EOF;
}

static long config_buf_ftell(struct config_source *conf)
{
	return conf->u.buf.pos;
}

#define MAX_INCLUDE_DEPTH 10
static const char include_depth_advice[] =
"exceeded maximum include depth (%d) while including\n"
"	%s\n"
"from\n"
"	%s\n"
"Do you have circular includes?";
static int handle_path_include(const char *path, struct config_include_data *inc)
{
	int ret = 0;
	struct strbuf buf = STRBUF_INIT;
	char *expanded;

	if (!path)
		return config_error_nonbool("include.path");

	expanded = expand_user_path(path);
	if (!expanded)
		return error("could not expand include path '%s'", path);
	path = expanded;

	/*
	 * Use an absolute path as-is, but interpret relative paths
	 * based on the including config file.
	 */
	if (!is_absolute_path(path)) {
		char *slash;

		if (!cf || !cf->path)
			return error("relative config includes must come from files");

		slash = find_last_dir_sep(cf->path);
		if (slash)
			strbuf_add(&buf, cf->path, slash - cf->path + 1);
		strbuf_addstr(&buf, path);
		path = buf.buf;
	}

	if (!access_or_die(path, R_OK, 0)) {
		if (++inc->depth > MAX_INCLUDE_DEPTH)
			die(include_depth_advice, MAX_INCLUDE_DEPTH, path,
			    !cf ? "<unknown>" :
			    cf->name ? cf->name :
			    "the command line");
		ret = git_config_from_file(git_config_include, path, inc);
		inc->depth--;
	}
	strbuf_release(&buf);
	free(expanded);
	return ret;
}

int git_config_include(const char *var, const char *value, void *data)
{
	struct config_include_data *inc = data;
	int ret;

	/*
	 * Pass along all values, including "include" directives; this makes it
	 * possible to query information on the includes themselves.
	 */
	ret = inc->fn(var, value, inc->data);
	if (ret < 0)
		return ret;

	if (!strcmp(var, "include.path"))
		ret = handle_path_include(value, inc);
	return ret;
}

void git_config_push_parameter(const char *text)
{
	struct strbuf env = STRBUF_INIT;
	const char *old = getenv(CONFIG_DATA_ENVIRONMENT);
	if (old && *old) {
		strbuf_addstr(&env, old);
		strbuf_addch(&env, ' ');
	}
	sq_quote_buf(&env, text);
	setenv(CONFIG_DATA_ENVIRONMENT, env.buf, 1);
	strbuf_release(&env);
}

static inline int iskeychar(int c)
{
	return isalnum(c) || c == '-';
}

/*
 * Auxiliary function to sanity-check and split the key into the section
 * identifier and variable name.
 *
 * Returns 0 on success, -1 when there is an invalid character in the key and
 * -2 if there is no section name in the key.
 *
 * store_key - pointer to char* which will hold a copy of the key with
 *             lowercase section and variable name
 * baselen - pointer to int which will hold the length of the
 *           section + subsection part, can be NULL
 */
static int git_config_parse_key_1(const char *key, char **store_key, int *baselen_, int quiet)
{
	int i, dot, baselen;
	const char *last_dot = strrchr(key, '.');

	/*
	 * Since "key" actually contains the section name and the real
	 * key name separated by a dot, we have to know where the dot is.
	 */

	if (last_dot == NULL || last_dot == key) {
		if (!quiet)
			error("key does not contain a section: %s", key);
		return -CONFIG_NO_SECTION_OR_NAME;
	}

	if (!last_dot[1]) {
		if (!quiet)
			error("key does not contain variable name: %s", key);
		return -CONFIG_NO_SECTION_OR_NAME;
	}

	baselen = last_dot - key;
	if (baselen_)
		*baselen_ = baselen;

	/*
	 * Validate the key and while at it, lower case it for matching.
	 */
	if (store_key)
		*store_key = xmallocz(strlen(key));

	dot = 0;
	for (i = 0; key[i]; i++) {
		unsigned char c = key[i];
		if (c == '.')
			dot = 1;
		/* Leave the extended basename untouched.. */
		if (!dot || i > baselen) {
			if (!iskeychar(c) ||
			    (i == baselen + 1 && !isalpha(c))) {
				if (!quiet)
					error("invalid key: %s", key);
				goto out_free_ret_1;
			}
			c = tolower(c);
		} else if (c == '\n') {
			if (!quiet)
				error("invalid key (newline): %s", key);
			goto out_free_ret_1;
		}
		if (store_key)
			(*store_key)[i] = c;
	}

	return 0;

out_free_ret_1:
	if (store_key) {
		free(*store_key);
		*store_key = NULL;
	}
	return -CONFIG_INVALID_KEY;
}

int git_config_parse_key(const char *key, char **store_key, int *baselen)
{
	return git_config_parse_key_1(key, store_key, baselen, 0);
}

int git_config_key_is_valid(const char *key)
{
	return !git_config_parse_key_1(key, NULL, NULL, 1);
}

int git_config_parse_parameter(const char *text,
			       config_fn_t fn, void *data)
{
	const char *value;
	char *canonical_name;
	struct strbuf **pair;
	int ret;

	pair = strbuf_split_str(text, '=', 2);
	if (!pair[0])
		return error("bogus config parameter: %s", text);

	if (pair[0]->len && pair[0]->buf[pair[0]->len - 1] == '=') {
		strbuf_setlen(pair[0], pair[0]->len - 1);
		value = pair[1] ? pair[1]->buf : "";
	} else {
		value = NULL;
	}

	strbuf_trim(pair[0]);
	if (!pair[0]->len) {
		strbuf_list_free(pair);
		return error("bogus config parameter: %s", text);
	}

	if (git_config_parse_key(pair[0]->buf, &canonical_name, NULL)) {
		ret = -1;
	} else {
		ret = (fn(canonical_name, value, data) < 0) ? -1 : 0;
		free(canonical_name);
	}
	strbuf_list_free(pair);
	return ret;
}

int git_config_from_parameters(config_fn_t fn, void *data)
{
	const char *env = getenv(CONFIG_DATA_ENVIRONMENT);
	int ret = 0;
	char *envw;
	const char **argv = NULL;
	int nr = 0, alloc = 0;
	int i;
	struct config_source source;

	if (!env)
		return 0;

	memset(&source, 0, sizeof(source));
	source.prev = cf;
	source.origin_type = CONFIG_ORIGIN_CMDLINE;
	cf = &source;

	/* sq_dequote will write over it */
	envw = xstrdup(env);

	if (sq_dequote_to_argv(envw, &argv, &nr, &alloc) < 0) {
		ret = error("bogus format in " CONFIG_DATA_ENVIRONMENT);
		goto out;
	}

	for (i = 0; i < nr; i++) {
		if (git_config_parse_parameter(argv[i], fn, data) < 0) {
			ret = -1;
			goto out;
		}
	}

out:
	free(argv);
	free(envw);
	cf = source.prev;
	return ret;
}

static int get_next_char(void)
{
	int c = cf->do_fgetc(cf);

	if (c == '\r') {
		/* DOS like systems */
		c = cf->do_fgetc(cf);
		if (c != '\n') {
			if (c != EOF)
				cf->do_ungetc(c, cf);
			c = '\r';
		}
	}
	if (c == '\n')
		cf->linenr++;
	if (c == EOF) {
		cf->eof = 1;
		cf->linenr++;
		c = '\n';
	}
	return c;
}

static char *parse_value(void)
{
	int quote = 0, comment = 0, space = 0;

	strbuf_reset(&cf->value);
	for (;;) {
		int c = get_next_char();
		if (c == '\n') {
			if (quote) {
				cf->linenr--;
				return NULL;
			}
			return cf->value.buf;
		}
		if (comment)
			continue;
		if (isspace(c) && !quote) {
			if (cf->value.len)
				space++;
			continue;
		}
		if (!quote) {
			if (c == ';' || c == '#') {
				comment = 1;
				continue;
			}
		}
		for (; space; space--)
			strbuf_addch(&cf->value, ' ');
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
			strbuf_addch(&cf->value, c);
			continue;
		}
		if (c == '"') {
			quote = 1-quote;
			continue;
		}
		strbuf_addch(&cf->value, c);
	}
}

static int get_value(config_fn_t fn, void *data, struct strbuf *name)
{
	int c;
	char *value;
	int ret;

	/* Get the full name */
	for (;;) {
		c = get_next_char();
		if (cf->eof)
			break;
		if (!iskeychar(c))
			break;
		strbuf_addch(name, tolower(c));
	}

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
	/*
	 * We already consumed the \n, but we need linenr to point to
	 * the line we just parsed during the call to fn to get
	 * accurate line number in error messages.
	 */
	cf->linenr--;
	ret = fn(name->buf, value, data);
	cf->linenr++;
	return ret;
}

static int get_extended_base_var(struct strbuf *name, int c)
{
	do {
		if (c == '\n')
			goto error_incomplete_line;
		c = get_next_char();
	} while (isspace(c));

	/* We require the format to be '[base "extension"]' */
	if (c != '"')
		return -1;
	strbuf_addch(name, '.');

	for (;;) {
		int c = get_next_char();
		if (c == '\n')
			goto error_incomplete_line;
		if (c == '"')
			break;
		if (c == '\\') {
			c = get_next_char();
			if (c == '\n')
				goto error_incomplete_line;
		}
		strbuf_addch(name, c);
	}

	/* Final ']' */
	if (get_next_char() != ']')
		return -1;
	return 0;
error_incomplete_line:
	cf->linenr--;
	return -1;
}

static int get_base_var(struct strbuf *name)
{
	for (;;) {
		int c = get_next_char();
		if (cf->eof)
			return -1;
		if (c == ']')
			return 0;
		if (isspace(c))
			return get_extended_base_var(name, c);
		if (!iskeychar(c) && c != '.')
			return -1;
		strbuf_addch(name, tolower(c));
	}
}

static int git_parse_source(config_fn_t fn, void *data)
{
	int comment = 0;
	int baselen = 0;
	struct strbuf *var = &cf->var;
	int error_return = 0;
	char *error_msg = NULL;

	/* U+FEFF Byte Order Mark in UTF8 */
	const char *bomptr = utf8_bom;

	for (;;) {
		int c = get_next_char();
		if (bomptr && *bomptr) {
			/* We are at the file beginning; skip UTF8-encoded BOM
			 * if present. Sane editors won't put this in on their
			 * own, but e.g. Windows Notepad will do it happily. */
			if (c == (*bomptr & 0377)) {
				bomptr++;
				continue;
			} else {
				/* Do not tolerate partial BOM. */
				if (bomptr != utf8_bom)
					break;
				/* No BOM at file beginning. Cool. */
				bomptr = NULL;
			}
		}
		if (c == '\n') {
			if (cf->eof)
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
			/* Reset prior to determining a new stem */
			strbuf_reset(var);
			if (get_base_var(var) < 0 || var->len < 1)
				break;
			strbuf_addch(var, '.');
			baselen = var->len;
			continue;
		}
		if (!isalpha(c))
			break;
		/*
		 * Truncate the var name back to the section header
		 * stem prior to grabbing the suffix part of the name
		 * and the value.
		 */
		strbuf_setlen(var, baselen);
		strbuf_addch(var, tolower(c));
		if (get_value(fn, data, var) < 0)
			break;
	}

	switch (cf->origin_type) {
	case CONFIG_ORIGIN_BLOB:
		error_msg = xstrfmt(_("bad config line %d in blob %s"),
				      cf->linenr, cf->name);
		break;
	case CONFIG_ORIGIN_FILE:
		error_msg = xstrfmt(_("bad config line %d in file %s"),
				      cf->linenr, cf->name);
		break;
	case CONFIG_ORIGIN_STDIN:
		error_msg = xstrfmt(_("bad config line %d in standard input"),
				      cf->linenr);
		break;
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		error_msg = xstrfmt(_("bad config line %d in submodule-blob %s"),
				       cf->linenr, cf->name);
		break;
	case CONFIG_ORIGIN_CMDLINE:
		error_msg = xstrfmt(_("bad config line %d in command line %s"),
				       cf->linenr, cf->name);
		break;
	default:
		error_msg = xstrfmt(_("bad config line %d in %s"),
				      cf->linenr, cf->name);
	}

	if (cf->die_on_error)
		die("%s", error_msg);
	else
		error_return = error("%s", error_msg);

	free(error_msg);
	return error_return;
}

static int parse_unit_factor(const char *end, uintmax_t *val)
{
	if (!*end)
		return 1;
	else if (!strcasecmp(end, "k")) {
		*val *= 1024;
		return 1;
	}
	else if (!strcasecmp(end, "m")) {
		*val *= 1024 * 1024;
		return 1;
	}
	else if (!strcasecmp(end, "g")) {
		*val *= 1024 * 1024 * 1024;
		return 1;
	}
	return 0;
}

static int git_parse_signed(const char *value, intmax_t *ret, intmax_t max)
{
	if (value && *value) {
		char *end;
		intmax_t val;
		uintmax_t uval;
		uintmax_t factor = 1;

		errno = 0;
		val = strtoimax(value, &end, 0);
		if (errno == ERANGE)
			return 0;
		if (!parse_unit_factor(end, &factor)) {
			errno = EINVAL;
			return 0;
		}
		uval = labs(val);
		uval *= factor;
		if (uval > max || labs(val) > uval) {
			errno = ERANGE;
			return 0;
		}
		val *= factor;
		*ret = val;
		return 1;
	}
	errno = EINVAL;
	return 0;
}

static int git_parse_unsigned(const char *value, uintmax_t *ret, uintmax_t max)
{
	if (value && *value) {
		char *end;
		uintmax_t val;
		uintmax_t oldval;

		errno = 0;
		val = strtoumax(value, &end, 0);
		if (errno == ERANGE)
			return 0;
		oldval = val;
		if (!parse_unit_factor(end, &val)) {
			errno = EINVAL;
			return 0;
		}
		if (val > max || oldval > val) {
			errno = ERANGE;
			return 0;
		}
		*ret = val;
		return 1;
	}
	errno = EINVAL;
	return 0;
}

static int git_parse_int(const char *value, int *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int)))
		return 0;
	*ret = tmp;
	return 1;
}

static int git_parse_int64(const char *value, int64_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int64_t)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_ulong(const char *value, unsigned long *ret)
{
	uintmax_t tmp;
	if (!git_parse_unsigned(value, &tmp, maximum_unsigned_value_of_type(long)))
		return 0;
	*ret = tmp;
	return 1;
}

NORETURN
static void die_bad_number(const char *name, const char *value)
{
	const char * error_type = (errno == ERANGE)? _("out of range"):_("invalid unit");

	if (!value)
		value = "";

	if (!(cf && cf->name))
		die(_("bad numeric config value '%s' for '%s': %s"),
		    value, name, error_type);

	switch (cf->origin_type) {
	case CONFIG_ORIGIN_BLOB:
		die(_("bad numeric config value '%s' for '%s' in blob %s: %s"),
		    value, name, cf->name, error_type);
	case CONFIG_ORIGIN_FILE:
		die(_("bad numeric config value '%s' for '%s' in file %s: %s"),
		    value, name, cf->name, error_type);
	case CONFIG_ORIGIN_STDIN:
		die(_("bad numeric config value '%s' for '%s' in standard input: %s"),
		    value, name, error_type);
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		die(_("bad numeric config value '%s' for '%s' in submodule-blob %s: %s"),
		    value, name, cf->name, error_type);
	case CONFIG_ORIGIN_CMDLINE:
		die(_("bad numeric config value '%s' for '%s' in command line %s: %s"),
		    value, name, cf->name, error_type);
	default:
		die(_("bad numeric config value '%s' for '%s' in %s: %s"),
		    value, name, cf->name, error_type);
	}
}

int git_config_int(const char *name, const char *value)
{
	int ret;
	if (!git_parse_int(value, &ret))
		die_bad_number(name, value);
	return ret;
}

int64_t git_config_int64(const char *name, const char *value)
{
	int64_t ret;
	if (!git_parse_int64(value, &ret))
		die_bad_number(name, value);
	return ret;
}

unsigned long git_config_ulong(const char *name, const char *value)
{
	unsigned long ret;
	if (!git_parse_ulong(value, &ret))
		die_bad_number(name, value);
	return ret;
}

int git_parse_maybe_bool(const char *value)
{
	if (!value)
		return 1;
	if (!*value)
		return 0;
	if (!strcasecmp(value, "true")
	    || !strcasecmp(value, "yes")
	    || !strcasecmp(value, "on"))
		return 1;
	if (!strcasecmp(value, "false")
	    || !strcasecmp(value, "no")
	    || !strcasecmp(value, "off"))
		return 0;
	return -1;
}

int git_config_maybe_bool(const char *name, const char *value)
{
	int v = git_parse_maybe_bool(value);
	if (0 <= v)
		return v;
	if (git_parse_int(value, &v))
		return !!v;
	return -1;
}

int git_config_bool_or_int(const char *name, const char *value, int *is_bool)
{
	int v = git_parse_maybe_bool(value);
	if (0 <= v) {
		*is_bool = 1;
		return v;
	}
	*is_bool = 0;
	return git_config_int(name, value);
}

int git_config_bool(const char *name, const char *value)
{
	int discard;
	return !!git_config_bool_or_int(name, value, &discard);
}

int git_config_string(const char **dest, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	*dest = xstrdup(value);
	return 0;
}

int git_config_pathname(const char **dest, const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	*dest = expand_user_path(value);
	if (!*dest)
		die(_("failed to expand user dir in: '%s'"), value);
	return 0;
}

static int git_default_core_config(const char *var, const char *value)
{
	/* This needs a better name */
	if (!strcmp(var, "core.filemode")) {
		trust_executable_bit = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "core.trustctime")) {
		trust_ctime = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "core.checkstat")) {
		if (!strcasecmp(value, "default"))
			check_stat = 1;
		else if (!strcasecmp(value, "minimal"))
			check_stat = 0;
	}

	if (!strcmp(var, "core.quotepath")) {
		quote_path_fully = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.symlinks")) {
		has_symlinks = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.ignorecase")) {
		ignore_case = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.attributesfile"))
		return git_config_pathname(&git_attributes_file, var, value);

	if (!strcmp(var, "core.hookspath"))
		return git_config_pathname(&git_hooks_path, var, value);

	if (!strcmp(var, "core.bare")) {
		is_bare_repository_cfg = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.ignorestat")) {
		assume_unchanged = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.prefersymlinkrefs")) {
		prefer_symlink_refs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.logallrefupdates")) {
		if (value && !strcasecmp(value, "always"))
			log_all_ref_updates = LOG_REFS_ALWAYS;
		else if (git_config_bool(var, value))
			log_all_ref_updates = LOG_REFS_NORMAL;
		else
			log_all_ref_updates = LOG_REFS_NONE;
		return 0;
	}

	if (!strcmp(var, "core.warnambiguousrefs")) {
		warn_ambiguous_refs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.abbrev")) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcasecmp(value, "auto"))
			default_abbrev = -1;
		else {
			int abbrev = git_config_int(var, value);
			if (abbrev < minimum_abbrev || abbrev > 40)
				return error("abbrev length out of range: %d", abbrev);
			default_abbrev = abbrev;
		}
		return 0;
	}

	if (!strcmp(var, "core.disambiguate"))
		return set_disambiguate_hint_config(var, value);

	if (!strcmp(var, "core.loosecompression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad zlib compression level %d"), level);
		zlib_compression_level = level;
		zlib_compression_seen = 1;
		return 0;
	}

	if (!strcmp(var, "core.compression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad zlib compression level %d"), level);
		core_compression_level = level;
		core_compression_seen = 1;
		if (!zlib_compression_seen)
			zlib_compression_level = level;
		if (!pack_compression_seen)
			pack_compression_level = level;
		return 0;
	}

	if (!strcmp(var, "core.packedgitwindowsize")) {
		int pgsz_x2 = getpagesize() * 2;
		packed_git_window_size = git_config_ulong(var, value);

		/* This value must be multiple of (pagesize * 2) */
		packed_git_window_size /= pgsz_x2;
		if (packed_git_window_size < 1)
			packed_git_window_size = 1;
		packed_git_window_size *= pgsz_x2;
		return 0;
	}

	if (!strcmp(var, "core.bigfilethreshold")) {
		big_file_threshold = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "core.packedgitlimit")) {
		packed_git_limit = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "core.deltabasecachelimit")) {
		delta_base_cache_limit = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "core.autocrlf")) {
		if (value && !strcasecmp(value, "input")) {
			auto_crlf = AUTO_CRLF_INPUT;
			return 0;
		}
		auto_crlf = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.safecrlf")) {
		if (value && !strcasecmp(value, "warn")) {
			safe_crlf = SAFE_CRLF_WARN;
			return 0;
		}
		safe_crlf = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.eol")) {
		if (value && !strcasecmp(value, "lf"))
			core_eol = EOL_LF;
		else if (value && !strcasecmp(value, "crlf"))
			core_eol = EOL_CRLF;
		else if (value && !strcasecmp(value, "native"))
			core_eol = EOL_NATIVE;
		else
			core_eol = EOL_UNSET;
		return 0;
	}

	if (!strcmp(var, "core.notesref")) {
		notes_ref_name = xstrdup(value);
		return 0;
	}

	if (!strcmp(var, "core.editor"))
		return git_config_string(&editor_program, var, value);

	if (!strcmp(var, "core.commentchar")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcasecmp(value, "auto"))
			auto_comment_line_char = 1;
		else if (value[0] && !value[1]) {
			comment_line_char = value[0];
			auto_comment_line_char = 0;
		} else
			return error("core.commentChar should only be one character");
		return 0;
	}

	if (!strcmp(var, "core.askpass"))
		return git_config_string(&askpass_program, var, value);

	if (!strcmp(var, "core.excludesfile"))
		return git_config_pathname(&excludes_file, var, value);

	if (!strcmp(var, "core.whitespace")) {
		if (!value)
			return config_error_nonbool(var);
		whitespace_rule_cfg = parse_whitespace_rule(value);
		return 0;
	}

	if (!strcmp(var, "core.fsyncobjectfiles")) {
		fsync_object_files = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.preloadindex")) {
		core_preload_index = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.createobject")) {
		if (!strcmp(value, "rename"))
			object_creation_mode = OBJECT_CREATION_USES_RENAMES;
		else if (!strcmp(value, "link"))
			object_creation_mode = OBJECT_CREATION_USES_HARDLINKS;
		else
			die(_("invalid mode for object creation: %s"), value);
		return 0;
	}

	if (!strcmp(var, "core.sparsecheckout")) {
		core_apply_sparse_checkout = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.precomposeunicode")) {
		precomposed_unicode = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.protecthfs")) {
		protect_hfs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.protectntfs")) {
		protect_ntfs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.hidedotfiles")) {
		if (value && !strcasecmp(value, "dotgitonly"))
			hide_dotfiles = HIDE_DOTFILES_DOTGITONLY;
		else
			hide_dotfiles = git_config_bool(var, value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_i18n_config(const char *var, const char *value)
{
	if (!strcmp(var, "i18n.commitencoding"))
		return git_config_string(&git_commit_encoding, var, value);

	if (!strcmp(var, "i18n.logoutputencoding"))
		return git_config_string(&git_log_output_encoding, var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_branch_config(const char *var, const char *value)
{
	if (!strcmp(var, "branch.autosetupmerge")) {
		if (value && !strcasecmp(value, "always")) {
			git_branch_track = BRANCH_TRACK_ALWAYS;
			return 0;
		}
		git_branch_track = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "branch.autosetuprebase")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcmp(value, "never"))
			autorebase = AUTOREBASE_NEVER;
		else if (!strcmp(value, "local"))
			autorebase = AUTOREBASE_LOCAL;
		else if (!strcmp(value, "remote"))
			autorebase = AUTOREBASE_REMOTE;
		else if (!strcmp(value, "always"))
			autorebase = AUTOREBASE_ALWAYS;
		else
			return error("malformed value for %s", var);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_push_config(const char *var, const char *value)
{
	if (!strcmp(var, "push.default")) {
		if (!value)
			return config_error_nonbool(var);
		else if (!strcmp(value, "nothing"))
			push_default = PUSH_DEFAULT_NOTHING;
		else if (!strcmp(value, "matching"))
			push_default = PUSH_DEFAULT_MATCHING;
		else if (!strcmp(value, "simple"))
			push_default = PUSH_DEFAULT_SIMPLE;
		else if (!strcmp(value, "upstream"))
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "tracking")) /* deprecated */
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "current"))
			push_default = PUSH_DEFAULT_CURRENT;
		else {
			error("malformed value for %s: %s", var, value);
			return error("Must be one of nothing, matching, simple, "
				     "upstream or current.");
		}
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_mailmap_config(const char *var, const char *value)
{
	if (!strcmp(var, "mailmap.file"))
		return git_config_pathname(&git_mailmap_file, var, value);
	if (!strcmp(var, "mailmap.blob"))
		return git_config_string(&git_mailmap_blob, var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int git_default_config(const char *var, const char *value, void *dummy)
{
	if (starts_with(var, "core."))
		return git_default_core_config(var, value);

	if (starts_with(var, "user."))
		return git_ident_config(var, value, dummy);

	if (starts_with(var, "i18n."))
		return git_default_i18n_config(var, value);

	if (starts_with(var, "branch."))
		return git_default_branch_config(var, value);

	if (starts_with(var, "push."))
		return git_default_push_config(var, value);

	if (starts_with(var, "mailmap."))
		return git_default_mailmap_config(var, value);

	if (starts_with(var, "advice."))
		return git_default_advice_config(var, value);

	if (!strcmp(var, "pager.color") || !strcmp(var, "color.pager")) {
		pager_use_color = git_config_bool(var,value);
		return 0;
	}

	if (!strcmp(var, "pack.packsizelimit")) {
		pack_size_limit_cfg = git_config_ulong(var, value);
		return 0;
	}

	if (!strcmp(var, "pack.compression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die(_("bad pack compression level %d"), level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

/*
 * All source specific fields in the union, die_on_error, name and the callbacks
 * fgetc, ungetc, ftell of top need to be initialized before calling
 * this function.
 */
static int do_config_from(struct config_source *top, config_fn_t fn, void *data)
{
	int ret;

	/* push config-file parsing state stack */
	top->prev = cf;
	top->linenr = 1;
	top->eof = 0;
	strbuf_init(&top->value, 1024);
	strbuf_init(&top->var, 1024);
	cf = top;

	ret = git_parse_source(fn, data);

	/* pop config-file parsing state stack */
	strbuf_release(&top->value);
	strbuf_release(&top->var);
	cf = top->prev;

	return ret;
}

static int do_config_from_file(config_fn_t fn,
		const enum config_origin_type origin_type,
		const char *name, const char *path, FILE *f,
		void *data)
{
	struct config_source top;

	top.u.file = f;
	top.origin_type = origin_type;
	top.name = name;
	top.path = path;
	top.die_on_error = 1;
	top.do_fgetc = config_file_fgetc;
	top.do_ungetc = config_file_ungetc;
	top.do_ftell = config_file_ftell;

	return do_config_from(&top, fn, data);
}

static int git_config_from_stdin(config_fn_t fn, void *data)
{
	return do_config_from_file(fn, CONFIG_ORIGIN_STDIN, "", NULL, stdin, data);
}

int git_config_from_file(config_fn_t fn, const char *filename, void *data)
{
	int ret = -1;
	FILE *f;

	f = fopen(filename, "r");
	if (f) {
		flockfile(f);
		ret = do_config_from_file(fn, CONFIG_ORIGIN_FILE, filename, filename, f, data);
		funlockfile(f);
		fclose(f);
	}
	return ret;
}

int git_config_from_mem(config_fn_t fn, const enum config_origin_type origin_type,
			const char *name, const char *buf, size_t len, void *data)
{
	struct config_source top;

	top.u.buf.buf = buf;
	top.u.buf.len = len;
	top.u.buf.pos = 0;
	top.origin_type = origin_type;
	top.name = name;
	top.path = NULL;
	top.die_on_error = 0;
	top.do_fgetc = config_buf_fgetc;
	top.do_ungetc = config_buf_ungetc;
	top.do_ftell = config_buf_ftell;

	return do_config_from(&top, fn, data);
}

int git_config_from_blob_sha1(config_fn_t fn,
			      const char *name,
			      const unsigned char *sha1,
			      void *data)
{
	enum object_type type;
	char *buf;
	unsigned long size;
	int ret;

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf)
		return error("unable to load config blob object '%s'", name);
	if (type != OBJ_BLOB) {
		free(buf);
		return error("reference '%s' does not point to a blob", name);
	}

	ret = git_config_from_mem(fn, CONFIG_ORIGIN_BLOB, name, buf, size, data);
	free(buf);

	return ret;
}

static int git_config_from_blob_ref(config_fn_t fn,
				    const char *name,
				    void *data)
{
	unsigned char sha1[20];

	if (get_sha1(name, sha1) < 0)
		return error("unable to resolve config blob '%s'", name);
	return git_config_from_blob_sha1(fn, name, sha1, data);
}

const char *git_etc_gitconfig(void)
{
	static const char *system_wide;
	if (!system_wide)
		system_wide = system_path(ETC_GITCONFIG);
	return system_wide;
}

/*
 * Parse environment variable 'k' as a boolean (in various
 * possible spellings); if missing, use the default value 'def'.
 */
int git_env_bool(const char *k, int def)
{
	const char *v = getenv(k);
	return v ? git_config_bool(k, v) : def;
}

/*
 * Parse environment variable 'k' as ulong with possibly a unit
 * suffix; if missing, use the default value 'val'.
 */
unsigned long git_env_ulong(const char *k, unsigned long val)
{
	const char *v = getenv(k);
	if (v && !git_parse_ulong(v, &val))
		die("failed to parse %s", k);
	return val;
}

int git_config_system(void)
{
	return !git_env_bool("GIT_CONFIG_NOSYSTEM", 0);
}

static int do_git_config_sequence(config_fn_t fn, void *data)
{
	int ret = 0;
	char *xdg_config = xdg_config_home("config");
	char *user_config = expand_user_path("~/.gitconfig");
	char *repo_config = have_git_dir() ? git_pathdup("config") : NULL;

	current_parsing_scope = CONFIG_SCOPE_SYSTEM;
	if (git_config_system() && !access_or_die(git_etc_gitconfig(), R_OK, 0))
		ret += git_config_from_file(fn, git_etc_gitconfig(),
					    data);

	current_parsing_scope = CONFIG_SCOPE_GLOBAL;
	if (xdg_config && !access_or_die(xdg_config, R_OK, ACCESS_EACCES_OK))
		ret += git_config_from_file(fn, xdg_config, data);

	if (user_config && !access_or_die(user_config, R_OK, ACCESS_EACCES_OK))
		ret += git_config_from_file(fn, user_config, data);

	current_parsing_scope = CONFIG_SCOPE_REPO;
	if (repo_config && !access_or_die(repo_config, R_OK, 0))
		ret += git_config_from_file(fn, repo_config, data);

	current_parsing_scope = CONFIG_SCOPE_CMDLINE;
	if (git_config_from_parameters(fn, data) < 0)
		die(_("unable to parse command-line config"));

	current_parsing_scope = CONFIG_SCOPE_UNKNOWN;
	free(xdg_config);
	free(user_config);
	free(repo_config);
	return ret;
}

int git_config_with_options(config_fn_t fn, void *data,
			    struct git_config_source *config_source,
			    int respect_includes)
{
	struct config_include_data inc = CONFIG_INCLUDE_INIT;

	if (respect_includes) {
		inc.fn = fn;
		inc.data = data;
		fn = git_config_include;
		data = &inc;
	}

	/*
	 * If we have a specific filename, use it. Otherwise, follow the
	 * regular lookup sequence.
	 */
	if (config_source && config_source->use_stdin)
		return git_config_from_stdin(fn, data);
	else if (config_source && config_source->file)
		return git_config_from_file(fn, config_source->file, data);
	else if (config_source && config_source->blob)
		return git_config_from_blob_ref(fn, config_source->blob, data);

	return do_git_config_sequence(fn, data);
}

static void git_config_raw(config_fn_t fn, void *data)
{
	if (git_config_with_options(fn, data, NULL, 1) < 0)
		/*
		 * git_config_with_options() normally returns only
		 * zero, as most errors are fatal, and
		 * non-fatal potential errors are guarded by "if"
		 * statements that are entered only when no error is
		 * possible.
		 *
		 * If we ever encounter a non-fatal error, it means
		 * something went really wrong and we should stop
		 * immediately.
		 */
		die(_("unknown error occurred while reading the configuration files"));
}

static void configset_iter(struct config_set *cs, config_fn_t fn, void *data)
{
	int i, value_index;
	struct string_list *values;
	struct config_set_element *entry;
	struct configset_list *list = &cs->list;

	for (i = 0; i < list->nr; i++) {
		entry = list->items[i].e;
		value_index = list->items[i].value_index;
		values = &entry->value_list;

		current_config_kvi = values->items[value_index].util;

		if (fn(entry->key, values->items[value_index].string, data) < 0)
			git_die_config_linenr(entry->key,
					      current_config_kvi->filename,
					      current_config_kvi->linenr);

		current_config_kvi = NULL;
	}
}

static void git_config_check_init(void);

void git_config(config_fn_t fn, void *data)
{
	git_config_check_init();
	configset_iter(&the_config_set, fn, data);
}

static struct config_set_element *configset_find_element(struct config_set *cs, const char *key)
{
	struct config_set_element k;
	struct config_set_element *found_entry;
	char *normalized_key;
	/*
	 * `key` may come from the user, so normalize it before using it
	 * for querying entries from the hashmap.
	 */
	if (git_config_parse_key(key, &normalized_key, NULL))
		return NULL;

	hashmap_entry_init(&k, strhash(normalized_key));
	k.key = normalized_key;
	found_entry = hashmap_get(&cs->config_hash, &k, NULL);
	free(normalized_key);
	return found_entry;
}

static int configset_add_value(struct config_set *cs, const char *key, const char *value)
{
	struct config_set_element *e;
	struct string_list_item *si;
	struct configset_list_item *l_item;
	struct key_value_info *kv_info = xmalloc(sizeof(*kv_info));

	e = configset_find_element(cs, key);
	/*
	 * Since the keys are being fed by git_config*() callback mechanism, they
	 * are already normalized. So simply add them without any further munging.
	 */
	if (!e) {
		e = xmalloc(sizeof(*e));
		hashmap_entry_init(e, strhash(key));
		e->key = xstrdup(key);
		string_list_init(&e->value_list, 1);
		hashmap_add(&cs->config_hash, e);
	}
	si = string_list_append_nodup(&e->value_list, xstrdup_or_null(value));

	ALLOC_GROW(cs->list.items, cs->list.nr + 1, cs->list.alloc);
	l_item = &cs->list.items[cs->list.nr++];
	l_item->e = e;
	l_item->value_index = e->value_list.nr - 1;

	if (!cf)
		die("BUG: configset_add_value has no source");
	if (cf->name) {
		kv_info->filename = strintern(cf->name);
		kv_info->linenr = cf->linenr;
		kv_info->origin_type = cf->origin_type;
	} else {
		/* for values read from `git_config_from_parameters()` */
		kv_info->filename = NULL;
		kv_info->linenr = -1;
		kv_info->origin_type = CONFIG_ORIGIN_CMDLINE;
	}
	kv_info->scope = current_parsing_scope;
	si->util = kv_info;

	return 0;
}

static int config_set_element_cmp(const struct config_set_element *e1,
				 const struct config_set_element *e2, const void *unused)
{
	return strcmp(e1->key, e2->key);
}

void git_configset_init(struct config_set *cs)
{
	hashmap_init(&cs->config_hash, (hashmap_cmp_fn)config_set_element_cmp, 0);
	cs->hash_initialized = 1;
	cs->list.nr = 0;
	cs->list.alloc = 0;
	cs->list.items = NULL;
}

void git_configset_clear(struct config_set *cs)
{
	struct config_set_element *entry;
	struct hashmap_iter iter;
	if (!cs->hash_initialized)
		return;

	hashmap_iter_init(&cs->config_hash, &iter);
	while ((entry = hashmap_iter_next(&iter))) {
		free(entry->key);
		string_list_clear(&entry->value_list, 1);
	}
	hashmap_free(&cs->config_hash, 1);
	cs->hash_initialized = 0;
	free(cs->list.items);
	cs->list.nr = 0;
	cs->list.alloc = 0;
	cs->list.items = NULL;
}

static int config_set_callback(const char *key, const char *value, void *cb)
{
	struct config_set *cs = cb;
	configset_add_value(cs, key, value);
	return 0;
}

int git_configset_add_file(struct config_set *cs, const char *filename)
{
	return git_config_from_file(config_set_callback, filename, cs);
}

int git_configset_get_value(struct config_set *cs, const char *key, const char **value)
{
	const struct string_list *values = NULL;
	/*
	 * Follows "last one wins" semantic, i.e., if there are multiple matches for the
	 * queried key in the files of the configset, the value returned will be the last
	 * value in the value list for that key.
	 */
	values = git_configset_get_value_multi(cs, key);

	if (!values)
		return 1;
	assert(values->nr > 0);
	*value = values->items[values->nr - 1].string;
	return 0;
}

const struct string_list *git_configset_get_value_multi(struct config_set *cs, const char *key)
{
	struct config_set_element *e = configset_find_element(cs, key);
	return e ? &e->value_list : NULL;
}

int git_configset_get_string_const(struct config_set *cs, const char *key, const char **dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value))
		return git_config_string(dest, key, value);
	else
		return 1;
}

int git_configset_get_string(struct config_set *cs, const char *key, char **dest)
{
	return git_configset_get_string_const(cs, key, (const char **)dest);
}

int git_configset_get_int(struct config_set *cs, const char *key, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_int(key, value);
		return 0;
	} else
		return 1;
}

int git_configset_get_ulong(struct config_set *cs, const char *key, unsigned long *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_ulong(key, value);
		return 0;
	} else
		return 1;
}

int git_configset_get_bool(struct config_set *cs, const char *key, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_bool(key, value);
		return 0;
	} else
		return 1;
}

int git_configset_get_bool_or_int(struct config_set *cs, const char *key,
				int *is_bool, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_bool_or_int(key, value, is_bool);
		return 0;
	} else
		return 1;
}

int git_configset_get_maybe_bool(struct config_set *cs, const char *key, int *dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value)) {
		*dest = git_config_maybe_bool(key, value);
		if (*dest == -1)
			return -1;
		return 0;
	} else
		return 1;
}

int git_configset_get_pathname(struct config_set *cs, const char *key, const char **dest)
{
	const char *value;
	if (!git_configset_get_value(cs, key, &value))
		return git_config_pathname(dest, key, value);
	else
		return 1;
}

static void git_config_check_init(void)
{
	if (the_config_set.hash_initialized)
		return;
	git_configset_init(&the_config_set);
	git_config_raw(config_set_callback, &the_config_set);
}

void git_config_clear(void)
{
	if (!the_config_set.hash_initialized)
		return;
	git_configset_clear(&the_config_set);
}

int git_config_get_value(const char *key, const char **value)
{
	git_config_check_init();
	return git_configset_get_value(&the_config_set, key, value);
}

const struct string_list *git_config_get_value_multi(const char *key)
{
	git_config_check_init();
	return git_configset_get_value_multi(&the_config_set, key);
}

int git_config_get_string_const(const char *key, const char **dest)
{
	int ret;
	git_config_check_init();
	ret = git_configset_get_string_const(&the_config_set, key, dest);
	if (ret < 0)
		git_die_config(key, NULL);
	return ret;
}

int git_config_get_string(const char *key, char **dest)
{
	git_config_check_init();
	return git_config_get_string_const(key, (const char **)dest);
}

int git_config_get_int(const char *key, int *dest)
{
	git_config_check_init();
	return git_configset_get_int(&the_config_set, key, dest);
}

int git_config_get_ulong(const char *key, unsigned long *dest)
{
	git_config_check_init();
	return git_configset_get_ulong(&the_config_set, key, dest);
}

int git_config_get_bool(const char *key, int *dest)
{
	git_config_check_init();
	return git_configset_get_bool(&the_config_set, key, dest);
}

int git_config_get_bool_or_int(const char *key, int *is_bool, int *dest)
{
	git_config_check_init();
	return git_configset_get_bool_or_int(&the_config_set, key, is_bool, dest);
}

int git_config_get_maybe_bool(const char *key, int *dest)
{
	git_config_check_init();
	return git_configset_get_maybe_bool(&the_config_set, key, dest);
}

int git_config_get_pathname(const char *key, const char **dest)
{
	int ret;
	git_config_check_init();
	ret = git_configset_get_pathname(&the_config_set, key, dest);
	if (ret < 0)
		git_die_config(key, NULL);
	return ret;
}

int git_config_get_untracked_cache(void)
{
	int val = -1;
	const char *v;

	/* Hack for test programs like test-dump-untracked-cache */
	if (ignore_untracked_cache_config)
		return -1;

	if (!git_config_get_maybe_bool("core.untrackedcache", &val))
		return val;

	if (!git_config_get_value("core.untrackedcache", &v)) {
		if (!strcasecmp(v, "keep"))
			return -1;

		error("unknown core.untrackedCache value '%s'; "
		      "using 'keep' default value", v);
		return -1;
	}

	return -1; /* default value */
}

NORETURN
void git_die_config_linenr(const char *key, const char *filename, int linenr)
{
	if (!filename)
		die(_("unable to parse '%s' from command-line config"), key);
	else
		die(_("bad config variable '%s' in file '%s' at line %d"),
		    key, filename, linenr);
}

NORETURN __attribute__((format(printf, 2, 3)))
void git_die_config(const char *key, const char *err, ...)
{
	const struct string_list *values;
	struct key_value_info *kv_info;

	if (err) {
		va_list params;
		va_start(params, err);
		vreportf("error: ", err, params);
		va_end(params);
	}
	values = git_config_get_value_multi(key);
	kv_info = values->items[values->nr - 1].util;
	git_die_config_linenr(key, kv_info->filename, kv_info->linenr);
}

/*
 * Find all the stuff for git_config_set() below.
 */

static struct {
	int baselen;
	char *key;
	int do_not_match;
	regex_t *value_regex;
	int multi_replace;
	size_t *offset;
	unsigned int offset_alloc;
	enum { START, SECTION_SEEN, SECTION_END_SEEN, KEY_SEEN } state;
	int seen;
} store;

static int matches(const char *key, const char *value)
{
	if (strcmp(key, store.key))
		return 0; /* not ours */
	if (!store.value_regex)
		return 1; /* always matches */
	if (store.value_regex == CONFIG_REGEX_NONE)
		return 0; /* never matches */

	return store.do_not_match ^
		(value && !regexec(store.value_regex, value, 0, NULL, 0));
}

static int store_aux(const char *key, const char *value, void *cb)
{
	const char *ep;
	size_t section_len;

	switch (store.state) {
	case KEY_SEEN:
		if (matches(key, value)) {
			if (store.seen == 1 && store.multi_replace == 0) {
				warning(_("%s has multiple values"), key);
			}

			ALLOC_GROW(store.offset, store.seen + 1,
				   store.offset_alloc);

			store.offset[store.seen] = cf->do_ftell(cf);
			store.seen++;
		}
		break;
	case SECTION_SEEN:
		/*
		 * What we are looking for is in store.key (both
		 * section and var), and its section part is baselen
		 * long.  We found key (again, both section and var).
		 * We would want to know if this key is in the same
		 * section as what we are looking for.  We already
		 * know we are in the same section as what should
		 * hold store.key.
		 */
		ep = strrchr(key, '.');
		section_len = ep - key;

		if ((section_len != store.baselen) ||
		    memcmp(key, store.key, section_len+1)) {
			store.state = SECTION_END_SEEN;
			break;
		}

		/*
		 * Do not increment matches: this is no match, but we
		 * just made sure we are in the desired section.
		 */
		ALLOC_GROW(store.offset, store.seen + 1,
			   store.offset_alloc);
		store.offset[store.seen] = cf->do_ftell(cf);
		/* fallthru */
	case SECTION_END_SEEN:
	case START:
		if (matches(key, value)) {
			ALLOC_GROW(store.offset, store.seen + 1,
				   store.offset_alloc);
			store.offset[store.seen] = cf->do_ftell(cf);
			store.state = KEY_SEEN;
			store.seen++;
		} else {
			if (strrchr(key, '.') - key == store.baselen &&
			      !strncmp(key, store.key, store.baselen)) {
					store.state = SECTION_SEEN;
					ALLOC_GROW(store.offset,
						   store.seen + 1,
						   store.offset_alloc);
					store.offset[store.seen] = cf->do_ftell(cf);
			}
		}
	}
	return 0;
}

static int write_error(const char *filename)
{
	error("failed to write new configuration file %s", filename);

	/* Same error code as "failed to rename". */
	return 4;
}

static int store_write_section(int fd, const char *key)
{
	const char *dot;
	int i, success;
	struct strbuf sb = STRBUF_INIT;

	dot = memchr(key, '.', store.baselen);
	if (dot) {
		strbuf_addf(&sb, "[%.*s \"", (int)(dot - key), key);
		for (i = dot - key + 1; i < store.baselen; i++) {
			if (key[i] == '"' || key[i] == '\\')
				strbuf_addch(&sb, '\\');
			strbuf_addch(&sb, key[i]);
		}
		strbuf_addstr(&sb, "\"]\n");
	} else {
		strbuf_addf(&sb, "[%.*s]\n", store.baselen, key);
	}

	success = write_in_full(fd, sb.buf, sb.len) == sb.len;
	strbuf_release(&sb);

	return success;
}

static int store_write_pair(int fd, const char *key, const char *value)
{
	int i, success;
	int length = strlen(key + store.baselen + 1);
	const char *quote = "";
	struct strbuf sb = STRBUF_INIT;

	/*
	 * Check to see if the value needs to be surrounded with a dq pair.
	 * Note that problematic characters are always backslash-quoted; this
	 * check is about not losing leading or trailing SP and strings that
	 * follow beginning-of-comment characters (i.e. ';' and '#') by the
	 * configuration parser.
	 */
	if (value[0] == ' ')
		quote = "\"";
	for (i = 0; value[i]; i++)
		if (value[i] == ';' || value[i] == '#')
			quote = "\"";
	if (i && value[i - 1] == ' ')
		quote = "\"";

	strbuf_addf(&sb, "\t%.*s = %s",
		    length, key + store.baselen + 1, quote);

	for (i = 0; value[i]; i++)
		switch (value[i]) {
		case '\n':
			strbuf_addstr(&sb, "\\n");
			break;
		case '\t':
			strbuf_addstr(&sb, "\\t");
			break;
		case '"':
		case '\\':
			strbuf_addch(&sb, '\\');
		default:
			strbuf_addch(&sb, value[i]);
			break;
		}
	strbuf_addf(&sb, "%s\n", quote);

	success = write_in_full(fd, sb.buf, sb.len) == sb.len;
	strbuf_release(&sb);

	return success;
}

static ssize_t find_beginning_of_line(const char *contents, size_t size,
	size_t offset_, int *found_bracket)
{
	size_t equal_offset = size, bracket_offset = size;
	ssize_t offset;

contline:
	for (offset = offset_-2; offset > 0
			&& contents[offset] != '\n'; offset--)
		switch (contents[offset]) {
			case '=': equal_offset = offset; break;
			case ']': bracket_offset = offset; break;
		}
	if (offset > 0 && contents[offset-1] == '\\') {
		offset_ = offset;
		goto contline;
	}
	if (bracket_offset < equal_offset) {
		*found_bracket = 1;
		offset = bracket_offset+1;
	} else
		offset++;

	return offset;
}

int git_config_set_in_file_gently(const char *config_filename,
				  const char *key, const char *value)
{
	return git_config_set_multivar_in_file_gently(config_filename, key, value, NULL, 0);
}

void git_config_set_in_file(const char *config_filename,
			    const char *key, const char *value)
{
	git_config_set_multivar_in_file(config_filename, key, value, NULL, 0);
}

int git_config_set_gently(const char *key, const char *value)
{
	return git_config_set_multivar_gently(key, value, NULL, 0);
}

void git_config_set(const char *key, const char *value)
{
	git_config_set_multivar(key, value, NULL, 0);
}

/*
 * If value==NULL, unset in (remove from) config,
 * if value_regex!=NULL, disregard key/value pairs where value does not match.
 * if value_regex==CONFIG_REGEX_NONE, do not match any existing values
 *     (only add a new one)
 * if multi_replace==0, nothing, or only one matching key/value is replaced,
 *     else all matching key/values (regardless how many) are removed,
 *     before the new pair is written.
 *
 * Returns 0 on success.
 *
 * This function does this:
 *
 * - it locks the config file by creating ".git/config.lock"
 *
 * - it then parses the config using store_aux() as validator to find
 *   the position on the key/value pair to replace. If it is to be unset,
 *   it must be found exactly once.
 *
 * - the config file is mmap()ed and the part before the match (if any) is
 *   written to the lock file, then the changed part and the rest.
 *
 * - the config file is removed and the lock file rename()d to it.
 *
 */
int git_config_set_multivar_in_file_gently(const char *config_filename,
					   const char *key, const char *value,
					   const char *value_regex,
					   int multi_replace)
{
	int fd = -1, in_fd = -1;
	int ret;
	struct lock_file *lock = NULL;
	char *filename_buf = NULL;
	char *contents = NULL;
	size_t contents_sz;

	/* parse-key returns negative; flip the sign to feed exit(3) */
	ret = 0 - git_config_parse_key(key, &store.key, &store.baselen);
	if (ret)
		goto out_free;

	store.multi_replace = multi_replace;

	if (!config_filename)
		config_filename = filename_buf = git_pathdup("config");

	/*
	 * The lock serves a purpose in addition to locking: the new
	 * contents of .git/config will be written into it.
	 */
	lock = xcalloc(1, sizeof(struct lock_file));
	fd = hold_lock_file_for_update(lock, config_filename, 0);
	if (fd < 0) {
		error_errno("could not lock config file %s", config_filename);
		free(store.key);
		ret = CONFIG_NO_LOCK;
		goto out_free;
	}

	/*
	 * If .git/config does not exist yet, write a minimal version.
	 */
	in_fd = open(config_filename, O_RDONLY);
	if ( in_fd < 0 ) {
		free(store.key);

		if ( ENOENT != errno ) {
			error_errno("opening %s", config_filename);
			ret = CONFIG_INVALID_FILE; /* same as "invalid config file" */
			goto out_free;
		}
		/* if nothing to unset, error out */
		if (value == NULL) {
			ret = CONFIG_NOTHING_SET;
			goto out_free;
		}

		store.key = (char *)key;
		if (!store_write_section(fd, key) ||
		    !store_write_pair(fd, key, value))
			goto write_err_out;
	} else {
		struct stat st;
		size_t copy_begin, copy_end;
		int i, new_line = 0;

		if (value_regex == NULL)
			store.value_regex = NULL;
		else if (value_regex == CONFIG_REGEX_NONE)
			store.value_regex = CONFIG_REGEX_NONE;
		else {
			if (value_regex[0] == '!') {
				store.do_not_match = 1;
				value_regex++;
			} else
				store.do_not_match = 0;

			store.value_regex = (regex_t*)xmalloc(sizeof(regex_t));
			if (regcomp(store.value_regex, value_regex,
					REG_EXTENDED)) {
				error("invalid pattern: %s", value_regex);
				free(store.value_regex);
				ret = CONFIG_INVALID_PATTERN;
				goto out_free;
			}
		}

		ALLOC_GROW(store.offset, 1, store.offset_alloc);
		store.offset[0] = 0;
		store.state = START;
		store.seen = 0;

		/*
		 * After this, store.offset will contain the *end* offset
		 * of the last match, or remain at 0 if no match was found.
		 * As a side effect, we make sure to transform only a valid
		 * existing config file.
		 */
		if (git_config_from_file(store_aux, config_filename, NULL)) {
			error("invalid config file %s", config_filename);
			free(store.key);
			if (store.value_regex != NULL &&
			    store.value_regex != CONFIG_REGEX_NONE) {
				regfree(store.value_regex);
				free(store.value_regex);
			}
			ret = CONFIG_INVALID_FILE;
			goto out_free;
		}

		free(store.key);
		if (store.value_regex != NULL &&
		    store.value_regex != CONFIG_REGEX_NONE) {
			regfree(store.value_regex);
			free(store.value_regex);
		}

		/* if nothing to unset, or too many matches, error out */
		if ((store.seen == 0 && value == NULL) ||
				(store.seen > 1 && multi_replace == 0)) {
			ret = CONFIG_NOTHING_SET;
			goto out_free;
		}

		if (fstat(in_fd, &st) == -1) {
			error_errno(_("fstat on %s failed"), config_filename);
			ret = CONFIG_INVALID_FILE;
			goto out_free;
		}

		contents_sz = xsize_t(st.st_size);
		contents = xmmap_gently(NULL, contents_sz, PROT_READ,
					MAP_PRIVATE, in_fd, 0);
		if (contents == MAP_FAILED) {
			if (errno == ENODEV && S_ISDIR(st.st_mode))
				errno = EISDIR;
			error_errno("unable to mmap '%s'", config_filename);
			ret = CONFIG_INVALID_FILE;
			contents = NULL;
			goto out_free;
		}
		close(in_fd);
		in_fd = -1;

		if (chmod(get_lock_file_path(lock), st.st_mode & 07777) < 0) {
			error_errno("chmod on %s failed", get_lock_file_path(lock));
			ret = CONFIG_NO_WRITE;
			goto out_free;
		}

		if (store.seen == 0)
			store.seen = 1;

		for (i = 0, copy_begin = 0; i < store.seen; i++) {
			if (store.offset[i] == 0) {
				store.offset[i] = copy_end = contents_sz;
			} else if (store.state != KEY_SEEN) {
				copy_end = store.offset[i];
			} else
				copy_end = find_beginning_of_line(
					contents, contents_sz,
					store.offset[i]-2, &new_line);

			if (copy_end > 0 && contents[copy_end-1] != '\n')
				new_line = 1;

			/* write the first part of the config */
			if (copy_end > copy_begin) {
				if (write_in_full(fd, contents + copy_begin,
						  copy_end - copy_begin) <
				    copy_end - copy_begin)
					goto write_err_out;
				if (new_line &&
				    write_str_in_full(fd, "\n") != 1)
					goto write_err_out;
			}
			copy_begin = store.offset[i];
		}

		/* write the pair (value == NULL means unset) */
		if (value != NULL) {
			if (store.state == START) {
				if (!store_write_section(fd, key))
					goto write_err_out;
			}
			if (!store_write_pair(fd, key, value))
				goto write_err_out;
		}

		/* write the rest of the config */
		if (copy_begin < contents_sz)
			if (write_in_full(fd, contents + copy_begin,
					  contents_sz - copy_begin) <
			    contents_sz - copy_begin)
				goto write_err_out;

		munmap(contents, contents_sz);
		contents = NULL;
	}

	if (commit_lock_file(lock) < 0) {
		error_errno("could not write config file %s", config_filename);
		ret = CONFIG_NO_WRITE;
		lock = NULL;
		goto out_free;
	}

	/*
	 * lock is committed, so don't try to roll it back below.
	 * NOTE: Since lockfile.c keeps a linked list of all created
	 * lock_file structures, it isn't safe to free(lock).  It's
	 * better to just leave it hanging around.
	 */
	lock = NULL;
	ret = 0;

	/* Invalidate the config cache */
	git_config_clear();

out_free:
	if (lock)
		rollback_lock_file(lock);
	free(filename_buf);
	if (contents)
		munmap(contents, contents_sz);
	if (in_fd >= 0)
		close(in_fd);
	return ret;

write_err_out:
	ret = write_error(get_lock_file_path(lock));
	goto out_free;

}

void git_config_set_multivar_in_file(const char *config_filename,
				     const char *key, const char *value,
				     const char *value_regex, int multi_replace)
{
	if (!git_config_set_multivar_in_file_gently(config_filename, key, value,
						    value_regex, multi_replace))
		return;
	if (value)
		die(_("could not set '%s' to '%s'"), key, value);
	else
		die(_("could not unset '%s'"), key);
}

int git_config_set_multivar_gently(const char *key, const char *value,
				   const char *value_regex, int multi_replace)
{
	return git_config_set_multivar_in_file_gently(NULL, key, value, value_regex,
						      multi_replace);
}

void git_config_set_multivar(const char *key, const char *value,
			     const char *value_regex, int multi_replace)
{
	git_config_set_multivar_in_file(NULL, key, value, value_regex,
					multi_replace);
}

static int section_name_match (const char *buf, const char *name)
{
	int i = 0, j = 0, dot = 0;
	if (buf[i] != '[')
		return 0;
	for (i = 1; buf[i] && buf[i] != ']'; i++) {
		if (!dot && isspace(buf[i])) {
			dot = 1;
			if (name[j++] != '.')
				break;
			for (i++; isspace(buf[i]); i++)
				; /* do nothing */
			if (buf[i] != '"')
				break;
			continue;
		}
		if (buf[i] == '\\' && dot)
			i++;
		else if (buf[i] == '"' && dot) {
			for (i++; isspace(buf[i]); i++)
				; /* do_nothing */
			break;
		}
		if (buf[i] != name[j++])
			break;
	}
	if (buf[i] == ']' && name[j] == 0) {
		/*
		 * We match, now just find the right length offset by
		 * gobbling up any whitespace after it, as well
		 */
		i++;
		for (; buf[i] && isspace(buf[i]); i++)
			; /* do nothing */
		return i;
	}
	return 0;
}

static int section_name_is_ok(const char *name)
{
	/* Empty section names are bogus. */
	if (!*name)
		return 0;

	/*
	 * Before a dot, we must be alphanumeric or dash. After the first dot,
	 * anything goes, so we can stop checking.
	 */
	for (; *name && *name != '.'; name++)
		if (*name != '-' && !isalnum(*name))
			return 0;
	return 1;
}

/* if new_name == NULL, the section is removed instead */
int git_config_rename_section_in_file(const char *config_filename,
				      const char *old_name, const char *new_name)
{
	int ret = 0, remove = 0;
	char *filename_buf = NULL;
	struct lock_file *lock;
	int out_fd;
	char buf[1024];
	FILE *config_file;
	struct stat st;

	if (new_name && !section_name_is_ok(new_name)) {
		ret = error("invalid section name: %s", new_name);
		goto out_no_rollback;
	}

	if (!config_filename)
		config_filename = filename_buf = git_pathdup("config");

	lock = xcalloc(1, sizeof(struct lock_file));
	out_fd = hold_lock_file_for_update(lock, config_filename, 0);
	if (out_fd < 0) {
		ret = error("could not lock config file %s", config_filename);
		goto out;
	}

	if (!(config_file = fopen(config_filename, "rb"))) {
		/* no config file means nothing to rename, no error */
		goto commit_and_out;
	}

	if (fstat(fileno(config_file), &st) == -1) {
		ret = error_errno(_("fstat on %s failed"), config_filename);
		goto out;
	}

	if (chmod(get_lock_file_path(lock), st.st_mode & 07777) < 0) {
		ret = error_errno("chmod on %s failed",
				  get_lock_file_path(lock));
		goto out;
	}

	while (fgets(buf, sizeof(buf), config_file)) {
		int i;
		int length;
		char *output = buf;
		for (i = 0; buf[i] && isspace(buf[i]); i++)
			; /* do nothing */
		if (buf[i] == '[') {
			/* it's a section */
			int offset = section_name_match(&buf[i], old_name);
			if (offset > 0) {
				ret++;
				if (new_name == NULL) {
					remove = 1;
					continue;
				}
				store.baselen = strlen(new_name);
				if (!store_write_section(out_fd, new_name)) {
					ret = write_error(get_lock_file_path(lock));
					goto out;
				}
				/*
				 * We wrote out the new section, with
				 * a newline, now skip the old
				 * section's length
				 */
				output += offset + i;
				if (strlen(output) > 0) {
					/*
					 * More content means there's
					 * a declaration to put on the
					 * next line; indent with a
					 * tab
					 */
					output -= 1;
					output[0] = '\t';
				}
			}
			remove = 0;
		}
		if (remove)
			continue;
		length = strlen(output);
		if (write_in_full(out_fd, output, length) != length) {
			ret = write_error(get_lock_file_path(lock));
			goto out;
		}
	}
	fclose(config_file);
commit_and_out:
	if (commit_lock_file(lock) < 0)
		ret = error_errno("could not write config file %s",
				  config_filename);
out:
	rollback_lock_file(lock);
out_no_rollback:
	free(filename_buf);
	return ret;
}

int git_config_rename_section(const char *old_name, const char *new_name)
{
	return git_config_rename_section_in_file(NULL, old_name, new_name);
}

/*
 * Call this to report error for your variable that should not
 * get a boolean value (i.e. "[my] var" means "true").
 */
#undef config_error_nonbool
int config_error_nonbool(const char *var)
{
	return error("missing value for '%s'", var);
}

int parse_config_key(const char *var,
		     const char *section,
		     const char **subsection, int *subsection_len,
		     const char **key)
{
	const char *dot;

	/* Does it start with "section." ? */
	if (!skip_prefix(var, section, &var) || *var != '.')
		return -1;

	/*
	 * Find the key; we don't know yet if we have a subsection, but we must
	 * parse backwards from the end, since the subsection may have dots in
	 * it, too.
	 */
	dot = strrchr(var, '.');
	*key = dot + 1;

	/* Did we have a subsection at all? */
	if (dot == var) {
		if (subsection) {
			*subsection = NULL;
			*subsection_len = 0;
		}
	}
	else {
		if (!subsection)
			return -1;
		*subsection = var + 1;
		*subsection_len = dot - *subsection;
	}

	return 0;
}

const char *current_config_origin_type(void)
{
	int type;
	if (current_config_kvi)
		type = current_config_kvi->origin_type;
	else if(cf)
		type = cf->origin_type;
	else
		die("BUG: current_config_origin_type called outside config callback");

	switch (type) {
	case CONFIG_ORIGIN_BLOB:
		return "blob";
	case CONFIG_ORIGIN_FILE:
		return "file";
	case CONFIG_ORIGIN_STDIN:
		return "standard input";
	case CONFIG_ORIGIN_SUBMODULE_BLOB:
		return "submodule-blob";
	case CONFIG_ORIGIN_CMDLINE:
		return "command line";
	default:
		die("BUG: unknown config origin type");
	}
}

const char *current_config_name(void)
{
	const char *name;
	if (current_config_kvi)
		name = current_config_kvi->filename;
	else if (cf)
		name = cf->name;
	else
		die("BUG: current_config_name called outside config callback");
	return name ? name : "";
}

enum config_scope current_config_scope(void)
{
	if (current_config_kvi)
		return current_config_kvi->scope;
	else
		return current_parsing_scope;
}
