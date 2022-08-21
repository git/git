#include "test-tool.h"
#include "cache.h"
#include "userdiff.h"
#include "config.h"

static int driver_cb(struct userdiff_driver *driver,
		     enum userdiff_driver_type type, void *priv)
{
	enum userdiff_driver_type *want_type = priv;
	if (type & *want_type && driver->funcname.pattern)
		puts(driver->name);
	return 0;
}

static int cmd__userdiff_config(const char *var, const char *value, void *cb)
{
	if (userdiff_config(var, value) < 0)
		return -1;
	return 0;
}

int cmd__userdiff(int argc, const char **argv)
{
	enum userdiff_driver_type want = 0;
	if (argc != 2)
		return 1;

	if (!strcmp(argv[1], "list-drivers"))
		want = (USERDIFF_DRIVER_TYPE_BUILTIN |
			USERDIFF_DRIVER_TYPE_CUSTOM);
	else if (!strcmp(argv[1], "list-builtin-drivers"))
		want = USERDIFF_DRIVER_TYPE_BUILTIN;
	else if (!strcmp(argv[1], "list-custom-drivers"))
		want = USERDIFF_DRIVER_TYPE_CUSTOM;
	else
		return error("unknown argument %s", argv[1]);

	if (want & USERDIFF_DRIVER_TYPE_CUSTOM) {
		setup_git_directory();
		git_config(cmd__userdiff_config, NULL);
	}

	for_each_userdiff_driver(driver_cb, &want);

	return 0;
}
