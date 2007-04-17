#ifndef ATTR_H
#define ATTR_H

/* An attribute is a pointer to this opaque structure */
struct git_attr;

struct git_attr *git_attr(const char *, int);

/* Internal use */
#define ATTR__TRUE	((void *) 1)
#define ATTR__FALSE	((void *) 0)
#define ATTR__UNSET	((void *) -1)

/* For public to check git_attr_check results */
#define ATTR_TRUE(v) ((v) == ATTR__TRUE)
#define ATTR_FALSE(v) ((v) == ATTR__FALSE)
#define ATTR_UNSET(v) ((v) == ATTR__UNSET)

struct git_attr_check {
	struct git_attr *attr;
	void *value;
};

int git_checkattr(const char *path, int, struct git_attr_check *);

#endif /* ATTR_H */
