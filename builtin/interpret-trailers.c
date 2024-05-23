/*
 * Builtin "git interpret-trailers"
 *
 * Copyright (c) 2013, 2014 Christian Couder <chriscool@tuxfamily.org>
 *
 */

#include "builtin.h"
#include "gettext.h"
#include "parse-options.h"
#include "string-list.h"
#include "tempfile.h"
#include "trailer.h"
#include "config.h"

static const char * const git_interpret_trailers_usage[] = {
	N_("git interpret-trailers [--in-place] [--trim-empty]\n"
	   "                       [(--trailer (<key>|<key-alias>)[(=|:)<value>])...]\n"
	   "                       [--parse] [<file>...]"),
	NULL
};

static enum trailer_where where;
static enum trailer_if_exists if_exists;
static enum trailer_if_missing if_missing;

static int option_parse_where(const struct option *opt,
			      const char *arg, int unset UNUSED)
{
	/* unset implies NULL arg, which is handled in our helper */
	return trailer_set_where(opt->value, arg);
}

static int option_parse_if_exists(const struct option *opt,
				  const char *arg, int unset UNUSED)
{
	/* unset implies NULL arg, which is handled in our helper */
	return trailer_set_if_exists(opt->value, arg);
}

static int option_parse_if_missing(const struct option *opt,
				   const char *arg, int unset UNUSED)
{
	/* unset implies NULL arg, which is handled in our helper */
	return trailer_set_if_missing(opt->value, arg);
}

static void new_trailers_clear(struct list_head *trailers)
{
	struct list_head *pos, *tmp;
	struct new_trailer_item *item;

	list_for_each_safe(pos, tmp, trailers) {
		item = list_entry(pos, struct new_trailer_item, list);
		list_del(pos);
		free(item);
	}
}

static int option_parse_trailer(const struct option *opt,
				   const char *arg, int unset)
{
	struct list_head *trailers = opt->value;
	struct new_trailer_item *item;

	if (unset) {
		new_trailers_clear(trailers);
		return 0;
	}

	if (!arg)
		return -1;

	item = xmalloc(sizeof(*item));
	item->text = arg;
	item->where = where;
	item->if_exists = if_exists;
	item->if_missing = if_missing;
	list_add_tail(&item->list, trailers);
	return 0;
}

static int parse_opt_parse(const struct option *opt, const char *arg,
			   int unset)
{
	struct process_trailer_options *v = opt->value;
	v->only_trailers = 1;
	v->only_input = 1;
	v->unfold = 1;
	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);
	return 0;
}

static struct tempfile *trailers_tempfile;

static FILE *create_in_place_tempfile(const char *file)
{
	struct stat st;
	struct strbuf filename_template = STRBUF_INIT;
	const char *tail;
	FILE *outfile;

	if (stat(file, &st))
		die_errno(_("could not stat %s"), file);
	if (!S_ISREG(st.st_mode))
		die(_("file %s is not a regular file"), file);
	if (!(st.st_mode & S_IWUSR))
		die(_("file %s is not writable by user"), file);

	/* Create temporary file in the same directory as the original */
	tail = strrchr(file, '/');
	if (tail)
		strbuf_add(&filename_template, file, tail - file + 1);
	strbuf_addstr(&filename_template, "git-interpret-trailers-XXXXXX");

	trailers_tempfile = xmks_tempfile_m(filename_template.buf, st.st_mode);
	strbuf_release(&filename_template);
	outfile = fdopen_tempfile(trailers_tempfile, "w");
	if (!outfile)
		die_errno(_("could not open temporary file"));

	return outfile;
}

static void read_input_file(struct strbuf *sb, const char *file)
{
	if (file) {
		if (strbuf_read_file(sb, file, 0) < 0)
			die_errno(_("could not read input file '%s'"), file);
	} else {
		if (strbuf_read(sb, fileno(stdin), 0) < 0)
			die_errno(_("could not read from stdin"));
	}
}

static void interpret_trailers(const struct process_trailer_options *opts,
			       struct list_head *new_trailer_head,
			       const char *file)
{
	LIST_HEAD(head);
	struct strbuf sb = STRBUF_INIT;
	struct strbuf trailer_block = STRBUF_INIT;
	struct trailer_info *info;
	FILE *outfile = stdout;

	trailer_config_init();

	read_input_file(&sb, file);

	if (opts->in_place)
		outfile = create_in_place_tempfile(file);

	info = parse_trailers(opts, sb.buf, &head);

	/* Print the lines before the trailers */
	if (!opts->only_trailers)
		fwrite(sb.buf, 1, trailer_block_start(info), outfile);

	if (!opts->only_trailers && !blank_line_before_trailer_block(info))
		fprintf(outfile, "\n");


	if (!opts->only_input) {
		LIST_HEAD(config_head);
		LIST_HEAD(arg_head);
		parse_trailers_from_config(&config_head);
		parse_trailers_from_command_line_args(&arg_head, new_trailer_head);
		list_splice(&config_head, &arg_head);
		process_trailers_lists(&head, &arg_head);
	}

	/* Print trailer block. */
	format_trailers(opts, &head, &trailer_block);
	free_trailers(&head);
	fwrite(trailer_block.buf, 1, trailer_block.len, outfile);
	strbuf_release(&trailer_block);

	/* Print the lines after the trailers as is */
	if (!opts->only_trailers)
		fwrite(sb.buf + trailer_block_end(info), 1, sb.len - trailer_block_end(info), outfile);
	trailer_info_release(info);

	if (opts->in_place)
		if (rename_tempfile(&trailers_tempfile, file))
			die_errno(_("could not rename temporary file to %s"), file);

	strbuf_release(&sb);
}

int cmd_interpret_trailers(int argc, const char **argv, const char *prefix)
{
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
	LIST_HEAD(trailers);

	struct option options[] = {
		OPT_BOOL(0, "in-place", &opts.in_place, N_("edit files in place")),
		OPT_BOOL(0, "trim-empty", &opts.trim_empty, N_("trim empty trailers")),

		OPT_CALLBACK(0, "where", &where, N_("placement"),
			     N_("where to place the new trailer"), option_parse_where),
		OPT_CALLBACK(0, "if-exists", &if_exists, N_("action"),
			     N_("action if trailer already exists"), option_parse_if_exists),
		OPT_CALLBACK(0, "if-missing", &if_missing, N_("action"),
			     N_("action if trailer is missing"), option_parse_if_missing),

		OPT_BOOL(0, "only-trailers", &opts.only_trailers, N_("output only the trailers")),
		OPT_BOOL(0, "only-input", &opts.only_input, N_("do not apply trailer.* configuration variables")),
		OPT_BOOL(0, "unfold", &opts.unfold, N_("reformat multiline trailer values as single-line values")),
		OPT_CALLBACK_F(0, "parse", &opts, NULL, N_("alias for --only-trailers --only-input --unfold"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG, parse_opt_parse),
		OPT_BOOL(0, "no-divider", &opts.no_divider, N_("do not treat \"---\" as the end of input")),
		OPT_CALLBACK(0, "trailer", &trailers, N_("trailer"),
				N_("trailer(s) to add"), option_parse_trailer),
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options,
			     git_interpret_trailers_usage, 0);

	if (opts.only_input && !list_empty(&trailers))
		usage_msg_opt(
			_("--trailer with --only-input does not make sense"),
			git_interpret_trailers_usage,
			options);

	if (argc) {
		int i;
		for (i = 0; i < argc; i++)
			interpret_trailers(&opts, &trailers, argv[i]);
	} else {
		if (opts.in_place)
			die(_("no input file given for in-place editing"));
		interpret_trailers(&opts, &trailers, NULL);
	}

	new_trailers_clear(&trailers);

	return 0;
}
