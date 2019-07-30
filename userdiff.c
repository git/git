#include "cache.h"
#include "config.h"
#include "userdiff.h"
#include "attr.h"

static struct userdiff_driver *drivers;
static int ndrivers;
static int drivers_alloc;

#define PATTERNS(name, pattern, word_regex)			\
	{ name, NULL, -1, { pattern, REG_EXTENDED },		\
	  word_regex "|[^[:space:]]|[\xc0-\xff][\x80-\xbf]+" }
#define IPATTERN(name, pattern, word_regex)			\
	{ name, NULL, -1, { pattern, REG_EXTENDED | REG_ICASE }, \
	  word_regex "|[^[:space:]]|[\xc0-\xff][\x80-\xbf]+" }
static struct userdiff_driver builtin_drivers[] = {
IPATTERN("ada",
	 "!^(.*[ \t])?(is[ \t]+new|renames|is[ \t]+separate)([ \t].*)?$\n"
	 "!^[ \t]*with[ \t].*$\n"
	 "^[ \t]*((procedure|function)[ \t]+.*)$\n"
	 "^[ \t]*((package|protected|task)[ \t]+.*)$",
	 /* -- */
	 "[a-zA-Z][a-zA-Z0-9_]*"
	 "|[-+]?[0-9][0-9#_.aAbBcCdDeEfF]*([eE][+-]?[0-9_]+)?"
	 "|=>|\\.\\.|\\*\\*|:=|/=|>=|<=|<<|>>|<>"),
IPATTERN("fortran",
	 "!^([C*]|[ \t]*!)\n"
	 "!^[ \t]*MODULE[ \t]+PROCEDURE[ \t]\n"
	 "^[ \t]*((END[ \t]+)?(PROGRAM|MODULE|BLOCK[ \t]+DATA"
		"|([^'\" \t]+[ \t]+)*(SUBROUTINE|FUNCTION))[ \t]+[A-Z].*)$",
	 /* -- */
	 "[a-zA-Z][a-zA-Z0-9_]*"
	 "|\\.([Ee][Qq]|[Nn][Ee]|[Gg][TtEe]|[Ll][TtEe]|[Tt][Rr][Uu][Ee]|[Ff][Aa][Ll][Ss][Ee]|[Aa][Nn][Dd]|[Oo][Rr]|[Nn]?[Ee][Qq][Vv]|[Nn][Oo][Tt])\\."
	 /* numbers and format statements like 2E14.4, or ES12.6, 9X.
	  * Don't worry about format statements without leading digits since
	  * they would have been matched above as a variable anyway. */
	 "|[-+]?[0-9.]+([AaIiDdEeFfLlTtXx][Ss]?[-+]?[0-9.]*)?(_[a-zA-Z0-9][a-zA-Z0-9_]*)?"
	 "|//|\\*\\*|::|[/<>=]="),
IPATTERN("fountain", "^((\\.[^.]|(int|ext|est|int\\.?/ext|i/e)[. ]).*)$",
	 "[^ \t-]+"),
PATTERNS("golang",
	 /* Functions */
	 "^[ \t]*(func[ \t]*.*(\\{[ \t]*)?)\n"
	 /* Structs and interfaces */
	 "^[ \t]*(type[ \t].*(struct|interface)[ \t]*(\\{[ \t]*)?)",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.eE]+i?|0[xX]?[0-9a-fA-F]+i?"
	 "|[-+*/<>%&^|=!:]=|--|\\+\\+|<<=?|>>=?|&\\^=?|&&|\\|\\||<-|\\.{3}"),
PATTERNS("html", "^[ \t]*(<[Hh][1-6]([ \t].*)?>.*)$",
	 "[^<>= \t]+"),
PATTERNS("java",
	 "!^[ \t]*(catch|do|for|if|instanceof|new|return|switch|throw|while)\n"
	 "^[ \t]*(([A-Za-z_][A-Za-z_0-9]*[ \t]+)+[A-Za-z_][A-Za-z_0-9]*[ \t]*\\([^;]*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[fFlL]?|0[xXbB]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]="
	 "|--|\\+\\+|<<=?|>>>?=?|&&|\\|\\|"),
PATTERNS("matlab",
	 /*
	  * Octave pattern is mostly the same as matlab, except that '%%%' and
	  * '##' can also be used to begin code sections, in addition to '%%'
	  * that is understood by both.
	  */
	 "^[[:space:]]*((classdef|function)[[:space:]].*)$|^(%%%?|##)[[:space:]].*$",
	 "[a-zA-Z_][a-zA-Z0-9_]*|[-+0-9.e]+|[=~<>]=|\\.[*/\\^']|\\|\\||&&"),
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
	 "|[-+*/<>%&^|=!]=|--|\\+\\+|<<=?|>>=?|&&|\\|\\||::|->"),
PATTERNS("pascal",
	 "^(((class[ \t]+)?(procedure|function)|constructor|destructor|interface|"
		"implementation|initialization|finalization)[ \t]*.*)$"
	 "\n"
	 "^(.*=[ \t]*(class|record).*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+|0[xXbB]?[0-9a-fA-F]+"
	 "|<>|<=|>=|:=|\\.\\."),
PATTERNS("perl",
	 "^package .*\n"
	 "^sub [[:alnum:]_':]+[ \t]*"
		"(\\([^)]*\\)[ \t]*)?" /* prototype */
		/*
		 * Attributes.  A regex can't count nested parentheses,
		 * so just slurp up whatever we see, taking care not
		 * to accept lines like "sub foo; # defined elsewhere".
		 *
		 * An attribute could contain a semicolon, but at that
		 * point it seems reasonable enough to give up.
		 */
		"(:[^;#]*)?"
		"(\\{[ \t]*)?" /* brace can come here or on the next line */
		"(#.*)?$\n" /* comment */
	 "^(BEGIN|END|INIT|CHECK|UNITCHECK|AUTOLOAD|DESTROY)[ \t]*"
		"(\\{[ \t]*)?" /* brace can come here or on the next line */
		"(#.*)?$\n"
	 "^=head[0-9] .*",	/* POD */
	 /* -- */
	 "[[:alpha:]_'][[:alnum:]_']*"
	 "|0[xb]?[0-9a-fA-F_]*"
	 /* taking care not to interpret 3..5 as (3.)(.5) */
	 "|[0-9a-fA-F_]+(\\.[0-9a-fA-F_]+)?([eE][-+]?[0-9_]+)?"
	 "|=>|-[rwxoRWXOezsfdlpSugkbctTBMAC>]|~~|::"
	 "|&&=|\\|\\|=|//=|\\*\\*="
	 "|&&|\\|\\||//|\\+\\+|--|\\*\\*|\\.\\.\\.?"
	 "|[-+*/%.^&<>=!|]="
	 "|=~|!~"
	 "|<<|<>|<=>|>>"),
PATTERNS("php",
	 "^[\t ]*(((public|protected|private|static)[\t ]+)*function.*)$\n"
	 "^[\t ]*((((final|abstract)[\t ]+)?class|interface|trait).*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+|0[xXbB]?[0-9a-fA-F]+"
	 "|[-+*/<>%&^|=!.]=|--|\\+\\+|<<=?|>>=?|===|&&|\\|\\||::|->"),
PATTERNS("python", "^[ \t]*((class|def)[ \t].*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[jJlL]?|0[xX]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]=|//=?|<<=?|>>=?|\\*\\*=?"),
	 /* -- */
PATTERNS("ruby", "^[ \t]*((class|module|def)[ \t].*)$",
	 /* -- */
	 "(@|@@|\\$)?[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+|0[xXbB]?[0-9a-fA-F]+|\\?(\\\\C-)?(\\\\M-)?."
	 "|//=?|[-+*/<>%&^|=!]=|<<=?|>>=?|===|\\.{1,3}|::|[!=]~"),
PATTERNS("rust",
	 "^[\t ]*((pub(\\([^\\)]+\\))?[\t ]+)?((async|const|unsafe|extern([\t ]+\"[^\"]+\"))[\t ]+)?(struct|enum|union|mod|trait|fn|impl)[< \t]+[^;]*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[0-9][0-9_a-fA-Fiosuxz]*(\\.([0-9]*[eE][+-]?)?[0-9_fF]*)?"
	 "|[-+*\\/<>%&^|=!:]=|<<=?|>>=?|&&|\\|\\||->|=>|\\.{2}=|\\.{3}|::"),
PATTERNS("bibtex", "(@[a-zA-Z]{1,}[ \t]*\\{{0,1}[ \t]*[^ \t\"@',\\#}{~%]*).*$",
	 "[={}\"]|[^={}\" \t]+"),
PATTERNS("tex", "^(\\\\((sub)*section|chapter|part)\\*{0,1}\\{.*)$",
	 "\\\\[a-zA-Z@]+|\\\\.|[a-zA-Z0-9\x80-\xff]+"),
PATTERNS("cpp",
	 /* Jump targets or access declarations */
	 "!^[ \t]*[A-Za-z_][A-Za-z_0-9]*:[[:space:]]*($|/[/*])\n"
	 /* functions/methods, variables, and compounds at top level */
	 "^((::[[:space:]]*)?[A-Za-z_].*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[fFlL]?|0[xXbB]?[0-9a-fA-F]+[lLuU]*"
	 "|[-+*/<>%&^|=!]=|--|\\+\\+|<<=?|>>=?|&&|\\|\\||::|->\\*?|\\.\\*"),
PATTERNS("csharp",
	 /* Keywords */
	 "!^[ \t]*(do|while|for|if|else|instanceof|new|return|switch|case|throw|catch|using)\n"
	 /* Methods and constructors */
	 "^[ \t]*(((static|public|internal|private|protected|new|virtual|sealed|override|unsafe|async)[ \t]+)*[][<>@.~_[:alnum:]]+[ \t]+[<>@._[:alnum:]]+[ \t]*\\(.*\\))[ \t]*$\n"
	 /* Properties */
	 "^[ \t]*(((static|public|internal|private|protected|new|virtual|sealed|override|unsafe)[ \t]+)*[][<>@.~_[:alnum:]]+[ \t]+[@._[:alnum:]]+)[ \t]*$\n"
	 /* Type definitions */
	 "^[ \t]*(((static|public|internal|private|protected|new|unsafe|sealed|abstract|partial)[ \t]+)*(class|enum|interface|struct)[ \t]+.*)$\n"
	 /* Namespace */
	 "^[ \t]*(namespace[ \t]+.*)$",
	 /* -- */
	 "[a-zA-Z_][a-zA-Z0-9_]*"
	 "|[-+0-9.e]+[fFlL]?|0[xXbB]?[0-9a-fA-F]+[lL]?"
	 "|[-+*/<>%&^|=!]=|--|\\+\\+|<<=?|>>=?|&&|\\|\\||::|->"),
IPATTERN("css",
	 "![:;][[:space:]]*$\n"
	 "^[_a-z0-9].*$",
	 /* -- */
	 /*
	  * This regex comes from W3C CSS specs. Should theoretically also
	  * allow ISO 10646 characters U+00A0 and higher,
	  * but they are not handled in this regex.
	  */
	 "-?[_a-zA-Z][-_a-zA-Z0-9]*" /* identifiers */
	 "|-?[0-9]+|\\#[0-9a-fA-F]+" /* numbers */
),
{ "default", NULL, -1, { NULL, 0 } },
};
#undef PATTERNS
#undef IPATTERN

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

static int parse_funcname(struct userdiff_funcname *f, const char *k,
		const char *v, int cflags)
{
	if (git_config_string(&f->pattern, k, v) < 0)
		return -1;
	f->cflags = cflags;
	return 0;
}

static int parse_tristate(int *b, const char *k, const char *v)
{
	if (v && !strcasecmp(v, "auto"))
		*b = -1;
	else
		*b = git_config_bool(k, v);
	return 0;
}

static int parse_bool(int *b, const char *k, const char *v)
{
	*b = git_config_bool(k, v);
	return 0;
}

int userdiff_config(const char *k, const char *v)
{
	struct userdiff_driver *drv;
	const char *name, *type;
	int namelen;

	if (parse_config_key(k, "diff", &name, &namelen, &type) || !name)
		return 0;

	drv = userdiff_find_by_namelen(name, namelen);
	if (!drv) {
		ALLOC_GROW(drivers, ndrivers+1, drivers_alloc);
		drv = &drivers[ndrivers++];
		memset(drv, 0, sizeof(*drv));
		drv->name = xmemdupz(name, namelen);
		drv->binary = -1;
	}

	if (!strcmp(type, "funcname"))
		return parse_funcname(&drv->funcname, k, v, 0);
	if (!strcmp(type, "xfuncname"))
		return parse_funcname(&drv->funcname, k, v, REG_EXTENDED);
	if (!strcmp(type, "binary"))
		return parse_tristate(&drv->binary, k, v);
	if (!strcmp(type, "command"))
		return git_config_string(&drv->external, k, v);
	if (!strcmp(type, "textconv"))
		return git_config_string(&drv->textconv, k, v);
	if (!strcmp(type, "cachetextconv"))
		return parse_bool(&drv->textconv_want_cache, k, v);
	if (!strcmp(type, "wordregex"))
		return git_config_string(&drv->word_regex, k, v);

	return 0;
}

struct userdiff_driver *userdiff_find_by_name(const char *name)
{
	int len = strlen(name);
	return userdiff_find_by_namelen(name, len);
}

struct userdiff_driver *userdiff_find_by_path(struct index_state *istate,
					      const char *path)
{
	static struct attr_check *check;

	if (!check)
		check = attr_check_initl("diff", NULL);
	if (!path)
		return NULL;
	git_check_attr(istate, path, check);

	if (ATTR_TRUE(check->items[0].value))
		return &driver_true;
	if (ATTR_FALSE(check->items[0].value))
		return &driver_false;
	if (ATTR_UNSET(check->items[0].value))
		return NULL;
	return userdiff_find_by_name(check->items[0].value);
}

struct userdiff_driver *userdiff_get_textconv(struct repository *r,
					      struct userdiff_driver *driver)
{
	if (!driver->textconv)
		return NULL;

	if (driver->textconv_want_cache && !driver->textconv_cache) {
		struct notes_cache *c = xmalloc(sizeof(*c));
		struct strbuf name = STRBUF_INIT;

		strbuf_addf(&name, "textconv/%s", driver->name);
		notes_cache_init(r, c, name.buf, driver->textconv);
		driver->textconv_cache = c;
		strbuf_release(&name);
	}

	return driver;
}
