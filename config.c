/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Johannes Schindelin, 2005
 *
 */
#include "cache.h"

#define MAXNAME (256)

static FILE *config_file;
static const char *config_file_name;
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
		if (!quote) {
			if (c == ';' || c == '#') {
				comment = 1;
				continue;
			}
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
		value[len++] = c;
	}
}

static inline int iskeychar(int c)
{
	return isalnum(c) || c == '-';
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
	return fn(name, value);
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
		if (c == EOF)
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
	die("bad config file line %d in %s", config_linenr, config_file_name);
}

int git_config_int(const char *name, const char *value)
{
	if (value && *value) {
		char *end;
		int val = strtol(value, &end, 0);
		if (!*end)
			return val;
	}
	die("bad config value for '%s' in %s", name, config_file_name);
}

int git_config_bool(const char *name, const char *value)
{
	if (!value)
		return 1;
	if (!*value)
		return 0;
	if (!strcasecmp(value, "true") || !strcasecmp(value, "yes"))
		return 1;
	if (!strcasecmp(value, "false") || !strcasecmp(value, "no"))
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

	if (!strcmp(var, "core.legacyheaders")) {
		use_legacy_headers = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.compression")) {
		int level = git_config_int(var, value);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad zlib compression level %d", level);
		zlib_compression_level = level;
		return 0;
	}

	if (!strcmp(var, "user.name")) {
		strlcpy(git_default_name, value, sizeof(git_default_name));
		return 0;
	}

	if (!strcmp(var, "user.email")) {
		strlcpy(git_default_email, value, sizeof(git_default_email));
		return 0;
	}

	if (!strcmp(var, "i18n.commitencoding")) {
		strlcpy(git_commit_encoding, value, sizeof(git_commit_encoding));
		return 0;
	}

	if (!strcmp(var, "pager.color") || !strcmp(var, "color.pager")) {
		pager_use_color = git_config_bool(var,value);
		return 0;
	}

	/* Add other config variables here and to Documentation/config.txt. */
	return 0;
}

int git_config_from_file(config_fn_t fn, const char *filename)
{
	int ret;
	FILE *f = fopen(filename, "r");

	ret = -1;
	if (f) {
		config_file = f;
		config_file_name = filename;
		config_linenr = 1;
		ret = git_parse_file(fn);
		fclose(f);
		config_file_name = NULL;
	}
	return ret;
}

int git_config(config_fn_t fn)
{
	int ret = 0;
	char *repo_config = NULL;
	const char *home = NULL, *filename;

	/* $GIT_CONFIG makes git read _only_ the given config file,
	 * $GIT_CONFIG_LOCAL will make it process it in addition to the
	 * global config file, the same way it would the per-repository
	 * config file otherwise. */
	filename = getenv("GIT_CONFIG");
	if (!filename) {
		home = getenv("HOME");
		filename = getenv("GIT_CONFIG_LOCAL");
		if (!filename)
			filename = repo_config = xstrdup(git_path("config"));
	}

	if (home) {
		char *user_config = xstrdup(mkpath("%s/.gitconfig", home));
		if (!access(user_config, R_OK))
			ret = git_config_from_file(fn, user_config);
		free(user_config);
	}

	ret += git_config_from_file(fn, filename);
	free(repo_config);
	return ret;
}

/*
 * Find all the stuff for git_config_set() below.
 */

#define MAX_MATCHES 512

static struct {
	int baselen;
	char* key;
	int do_not_match;
	regex_t* value_regex;
	int multi_replace;
	off_t offset[MAX_MATCHES];
	enum { START, SECTION_SEEN, SECTION_END_SEEN, KEY_SEEN } state;
	int seen;
} store;

static int matches(const char* key, const char* value)
{
	return !strcmp(key, store.key) &&
		(store.value_regex == NULL ||
		 (store.do_not_match ^
		  !regexec(store.value_regex, value, 0, NULL, 0)));
}

static int store_aux(const char* key, const char* value)
{
	switch (store.state) {
	case KEY_SEEN:
		if (matches(key, value)) {
			if (store.seen == 1 && store.multi_replace == 0) {
				fprintf(stderr,
					"Warning: %s has multiple values\n",
					key);
			} else if (store.seen >= MAX_MATCHES) {
				fprintf(stderr, "Too many matches\n");
				return 1;
			}

			store.offset[store.seen] = ftell(config_file);
			store.seen++;
		}
		break;
	case SECTION_SEEN:
		if (strncmp(key, store.key, store.baselen+1)) {
			store.state = SECTION_END_SEEN;
			break;
		} else
			/* do not increment matches: this is no match */
			store.offset[store.seen] = ftell(config_file);
		/* fallthru */
	case SECTION_END_SEEN:
	case START:
		if (matches(key, value)) {
			store.offset[store.seen] = ftell(config_file);
			store.state = KEY_SEEN;
			store.seen++;
		} else {
			if (strrchr(key, '.') - key == store.baselen &&
			      !strncmp(key, store.key, store.baselen)) {
					store.state = SECTION_SEEN;
					store.offset[store.seen] = ftell(config_file);
			}
		}
	}
	return 0;
}

static void store_write_section(int fd, const char* key)
{
	const char *dot = strchr(key, '.');
	int len1 = store.baselen, len2 = -1;

	dot = strchr(key, '.');
	if (dot) {
		int dotlen = dot - key;
		if (dotlen < len1) {
			len2 = len1 - dotlen - 1;
			len1 = dotlen;
		}
	}

	write(fd, "[", 1);
	write(fd, key, len1);
	if (len2 >= 0) {
		write(fd, " \"", 2);
		while (--len2 >= 0) {
			unsigned char c = *++dot;
			if (c == '"')
				write(fd, "\\", 1);
			write(fd, &c, 1);
		}
		write(fd, "\"", 1);
	}
	write(fd, "]\n", 2);
}

static void store_write_pair(int fd, const char* key, const char* value)
{
	int i;

	write(fd, "\t", 1);
	write(fd, key+store.baselen+1,
		strlen(key+store.baselen+1));
	write(fd, " = ", 3);
	for (i = 0; value[i]; i++)
		switch (value[i]) {
		case '\n': write(fd, "\\n", 2); break;
		case '\t': write(fd, "\\t", 2); break;
		case '"': case '\\': write(fd, "\\", 1);
		default: write(fd, value+i, 1);
	}
	write(fd, "\n", 1);
}

static int find_beginning_of_line(const char* contents, int size,
	int offset_, int* found_bracket)
{
	int equal_offset = size, bracket_offset = size;
	int offset;

	for (offset = offset_-2; offset > 0 
			&& contents[offset] != '\n'; offset--)
		switch (contents[offset]) {
			case '=': equal_offset = offset; break;
			case ']': bracket_offset = offset; break;
		}
	if (bracket_offset < equal_offset) {
		*found_bracket = 1;
		offset = bracket_offset+1;
	} else
		offset++;

	return offset;
}

int git_config_set(const char* key, const char* value)
{
	return git_config_set_multivar(key, value, NULL, 0);
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
int git_config_set_multivar(const char* key, const char* value,
	const char* value_regex, int multi_replace)
{
	int i, dot;
	int fd = -1, in_fd;
	int ret;
	char* config_filename;
	char* lock_file;
	const char* last_dot = strrchr(key, '.');

	config_filename = getenv("GIT_CONFIG");
	if (!config_filename) {
		config_filename = getenv("GIT_CONFIG_LOCAL");
		if (!config_filename)
			config_filename  = git_path("config");
	}
	config_filename = xstrdup(config_filename);
	lock_file = xstrdup(mkpath("%s.lock", config_filename));

	/*
	 * Since "key" actually contains the section name and the real
	 * key name separated by a dot, we have to know where the dot is.
	 */

	if (last_dot == NULL) {
		fprintf(stderr, "key does not contain a section: %s\n", key);
		ret = 2;
		goto out_free;
	}
	store.baselen = last_dot - key;

	store.multi_replace = multi_replace;

	/*
	 * Validate the key and while at it, lower case it for matching.
	 */
	store.key = xmalloc(strlen(key) + 1);
	dot = 0;
	for (i = 0; key[i]; i++) {
		unsigned char c = key[i];
		if (c == '.')
			dot = 1;
		/* Leave the extended basename untouched.. */
		if (!dot || i > store.baselen) {
			if (!iskeychar(c) || (i == store.baselen+1 && !isalpha(c))) {
				fprintf(stderr, "invalid key: %s\n", key);
				free(store.key);
				ret = 1;
				goto out_free;
			}
			c = tolower(c);
		}
		store.key[i] = c;
	}
	store.key[i] = 0;

	/*
	 * The lock_file serves a purpose in addition to locking: the new
	 * contents of .git/config will be written into it.
	 */
	fd = open(lock_file, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0 || adjust_shared_perm(lock_file)) {
		fprintf(stderr, "could not lock config file\n");
		free(store.key);
		ret = -1;
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
			ret = 3; /* same as "invalid config file" */
			goto out_free;
		}
		/* if nothing to unset, error out */
		if (value == NULL) {
			ret = 5;
			goto out_free;
		}

		store.key = (char*)key;
		store_write_section(fd, key);
		store_write_pair(fd, key, value);
	} else{
		struct stat st;
		char* contents;
		int i, copy_begin, copy_end, new_line = 0;

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
				fprintf(stderr, "Invalid pattern: %s\n",
					value_regex);
				free(store.value_regex);
				ret = 6;
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
		if (git_config_from_file(store_aux, config_filename)) {
			fprintf(stderr, "invalid config file\n");
			free(store.key);
			if (store.value_regex != NULL) {
				regfree(store.value_regex);
				free(store.value_regex);
			}
			ret = 3;
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
			ret = 5;
			goto out_free;
		}

		fstat(in_fd, &st);
		contents = mmap(NULL, st.st_size, PROT_READ,
			MAP_PRIVATE, in_fd, 0);
		close(in_fd);

		if (store.seen == 0)
			store.seen = 1;

		for (i = 0, copy_begin = 0; i < store.seen; i++) {
			if (store.offset[i] == 0) {
				store.offset[i] = copy_end = st.st_size;
			} else if (store.state != KEY_SEEN) {
				copy_end = store.offset[i];
			} else
				copy_end = find_beginning_of_line(
					contents, st.st_size,
					store.offset[i]-2, &new_line);

			/* write the first part of the config */
			if (copy_end > copy_begin) {
				write(fd, contents + copy_begin,
				copy_end - copy_begin);
				if (new_line)
					write(fd, "\n", 1);
			}
			copy_begin = store.offset[i];
		}

		/* write the pair (value == NULL means unset) */
		if (value != NULL) {
			if (store.state == START)
				store_write_section(fd, key);
			store_write_pair(fd, key, value);
		}

		/* write the rest of the config */
		if (copy_begin < st.st_size)
			write(fd, contents + copy_begin,
				st.st_size - copy_begin);

		munmap(contents, st.st_size);
		unlink(config_filename);
	}

	if (rename(lock_file, config_filename) < 0) {
		fprintf(stderr, "Could not rename the lock file?\n");
		ret = 4;
		goto out_free;
	}

	ret = 0;

out_free:
	if (0 <= fd)
		close(fd);
	free(config_filename);
	if (lock_file) {
		unlink(lock_file);
		free(lock_file);
	}
	return ret;
}

int git_config_rename_section(const char *old_name, const char *new_name)
{
	int ret = 0;
	char *config_filename;
	struct lock_file *lock = xcalloc(sizeof(struct lock_file), 1);
	int out_fd;
	char buf[1024];

	config_filename = getenv("GIT_CONFIG");
	if (!config_filename) {
		config_filename = getenv("GIT_CONFIG_LOCAL");
		if (!config_filename)
			config_filename  = git_path("config");
	}
	config_filename = xstrdup(config_filename);
	out_fd = hold_lock_file_for_update(lock, config_filename, 0);
	if (out_fd < 0) {
		ret = error("Could not lock config file!");
		goto out;
	}

	if (!(config_file = fopen(config_filename, "rb"))) {
		ret = error("Could not open config file!");
		goto out;
	}

	while (fgets(buf, sizeof(buf), config_file)) {
		int i;
		for (i = 0; buf[i] && isspace(buf[i]); i++)
			; /* do nothing */
		if (buf[i] == '[') {
			/* it's a section */
			int j = 0, dot = 0;
			for (i++; buf[i] && buf[i] != ']'; i++) {
				if (!dot && isspace(buf[i])) {
					dot = 1;
					if (old_name[j++] != '.')
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
				if (buf[i] != old_name[j++])
					break;
			}
			if (buf[i] == ']') {
				/* old_name matches */
				ret++;
				store.baselen = strlen(new_name);
				store_write_section(out_fd, new_name);
				continue;
			}
		}
		write(out_fd, buf, strlen(buf));
	}
	fclose(config_file);
	if (close(out_fd) || commit_lock_file(lock) < 0)
		ret = error("Cannot commit config file!");
 out:
	free(config_filename);
	return ret;
}

