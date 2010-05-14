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
 * Send one or more git_attr_check to git_checkattr(), and
 * each 'value' member tells what its value is.
 * Unset one is returned as NULL.
 */
struct git_attr_check {
	struct git_attr *attr;
	const char *value;
};

int git_checkattr(const char *path, int, struct git_attr_check *);

enum git_attr_direction {
	GIT_ATTR_CHECKIN,
	GIT_ATTR_CHECKOUT,
	GIT_ATTR_INDEX
};
void git_attr_set_direction(enum git_attr_direction, struct index_state *);

#endif /* ATTR_H */
