#include "git-compat-util.h"
#include "parse-options.h"
#include "strbuf.h"

#define OPT_SHORT 1
#define OPT_UNSET 2

struct optparse_t {
	const char **argv;
	int argc;
	const char *opt;
};

static inline const char *get_arg(struct optparse_t *p)
{
	if (p->opt) {
		const char *res = p->opt;
		p->opt = NULL;
		return res;
	}
	p->argc--;
	return *++p->argv;
}

static inline const char *skip_prefix(const char *str, const char *prefix)
{
	size_t len = strlen(prefix);
	return strncmp(str, prefix, len) ? NULL : str + len;
}

static int opterror(const struct option *opt, const char *reason, int flags)
{
	if (flags & OPT_SHORT)
		return error("switch `%c' %s", opt->short_name, reason);
	if (flags & OPT_UNSET)
		return error("option `no-%s' %s", opt->long_name, reason);
	return error("option `%s' %s", opt->long_name, reason);
}

static int get_value(struct optparse_t *p,
                     const struct option *opt, int flags)
{
	const char *s;

	if (p->opt && (flags & OPT_UNSET))
		return opterror(opt, "takes no value", flags);

	switch (opt->type) {
	case OPTION_BOOLEAN:
		if (!(flags & OPT_SHORT) && p->opt)
			return opterror(opt, "takes no value", flags);
		if (flags & OPT_UNSET)
			*(int *)opt->value = 0;
		else
			(*(int *)opt->value)++;
		return 0;

	case OPTION_STRING:
		if (flags & OPT_UNSET) {
			*(const char **)opt->value = (const char *)NULL;
			return 0;
		}
		if (!p->opt && p->argc <= 1)
			return opterror(opt, "requires a value", flags);
		*(const char **)opt->value = get_arg(p);
		return 0;

	case OPTION_INTEGER:
		if (flags & OPT_UNSET) {
			*(int *)opt->value = 0;
			return 0;
		}
		if (!p->opt && p->argc <= 1)
			return opterror(opt, "requires a value", flags);
		*(int *)opt->value = strtol(get_arg(p), (char **)&s, 10);
		if (*s)
			return opterror(opt, "expects a numerical value", flags);
		return 0;

	default:
		die("should not happen, someone must be hit on the forehead");
	}
}

static int parse_short_opt(struct optparse_t *p, const struct option *options)
{
	for (; options->type != OPTION_END; options++) {
		if (options->short_name == *p->opt) {
			p->opt = p->opt[1] ? p->opt + 1 : NULL;
			return get_value(p, options, OPT_SHORT);
		}
	}
	return error("unknown switch `%c'", *p->opt);
}

static int parse_long_opt(struct optparse_t *p, const char *arg,
                          const struct option *options)
{
	for (; options->type != OPTION_END; options++) {
		const char *rest;
		int flags = 0;

		if (!options->long_name)
			continue;

		rest = skip_prefix(arg, options->long_name);
		if (!rest) {
			if (strncmp(arg, "no-", 3))
				continue;
			flags |= OPT_UNSET;
			rest = skip_prefix(arg + 3, options->long_name);
			if (!rest)
				continue;
		}
		if (*rest) {
			if (*rest != '=')
				continue;
			p->opt = rest + 1;
		}
		return get_value(p, options, flags);
	}
	return error("unknown option `%s'", arg);
}

int parse_options(int argc, const char **argv, const struct option *options,
                  const char *usagestr, int flags)
{
	struct optparse_t args = { argv + 1, argc - 1, NULL };
	int j = 0;

	for (; args.argc; args.argc--, args.argv++) {
		const char *arg = args.argv[0];

		if (*arg != '-' || !arg[1]) {
			argv[j++] = args.argv[0];
			continue;
		}

		if (arg[1] != '-') {
			args.opt = arg + 1;
			do {
				if (*args.opt == 'h')
					usage(usagestr);
				if (parse_short_opt(&args, options) < 0)
					usage(usagestr);
			} while (args.opt);
			continue;
		}

		if (!arg[2]) { /* "--" */
			if (!(flags & PARSE_OPT_KEEP_DASHDASH)) {
				args.argc--;
				args.argv++;
			}
			break;
		}

		if (!strcmp(arg + 2, "help"))
			usage(usagestr);
		if (parse_long_opt(&args, arg + 2, options))
			usage(usagestr);
	}

	memmove(argv + j, args.argv, args.argc * sizeof(*argv));
	argv[j + args.argc] = NULL;
	return j + args.argc;
}
