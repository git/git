/*
 * Check-out files from the "current cache directory"
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 */
#include "builtin.h"
#include "cache.h"
#include "quote.h"
#include "cache-tree.h"
#include "parse-options.h"

#define CHECKOUT_ALL 4
static int line_termination = '\n';
static int checkout_stage; /* default to checkout stage0 */
static int to_tempfile;
static char topath[4][PATH_MAX + 1];

static struct checkout state;

static void write_tempfile_record(const char *name, int prefix_length)
{
	int i;

	if (CHECKOUT_ALL == checkout_stage) {
		for (i = 1; i < 4; i++) {
			if (i > 1)
				putchar(' ');
			if (topath[i][0])
				fputs(topath[i], stdout);
			else
				putchar('.');
		}
	} else
		fputs(topath[checkout_stage], stdout);

	putchar('\t');
	write_name_quoted(name + prefix_length, stdout, line_termination);

	for (i = 0; i < 4; i++) {
		topath[i][0] = 0;
	}
}

static int checkout_file(const char *name, int prefix_length)
{
	int namelen = strlen(name);
	int pos = cache_name_pos(name, namelen);
	int has_same_name = 0;
	int did_checkout = 0;
	int errs = 0;

	if (pos < 0)
		pos = -pos - 1;

	while (pos < active_nr) {
		struct cache_entry *ce = active_cache[pos];
		if (ce_namelen(ce) != namelen ||
		    memcmp(ce->name, name, namelen))
			break;
		has_same_name = 1;
		pos++;
		if (ce_stage(ce) != checkout_stage
		    && (CHECKOUT_ALL != checkout_stage || !ce_stage(ce)))
			continue;
		did_checkout = 1;
		if (checkout_entry(ce, &state,
		    to_tempfile ? topath[ce_stage(ce)] : NULL) < 0)
			errs++;
	}

	if (did_checkout) {
		if (to_tempfile)
			write_tempfile_record(name, prefix_length);
		return errs > 0 ? -1 : 0;
	}

	if (!state.quiet) {
		fprintf(stderr, "git checkout-index: %s ", name);
		if (!has_same_name)
			fprintf(stderr, "is not in the cache");
		else if (checkout_stage)
			fprintf(stderr, "does not exist at stage %d",
				checkout_stage);
		else
			fprintf(stderr, "is unmerged");
		fputc('\n', stderr);
	}
	return -1;
}

static void checkout_all(const char *prefix, int prefix_length)
{
	int i, errs = 0;
	struct cache_entry *last_ce = NULL;

	for (i = 0; i < active_nr ; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce) != checkout_stage
		    && (CHECKOUT_ALL != checkout_stage || !ce_stage(ce)))
			continue;
		if (prefix && *prefix &&
		    (ce_namelen(ce) <= prefix_length ||
		     memcmp(prefix, ce->name, prefix_length)))
			continue;
		if (last_ce && to_tempfile) {
			if (ce_namelen(last_ce) != ce_namelen(ce)
			    || memcmp(last_ce->name, ce->name, ce_namelen(ce)))
				write_tempfile_record(last_ce->name, prefix_length);
		}
		if (checkout_entry(ce, &state,
		    to_tempfile ? topath[ce_stage(ce)] : NULL) < 0)
			errs++;
		last_ce = ce;
	}
	if (last_ce && to_tempfile)
		write_tempfile_record(last_ce->name, prefix_length);
	if (errs)
		/* we have already done our error reporting.
		 * exit with the same code as die().
		 */
		exit(128);
}

static const char * const builtin_checkout_index_usage[] = {
	"git checkout-index [options] [--] [<file>...]",
	NULL
};

static struct lock_file lock_file;

static int option_parse_u(const struct option *opt,
			      const char *arg, int unset)
{
	int *newfd = opt->value;

	state.refresh_cache = 1;
	if (*newfd < 0)
		*newfd = hold_locked_index(&lock_file, 1);
	return 0;
}

static int option_parse_z(const struct option *opt,
			  const char *arg, int unset)
{
	if (unset)
		line_termination = '\n';
	else
		line_termination = 0;
	return 0;
}

static int option_parse_prefix(const struct option *opt,
			       const char *arg, int unset)
{
	state.base_dir = arg;
	state.base_dir_len = strlen(arg);
	return 0;
}

static int option_parse_stage(const struct option *opt,
			      const char *arg, int unset)
{
	if (!strcmp(arg, "all")) {
		to_tempfile = 1;
		checkout_stage = CHECKOUT_ALL;
	} else {
		int ch = arg[0];
		if ('1' <= ch && ch <= '3')
			checkout_stage = arg[0] - '0';
		else
			die("stage should be between 1 and 3 or all");
	}
	return 0;
}

int cmd_checkout_index(int argc, const char **argv, const char *prefix)
{
	int i;
	int newfd = -1;
	int all = 0;
	int read_from_stdin = 0;
	int prefix_length;
	int force = 0, quiet = 0, not_new = 0;
	struct option builtin_checkout_index_options[] = {
		OPT_BOOLEAN('a', "all", &all,
			"checks out all files in the index"),
		OPT__FORCE(&force, "forces overwrite of existing files"),
		OPT__QUIET(&quiet,
			"no warning for existing files and files not in index"),
		OPT_BOOLEAN('n', "no-create", &not_new,
			"don't checkout new files"),
		{ OPTION_CALLBACK, 'u', "index", &newfd, NULL,
			"update stat information in the index file",
			PARSE_OPT_NOARG, option_parse_u },
		{ OPTION_CALLBACK, 'z', NULL, NULL, NULL,
			"paths are separated with NUL character",
			PARSE_OPT_NOARG, option_parse_z },
		OPT_BOOLEAN(0, "stdin", &read_from_stdin,
			"read list of paths from the standard input"),
		OPT_BOOLEAN(0, "temp", &to_tempfile,
			"write the content to temporary files"),
		OPT_CALLBACK(0, "prefix", NULL, "string",
			"when creating files, prepend <string>",
			option_parse_prefix),
		OPT_CALLBACK(0, "stage", NULL, NULL,
			"copy out the files from named stage",
			option_parse_stage),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_checkout_index_usage,
				   builtin_checkout_index_options);
	git_config(git_default_config, NULL);
	state.base_dir = "";
	prefix_length = prefix ? strlen(prefix) : 0;

	if (read_cache() < 0) {
		die("invalid cache");
	}

	argc = parse_options(argc, argv, prefix, builtin_checkout_index_options,
			builtin_checkout_index_usage, 0);
	state.force = force;
	state.quiet = quiet;
	state.not_new = not_new;

	if (state.base_dir_len || to_tempfile) {
		/* when --prefix is specified we do not
		 * want to update cache.
		 */
		if (state.refresh_cache) {
			rollback_lock_file(&lock_file);
			newfd = -1;
		}
		state.refresh_cache = 0;
	}

	/* Check out named files first */
	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		const char *p;

		if (all)
			die("git checkout-index: don't mix '--all' and explicit filenames");
		if (read_from_stdin)
			die("git checkout-index: don't mix '--stdin' and explicit filenames");
		p = prefix_path(prefix, prefix_length, arg);
		checkout_file(p, prefix_length);
		if (p < arg || p > arg + strlen(arg))
			free((char *)p);
	}

	if (read_from_stdin) {
		struct strbuf buf = STRBUF_INIT, nbuf = STRBUF_INIT;

		if (all)
			die("git checkout-index: don't mix '--all' and '--stdin'");

		while (strbuf_getline(&buf, stdin, line_termination) != EOF) {
			const char *p;
			if (line_termination && buf.buf[0] == '"') {
				strbuf_reset(&nbuf);
				if (unquote_c_style(&nbuf, buf.buf, NULL))
					die("line is badly quoted");
				strbuf_swap(&buf, &nbuf);
			}
			p = prefix_path(prefix, prefix_length, buf.buf);
			checkout_file(p, prefix_length);
			if (p < buf.buf || p > buf.buf + buf.len)
				free((char *)p);
		}
		strbuf_release(&nbuf);
		strbuf_release(&buf);
	}

	if (all)
		checkout_all(prefix, prefix_length);

	if (0 <= newfd &&
	    (write_cache(newfd, active_cache, active_nr) ||
	     commit_locked_index(&lock_file)))
		die("Unable to write new index file");
	return 0;
}
