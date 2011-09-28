#ifndef PARSE_OPTIONS_H
#define PARSE_OPTIONS_H

enum parse_opt_type {
	/* special types */
	OPTION_END,
	OPTION_ARGUMENT,
	OPTION_GROUP,
	OPTION_NUMBER,
	/* options with no arguments */
	OPTION_BIT,
	OPTION_NEGBIT,
	OPTION_COUNTUP,
	OPTION_SET_INT,
	OPTION_SET_PTR,
	/* options with arguments (usually) */
	OPTION_STRING,
	OPTION_INTEGER,
	OPTION_CALLBACK,
	OPTION_LOWLEVEL_CALLBACK,
	OPTION_FILENAME
};

/* Deprecated synonym */
#define OPTION_BOOLEAN OPTION_COUNTUP

enum parse_opt_flags {
	PARSE_OPT_KEEP_DASHDASH = 1,
	PARSE_OPT_STOP_AT_NON_OPTION = 2,
	PARSE_OPT_KEEP_ARGV0 = 4,
	PARSE_OPT_KEEP_UNKNOWN = 8,
	PARSE_OPT_NO_INTERNAL_HELP = 16
};

enum parse_opt_option_flags {
	PARSE_OPT_OPTARG  = 1,
	PARSE_OPT_NOARG   = 2,
	PARSE_OPT_NONEG   = 4,
	PARSE_OPT_HIDDEN  = 8,
	PARSE_OPT_LASTARG_DEFAULT = 16,
	PARSE_OPT_NODASH = 32,
	PARSE_OPT_LITERAL_ARGHELP = 64,
	PARSE_OPT_NEGHELP = 128,
	PARSE_OPT_SHELL_EVAL = 256
};

struct option;
typedef int parse_opt_cb(const struct option *, const char *arg, int unset);

struct parse_opt_ctx_t;
typedef int parse_opt_ll_cb(struct parse_opt_ctx_t *ctx,
				const struct option *opt, int unset);

/*
 * `type`::
 *   holds the type of the option, you must have an OPTION_END last in your
 *   array.
 *
 * `short_name`::
 *   the character to use as a short option name, '\0' if none.
 *
 * `long_name`::
 *   the long option name, without the leading dashes, NULL if none.
 *
 * `value`::
 *   stores pointers to the values to be filled.
 *
 * `argh`::
 *   token to explain the kind of argument this option wants. Keep it
 *   homogeneous across the repository.
 *
 * `help`::
 *   the short help associated to what the option does.
 *   Must never be NULL (except for OPTION_END).
 *   OPTION_GROUP uses this pointer to store the group header.
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
 *   PARSE_OPT_NODASH: this option doesn't start with a dash.
 *   PARSE_OPT_LITERAL_ARGHELP: says that argh shouldn't be enclosed in brackets
 *				(i.e. '<argh>') in the help message.
 *				Useful for options with multiple parameters.
 *   PARSE_OPT_NEGHELP: says that the long option should always be shown with
 *				the --no prefix in the usage message. Sometimes
 *				useful for users of OPTION_NEGBIT.
 *
 * `callback`::
 *   pointer to the callback to use for OPTION_CALLBACK or
 *   OPTION_LOWLEVEL_CALLBACK.
 *
 * `defval`::
 *   default value to fill (*->value) with for PARSE_OPT_OPTARG.
 *   OPTION_{BIT,SET_INT,SET_PTR} store the {mask,integer,pointer} to put in
 *   the value when met.
 *   CALLBACKS can use it like they want.
 */
struct option {
	enum parse_opt_type type;
	int short_name;
	const char *long_name;
	void *value;
	const char *argh;
	const char *help;

	int flags;
	parse_opt_cb *callback;
	intptr_t defval;
};

#define OPT_END()                   { OPTION_END }
#define OPT_ARGUMENT(l, h)          { OPTION_ARGUMENT, 0, (l), NULL, NULL, \
				      (h), PARSE_OPT_NOARG}
#define OPT_GROUP(h)                { OPTION_GROUP, 0, NULL, NULL, NULL, (h) }
#define OPT_BIT(s, l, v, h, b)      { OPTION_BIT, (s), (l), (v), NULL, (h), \
				      PARSE_OPT_NOARG, NULL, (b) }
#define OPT_NEGBIT(s, l, v, h, b)   { OPTION_NEGBIT, (s), (l), (v), NULL, \
				      (h), PARSE_OPT_NOARG, NULL, (b) }
#define OPT_COUNTUP(s, l, v, h)     { OPTION_COUNTUP, (s), (l), (v), NULL, \
				      (h), PARSE_OPT_NOARG }
#define OPT_SET_INT(s, l, v, h, i)  { OPTION_SET_INT, (s), (l), (v), NULL, \
				      (h), PARSE_OPT_NOARG, NULL, (i) }
#define OPT_BOOL(s, l, v, h)        OPT_SET_INT(s, l, v, h, 1)
#define OPT_SET_PTR(s, l, v, h, p)  { OPTION_SET_PTR, (s), (l), (v), NULL, \
				      (h), PARSE_OPT_NOARG, NULL, (p) }
#define OPT_INTEGER(s, l, v, h)     { OPTION_INTEGER, (s), (l), (v), "n", (h) }
#define OPT_STRING(s, l, v, a, h)   { OPTION_STRING,  (s), (l), (v), (a), (h) }
#define OPT_STRING_LIST(s, l, v, a, h) \
				    { OPTION_CALLBACK, (s), (l), (v), (a), \
				      (h), 0, &parse_opt_string_list }
#define OPT_UYN(s, l, v, h)         { OPTION_CALLBACK, (s), (l), (v), NULL, \
				      (h), PARSE_OPT_NOARG, &parse_opt_tertiary }
#define OPT_DATE(s, l, v, h) \
	{ OPTION_CALLBACK, (s), (l), (v), "time",(h), 0, \
	  parse_opt_approxidate_cb }
#define OPT_CALLBACK(s, l, v, a, h, f) \
	{ OPTION_CALLBACK, (s), (l), (v), (a), (h), 0, (f) }
#define OPT_NUMBER_CALLBACK(v, h, f) \
	{ OPTION_NUMBER, 0, NULL, (v), NULL, (h), \
	  PARSE_OPT_NOARG | PARSE_OPT_NONEG, (f) }
#define OPT_FILENAME(s, l, v, h)    { OPTION_FILENAME, (s), (l), (v), \
				       "file", (h) }
#define OPT_COLOR_FLAG(s, l, v, h) \
	{ OPTION_CALLBACK, (s), (l), (v), "when", (h), PARSE_OPT_OPTARG, \
		parse_opt_color_flag_cb, (intptr_t)"always" }

#define OPT_NOOP_NOARG(s, l) \
	{ OPTION_CALLBACK, (s), (l), NULL, NULL, \
	  "no-op (backward compatibility)", \
	  PARSE_OPT_HIDDEN | PARSE_OPT_NOARG, parse_opt_noop_cb }

/* Deprecated synonym */
#define OPT_BOOLEAN OPT_COUNTUP

/* parse_options() will filter out the processed options and leave the
 * non-option arguments in argv[].
 * Returns the number of arguments left in argv[].
 */
extern int parse_options(int argc, const char **argv, const char *prefix,
                         const struct option *options,
                         const char * const usagestr[], int flags);

extern NORETURN void usage_with_options(const char * const *usagestr,
                                        const struct option *options);

extern NORETURN void usage_msg_opt(const char *msg,
				   const char * const *usagestr,
				   const struct option *options);

extern int optbug(const struct option *opt, const char *reason);
extern int opterror(const struct option *opt, const char *reason, int flags);
/*----- incremental advanced APIs -----*/

enum {
	PARSE_OPT_HELP = -1,
	PARSE_OPT_DONE,
	PARSE_OPT_NON_OPTION,
	PARSE_OPT_UNKNOWN
};

/*
 * It's okay for the caller to consume argv/argc in the usual way.
 * Other fields of that structure are private to parse-options and should not
 * be modified in any way.
 */
struct parse_opt_ctx_t {
	const char **argv;
	const char **out;
	int argc, cpidx;
	const char *opt;
	int flags;
	const char *prefix;
};

extern void parse_options_start(struct parse_opt_ctx_t *ctx,
				int argc, const char **argv, const char *prefix,
				const struct option *options, int flags);

extern int parse_options_step(struct parse_opt_ctx_t *ctx,
			      const struct option *options,
			      const char * const usagestr[]);

extern int parse_options_end(struct parse_opt_ctx_t *ctx);

extern int parse_options_concat(struct option *dst, size_t, struct option *src);

/*----- some often used options -----*/
extern int parse_opt_abbrev_cb(const struct option *, const char *, int);
extern int parse_opt_approxidate_cb(const struct option *, const char *, int);
extern int parse_opt_color_flag_cb(const struct option *, const char *, int);
extern int parse_opt_verbosity_cb(const struct option *, const char *, int);
extern int parse_opt_with_commit(const struct option *, const char *, int);
extern int parse_opt_tertiary(const struct option *, const char *, int);
extern int parse_opt_string_list(const struct option *, const char *, int);
extern int parse_opt_noop_cb(const struct option *, const char *, int);

#define OPT__VERBOSE(var, h)  OPT_BOOLEAN('v', "verbose", (var), (h))
#define OPT__QUIET(var, h)    OPT_BOOLEAN('q', "quiet",   (var), (h))
#define OPT__VERBOSITY(var) \
	{ OPTION_CALLBACK, 'v', "verbose", (var), NULL, "be more verbose", \
	  PARSE_OPT_NOARG, &parse_opt_verbosity_cb, 0 }, \
	{ OPTION_CALLBACK, 'q', "quiet", (var), NULL, "be more quiet", \
	  PARSE_OPT_NOARG, &parse_opt_verbosity_cb, 0 }
#define OPT__DRY_RUN(var, h)  OPT_BOOLEAN('n', "dry-run", (var), (h))
#define OPT__FORCE(var, h)    OPT_BOOLEAN('f', "force",   (var), (h))
#define OPT__ABBREV(var)  \
	{ OPTION_CALLBACK, 0, "abbrev", (var), "n", \
	  "use <n> digits to display SHA-1s", \
	  PARSE_OPT_OPTARG, &parse_opt_abbrev_cb, 0 }
#define OPT__COLOR(var, h) \
	OPT_COLOR_FLAG(0, "color", (var), (h))

#endif
