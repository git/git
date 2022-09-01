#include "git-compat-util.h"
#include "parse-options.h"
#include "cache.h"
#include "config.h"
#include "commit.h"
#include "color.h"
#include "utf8.h"

static int disallow_abbreviated_options;

enum opt_parsed {
	OPT_LONG  = 0,
	OPT_SHORT = 1<<0,
	OPT_UNSET = 1<<1,
};

static void optbug(const struct option *opt, const char *reason)
{
	if (opt->long_name && opt->short_name)
		bug("switch '%c' (--%s) %s", opt->short_name,
		    opt->long_name, reason);
	else if (opt->long_name)
		bug("option '%s' %s", opt->long_name, reason);
	else
		bug("switch '%c' %s", opt->short_name, reason);
}

static const char *optname(const struct option *opt, enum opt_parsed flags)
{
	static struct strbuf sb = STRBUF_INIT;

	strbuf_reset(&sb);
	if (flags & OPT_SHORT)
		strbuf_addf(&sb, "switch `%c'", opt->short_name);
	else if (flags & OPT_UNSET)
		strbuf_addf(&sb, "option `no-%s'", opt->long_name);
	else if (flags == OPT_LONG)
		strbuf_addf(&sb, "option `%s'", opt->long_name);
	else
		BUG("optname() got unknown flags %d", flags);

	return sb.buf;
}

static enum parse_opt_result get_arg(struct parse_opt_ctx_t *p,
				     const struct option *opt,
				     enum opt_parsed flags, const char **arg)
{
	if (p->opt) {
		*arg = p->opt;
		p->opt = NULL;
	} else if (p->argc == 1 && (opt->flags & PARSE_OPT_LASTARG_DEFAULT)) {
		*arg = (const char *)opt->defval;
	} else if (p->argc > 1) {
		p->argc--;
		*arg = *++p->argv;
	} else
		return error(_("%s requires a value"), optname(opt, flags));
	return 0;
}

static void fix_filename(const char *prefix, const char **file)
{
	if (!file || !*file || !prefix || is_absolute_path(*file)
	    || !strcmp("-", *file))
		return;
	*file = prefix_filename(prefix, *file);
}

static enum parse_opt_result opt_command_mode_error(
	const struct option *opt,
	const struct option *all_opts,
	enum opt_parsed flags)
{
	const struct option *that;
	struct strbuf that_name = STRBUF_INIT;

	/*
	 * Find the other option that was used to set the variable
	 * already, and report that this is not compatible with it.
	 */
	for (that = all_opts; that->type != OPTION_END; that++) {
		if (that == opt ||
		    !(that->flags & PARSE_OPT_CMDMODE) ||
		    that->value != opt->value ||
		    that->defval != *(int *)opt->value)
			continue;

		if (that->long_name)
			strbuf_addf(&that_name, "--%s", that->long_name);
		else
			strbuf_addf(&that_name, "-%c", that->short_name);
		error(_("%s is incompatible with %s"),
		      optname(opt, flags), that_name.buf);
		strbuf_release(&that_name);
		return PARSE_OPT_ERROR;
	}
	return error(_("%s : incompatible with something else"),
		     optname(opt, flags));
}

static enum parse_opt_result get_value(struct parse_opt_ctx_t *p,
				       const struct option *opt,
				       const struct option *all_opts,
				       enum opt_parsed flags)
{
	const char *s, *arg;
	const int unset = flags & OPT_UNSET;
	int err;

	if (unset && p->opt)
		return error(_("%s takes no value"), optname(opt, flags));
	if (unset && (opt->flags & PARSE_OPT_NONEG))
		return error(_("%s isn't available"), optname(opt, flags));
	if (!(flags & OPT_SHORT) && p->opt && (opt->flags & PARSE_OPT_NOARG))
		return error(_("%s takes no value"), optname(opt, flags));

	/*
	 * Giving the same mode option twice, although unnecessary,
	 * is not a grave error, so let it pass.
	 */
	if ((opt->flags & PARSE_OPT_CMDMODE) &&
	    *(int *)opt->value && *(int *)opt->value != opt->defval)
		return opt_command_mode_error(opt, all_opts, flags);

	switch (opt->type) {
	case OPTION_LOWLEVEL_CALLBACK:
		return opt->ll_callback(p, opt, NULL, unset);

	case OPTION_BIT:
		if (unset)
			*(int *)opt->value &= ~opt->defval;
		else
			*(int *)opt->value |= opt->defval;
		return 0;

	case OPTION_NEGBIT:
		if (unset)
			*(int *)opt->value |= opt->defval;
		else
			*(int *)opt->value &= ~opt->defval;
		return 0;

	case OPTION_BITOP:
		if (unset)
			BUG("BITOP can't have unset form");
		*(int *)opt->value &= ~opt->extra;
		*(int *)opt->value |= opt->defval;
		return 0;

	case OPTION_COUNTUP:
		if (*(int *)opt->value < 0)
			*(int *)opt->value = 0;
		*(int *)opt->value = unset ? 0 : *(int *)opt->value + 1;
		return 0;

	case OPTION_SET_INT:
		*(int *)opt->value = unset ? 0 : opt->defval;
		return 0;

	case OPTION_STRING:
		if (unset)
			*(const char **)opt->value = NULL;
		else if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			*(const char **)opt->value = (const char *)opt->defval;
		else
			return get_arg(p, opt, flags, (const char **)opt->value);
		return 0;

	case OPTION_FILENAME:
		err = 0;
		if (unset)
			*(const char **)opt->value = NULL;
		else if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			*(const char **)opt->value = (const char *)opt->defval;
		else
			err = get_arg(p, opt, flags, (const char **)opt->value);

		if (!err)
			fix_filename(p->prefix, (const char **)opt->value);
		return err;

	case OPTION_CALLBACK:
	{
		const char *p_arg = NULL;
		int p_unset;

		if (unset)
			p_unset = 1;
		else if (opt->flags & PARSE_OPT_NOARG)
			p_unset = 0;
		else if (opt->flags & PARSE_OPT_OPTARG && !p->opt)
			p_unset = 0;
		else if (get_arg(p, opt, flags, &arg))
			return -1;
		else {
			p_unset = 0;
			p_arg = arg;
		}
		if (opt->callback)
			return (*opt->callback)(opt, p_arg, p_unset) ? (-1) : 0;
		else
			return (*opt->ll_callback)(p, opt, p_arg, p_unset);
	}
	case OPTION_INTEGER:
		if (unset) {
			*(int *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(int *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		if (!*arg)
			return error(_("%s expects a numerical value"),
				     optname(opt, flags));
		*(int *)opt->value = strtol(arg, (char **)&s, 10);
		if (*s)
			return error(_("%s expects a numerical value"),
				     optname(opt, flags));
		return 0;

	case OPTION_MAGNITUDE:
		if (unset) {
			*(unsigned long *)opt->value = 0;
			return 0;
		}
		if (opt->flags & PARSE_OPT_OPTARG && !p->opt) {
			*(unsigned long *)opt->value = opt->defval;
			return 0;
		}
		if (get_arg(p, opt, flags, &arg))
			return -1;
		if (!git_parse_ulong(arg, opt->value))
			return error(_("%s expects a non-negative integer value"
				       " with an optional k/m/g suffix"),
				     optname(opt, flags));
		return 0;

	default:
		BUG("opt->type %d should not happen", opt->type);
	}
}

static enum parse_opt_result parse_short_opt(struct parse_opt_ctx_t *p,
					     const struct option *options)
{
	const struct option *all_opts = options;
	const struct option *numopt = NULL;

	for (; options->type != OPTION_END; options++) {
		if (options->short_name == *p->opt) {
			p->opt = p->opt[1] ? p->opt + 1 : NULL;
			return get_value(p, options, all_opts, OPT_SHORT);
		}

		/*
		 * Handle the numerical option later, explicit one-digit
		 * options take precedence over it.
		 */
		if (options->type == OPTION_NUMBER)
			numopt = options;
	}
	if (numopt && isdigit(*p->opt)) {
		size_t len = 1;
		char *arg;
		int rc;

		while (isdigit(p->opt[len]))
			len++;
		arg = xmemdupz(p->opt, len);
		p->opt = p->opt[len] ? p->opt + len : NULL;
		if (numopt->callback)
			rc = (*numopt->callback)(numopt, arg, 0) ? (-1) : 0;
		else
			rc = (*numopt->ll_callback)(p, numopt, arg, 0);
		free(arg);
		return rc;
	}
	return PARSE_OPT_UNKNOWN;
}

static int has_string(const char *it, const char **array)
{
	while (*array)
		if (!strcmp(it, *(array++)))
			return 1;
	return 0;
}

static int is_alias(struct parse_opt_ctx_t *ctx,
		    const struct option *one_opt,
		    const struct option *another_opt)
{
	const char **group;

	if (!ctx->alias_groups)
		return 0;

	if (!one_opt->long_name || !another_opt->long_name)
		return 0;

	for (group = ctx->alias_groups; *group; group += 3) {
		/* it and other are from the same family? */
		if (has_string(one_opt->long_name, group) &&
		    has_string(another_opt->long_name, group))
			return 1;
	}
	return 0;
}

static enum parse_opt_result parse_long_opt(
	struct parse_opt_ctx_t *p, const char *arg,
	const struct option *options)
{
	const struct option *all_opts = options;
	const char *arg_end = strchrnul(arg, '=');
	const struct option *abbrev_option = NULL, *ambiguous_option = NULL;
	enum opt_parsed abbrev_flags = OPT_LONG, ambiguous_flags = OPT_LONG;

	for (; options->type != OPTION_END; options++) {
		const char *rest, *long_name = options->long_name;
		enum opt_parsed flags = OPT_LONG, opt_flags = OPT_LONG;

		if (options->type == OPTION_SUBCOMMAND)
			continue;
		if (!long_name)
			continue;

again:
		if (!skip_prefix(arg, long_name, &rest))
			rest = NULL;
		if (!rest) {
			/* abbreviated? */
			if (!(p->flags & PARSE_OPT_KEEP_UNKNOWN_OPT) &&
			    !strncmp(long_name, arg, arg_end - arg)) {
is_abbreviated:
				if (abbrev_option &&
				    !is_alias(p, abbrev_option, options)) {
					/*
					 * If this is abbreviated, it is
					 * ambiguous. So when there is no
					 * exact match later, we need to
					 * error out.
					 */
					ambiguous_option = abbrev_option;
					ambiguous_flags = abbrev_flags;
				}
				if (!(flags & OPT_UNSET) && *arg_end)
					p->opt = arg_end + 1;
				abbrev_option = options;
				abbrev_flags = flags ^ opt_flags;
				continue;
			}
			/* negation allowed? */
			if (options->flags & PARSE_OPT_NONEG)
				continue;
			/* negated and abbreviated very much? */
			if (starts_with("no-", arg)) {
				flags |= OPT_UNSET;
				goto is_abbreviated;
			}
			/* negated? */
			if (!starts_with(arg, "no-")) {
				if (skip_prefix(long_name, "no-", &long_name)) {
					opt_flags |= OPT_UNSET;
					goto again;
				}
				continue;
			}
			flags |= OPT_UNSET;
			if (!skip_prefix(arg + 3, long_name, &rest)) {
				/* abbreviated and negated? */
				if (starts_with(long_name, arg + 3))
					goto is_abbreviated;
				else
					continue;
			}
		}
		if (*rest) {
			if (*rest != '=')
				continue;
			p->opt = rest + 1;
		}
		return get_value(p, options, all_opts, flags ^ opt_flags);
	}

	if (disallow_abbreviated_options && (ambiguous_option || abbrev_option))
		die("disallowed abbreviated or ambiguous option '%.*s'",
		    (int)(arg_end - arg), arg);

	if (ambiguous_option) {
		error(_("ambiguous option: %s "
			"(could be --%s%s or --%s%s)"),
			arg,
			(ambiguous_flags & OPT_UNSET) ?  "no-" : "",
			ambiguous_option->long_name,
			(abbrev_flags & OPT_UNSET) ?  "no-" : "",
			abbrev_option->long_name);
		return PARSE_OPT_HELP;
	}
	if (abbrev_option)
		return get_value(p, abbrev_option, all_opts, abbrev_flags);
	return PARSE_OPT_UNKNOWN;
}

static enum parse_opt_result parse_nodash_opt(struct parse_opt_ctx_t *p,
					      const char *arg,
					      const struct option *options)
{
	const struct option *all_opts = options;

	for (; options->type != OPTION_END; options++) {
		if (!(options->flags & PARSE_OPT_NODASH))
			continue;
		if (options->short_name == arg[0] && arg[1] == '\0')
			return get_value(p, options, all_opts, OPT_SHORT);
	}
	return PARSE_OPT_ERROR;
}

static enum parse_opt_result parse_subcommand(const char *arg,
					      const struct option *options)
{
	for (; options->type != OPTION_END; options++)
		if (options->type == OPTION_SUBCOMMAND &&
		    !strcmp(options->long_name, arg)) {
			*(parse_opt_subcommand_fn **)options->value = options->subcommand_fn;
			return PARSE_OPT_SUBCOMMAND;
		}

	return PARSE_OPT_UNKNOWN;
}

static void check_typos(const char *arg, const struct option *options)
{
	if (strlen(arg) < 3)
		return;

	if (starts_with(arg, "no-")) {
		error(_("did you mean `--%s` (with two dashes)?"), arg);
		exit(129);
	}

	for (; options->type != OPTION_END; options++) {
		if (!options->long_name)
			continue;
		if (starts_with(options->long_name, arg)) {
			error(_("did you mean `--%s` (with two dashes)?"), arg);
			exit(129);
		}
	}
}

static void parse_options_check(const struct option *opts)
{
	char short_opts[128];
	void *subcommand_value = NULL;

	memset(short_opts, '\0', sizeof(short_opts));
	for (; opts->type != OPTION_END; opts++) {
		if ((opts->flags & PARSE_OPT_LASTARG_DEFAULT) &&
		    (opts->flags & PARSE_OPT_OPTARG))
			optbug(opts, "uses incompatible flags "
			       "LASTARG_DEFAULT and OPTARG");
		if (opts->short_name) {
			if (0x7F <= opts->short_name)
				optbug(opts, "invalid short name");
			else if (short_opts[opts->short_name]++)
				optbug(opts, "short name already used");
		}
		if (opts->flags & PARSE_OPT_NODASH &&
		    ((opts->flags & PARSE_OPT_OPTARG) ||
		     !(opts->flags & PARSE_OPT_NOARG) ||
		     !(opts->flags & PARSE_OPT_NONEG) ||
		     opts->long_name))
			optbug(opts, "uses feature "
			       "not supported for dashless options");
		switch (opts->type) {
		case OPTION_COUNTUP:
		case OPTION_BIT:
		case OPTION_NEGBIT:
		case OPTION_SET_INT:
		case OPTION_NUMBER:
			if ((opts->flags & PARSE_OPT_OPTARG) ||
			    !(opts->flags & PARSE_OPT_NOARG))
				optbug(opts, "should not accept an argument");
			break;
		case OPTION_CALLBACK:
			if (!opts->callback && !opts->ll_callback)
				optbug(opts, "OPTION_CALLBACK needs one callback");
			else if (opts->callback && opts->ll_callback)
				optbug(opts, "OPTION_CALLBACK can't have two callbacks");
			break;
		case OPTION_LOWLEVEL_CALLBACK:
			if (!opts->ll_callback)
				optbug(opts, "OPTION_LOWLEVEL_CALLBACK needs a callback");
			if (opts->callback)
				optbug(opts, "OPTION_LOWLEVEL_CALLBACK needs no high level callback");
			break;
		case OPTION_ALIAS:
			optbug(opts, "OPT_ALIAS() should not remain at this point. "
			       "Are you using parse_options_step() directly?\n"
			       "That case is not supported yet.");
			break;
		case OPTION_SUBCOMMAND:
			if (!opts->value || !opts->subcommand_fn)
				optbug(opts, "OPTION_SUBCOMMAND needs a value and a subcommand function");
			if (!subcommand_value)
				subcommand_value = opts->value;
			else if (subcommand_value != opts->value)
				optbug(opts, "all OPTION_SUBCOMMANDs need the same value");
			break;
		default:
			; /* ok. (usually accepts an argument) */
		}
		if (opts->argh &&
		    strcspn(opts->argh, " _") != strlen(opts->argh))
			optbug(opts, "multi-word argh should use dash to separate words");
	}
	BUG_if_bug("invalid 'struct option'");
}

static int has_subcommands(const struct option *options)
{
	for (; options->type != OPTION_END; options++)
		if (options->type == OPTION_SUBCOMMAND)
			return 1;
	return 0;
}

static void parse_options_start_1(struct parse_opt_ctx_t *ctx,
				  int argc, const char **argv, const char *prefix,
				  const struct option *options,
				  enum parse_opt_flags flags)
{
	ctx->argc = argc;
	ctx->argv = argv;
	if (!(flags & PARSE_OPT_ONE_SHOT)) {
		ctx->argc--;
		ctx->argv++;
	}
	ctx->total = ctx->argc;
	ctx->out   = argv;
	ctx->prefix = prefix;
	ctx->cpidx = ((flags & PARSE_OPT_KEEP_ARGV0) != 0);
	ctx->flags = flags;
	ctx->has_subcommands = has_subcommands(options);
	if (!ctx->has_subcommands && (flags & PARSE_OPT_SUBCOMMAND_OPTIONAL))
		BUG("Using PARSE_OPT_SUBCOMMAND_OPTIONAL without subcommands");
	if (ctx->has_subcommands) {
		if (flags & PARSE_OPT_STOP_AT_NON_OPTION)
			BUG("subcommands are incompatible with PARSE_OPT_STOP_AT_NON_OPTION");
		if (!(flags & PARSE_OPT_SUBCOMMAND_OPTIONAL)) {
			if (flags & PARSE_OPT_KEEP_UNKNOWN_OPT)
				BUG("subcommands are incompatible with PARSE_OPT_KEEP_UNKNOWN_OPT unless in combination with PARSE_OPT_SUBCOMMAND_OPTIONAL");
			if (flags & PARSE_OPT_KEEP_DASHDASH)
				BUG("subcommands are incompatible with PARSE_OPT_KEEP_DASHDASH unless in combination with PARSE_OPT_SUBCOMMAND_OPTIONAL");
		}
	}
	if ((flags & PARSE_OPT_KEEP_UNKNOWN_OPT) &&
	    (flags & PARSE_OPT_STOP_AT_NON_OPTION) &&
	    !(flags & PARSE_OPT_ONE_SHOT))
		BUG("STOP_AT_NON_OPTION and KEEP_UNKNOWN don't go together");
	if ((flags & PARSE_OPT_ONE_SHOT) &&
	    (flags & PARSE_OPT_KEEP_ARGV0))
		BUG("Can't keep argv0 if you don't have it");
	parse_options_check(options);
}

void parse_options_start(struct parse_opt_ctx_t *ctx,
			 int argc, const char **argv, const char *prefix,
			 const struct option *options,
			 enum parse_opt_flags flags)
{
	memset(ctx, 0, sizeof(*ctx));
	parse_options_start_1(ctx, argc, argv, prefix, options, flags);
}

static void show_negated_gitcomp(const struct option *opts, int show_all,
				 int nr_noopts)
{
	int printed_dashdash = 0;

	for (; opts->type != OPTION_END; opts++) {
		int has_unset_form = 0;
		const char *name;

		if (!opts->long_name)
			continue;
		if (!show_all &&
			(opts->flags & (PARSE_OPT_HIDDEN | PARSE_OPT_NOCOMPLETE)))
			continue;
		if (opts->flags & PARSE_OPT_NONEG)
			continue;

		switch (opts->type) {
		case OPTION_STRING:
		case OPTION_FILENAME:
		case OPTION_INTEGER:
		case OPTION_MAGNITUDE:
		case OPTION_CALLBACK:
		case OPTION_BIT:
		case OPTION_NEGBIT:
		case OPTION_COUNTUP:
		case OPTION_SET_INT:
			has_unset_form = 1;
			break;
		default:
			break;
		}
		if (!has_unset_form)
			continue;

		if (skip_prefix(opts->long_name, "no-", &name)) {
			if (nr_noopts < 0)
				printf(" --%s", name);
		} else if (nr_noopts >= 0) {
			if (nr_noopts && !printed_dashdash) {
				printf(" --");
				printed_dashdash = 1;
			}
			printf(" --no-%s", opts->long_name);
			nr_noopts++;
		}
	}
}

static int show_gitcomp(const struct option *opts, int show_all)
{
	const struct option *original_opts = opts;
	int nr_noopts = 0;

	for (; opts->type != OPTION_END; opts++) {
		const char *prefix = "--";
		const char *suffix = "";

		if (!opts->long_name)
			continue;
		if (!show_all &&
			(opts->flags & (PARSE_OPT_HIDDEN | PARSE_OPT_NOCOMPLETE | PARSE_OPT_FROM_ALIAS)))
			continue;

		switch (opts->type) {
		case OPTION_SUBCOMMAND:
			prefix = "";
			break;
		case OPTION_GROUP:
			continue;
		case OPTION_STRING:
		case OPTION_FILENAME:
		case OPTION_INTEGER:
		case OPTION_MAGNITUDE:
		case OPTION_CALLBACK:
			if (opts->flags & PARSE_OPT_NOARG)
				break;
			if (opts->flags & PARSE_OPT_OPTARG)
				break;
			if (opts->flags & PARSE_OPT_LASTARG_DEFAULT)
				break;
			suffix = "=";
			break;
		default:
			break;
		}
		if (opts->flags & PARSE_OPT_COMP_ARG)
			suffix = "=";
		if (starts_with(opts->long_name, "no-"))
			nr_noopts++;
		printf("%s%s%s%s", opts == original_opts ? "" : " ",
		       prefix, opts->long_name, suffix);
	}
	show_negated_gitcomp(original_opts, show_all, -1);
	show_negated_gitcomp(original_opts, show_all, nr_noopts);
	fputc('\n', stdout);
	return PARSE_OPT_COMPLETE;
}

/*
 * Scan and may produce a new option[] array, which should be used
 * instead of the original 'options'.
 *
 * Right now this is only used to preprocess and substitute
 * OPTION_ALIAS.
 *
 * The returned options should be freed using free_preprocessed_options.
 */
static struct option *preprocess_options(struct parse_opt_ctx_t *ctx,
					 const struct option *options)
{
	struct option *newopt;
	int i, nr, alias;
	int nr_aliases = 0;

	for (nr = 0; options[nr].type != OPTION_END; nr++) {
		if (options[nr].type == OPTION_ALIAS)
			nr_aliases++;
	}

	if (!nr_aliases)
		return NULL;

	ALLOC_ARRAY(newopt, nr + 1);
	COPY_ARRAY(newopt, options, nr + 1);

	/* each alias has two string pointers and NULL */
	CALLOC_ARRAY(ctx->alias_groups, 3 * (nr_aliases + 1));

	for (alias = 0, i = 0; i < nr; i++) {
		int short_name;
		const char *long_name;
		const char *source;
		struct strbuf help = STRBUF_INIT;
		int j;

		if (newopt[i].type != OPTION_ALIAS)
			continue;

		short_name = newopt[i].short_name;
		long_name = newopt[i].long_name;
		source = newopt[i].value;

		if (!long_name)
			BUG("An alias must have long option name");
		strbuf_addf(&help, _("alias of --%s"), source);

		for (j = 0; j < nr; j++) {
			const char *name = options[j].long_name;

			if (!name || strcmp(name, source))
				continue;

			if (options[j].type == OPTION_ALIAS)
				BUG("No please. Nested aliases are not supported.");

			memcpy(newopt + i, options + j, sizeof(*newopt));
			newopt[i].short_name = short_name;
			newopt[i].long_name = long_name;
			newopt[i].help = strbuf_detach(&help, NULL);
			newopt[i].flags |= PARSE_OPT_FROM_ALIAS;
			break;
		}

		if (j == nr)
			BUG("could not find source option '%s' of alias '%s'",
			    source, newopt[i].long_name);
		ctx->alias_groups[alias * 3 + 0] = newopt[i].long_name;
		ctx->alias_groups[alias * 3 + 1] = options[j].long_name;
		ctx->alias_groups[alias * 3 + 2] = NULL;
		alias++;
	}

	return newopt;
}

static void free_preprocessed_options(struct option *options)
{
	int i;

	if (!options)
		return;

	for (i = 0; options[i].type != OPTION_END; i++) {
		if (options[i].flags & PARSE_OPT_FROM_ALIAS)
			free((void *)options[i].help);
	}
	free(options);
}

static enum parse_opt_result usage_with_options_internal(struct parse_opt_ctx_t *,
							 const char * const *,
							 const struct option *,
							 int, int);

enum parse_opt_result parse_options_step(struct parse_opt_ctx_t *ctx,
					 const struct option *options,
					 const char * const usagestr[])
{
	int internal_help = !(ctx->flags & PARSE_OPT_NO_INTERNAL_HELP);

	/* we must reset ->opt, unknown short option leave it dangling */
	ctx->opt = NULL;

	for (; ctx->argc; ctx->argc--, ctx->argv++) {
		const char *arg = ctx->argv[0];

		if (ctx->flags & PARSE_OPT_ONE_SHOT &&
		    ctx->argc != ctx->total)
			break;

		if (*arg != '-' || !arg[1]) {
			if (parse_nodash_opt(ctx, arg, options) == 0)
				continue;
			if (!ctx->has_subcommands) {
				if (ctx->flags & PARSE_OPT_STOP_AT_NON_OPTION)
					return PARSE_OPT_NON_OPTION;
				ctx->out[ctx->cpidx++] = ctx->argv[0];
				continue;
			}
			switch (parse_subcommand(arg, options)) {
			case PARSE_OPT_SUBCOMMAND:
				return PARSE_OPT_SUBCOMMAND;
			case PARSE_OPT_UNKNOWN:
				if (ctx->flags & PARSE_OPT_SUBCOMMAND_OPTIONAL)
					/*
					 * arg is neither a short or long
					 * option nor a subcommand.  Since
					 * this command has a default
					 * operation mode, we have to treat
					 * this arg and all remaining args
					 * as args meant to that default
					 * operation mode.
					 * So we are done parsing.
					 */
					return PARSE_OPT_DONE;
				error(_("unknown subcommand: `%s'"), arg);
				usage_with_options(usagestr, options);
			case PARSE_OPT_COMPLETE:
			case PARSE_OPT_HELP:
			case PARSE_OPT_ERROR:
			case PARSE_OPT_DONE:
			case PARSE_OPT_NON_OPTION:
				/* Impossible. */
				BUG("parse_subcommand() cannot return these");
			}
		}

		/* lone -h asks for help */
		if (internal_help && ctx->total == 1 && !strcmp(arg + 1, "h"))
			goto show_usage;

		/*
		 * lone --git-completion-helper and --git-completion-helper-all
		 * are asked by git-completion.bash
		 */
		if (ctx->total == 1 && !strcmp(arg, "--git-completion-helper"))
			return show_gitcomp(options, 0);
		if (ctx->total == 1 && !strcmp(arg, "--git-completion-helper-all"))
			return show_gitcomp(options, 1);

		if (arg[1] != '-') {
			ctx->opt = arg + 1;
			switch (parse_short_opt(ctx, options)) {
			case PARSE_OPT_ERROR:
				return PARSE_OPT_ERROR;
			case PARSE_OPT_UNKNOWN:
				if (ctx->opt)
					check_typos(arg + 1, options);
				if (internal_help && *ctx->opt == 'h')
					goto show_usage;
				goto unknown;
			case PARSE_OPT_NON_OPTION:
			case PARSE_OPT_SUBCOMMAND:
			case PARSE_OPT_HELP:
			case PARSE_OPT_COMPLETE:
				BUG("parse_short_opt() cannot return these");
			case PARSE_OPT_DONE:
				break;
			}
			if (ctx->opt)
				check_typos(arg + 1, options);
			while (ctx->opt) {
				switch (parse_short_opt(ctx, options)) {
				case PARSE_OPT_ERROR:
					return PARSE_OPT_ERROR;
				case PARSE_OPT_UNKNOWN:
					if (internal_help && *ctx->opt == 'h')
						goto show_usage;

					/* fake a short option thing to hide the fact that we may have
					 * started to parse aggregated stuff
					 *
					 * This is leaky, too bad.
					 */
					ctx->argv[0] = xstrdup(ctx->opt - 1);
					*(char *)ctx->argv[0] = '-';
					goto unknown;
				case PARSE_OPT_NON_OPTION:
				case PARSE_OPT_SUBCOMMAND:
				case PARSE_OPT_COMPLETE:
				case PARSE_OPT_HELP:
					BUG("parse_short_opt() cannot return these");
				case PARSE_OPT_DONE:
					break;
				}
			}
			continue;
		}

		if (!arg[2] /* "--" */ ||
		    !strcmp(arg + 2, "end-of-options")) {
			if (!(ctx->flags & PARSE_OPT_KEEP_DASHDASH)) {
				ctx->argc--;
				ctx->argv++;
			}
			break;
		}

		if (internal_help && !strcmp(arg + 2, "help-all"))
			return usage_with_options_internal(ctx, usagestr, options, 1, 0);
		if (internal_help && !strcmp(arg + 2, "help"))
			goto show_usage;
		switch (parse_long_opt(ctx, arg + 2, options)) {
		case PARSE_OPT_ERROR:
			return PARSE_OPT_ERROR;
		case PARSE_OPT_UNKNOWN:
			goto unknown;
		case PARSE_OPT_HELP:
			goto show_usage;
		case PARSE_OPT_NON_OPTION:
		case PARSE_OPT_SUBCOMMAND:
		case PARSE_OPT_COMPLETE:
			BUG("parse_long_opt() cannot return these");
		case PARSE_OPT_DONE:
			break;
		}
		continue;
unknown:
		if (ctx->flags & PARSE_OPT_ONE_SHOT)
			break;
		if (ctx->has_subcommands &&
		    (ctx->flags & PARSE_OPT_SUBCOMMAND_OPTIONAL) &&
		    (ctx->flags & PARSE_OPT_KEEP_UNKNOWN_OPT)) {
			/*
			 * Found an unknown option given to a command with
			 * subcommands that has a default operation mode:
			 * we treat this option and all remaining args as
			 * arguments meant to that default operation mode.
			 * So we are done parsing.
			 */
			return PARSE_OPT_DONE;
		}
		if (!(ctx->flags & PARSE_OPT_KEEP_UNKNOWN_OPT))
			return PARSE_OPT_UNKNOWN;
		ctx->out[ctx->cpidx++] = ctx->argv[0];
		ctx->opt = NULL;
	}
	return PARSE_OPT_DONE;

 show_usage:
	return usage_with_options_internal(ctx, usagestr, options, 0, 0);
}

int parse_options_end(struct parse_opt_ctx_t *ctx)
{
	if (ctx->flags & PARSE_OPT_ONE_SHOT)
		return ctx->total - ctx->argc;

	MOVE_ARRAY(ctx->out + ctx->cpidx, ctx->argv, ctx->argc);
	ctx->out[ctx->cpidx + ctx->argc] = NULL;
	return ctx->cpidx + ctx->argc;
}

int parse_options(int argc, const char **argv,
		  const char *prefix,
		  const struct option *options,
		  const char * const usagestr[],
		  enum parse_opt_flags flags)
{
	struct parse_opt_ctx_t ctx;
	struct option *real_options;

	disallow_abbreviated_options =
		git_env_bool("GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS", 0);

	memset(&ctx, 0, sizeof(ctx));
	real_options = preprocess_options(&ctx, options);
	if (real_options)
		options = real_options;
	parse_options_start_1(&ctx, argc, argv, prefix, options, flags);
	switch (parse_options_step(&ctx, options, usagestr)) {
	case PARSE_OPT_HELP:
	case PARSE_OPT_ERROR:
		exit(129);
	case PARSE_OPT_COMPLETE:
		exit(0);
	case PARSE_OPT_NON_OPTION:
	case PARSE_OPT_SUBCOMMAND:
		break;
	case PARSE_OPT_DONE:
		if (ctx.has_subcommands &&
		    !(flags & PARSE_OPT_SUBCOMMAND_OPTIONAL)) {
			error(_("need a subcommand"));
			usage_with_options(usagestr, options);
		}
		break;
	case PARSE_OPT_UNKNOWN:
		if (ctx.argv[0][1] == '-') {
			error(_("unknown option `%s'"), ctx.argv[0] + 2);
		} else if (isascii(*ctx.opt)) {
			error(_("unknown switch `%c'"), *ctx.opt);
		} else {
			error(_("unknown non-ascii option in string: `%s'"),
			      ctx.argv[0]);
		}
		usage_with_options(usagestr, options);
	}

	precompose_argv_prefix(argc, argv, NULL);
	free_preprocessed_options(real_options);
	free(ctx.alias_groups);
	return parse_options_end(&ctx);
}

static int usage_argh(const struct option *opts, FILE *outfile)
{
	const char *s;
	int literal = (opts->flags & PARSE_OPT_LITERAL_ARGHELP) ||
		!opts->argh || !!strpbrk(opts->argh, "()<>[]|");
	if (opts->flags & PARSE_OPT_OPTARG)
		if (opts->long_name)
			s = literal ? "[=%s]" : "[=<%s>]";
		else
			s = literal ? "[%s]" : "[<%s>]";
	else
		s = literal ? " %s" : " <%s>";
	return utf8_fprintf(outfile, s, opts->argh ? _(opts->argh) : _("..."));
}

#define USAGE_OPTS_WIDTH 24
#define USAGE_GAP         2

static enum parse_opt_result usage_with_options_internal(struct parse_opt_ctx_t *ctx,
							 const char * const *usagestr,
							 const struct option *opts,
							 int full, int err)
{
	FILE *outfile = err ? stderr : stdout;
	int need_newline;

	const char *usage_prefix = _("usage: %s");
	/*
	 * The translation could be anything, but we can count on
	 * msgfmt(1)'s --check option to have asserted that "%s" is in
	 * the translation. So compute the length of the "usage: "
	 * part. We are assuming that the translator wasn't overly
	 * clever and used e.g. "%1$s" instead of "%s", there's only
	 * one "%s" in "usage_prefix" above, so there's no reason to
	 * do so even with a RTL language.
	 */
	size_t usage_len = strlen(usage_prefix) - strlen("%s");
	/*
	 * TRANSLATORS: the colon here should align with the
	 * one in "usage: %s" translation.
	 */
	const char *or_prefix = _("   or: %s");
	/*
	 * TRANSLATORS: You should only need to translate this format
	 * string if your language is a RTL language (e.g. Arabic,
	 * Hebrew etc.), not if it's a LTR language (e.g. German,
	 * Russian, Chinese etc.).
	 *
	 * When a translated usage string has an embedded "\n" it's
	 * because options have wrapped to the next line. The line
	 * after the "\n" will then be padded to align with the
	 * command name, such as N_("git cmd [opt]\n<8
	 * spaces>[opt2]"), where the 8 spaces are the same length as
	 * "git cmd ".
	 *
	 * This format string prints out that already-translated
	 * line. The "%*s" is whitespace padding to account for the
	 * padding at the start of the line that we add in this
	 * function. The "%s" is a line in the (hopefully already
	 * translated) N_() usage string, which contained embedded
	 * newlines before we split it up.
	 */
	const char *usage_continued = _("%*s%s");
	const char *prefix = usage_prefix;
	int saw_empty_line = 0;

	if (!usagestr)
		return PARSE_OPT_HELP;

	if (!err && ctx && ctx->flags & PARSE_OPT_SHELL_EVAL)
		fprintf(outfile, "cat <<\\EOF\n");

	while (*usagestr) {
		const char *str = _(*usagestr++);
		struct string_list list = STRING_LIST_INIT_DUP;
		unsigned int j;

		if (!saw_empty_line && !*str)
			saw_empty_line = 1;

		string_list_split(&list, str, '\n', -1);
		for (j = 0; j < list.nr; j++) {
			const char *line = list.items[j].string;

			if (saw_empty_line && *line)
				fprintf_ln(outfile, _("    %s"), line);
			else if (saw_empty_line)
				fputc('\n', outfile);
			else if (!j)
				fprintf_ln(outfile, prefix, line);
			else
				fprintf_ln(outfile, usage_continued,
					   (int)usage_len, "", line);
		}
		string_list_clear(&list, 0);

		prefix = or_prefix;
	}

	need_newline = 1;

	for (; opts->type != OPTION_END; opts++) {
		size_t pos;
		int pad;

		if (opts->type == OPTION_SUBCOMMAND)
			continue;
		if (opts->type == OPTION_GROUP) {
			fputc('\n', outfile);
			need_newline = 0;
			if (*opts->help)
				fprintf(outfile, "%s\n", _(opts->help));
			continue;
		}
		if (!full && (opts->flags & PARSE_OPT_HIDDEN))
			continue;

		if (need_newline) {
			fputc('\n', outfile);
			need_newline = 0;
		}

		pos = fprintf(outfile, "    ");
		if (opts->short_name) {
			if (opts->flags & PARSE_OPT_NODASH)
				pos += fprintf(outfile, "%c", opts->short_name);
			else
				pos += fprintf(outfile, "-%c", opts->short_name);
		}
		if (opts->long_name && opts->short_name)
			pos += fprintf(outfile, ", ");
		if (opts->long_name)
			pos += fprintf(outfile, "--%s", opts->long_name);
		if (opts->type == OPTION_NUMBER)
			pos += utf8_fprintf(outfile, _("-NUM"));

		if ((opts->flags & PARSE_OPT_LITERAL_ARGHELP) ||
		    !(opts->flags & PARSE_OPT_NOARG))
			pos += usage_argh(opts, outfile);

		if (pos <= USAGE_OPTS_WIDTH)
			pad = USAGE_OPTS_WIDTH - pos;
		else {
			fputc('\n', outfile);
			pad = USAGE_OPTS_WIDTH;
		}
		if (opts->type == OPTION_ALIAS) {
			fprintf(outfile, "%*s", pad + USAGE_GAP, "");
			fprintf_ln(outfile, _("alias of --%s"),
				   (const char *)opts->value);
			continue;
		}
		fprintf(outfile, "%*s%s\n", pad + USAGE_GAP, "", _(opts->help));
	}
	fputc('\n', outfile);

	if (!err && ctx && ctx->flags & PARSE_OPT_SHELL_EVAL)
		fputs("EOF\n", outfile);

	return PARSE_OPT_HELP;
}

void NORETURN usage_with_options(const char * const *usagestr,
			const struct option *opts)
{
	usage_with_options_internal(NULL, usagestr, opts, 0, 1);
	exit(129);
}

void NORETURN usage_msg_opt(const char *msg,
		   const char * const *usagestr,
		   const struct option *options)
{
	die_message("%s\n", msg); /* The extra \n is intentional */
	usage_with_options(usagestr, options);
}

void NORETURN usage_msg_optf(const char * const fmt,
			     const char * const *usagestr,
			     const struct option *options, ...)
{
	struct strbuf msg = STRBUF_INIT;
	va_list ap;
	va_start(ap, options);
	strbuf_vaddf(&msg, fmt, ap);
	va_end(ap);

	usage_msg_opt(msg.buf, usagestr, options);
}

void die_for_incompatible_opt4(int opt1, const char *opt1_name,
			       int opt2, const char *opt2_name,
			       int opt3, const char *opt3_name,
			       int opt4, const char *opt4_name)
{
	int count = 0;
	const char *options[4];

	if (opt1)
		options[count++] = opt1_name;
	if (opt2)
		options[count++] = opt2_name;
	if (opt3)
		options[count++] = opt3_name;
	if (opt4)
		options[count++] = opt4_name;
	switch (count) {
	case 4:
		die(_("options '%s', '%s', '%s', and '%s' cannot be used together"),
		    opt1_name, opt2_name, opt3_name, opt4_name);
		break;
	case 3:
		die(_("options '%s', '%s', and '%s' cannot be used together"),
		    options[0], options[1], options[2]);
		break;
	case 2:
		die(_("options '%s' and '%s' cannot be used together"),
		    options[0], options[1]);
		break;
	default:
		break;
	}
}
