#include "cache.h"
#include "userdiff.h"
#include "cache.h"
#include "attr.h"

static struct userdiff_driver *drivers;
static int ndrivers;
static int drivers_alloc;

#define PATTERNS(name, pattern, word_regex)			\
	{ name, NULL, -1, { pattern, REG_EXTENDED }, word_regex }
static struct userdiff_driver builtin_drivers[] = {
PATTERNS("html", "^[ \t]*(<[Hh][1-6][ \t].*>.*)$",
	 "[^<>= \t]+|[^[:space:]]|[\x80-\xff]+"),
PATTERNS("java",
	 "!^[ \t]*(catch|do|for|if|instanceof|new|return|switch|throw|while)\n"
	 "^[ \t]*(([A-Za-z_][A-Za-z_0-9]*[ \t]+)+[A-Za-z_][A-Za-z_0-9]*[ \t]*\\([^;]*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[fFlL]?|0[xXbB]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]="
	 "|--|\\+\\+|<<=?|>>>?=?|&&|\\|\\|"
	 "|[^[:space:]]|[\x80-\xff]+"),
PATTERNS("objc",
	 /* Negate C statements that can look like functions */
	 "!^[ \t]*(do|for|if|else|return|switch|while)\n"
	 /* Objective-C methods */
	 "^[ \t]*([-+][ \t]*\\([ \t]*[A-Za-z_][A-Za-z_0-9* \t]*\\)[ \t]*[A-Za-z_].*)$\n"
	 /* C functions */
	 "^[ \t]*(([A-Za-z_][A-Za-z_0-9]*[ \t]+)+[A-Za-z_][A-Za-z_0-9]*[ \t]*\\([^;]*)$\n"
	 /* Objective-C class/protocol definitions */
	 "^(@(implementation|interface|protocol)[ \t].*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[fFlL]?|0[xXbB]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]=|--|\\+\\+|<<=?|>>=?|&&|\\|\\||::|->"
	 "|[^[:space:]]|[\x80-\xff]+"),
PATTERNS("pascal",
	 "^((procedure|function|constructor|destructor|interface|"
		"implementation|initialization|finalization)[ \t]*.*)$"
	 "\n"
	 "^(.*=[ \t]*(class|record).*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+|0[xXbB]?[0-9a-fA-F]+"
	 "|<>|<=|>=|:=|\\.\\."
	 "|[^[:space:]]|[\x80-\xff]+"),
PATTERNS("php",
	 "^[\t ]*(((public|protected|private|static)[\t ]+)*function.*)$\n"
	 "^[\t ]*(class.*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+|0[xXbB]?[0-9a-fA-F]+"
	 "|[-+*/<>%&^|=!.]=|--|\\+\\+|<<=?|>>=?|===|&&|\\|\\||::|->"
	 "|[^[:space:]]|[\x80-\xff]+"),
PATTERNS("python", "^[ \t]*((class|def)[ \t].*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[jJlL]?|0[xX]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]=|//=?|<<=?|>>=?|\\*\\*=?"
	 "|[^[:space:]|[\x80-\xff]+"),
	 /* -- */
PATTERNS("ruby", "^[ \t]*((class|module|def)[ \t].*)$",
	 /* -- */
	 "(@|@@|\\$)?[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+|0[xXbB]?[0-9a-fA-F]+|\\?(\\\\C-)?(\\\\M-)?."
	 "|//=?|[-+*/<>%&^|=!]=|<<=?|>>=?|===|\\.{1,3}|::|[!=]~"
	 "|[^[:space:]|[\x80-\xff]+"),
PATTERNS("bibtex", "(@[a-zA-Z]{1,}[ \t]*\\{{0,1}[ \t]*[^ \t\"@',\\#}{~%]*).*$",
	 "[={}\"]|[^={}\" \t]+"),
PATTERNS("tex", "^(\\\\((sub)*section|chapter|part)\\*{0,1}\\{.*)$",
	 "\\\\[a-zA-Z@]+|\\\\.|[a-zA-Z0-9\x80-\xff]+|[^[:space:]]"),
PATTERNS("cpp",
	 /* Jump targets or access declarations */
	 "!^[ \t]*[A-Za-z_][A-Za-z_0-9]*:.*$\n"
	 /* C/++ functions/methods at top level */
	 "^([A-Za-z_][A-Za-z_0-9]*([ \t]+[A-Za-z_][A-Za-z_0-9]*([ \t]*::[ \t]*[^[:space:]]+)?){1,}[ \t]*\\([^;]*)$\n"
	 /* compound type at top level */
	 "^((struct|class|enum)[^;]*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[fFlL]?|0[xXbB]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]=|--|\\+\\+|<<=?|>>=?|&&|\\|\\||::|->"
	 "|[^[:space:]]|[\x80-\xff]+"),
{ "default", NULL, -1, { NULL, 0 } },
};
#undef PATTERNS

static struct userdiff_driver driver_true = {
	"diff=true",
	NULL,
	0,
	{ NULL, 0 }
};

static struct userdiff_driver driver_false = {
	"!diff",
	NULL,
	1,
	{ NULL, 0 }
};

static struct userdiff_driver *userdiff_find_by_namelen(const char *k, int len)
{
	int i;
	for (i = 0; i < ndrivers; i++) {
		struct userdiff_driver *drv = drivers + i;
		if (!strncmp(drv->name, k, len) && !drv->name[len])
			return drv;
	}
	for (i = 0; i < ARRAY_SIZE(builtin_drivers); i++) {
		struct userdiff_driver *drv = builtin_drivers + i;
		if (!strncmp(drv->name, k, len) && !drv->name[len])
			return drv;
	}
	return NULL;
}

static struct userdiff_driver *parse_driver(const char *var,
		const char *value, const char *type)
{
	struct userdiff_driver *drv;
	const char *dot;
	const char *name;
	int namelen;

	if (prefixcmp(var, "diff."))
		return NULL;
	dot = strrchr(var, '.');
	if (dot == var + 4)
		return NULL;
	if (strcmp(type, dot+1))
		return NULL;

	name = var + 5;
	namelen = dot - name;
	drv = userdiff_find_by_namelen(name, namelen);
	if (!drv) {
		ALLOC_GROW(drivers, ndrivers+1, drivers_alloc);
		drv = &drivers[ndrivers++];
		memset(drv, 0, sizeof(*drv));
		drv->name = xmemdupz(name, namelen);
		drv->binary = -1;
	}
	return drv;
}

static int parse_funcname(struct userdiff_funcname *f, const char *k,
		const char *v, int cflags)
{
	if (git_config_string(&f->pattern, k, v) < 0)
		return -1;
	f->cflags = cflags;
	return 1;
}

static int parse_string(const char **d, const char *k, const char *v)
{
	if (git_config_string(d, k, v) < 0)
		return -1;
	return 1;
}

static int parse_tristate(int *b, const char *k, const char *v)
{
	if (v && !strcasecmp(v, "auto"))
		*b = -1;
	else
		*b = git_config_bool(k, v);
	return 1;
}

static int parse_bool(int *b, const char *k, const char *v)
{
	*b = git_config_bool(k, v);
	return 1;
}

int userdiff_config(const char *k, const char *v)
{
	struct userdiff_driver *drv;

	if ((drv = parse_driver(k, v, "funcname")))
		return parse_funcname(&drv->funcname, k, v, 0);
	if ((drv = parse_driver(k, v, "xfuncname")))
		return parse_funcname(&drv->funcname, k, v, REG_EXTENDED);
	if ((drv = parse_driver(k, v, "binary")))
		return parse_tristate(&drv->binary, k, v);
	if ((drv = parse_driver(k, v, "command")))
		return parse_string(&drv->external, k, v);
	if ((drv = parse_driver(k, v, "textconv")))
		return parse_string(&drv->textconv, k, v);
	if ((drv = parse_driver(k, v, "cachetextconv")))
		return parse_bool(&drv->textconv_want_cache, k, v);
	if ((drv = parse_driver(k, v, "wordregex")))
		return parse_string(&drv->word_regex, k, v);

	return 0;
}

struct userdiff_driver *userdiff_find_by_name(const char *name) {
	int len = strlen(name);
	return userdiff_find_by_namelen(name, len);
}

struct userdiff_driver *userdiff_find_by_path(const char *path)
{
	static struct git_attr *attr;
	struct git_attr_check check;

	if (!attr)
		attr = git_attr("diff");
	check.attr = attr;

	if (!path)
		return NULL;
	if (git_checkattr(path, 1, &check))
		return NULL;

	if (ATTR_TRUE(check.value))
		return &driver_true;
	if (ATTR_FALSE(check.value))
		return &driver_false;
	if (ATTR_UNSET(check.value))
		return NULL;
	return userdiff_find_by_name(check.value);
}
