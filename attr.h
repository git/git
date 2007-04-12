#ifndef ATTR_H
#define ATTR_H

/* An attribute is a pointer to this opaque structure */
struct git_attr;

struct git_attr *git_attr(const char *, int);

struct git_attr_check {
	struct git_attr *attr;
	int isset;
};

int git_checkattr(const char *path, int, struct git_attr_check *);

#endif /* ATTR_H */
