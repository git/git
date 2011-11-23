#ifndef ATTR_H
#define ATTR_H

/* An attribute is a pointer to this opaque structure */
struct git_attr;

/*
 * Given a string, return the gitattribute object that
 * corresponds to it.
 */
struct git_attr *git_attr(const char *);

/* Internal use */
extern const char git_attr__true[];
extern const char git_attr__false[];

/* For public to check git_attr_check results */
#define ATTR_TRUE(v) ((v) == git_attr__true)
#define ATTR_FALSE(v) ((v) == git_attr__false)
#define ATTR_UNSET(v) ((v) == NULL)

/*
 * Send one or more git_attr_check to git_check_attr(), and
 * each 'value' member tells what its value is.
 * Unset one is returned as NULL.
 */
struct git_attr_check {
	struct git_attr *attr;
	const char *value;
};

/*
 * Return the name of the attribute represented by the argument.  The
 * return value is a pointer to a null-delimited string that is part
 * of the internal data structure; it should not be modified or freed.
 */
char *git_attr_name(struct git_attr *);

int git_check_attr(const char *path, int, struct git_attr_check *);

/*
 * Retrieve all attributes that apply to the specified path.  *num
 * will be set the the number of attributes on the path; **check will
 * be set to point at a newly-allocated array of git_attr_check
 * objects describing the attributes and their values.  *check must be
 * free()ed by the caller.
 */
int git_all_attrs(const char *path, int *num, struct git_attr_check **check);

enum git_attr_direction {
	GIT_ATTR_CHECKIN,
	GIT_ATTR_CHECKOUT,
	GIT_ATTR_INDEX
};
void git_attr_set_direction(enum git_attr_direction, struct index_state *);

#endif /* ATTR_H */
