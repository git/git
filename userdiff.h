#ifndef USERDIFF_H
#define USERDIFF_H

struct userdiff_funcname {
	const char *pattern;
	int cflags;
};

struct userdiff_driver {
	const char *name;
	const char *external;
	int binary;
	struct userdiff_funcname funcname;
};

int userdiff_config_basic(const char *k, const char *v);
int userdiff_config_porcelain(const char *k, const char *v);
struct userdiff_driver *userdiff_find_by_name(const char *name);
struct userdiff_driver *userdiff_find_by_path(const char *path);

#endif /* USERDIFF */
