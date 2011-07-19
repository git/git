/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Johannes Schindelin, 2005
 *
 */
#include "cache.h"
#include "exec_cmd.h"
#include "strbuf.h"
#include "quote.h"

#define MAXNAME (256)

typedef struct config_file {
	struct config_file *prev;
	FILE *f;
	const char *name;
	int linenr;
	int eof;
	struct strbuf value;
	char var[MAXNAME];
} config_file;

static config_file *cf;

static int zlib_compression_seen;

const char *config_exclusive_filename = NULL;

static void lowercase(char *p)
{
	for (; *p; p++)
		*p = tolower(*p);
}

void git_config_push_parameter(const char *text)
{
	struct strbuf env = STRBUF_INIT;
	const char *old = getenv(CONFIG_DATA_ENVIRONMENT);
	if (old) {
		strbuf_addstr(&env, old);
		strbuf_addch(&env, ' ');
	}
	sq_quote_buf(&env, text);
	setenv(CONFIG_DATA_ENVIRONMENT, env.buf, 1);
	strbuf_release(&env);
}

int git_config_parse_parameter(const char *text,
			       config_fn_t fn, void *data)
{
	struct strbuf **pair;
	pair = strbuf_split_str(text, '=', 2);
	if (!pair[0])
		return error("bogus config parameter: %s", text);
	if (pair[0]->len && pair[0]->buf[pair[0]->len - 1] == '=')
		strbuf_setlen(pair[0], pair[0]->len - 1);
	strbuf_trim(pair[0]);
	if (!pair[0]->len) {
		strbuf_list_free(pair);
		return error("bogus config parameter: %s", text);
	}
	lowercase(pair[0]->buf);
	if (fn(pair[0]->buf, pair[1] ? pair[1]->buf : NULL, data) < 0) {
		strbuf_list_free(pair);
		return -1;
	}
	strbuf_list_free(pair);
	return 0;
}

int git_config_from_parameters(config_fn_t fn, void *data)
{
	const char *env = getenv(CONFIG_DATA_ENVIRONMENT);
	char *envw;
	const char **argv = NULL;
	int nr = 0, alloc = 0;
	int i;

	if (!env)
		return 0;
	/* sq_dequote will write over it */
	envw = xstrdup(env);

	if (sq_dequote_to_argv(envw, &argv, &nr, &alloc) < 0) {
		free(envw);
		return error("bogus format in " CONFIG_DATA_ENVIRONMENT);
	}

	for (i = 0; i < nr; i++) {
		if (git_config_parse_parameter(argv[i], fn, data) < 0) {
			free(argv);
			free(envw);
			return -1;
		}
	}

	free(argv);
	free(envw);
	return nr > 0;
}

static int get_next_char(void)
{
	int c;
	FILE *f;

	c = '\n';
	if (cf && ((f = cf->f) != NULL)) {
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
			cf->linenr++;
		if (c == EOF) {
			cf->eof = 1;
			c = '\n';
		}
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
			if (quote)
				return NULL;
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

static inline int iskeychar(int c)
{
	return isalnum(c) || c == '-';
}

static int get_value(config_fn_t fn, void *data, char *name, unsigned int len)
{
	int c;
	char *value;

	/* Get the full name */
	for (;;) {
		c = get_next_char();
		if (cf->eof)
			break;
		if (!iskeychar(c))
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
	return fn(name, value, data);
}

static int get_extended_base_var(char *name, int baselen, int c)
{
	do {
		if (c == '\n')
			return -1;
		c = get_next_char();
	} while (isspace(c));

	/* We require the format to be '[base "extension"]' */
	if (c != '"')
		return -1;
	name[baselen++] = '.';

	for (;;) {
		int c = get_next_char();
		if (c == '\n')
			return -1;
		if (c == '"')
			break;
		if (c == '\\') {
			c = get_next_char();
			if (c == '\n')
				return -1;
		}
		name[baselen++] = c;
		if (baselen > MAXNAME / 2)
			return -1;
	}

	/* Final ']' */
	if (get_next_char() != ']')
		return -1;
	return baselen;
}

static int get_base_var(char *name)
{
	int baselen = 0;

	for (;;) {
		int c = get_next_char();
		if (cf->eof)
			return -1;
		if (c == ']')
			return baselen;
		if (isspace(c))
			return get_extended_base_var(name, baselen, c);
		if (!iskeychar(c) && c != '.')
			return -1;
		if (baselen > MAXNAME / 2)
			return -1;
		name[baselen++] = tolower(c);
	}
}

static int git_parse_file(config_fn_t fn, void *data)
{
	int comment = 0;
	int baselen = 0;
	char *var = cf->var;

	/* U+FEFF Byte Order Mark in UTF8 */
	static const unsigned char *utf8_bom = (unsigned char *) "\xef\xbb\xbf";
	const unsigned char *bomptr = utf8_bom;

	for (;;) {
		int c = get_next_char();
		if (bomptr && *bomptr) {
			/* We are at the file beginning; skip UTF8-encoded BOM
			 * if present. Sane editors won't put this in on their
			 * own, but e.g. Windows Notepad will do it happily. */
			if ((unsigned char) c == *bomptr) {
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
		if (get_value(fn, data, var, baselen+1) < 0)
			break;
	}
	die("bad config file line %d in %s", cf->linenr, cf->name);
}

static int parse_unit_factor(const char *end, unsigned long *val)
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

static int git_parse_long(const char *value, long *ret)
{
	if (value && *value) {
		char *end;
		long val = strtol(value, &end, 0);
		unsigned long factor = 1;
		if (!parse_unit_factor(end, &factor))
			return 0;
		*ret = val * factor;
		return 1;
	}
	return 0;
}

int git_parse_ulong(const char *value, unsigned long *ret)
{
	if (value && *value) {
		char *end;
		unsigned long val = strtoul(value, &end, 0);
		if (!parse_unit_factor(end, &val))
			return 0;
		*ret = val;
		return 1;
	}
	return 0;
}

static void die_bad_config(const char *name)
{
	if (cf && cf->name)
		die("bad config value for '%s' in %s", name, cf->name);
	die("bad config value for '%s'", name);
}

int git_config_int(const char *name, const char *value)
{
	long ret = 0;
	if (!git_parse_long(value, &ret))
		die_bad_config(name);
	return ret;
}

unsigned long git_config_ulong(const char *name, const char *value)
{
	unsigned long ret;
	if (!git_parse_ulong(value, &ret))
		die_bad_config(name);
	return ret;
}

static int git_config_maybe_bool_text(const char *name, const char *value)
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
	long v = git_config_maybe_bool_text(name, value);
	if (0 <= v)
		return v;
	if (git_parse_long(value, &v))
		return !!v;
	return -1;
}

int git_config_bool_or_int(const char *name, const char *value, int *is_bool)
{
	int v = git_config_maybe_bool_text(name, value);
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
		die("Failed to expand user dir in: '%s'", value);
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
		log_all_ref_updates = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.warnambiguousrefs")) {
		warn_ambiguous_refs = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.abbrev")) {
		int abbrev = git_config_int(var, value);
		if (abbrev < minimum_abbrev || abbrev > 40)
			return -1;
		default_abbrev = abbrev;
		return 0;
	}

	if (!strcmp(var, "core.loosecompression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad zlib compression level %d", level);
		zlib_compression_level = level;
		zlib_compression_seen = 1;
		return 0;
	}

	if (!strcmp(var, "core.compression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad zlib compression level %d", level);
		core_compression_level = level;
		core_compression_seen = 1;
		if (!zlib_compression_seen)
			zlib_compression_level = level;
		return 0;
	}

	if (!strcmp(var, "core.packedgitwindowsize")) {
		int pgsz_x2 = getpagesize() * 2;
		packed_git_window_size = git_config_int(var, value);

		/* This value must be multiple of (pagesize * 2) */
		packed_git_window_size /= pgsz_x2;
		if (packed_git_window_size < 1)
			packed_git_window_size = 1;
		packed_git_window_size *= pgsz_x2;
		return 0;
	}

	if (!strcmp(var, "core.bigfilethreshold")) {
		long n = git_config_int(var, value);
		big_file_threshold = 0 < n ? n : 0;
		return 0;
	}

	if (!strcmp(var, "core.packedgitlimit")) {
		packed_git_limit = git_config_int(var, value);
		return 0;
	}

	if (!strcmp(var, "core.deltabasecachelimit")) {
		delta_base_cache_limit = git_config_int(var, value);
		return 0;
	}

	if (!strcmp(var, "core.autocrlf")) {
		if (value && !strcasecmp(value, "input")) {
			if (core_eol == EOL_CRLF)
				return error("core.autocrlf=input conflicts with core.eol=crlf");
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
		if (core_eol == EOL_CRLF && auto_crlf == AUTO_CRLF_INPUT)
			return error("core.autocrlf=input conflicts with core.eol=crlf");
		return 0;
	}

	if (!strcmp(var, "core.notesref")) {
		notes_ref_name = xstrdup(value);
		return 0;
	}

	if (!strcmp(var, "core.pager"))
		return git_config_string(&pager_program, var, value);

	if (!strcmp(var, "core.editor"))
		return git_config_string(&editor_program, var, value);

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
			die("Invalid mode for object creation: %s", value);
		return 0;
	}

	if (!strcmp(var, "core.sparsecheckout")) {
		core_apply_sparse_checkout = git_config_bool(var, value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_user_config(const char *var, const char *value)
{
	if (!strcmp(var, "user.name")) {
		if (!value)
			return config_error_nonbool(var);
		strlcpy(git_default_name, value, sizeof(git_default_name));
		user_ident_explicitly_given |= IDENT_NAME_GIVEN;
		return 0;
	}

	if (!strcmp(var, "user.email")) {
		if (!value)
			return config_error_nonbool(var);
		strlcpy(git_default_email, value, sizeof(git_default_email));
		user_ident_explicitly_given |= IDENT_MAIL_GIVEN;
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
			return error("Malformed value for %s", var);
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
		else if (!strcmp(value, "upstream"))
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "tracking")) /* deprecated */
			push_default = PUSH_DEFAULT_UPSTREAM;
		else if (!strcmp(value, "current"))
			push_default = PUSH_DEFAULT_CURRENT;
		else {
			error("Malformed value for %s: %s", var, value);
			return error("Must be one of nothing, matching, "
				     "tracking or current.");
		}
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

static int git_default_mailmap_config(const char *var, const char *value)
{
	if (!strcmp(var, "mailmap.file"))
		return git_config_string(&git_mailmap_file, var, value);

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int git_default_config(const char *var, const char *value, void *dummy)
{
	if (!prefixcmp(var, "core."))
		return git_default_core_config(var, value);

	if (!prefixcmp(var, "user."))
		return git_default_user_config(var, value);

	if (!prefixcmp(var, "i18n."))
		return git_default_i18n_config(var, value);

	if (!prefixcmp(var, "branch."))
		return git_default_branch_config(var, value);

	if (!prefixcmp(var, "push."))
		return git_default_push_config(var, value);

	if (!prefixcmp(var, "mailmap."))
		return git_default_mailmap_config(var, value);

	if (!prefixcmp(var, "advice."))
		return git_default_advice_config(var, value);

	if (!strcmp(var, "pager.color") || !strcmp(var, "color.pager")) {
		pager_use_color = git_config_bool(var,value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int git_config_from_file(config_fn_t fn, const char *filename, void *data)
{
	int ret;
	FILE *f = fopen(filename, "r");

	ret = -1;
	if (f) {
		config_file top;

		/* push config-file parsing state stack */
		top.prev = cf;
		top.f = f;
		top.name = filename;
		top.linenr = 1;
		top.eof = 0;
		strbuf_init(&top.value, 1024);
		cf = &top;

		ret = git_parse_file(fn, data);

		/* pop config-file parsing state stack */
		strbuf_release(&top.value);
		cf = top.prev;

		fclose(f);
	}
	return ret;
}

const char *git_etc_gitconfig(void)
{
	static const char *system_wide;
	if (!system_wide)
		system_wide = system_path(ETC_GITCONFIG);
	return system_wide;
}

int git_env_bool(const char *k, int def)
{
	const char *v = getenv(k);
	return v ? git_config_bool(k, v) : def;
}

int git_config_system(void)
{
	return !git_env_bool("GIT_CONFIG_NOSYSTEM", 0);
}

int git_config_early(config_fn_t fn, void *data, const char *repo_config)
{
	int ret = 0, found = 0;
	const char *home = NULL;

	/* Setting $GIT_CONFIG makes git read _only_ the given config file. */
	if (config_exclusive_filename)
		return git_config_from_file(fn, config_exclusive_filename, data);
	if (git_config_system() && !access(git_etc_gitconfig(), R_OK)) {
		ret += git_config_from_file(fn, git_etc_gitconfig(),
					    data);
		found += 1;
	}

	home = getenv("HOME");
	if (home) {
		char *user_config = xstrdup(mkpath("%s/.gitconfig", home));
		if (!access(user_config, R_OK)) {
			ret += git_config_from_file(fn, user_config, data);
			found += 1;
		}
		free(user_config);
	}

	if (repo_config && !access(repo_config, R_OK)) {
		ret += git_config_from_file(fn, repo_config, data);
		found += 1;
	}

	switch (git_config_from_parameters(fn, data)) {
	case -1: /* error */
		die("unable to parse command-line config");
		break;
	case 0: /* found nothing */
		break;
	default: /* found at least one item */
		found++;
		break;
	}

	return ret == 0 ? found : ret;
}

int git_config(config_fn_t fn, void *data)
{
	char *repo_config = NULL;
	int ret;

	repo_config = git_pathdup("config");
	ret = git_config_early(fn, data, repo_config);
	if (repo_config)
		free(repo_config);
	return ret;
}

/*
 * Find all the stuff for git_config_set() below.
 */

#define MAX_MATCHES 512

static struct {
	int baselen;
	char *key;
	int do_not_match;
	regex_t *value_regex;
	int multi_replace;
	size_t offset[MAX_MATCHES];
	enum { START, SECTION_SEEN, SECTION_END_SEEN, KEY_SEEN } state;
	int seen;
} store;

static int matches(const char *key, const char *value)
{
	return !strcmp(key, store.key) &&
		(store.value_regex == NULL ||
		 (store.do_not_match ^
		  !regexec(store.value_regex, value, 0, NULL, 0)));
}

static int store_aux(const char *key, const char *value, void *cb)
{
	const char *ep;
	size_t section_len;
	FILE *f = cf->f;

	switch (store.state) {
	case KEY_SEEN:
		if (matches(key, value)) {
			if (store.seen == 1 && store.multi_replace == 0) {
				warning("%s has multiple values", key);
			} else if (store.seen >= MAX_MATCHES) {
				error("too many matches for %s", key);
				return 1;
			}

			store.offset[store.seen] = ftell(f);
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
		store.offset[store.seen] = ftell(f);
		/* fallthru */
	case SECTION_END_SEEN:
	case START:
		if (matches(key, value)) {
			store.offset[store.seen] = ftell(f);
			store.state = KEY_SEEN;
			store.seen++;
		} else {
			if (strrchr(key, '.') - key == store.baselen &&
			      !strncmp(key, store.key, store.baselen)) {
					store.state = SECTION_SEEN;
					store.offset[store.seen] = ftell(f);
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

int git_config_set(const char *key, const char *value)
{
	return git_config_set_multivar(key, value, NULL, 0);
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
int git_config_parse_key(const char *key, char **store_key, int *baselen_)
{
	int i, dot, baselen;
	const char *last_dot = strrchr(key, '.');

	/*
	 * Since "key" actually contains the section name and the real
	 * key name separated by a dot, we have to know where the dot is.
	 */

	if (last_dot == NULL || last_dot == key) {
		error("key does not contain a section: %s", key);
		return -CONFIG_NO_SECTION_OR_NAME;
	}

	if (!last_dot[1]) {
		error("key does not contain variable name: %s", key);
		return -CONFIG_NO_SECTION_OR_NAME;
	}

	baselen = last_dot - key;
	if (baselen_)
		*baselen_ = baselen;

	/*
	 * Validate the key and while at it, lower case it for matching.
	 */
	*store_key = xmalloc(strlen(key) + 1);

	dot = 0;
	for (i = 0; key[i]; i++) {
		unsigned char c = key[i];
		if (c == '.')
			dot = 1;
		/* Leave the extended basename untouched.. */
		if (!dot || i > baselen) {
			if (!iskeychar(c) ||
			    (i == baselen + 1 && !isalpha(c))) {
				error("invalid key: %s", key);
				goto out_free_ret_1;
			}
			c = tolower(c);
		} else if (c == '\n') {
			error("invalid key (newline): %s", key);
			goto out_free_ret_1;
		}
		(*store_key)[i] = c;
	}
	(*store_key)[i] = 0;

	return 0;

out_free_ret_1:
	free(*store_key);
	return -CONFIG_INVALID_KEY;
}

/*
 * If value==NULL, unset in (remove from) config,
 * if value_regex!=NULL, disregard key/value pairs where value does not match.
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
int git_config_set_multivar(const char *key, const char *value,
	const char *value_regex, int multi_replace)
{
	int fd = -1, in_fd;
	int ret;
	char *config_filename;
	struct lock_file *lock = NULL;

	if (config_exclusive_filename)
		config_filename = xstrdup(config_exclusive_filename);
	else
		config_filename = git_pathdup("config");

	/* parse-key returns negative; flip the sign to feed exit(3) */
	ret = 0 - git_config_parse_key(key, &store.key, &store.baselen);
	if (ret)
		goto out_free;

	store.multi_replace = multi_replace;


	/*
	 * The lock serves a purpose in addition to locking: the new
	 * contents of .git/config will be written into it.
	 */
	lock = xcalloc(sizeof(struct lock_file), 1);
	fd = hold_lock_file_for_update(lock, config_filename, 0);
	if (fd < 0) {
		error("could not lock config file %s: %s", config_filename, strerror(errno));
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
			error("opening %s: %s", config_filename,
			      strerror(errno));
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
		char *contents;
		size_t contents_sz, copy_begin, copy_end;
		int i, new_line = 0;

		if (value_regex == NULL)
			store.value_regex = NULL;
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
			if (store.value_regex != NULL) {
				regfree(store.value_regex);
				free(store.value_regex);
			}
			ret = CONFIG_INVALID_FILE;
			goto out_free;
		}

		free(store.key);
		if (store.value_regex != NULL) {
			regfree(store.value_regex);
			free(store.value_regex);
		}

		/* if nothing to unset, or too many matches, error out */
		if ((store.seen == 0 && value == NULL) ||
				(store.seen > 1 && multi_replace == 0)) {
			ret = CONFIG_NOTHING_SET;
			goto out_free;
		}

		fstat(in_fd, &st);
		contents_sz = xsize_t(st.st_size);
		contents = xmmap(NULL, contents_sz, PROT_READ,
			MAP_PRIVATE, in_fd, 0);
		close(in_fd);

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
	}

	if (commit_lock_file(lock) < 0) {
		error("could not commit config file %s", config_filename);
		ret = CONFIG_NO_WRITE;
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

out_free:
	if (lock)
		rollback_lock_file(lock);
	free(config_filename);
	return ret;

write_err_out:
	ret = write_error(lock->filename);
	goto out_free;

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

/* if new_name == NULL, the section is removed instead */
int git_config_rename_section(const char *old_name, const char *new_name)
{
	int ret = 0, remove = 0;
	char *config_filename;
	struct lock_file *lock = xcalloc(sizeof(struct lock_file), 1);
	int out_fd;
	char buf[1024];
	FILE *config_file;

	if (config_exclusive_filename)
		config_filename = xstrdup(config_exclusive_filename);
	else
		config_filename = git_pathdup("config");
	out_fd = hold_lock_file_for_update(lock, config_filename, 0);
	if (out_fd < 0) {
		ret = error("could not lock config file %s", config_filename);
		goto out;
	}

	if (!(config_file = fopen(config_filename, "rb"))) {
		/* no config file means nothing to rename, no error */
		goto unlock_and_out;
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
					ret = write_error(lock->filename);
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
			ret = write_error(lock->filename);
			goto out;
		}
	}
	fclose(config_file);
 unlock_and_out:
	if (commit_lock_file(lock) < 0)
		ret = error("could not commit config file %s", config_filename);
 out:
	free(config_filename);
	return ret;
}

/*
 * Call this to report error for your variable that should not
 * get a boolean value (i.e. "[my] var" means "true").
 */
int config_error_nonbool(const char *var)
{
	return error("Missing value for '%s'", var);
}
