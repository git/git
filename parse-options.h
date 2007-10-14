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
};

struct option;
typedef int parse_opt_cb(const struct option *, const char *arg, int unset);

struct option {
	enum parse_opt_type type;
	int short_name;
	const char *long_name;
	void *value;
	const char *argh;
	const char *help;

	int flags;
	parse_opt_cb *callback;
	/* holds default value for PARSE_OPT_OPTARG,
	   though callbacks can use it like they want */
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

#endif
