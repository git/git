#ifndef PARSE_OPTIONS_H
#define PARSE_OPTIONS_H

enum parse_opt_type {
	OPTION_END,
	OPTION_BOOLEAN,
	OPTION_STRING,
	OPTION_INTEGER,
};

enum parse_opt_flags {
	PARSE_OPT_KEEP_DASHDASH = 1,
};

struct option {
	enum parse_opt_type type;
	int short_name;
	const char *long_name;
	void *value;
};

#define OPT_END()                   { OPTION_END }
#define OPT_BOOLEAN(s, l, v, h)     { OPTION_BOOLEAN, (s), (l), (v) }
#define OPT_INTEGER(s, l, v, h)     { OPTION_INTEGER, (s), (l), (v) }
#define OPT_STRING(s, l, v, a, h)   { OPTION_STRING,  (s), (l), (v) }

/* parse_options() will filter out the processed options and leave the
 * non-option argments in argv[].
 * Returns the number of arguments left in argv[].
 */
extern int parse_options(int argc, const char **argv,
                         const struct option *options,
                         const char *usagestr, int flags);

#endif
