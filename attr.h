#ifndef ATTR_H
#define ATTR_H

struct index_state;

/* An attribute is a pointer to this opaque structure */
struct git_attr;

/* opaque structures used internally for attribute collection */
struct all_attrs_item;
struct attr_stack;
struct index_state;

/*
 * Given a string, return the gitattribute object that
 * corresponds to it.
 */
const struct git_attr *git_attr(const char *);

/* Internal use */
extern const char git_attr__true[];
extern const char git_attr__false[];

/* For public to check git_attr_check results */
#define ATTR_TRUE(v) ((v) == git_attr__true)
#define ATTR_FALSE(v) ((v) == git_attr__false)
#define ATTR_UNSET(v) ((v) == NULL)

/*
 * Send one or more git_attr_check to git_check_attrs(), and
 * each 'value' member tells what its value is.
 * Unset one is returned as NULL.
 */
struct attr_check_item {
	const struct git_attr *attr;
	const char *value;
};

struct attr_check {
	int nr;
	int alloc;
	struct attr_check_item *items;
	int all_attrs_nr;
	struct all_attrs_item *all_attrs;
	struct attr_stack *stack;
};

struct attr_check *attr_check_alloc(void);
struct attr_check *attr_check_initl(const char *, ...);
struct attr_check *attr_check_dup(const struct attr_check *check);

struct attr_check_item *attr_check_append(struct attr_check *check,
					  const struct git_attr *attr);

void attr_check_reset(struct attr_check *check);
void attr_check_clear(struct attr_check *check);
void attr_check_free(struct attr_check *check);

/*
 * Return the name of the attribute represented by the argument.  The
 * return value is a pointer to a null-delimited string that is part
 * of the internal data structure; it should not be modified or freed.
 */
const char *git_attr_name(const struct git_attr *);

void git_check_attr(const struct index_state *istate,
		    const char *path, struct attr_check *check);

/*
 * Retrieve all attributes that apply to the specified path.
 * check holds the attributes and their values.
 */
void git_all_attrs(const struct index_state *istate,
		   const char *path, struct attr_check *check);

enum git_attr_direction {
	GIT_ATTR_CHECKIN,
	GIT_ATTR_CHECKOUT,
	GIT_ATTR_INDEX
};
void git_attr_set_direction(enum git_attr_direction new_direction);

void attr_start(void);

#endif /* ATTR_H */
