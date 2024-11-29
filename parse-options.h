#ifndef PARSE_OPTIONS_H
#define PARSE_OPTIONS_H

#include "gettext.h"

struct repository;

/**
 * Refer to Documentation/technical/api-parse-options.txt for the API doc.
 */

enum parse_opt_type {
	/* special types */
	OPTION_END,
	OPTION_GROUP,
	OPTION_NUMBER,
	OPTION_ALIAS,
	OPTION_SUBCOMMAND,
	/* options with no arguments */
	OPTION_BIT,
	OPTION_NEGBIT,
	OPTION_BITOP,
	OPTION_COUNTUP,
	OPTION_SET_INT,
	/* options with arguments (usually) */
	OPTION_STRING,
	OPTION_INTEGER,
	OPTION_MAGNITUDE,
	OPTION_CALLBACK,
	OPTION_LOWLEVEL_CALLBACK,
	OPTION_FILENAME
};

enum parse_opt_flags {
	PARSE_OPT_KEEP_DASHDASH = 1 << 0,
	PARSE_OPT_STOP_AT_NON_OPTION = 1 << 1,
	PARSE_OPT_KEEP_ARGV0 = 1 << 2,
	PARSE_OPT_KEEP_UNKNOWN_OPT = 1 << 3,
	PARSE_OPT_NO_INTERNAL_HELP = 1 << 4,
	PARSE_OPT_ONE_SHOT = 1 << 5,
	PARSE_OPT_SHELL_EVAL = 1 << 6,
	PARSE_OPT_SUBCOMMAND_OPTIONAL = 1 << 7,
};

enum parse_opt_option_flags {
	PARSE_OPT_OPTARG  = 1 << 0,
	PARSE_OPT_NOARG   = 1 << 1,
	PARSE_OPT_NONEG   = 1 << 2,
	PARSE_OPT_HIDDEN  = 1 << 3,
	PARSE_OPT_LASTARG_DEFAULT = 1 << 4,
	PARSE_OPT_NODASH = 1 << 5,
	PARSE_OPT_LITERAL_ARGHELP = 1 << 6,
	PARSE_OPT_FROM_ALIAS = 1 << 7,
	PARSE_OPT_NOCOMPLETE = 1 << 9,
	PARSE_OPT_COMP_ARG = 1 << 10,
	PARSE_OPT_CMDMODE = 1 << 11,
};

enum parse_opt_result {
	PARSE_OPT_COMPLETE = -3,
	PARSE_OPT_HELP = -2,
	PARSE_OPT_ERROR = -1,	/* must be the same as error() */
	PARSE_OPT_DONE = 0,	/* fixed so that "return 0" works */
	PARSE_OPT_NON_OPTION,
	PARSE_OPT_SUBCOMMAND,
	PARSE_OPT_UNKNOWN
};

struct option;
typedef int parse_opt_cb(const struct option *, const char *arg, int unset);

struct parse_opt_ctx_t;
typedef enum parse_opt_result parse_opt_ll_cb(struct parse_opt_ctx_t *ctx,
					      const struct option *opt,
					      const char *arg, int unset);

typedef int parse_opt_subcommand_fn(int argc, const char **argv,
				    const char *prefix, struct repository *repo);

/*
 * `type`::
 *   holds the type of the option, you must have an OPTION_END last in your
 *   array.
 *
 * `short_name`::
 *   the character to use as a short option name, '\0' if none.
 *
 * `long_name`::
 *   the long option (without the leading dashes) or subcommand name,
 *   NULL if none.
 *
 * `value`::
 *   stores pointers to the values to be filled.
 *
 * `argh`::
 *   token to explain the kind of argument this option wants. Does not
 *   begin in capital letter, and does not end with a full stop.
 *   Should be wrapped by N_() for translation.
 *   Is automatically enclosed in brackets when printed, unless it
 *   contains any of the following characters: ()<>[]|
 *   E.g. "name" is shown as "<name>" to indicate that a name value
 *   needs to be supplied, not the literal string "name", but
 *   "<start>,<end>" and "(this|that)" are printed verbatim.
 *
 * `help`::
 *   the short help associated to what the option does.
 *   Must never be NULL (except for OPTION_END and OPTION_SUBCOMMAND).
 *   OPTION_GROUP uses this pointer to store the group header.
 *   Should be wrapped by N_() for translation.
 *
 * `flags`::
 *   mask of parse_opt_option_flags.
 *   PARSE_OPT_OPTARG: says that the argument is optional (not for BOOLEANs)
 *   PARSE_OPT_NOARG: says that this option does not take an argument
 *   PARSE_OPT_NONEG: says that this option cannot be negated
 *   PARSE_OPT_HIDDEN: this option is skipped in the default usage, and
 *                     shown only in the full usage.
 *   PARSE_OPT_LASTARG_DEFAULT: says that this option will take the default
 *				value if no argument is given when the option
 *				is last on the command line. If the option is
 *				not last it will require an argument.
 *				Should not be used with PARSE_OPT_OPTARG.
 *   PARSE_OPT_NODASH: this option doesn't start with a dash; can only be a
 *		       short option and can't accept arguments.
 *   PARSE_OPT_LITERAL_ARGHELP: says that argh shouldn't be enclosed in brackets
 *				(i.e. '<argh>') in the help message.
 *				Useful for options with multiple parameters.
 *   PARSE_OPT_NOCOMPLETE: by default all visible options are completable
 *			   by git-completion.bash. This option suppresses that.
 *   PARSE_OPT_COMP_ARG: this option forces to git-completion.bash to
 *			 complete an option as --name= not --name even if
 *			 the option takes optional argument.
 *
 * `callback`::
 *   pointer to the callback to use for OPTION_CALLBACK
 *
 * `defval`::
 *   default value to fill (*->value) with for PARSE_OPT_OPTARG.
 *   OPTION_{BIT,SET_INT} store the {mask,integer} to put in the value when met.
 *   CALLBACKS can use it like they want.
 *
 * `ll_callback`::
 *   pointer to the callback to use for OPTION_LOWLEVEL_CALLBACK
 *
 * `subcommand_fn`::
 *   pointer to a function to use for OPTION_SUBCOMMAND.
 *   It will be put in value when the subcommand is given on the command line.
 */
struct option {
	enum parse_opt_type type;
	int short_name;
	const char *long_name;
	void *value;
	const char *argh;
	const char *help;

	enum parse_opt_option_flags flags;
	parse_opt_cb *callback;
	intptr_t defval;
	parse_opt_ll_cb *ll_callback;
	intptr_t extra;
	parse_opt_subcommand_fn *subcommand_fn;
};

#define OPT_BIT_F(s, l, v, h, b, f) { \
	.type = OPTION_BIT, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG|(f), \
	.callback = NULL, \
	.defval = (b), \
}
#define OPT_COUNTUP_F(s, l, v, h, f) { \
	.type = OPTION_COUNTUP, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG|(f), \
}
#define OPT_SET_INT_F(s, l, v, h, i, f) { \
	.type = OPTION_SET_INT, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG | (f), \
	.defval = (i), \
}
#define OPT_BOOL_F(s, l, v, h, f)   OPT_SET_INT_F(s, l, v, h, 1, f)
#define OPT_CALLBACK_F(s, l, v, a, h, f, cb) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = (a), \
	.help = (h), \
	.flags = (f), \
	.callback = (cb), \
}
#define OPT_STRING_F(s, l, v, a, h, f) { \
	.type = OPTION_STRING, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = (a), \
	.help = (h), \
	.flags = (f), \
}
#define OPT_INTEGER_F(s, l, v, h, f) { \
	.type = OPTION_INTEGER, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("n"), \
	.help = (h), \
	.flags = (f), \
}

#define OPT_END() { \
	.type = OPTION_END, \
}
#define OPT_GROUP(h) { \
	.type = OPTION_GROUP, \
	.help = (h), \
}
#define OPT_BIT(s, l, v, h, b)      OPT_BIT_F(s, l, v, h, b, 0)
#define OPT_BITOP(s, l, v, h, set, clear) { \
	.type = OPTION_BITOP, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG|PARSE_OPT_NONEG, \
	.defval = (set), \
	.extra = (clear), \
}
#define OPT_NEGBIT(s, l, v, h, b) { \
	.type = OPTION_NEGBIT, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG, \
	.defval = (b), \
}
#define OPT_COUNTUP(s, l, v, h)     OPT_COUNTUP_F(s, l, v, h, 0)
#define OPT_SET_INT(s, l, v, h, i)  OPT_SET_INT_F(s, l, v, h, i, 0)
#define OPT_BOOL(s, l, v, h)        OPT_BOOL_F(s, l, v, h, 0)
#define OPT_HIDDEN_BOOL(s, l, v, h) { \
	.type = OPTION_SET_INT, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG | PARSE_OPT_HIDDEN, \
	.defval = 1, \
}
#define OPT_CMDMODE_F(s, l, v, h, i, f) { \
	.type = OPTION_SET_INT, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_CMDMODE|PARSE_OPT_NOARG|PARSE_OPT_NONEG | (f), \
	.defval = (i), \
}
#define OPT_CMDMODE(s, l, v, h, i)  OPT_CMDMODE_F(s, l, v, h, i, 0)

#define OPT_INTEGER(s, l, v, h)     OPT_INTEGER_F(s, l, v, h, 0)
#define OPT_MAGNITUDE(s, l, v, h) { \
	.type = OPTION_MAGNITUDE, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("n"), \
	.help = (h), \
	.flags = PARSE_OPT_NONEG, \
}
#define OPT_STRING(s, l, v, a, h)   OPT_STRING_F(s, l, v, a, h, 0)
#define OPT_STRING_LIST(s, l, v, a, h) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = (a), \
	.help = (h), \
	.callback = &parse_opt_string_list, \
}
#define OPT_STRVEC(s, l, v, a, h) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = (a), \
	.help = (h), \
	.callback = &parse_opt_strvec, \
}
#define OPT_UYN(s, l, v, h) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG, \
	.callback = &parse_opt_tertiary, \
}
#define OPT_EXPIRY_DATE(s, l, v, h) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("expiry-date"), \
	.help = (h), \
	.callback = parse_opt_expiry_date_cb, \
}
#define OPT_CALLBACK(s, l, v, a, h, cb) OPT_CALLBACK_F(s, l, v, a, h, 0, cb)
#define OPT_NUMBER_CALLBACK(v, h, cb) { \
	.type = OPTION_NUMBER, \
	.value = (v), \
	.help = (h), \
	.flags = PARSE_OPT_NOARG | PARSE_OPT_NONEG, \
	.callback = (cb), \
}
#define OPT_FILENAME(s, l, v, h) { \
	.type = OPTION_FILENAME, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("file"), \
	.help = (h), \
}
#define OPT_COLOR_FLAG(s, l, v, h) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("when"), \
	.help = (h), \
	.flags = PARSE_OPT_OPTARG, \
	.callback = parse_opt_color_flag_cb, \
	.defval = (intptr_t)"always", \
}

#define OPT_NOOP_NOARG(s, l) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.help = N_("no-op (backward compatibility)"), \
	.flags = PARSE_OPT_HIDDEN | PARSE_OPT_NOARG, \
	.callback = parse_opt_noop_cb, \
}

static char *parse_options_noop_ignored_value MAYBE_UNUSED;
#define OPT_NOOP_ARG(s, l) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = &parse_options_noop_ignored_value, \
	.argh = "ignored", \
	.help = N_("no-op (backward compatibility)"), \
	.flags = PARSE_OPT_HIDDEN, \
	.callback = parse_opt_noop_cb, \
}

#define OPT_ALIAS(s, l, source_long_name) { \
	.type = OPTION_ALIAS, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (char *)(source_long_name), \
}

#define OPT_SUBCOMMAND_F(l, v, fn, f) { \
	.type = OPTION_SUBCOMMAND, \
	.long_name = (l), \
	.value = (v), \
	.flags = (f), \
	.subcommand_fn = (fn), \
}
#define OPT_SUBCOMMAND(l, v, fn)    OPT_SUBCOMMAND_F((l), (v), (fn), 0)

/*
 * parse_options() will filter out the processed options and leave the
 * non-option arguments in argv[]. argv0 is assumed program name and
 * skipped.
 *
 * usagestr strings should be marked for translation with N_().
 *
 * Returns the number of arguments left in argv[].
 *
 * In one-shot mode, argv0 is not a program name, argv[] is left
 * untouched and parse_options() returns the number of options
 * processed.
 */
int parse_options(int argc, const char **argv, const char *prefix,
		  const struct option *options,
		  const char * const usagestr[],
		  enum parse_opt_flags flags);

NORETURN void usage_with_options(const char * const *usagestr,
				 const struct option *options);

NORETURN void usage_msg_opt(const char *msg,
			    const char * const *usagestr,
			    const struct option *options);

/**
 * usage_msg_optf() is like usage_msg_opt() except that the first
 * argument is a format string, and optional format arguments follow
 * after the 3rd option.
 */
__attribute__((format (printf,1,4)))
void NORETURN usage_msg_optf(const char *fmt,
			     const char * const *usagestr,
			     const struct option *options, ...);

void die_for_incompatible_opt4(int opt1, const char *opt1_name,
			       int opt2, const char *opt2_name,
			       int opt3, const char *opt3_name,
			       int opt4, const char *opt4_name);


static inline void die_for_incompatible_opt3(int opt1, const char *opt1_name,
					     int opt2, const char *opt2_name,
					     int opt3, const char *opt3_name)
{
	die_for_incompatible_opt4(opt1, opt1_name,
				  opt2, opt2_name,
				  opt3, opt3_name,
				  0, "");
}

/*
 * Use these assertions for callbacks that expect to be called with NONEG and
 * NOARG respectively, and do not otherwise handle the "unset" and "arg"
 * parameters.
 */
#define BUG_ON_OPT_NEG(unset) do { \
	if ((unset)) \
		BUG("option callback does not expect negation"); \
} while (0)
#define BUG_ON_OPT_ARG(arg) do { \
	if ((arg)) \
		BUG("option callback does not expect an argument"); \
} while (0)

/*
 * Similar to the assertions above, but checks that "arg" is always non-NULL.
 * This assertion also implies BUG_ON_OPT_NEG(), letting you declare both
 * assertions in a single line.
 */
#define BUG_ON_OPT_NEG_NOARG(unset, arg) do { \
	BUG_ON_OPT_NEG(unset); \
	if(!(arg)) \
		BUG("option callback expects an argument"); \
} while(0)

/*----- incremental advanced APIs -----*/

struct parse_opt_cmdmode_list;

/*
 * It's okay for the caller to consume argv/argc in the usual way.
 * Other fields of that structure are private to parse-options and should not
 * be modified in any way.
 */
struct parse_opt_ctx_t {
	const char **argv;
	const char **out;
	int argc, cpidx, total;
	const char *opt;
	enum parse_opt_flags flags;
	unsigned has_subcommands;
	const char *prefix;
	const char **alias_groups; /* must be in groups of 3 elements! */
	struct parse_opt_cmdmode_list *cmdmode_list;
};

void parse_options_start(struct parse_opt_ctx_t *ctx,
			 int argc, const char **argv, const char *prefix,
			 const struct option *options,
			 enum parse_opt_flags flags);

enum parse_opt_result parse_options_step(struct parse_opt_ctx_t *ctx,
					 const struct option *options,
					 const char * const usagestr[]);

int parse_options_end(struct parse_opt_ctx_t *ctx);

struct option *parse_options_dup(const struct option *a);
struct option *parse_options_concat(const struct option *a, const struct option *b);

/*----- some often used options -----*/
int parse_opt_abbrev_cb(const struct option *, const char *, int);
int parse_opt_expiry_date_cb(const struct option *, const char *, int);
int parse_opt_color_flag_cb(const struct option *, const char *, int);
int parse_opt_verbosity_cb(const struct option *, const char *, int);
/* value is struct oid_array* */
int parse_opt_object_name(const struct option *, const char *, int);
/* value is struct object_id* */
int parse_opt_object_id(const struct option *, const char *, int);
int parse_opt_commits(const struct option *, const char *, int);
int parse_opt_commit(const struct option *, const char *, int);
int parse_opt_tertiary(const struct option *, const char *, int);
int parse_opt_string_list(const struct option *, const char *, int);
int parse_opt_strvec(const struct option *, const char *, int);
int parse_opt_noop_cb(const struct option *, const char *, int);
int parse_opt_passthru(const struct option *, const char *, int);
int parse_opt_passthru_argv(const struct option *, const char *, int);
/* value is enum branch_track* */
int parse_opt_tracking_mode(const struct option *, const char *, int);

#define OPT__VERBOSE(var, h)  OPT_COUNTUP('v', "verbose", (var), (h))
#define OPT__QUIET(var, h)    OPT_COUNTUP('q', "quiet",   (var), (h))
#define OPT__VERBOSITY(var) { \
	.type = OPTION_CALLBACK, \
	.short_name = 'v', \
	.long_name = "verbose", \
	.value = (var), \
	.help = N_("be more verbose"), \
	.flags = PARSE_OPT_NOARG, \
	.callback = &parse_opt_verbosity_cb, \
}, { \
	.type = OPTION_CALLBACK, \
	.short_name = 'q', \
	.long_name = "quiet", \
	.value = (var), \
	.help = N_("be more quiet"), \
	.flags = PARSE_OPT_NOARG, \
	.callback = &parse_opt_verbosity_cb, \
}
#define OPT__DRY_RUN(var, h)  OPT_BOOL('n', "dry-run", (var), (h))
#define OPT__FORCE(var, h, f) OPT_COUNTUP_F('f', "force",   (var), (h), (f))
#define OPT__ABBREV(var) { \
	.type = OPTION_CALLBACK, \
	.long_name = "abbrev", \
	.value = (var), \
	.argh = N_("n"), \
	.help = N_("use <n> digits to display object names"), \
	.flags = PARSE_OPT_OPTARG, \
	.callback = &parse_opt_abbrev_cb, \
}
#define OPT__SUPER_PREFIX(var) \
	OPT_STRING_F(0, "super-prefix", (var), N_("prefix"), \
		N_("prefixed path to initial superproject"), PARSE_OPT_HIDDEN)

#define OPT__COLOR(var, h) \
	OPT_COLOR_FLAG(0, "color", (var), (h))
#define OPT_COLUMN(s, l, v, h) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("style"), \
	.help = (h), \
	.flags = PARSE_OPT_OPTARG, \
	.callback = parseopt_column_callback, \
}
#define OPT_PASSTHRU(s, l, v, a, h, f) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = (a), \
	.help = (h), \
	.flags = (f), \
	.callback = parse_opt_passthru, \
}
#define OPT_PASSTHRU_ARGV(s, l, v, a, h, f) { \
	.type = OPTION_CALLBACK, \
	.short_name = (s), \
	.long_name = (l), \
	.value = (v), \
	.argh = (a), \
	.help = (h), \
	.flags = (f), \
	.callback = parse_opt_passthru_argv, \
}
#define _OPT_CONTAINS_OR_WITH(l, v, h, f) { \
	.type = OPTION_CALLBACK, \
	.long_name = (l), \
	.value = (v), \
	.argh = N_("commit"), \
	.help = (h), \
	.flags = PARSE_OPT_LASTARG_DEFAULT | (f), \
	.callback = parse_opt_commits, \
	.defval = (intptr_t) "HEAD", \
}
#define OPT_CONTAINS(v, h) _OPT_CONTAINS_OR_WITH("contains", v, h, PARSE_OPT_NONEG)
#define OPT_NO_CONTAINS(v, h) _OPT_CONTAINS_OR_WITH("no-contains", v, h, PARSE_OPT_NONEG)
#define OPT_WITH(v, h) _OPT_CONTAINS_OR_WITH("with", v, h, PARSE_OPT_HIDDEN | PARSE_OPT_NONEG)
#define OPT_WITHOUT(v, h) _OPT_CONTAINS_OR_WITH("without", v, h, PARSE_OPT_HIDDEN | PARSE_OPT_NONEG)
#define OPT_CLEANUP(v) OPT_STRING(0, "cleanup", v, N_("mode"), N_("how to strip spaces and #comments from message"))
#define OPT_PATHSPEC_FROM_FILE(v) OPT_FILENAME(0, "pathspec-from-file", v, N_("read pathspec from file"))
#define OPT_PATHSPEC_FILE_NUL(v)  OPT_BOOL(0, "pathspec-file-nul", v, N_("with --pathspec-from-file, pathspec elements are separated with NUL character"))
#define OPT_AUTOSTASH(v) OPT_BOOL(0, "autostash", v, N_("automatically stash/stash pop before and after"))

#define OPT_IPVERSION(v) \
	OPT_SET_INT_F('4', "ipv4", (v), N_("use IPv4 addresses only"), \
		TRANSPORT_FAMILY_IPV4, PARSE_OPT_NONEG), \
	OPT_SET_INT_F('6', "ipv6", (v), N_("use IPv6 addresses only"), \
		TRANSPORT_FAMILY_IPV6, PARSE_OPT_NONEG)

#endif
