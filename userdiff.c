#include "userdiff.h"
#include "cache.h"
#include "attr.h"

static struct userdiff_driver *drivers;
static int ndrivers;
static int drivers_alloc;

#define FUNCNAME(name, pattern) \
	{ name, NULL, { pattern, REG_EXTENDED } }
static struct userdiff_driver builtin_drivers[] = {
FUNCNAME("html", "^[ \t]*(<[Hh][1-6][ \t].*>.*)$"),
FUNCNAME("java",
	 "!^[ \t]*(catch|do|for|if|instanceof|new|return|switch|throw|while)\n"
	 "^[ \t]*(([ \t]*[A-Za-z_][A-Za-z_0-9]*){2,}[ \t]*\\([^;]*)$"),
FUNCNAME("objc",
	 /* Negate C statements that can look like functions */
	 "!^[ \t]*(do|for|if|else|return|switch|while)\n"
	 /* Objective-C methods */
	 "^[ \t]*([-+][ \t]*\\([ \t]*[A-Za-z_][A-Za-z_0-9* \t]*\\)[ \t]*[A-Za-z_].*)$\n"
	 /* C functions */
	 "^[ \t]*(([ \t]*[A-Za-z_][A-Za-z_0-9]*){2,}[ \t]*\\([^;]*)$\n"
	 /* Objective-C class/protocol definitions */
	 "^(@(implementation|interface|protocol)[ \t].*)$"),
FUNCNAME("pascal",
	 "^((procedure|function|constructor|destructor|interface|"
		"implementation|initialization|finalization)[ \t]*.*)$"
	 "\n"
	 "^(.*=[ \t]*(class|record).*)$"),
FUNCNAME("php", "^[\t ]*((function|class).*)"),
FUNCNAME("python", "^[ \t]*((class|def)[ \t].*)$"),
FUNCNAME("ruby", "^[ \t]*((class|module|def)[ \t].*)$"),
FUNCNAME("bibtex", "(@[a-zA-Z]{1,}[ \t]*\\{{0,1}[ \t]*[^ \t\"@',\\#}{~%]*).*$"),
FUNCNAME("tex", "^(\\\\((sub)*section|chapter|part)\\*{0,1}\\{.*)$"),
};
#undef FUNCNAME

static struct userdiff_driver driver_true = {
	"diff=true",
	NULL,
	{ NULL, 0 }
};
struct userdiff_driver *USERDIFF_ATTR_TRUE = &driver_true;

static struct userdiff_driver driver_false = {
	"!diff",
	NULL,
	{ NULL, 0 }
};
struct userdiff_driver *USERDIFF_ATTR_FALSE = &driver_false;

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

int userdiff_config_basic(const char *k, const char *v)
{
	struct userdiff_driver *drv;

	if ((drv = parse_driver(k, v, "funcname")))
		return parse_funcname(&drv->funcname, k, v, 0);
	if ((drv = parse_driver(k, v, "xfuncname")))
		return parse_funcname(&drv->funcname, k, v, REG_EXTENDED);

	return 0;
}

int userdiff_config_porcelain(const char *k, const char *v)
{
	struct userdiff_driver *drv;

	if ((drv = parse_driver(k, v, "command")))
		return parse_string(&drv->external, k, v);

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
		attr = git_attr("diff", 4);
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
