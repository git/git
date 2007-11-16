#ifndef PARSE_OPTIONS_H
#define PARSE_OPTIONS_H

enum parse_opt_type {
	OPTION_END,
	OPTION_GROUP,
	OPTION_BOOLEAN,
	OPTION_STRING,
	OPTION_INTEGER,
	OPTION_CALLBACK,
};

enum parse_opt_flags {
	PARSE_OPT_KEEP_DASHDASH = 1,
};

enum parse_opt_option_flags {
	PARSE_OPT_OPTARG  = 1,
	PARSE_OPT_NOARG   = 2,
};

struct option;
typedef int parse_opt_cb(const struct option *, const char *arg, int unset);

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
 *   homogenous across the repository.
 *
 * `help`::
 *   the short help associated to what the option does.
 *   Must never be NULL (except for OPTION_END).
 *   OPTION_GROUP uses this pointer to store the group header.
 *
 * `flags`::
 *   mask of parse_opt_option_flags.
 *   PARSE_OPT_OPTARG: says that the argument is optionnal (not for BOOLEANs)
 *   PARSE_OPT_NOARG: says that this option takes no argument, for CALLBACKs
 *
 * `callback`::
 *   pointer to the callback to use for OPTION_CALLBACK.
 *
 * `defval`::
 *   default value to fill (*->value) with for PARSE_OPT_OPTARG.
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
#define OPT_GROUP(h)                { OPTION_GROUP, 0, NULL, NULL, NULL, (h) }
#define OPT_BOOLEAN(s, l, v, h)     { OPTION_BOOLEAN, (s), (l), (v), NULL, (h) }
#define OPT_INTEGER(s, l, v, h)     { OPTION_INTEGER, (s), (l), (v), NULL, (h) }
#define OPT_STRING(s, l, v, a, h)   { OPTION_STRING,  (s), (l), (v), (a), (h) }
#define OPT_CALLBACK(s, l, v, a, h, f) \
	{ OPTION_CALLBACK, (s), (l), (v), (a), (h), 0, (f) }

/* parse_options() will filter out the processed options and leave the
 * non-option argments in argv[].
 * Returns the number of arguments left in argv[].
 */
extern int parse_options(int argc, const char **argv,
                         const struct option *options,
                         const char * const usagestr[], int flags);

extern NORETURN void usage_with_options(const char * const *usagestr,
                                        const struct option *options);

/*----- some often used options -----*/
extern int parse_opt_abbrev_cb(const struct option *, const char *, int);

#define OPT__VERBOSE(var)  OPT_BOOLEAN('v', "verbose", (var), "be verbose")
#define OPT__QUIET(var)    OPT_BOOLEAN('q', "quiet",   (var), "be quiet")
#define OPT__DRY_RUN(var)  OPT_BOOLEAN('n', "dry-run", (var), "dry run")
#define OPT__ABBREV(var)  \
	{ OPTION_CALLBACK, 0, "abbrev", (var), "n", \
	  "use <n> digits to display SHA-1s", \
	  PARSE_OPT_OPTARG, &parse_opt_abbrev_cb, 0 }

#endif
