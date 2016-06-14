#ifndef ATTR_H
#define ATTR_H

/* An attribute is a pointer to this opaque structure */
struct git_attr;

/*
 * Given a string, return the gitattribute object that
 * corresponds to it.
 */
extern struct git_attr *git_attr(const char *);

/* The same, but with counted string */
extern struct git_attr *git_attr_counted(const char *, size_t);

extern int attr_name_valid(const char *name, size_t namelen);
extern void invalid_attr_name_message(struct strbuf *, const char *, int);

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
struct git_attr_check_elem {
	const struct git_attr *attr;
	const char *value;
};

struct git_attr_check {
	int finalized;
	int check_nr;
	int check_alloc;
	struct git_attr_check_elem *check;
};

extern struct git_attr_check *git_attr_check_initl(const char *, ...);
extern int git_check_attr(const char *path, struct git_attr_check *);
extern int git_check_attr_counted(const char *, int, struct git_attr_check *);

extern struct git_attr_check *git_attr_check_alloc(void);
extern struct git_attr_check_elem *git_attr_check_append(struct git_attr_check *, const struct git_attr *);

extern void git_attr_check_clear(struct git_attr_check *);
extern void git_attr_check_free(struct git_attr_check *);

/*
 * Return the name of the attribute represented by the argument.  The
 * return value is a pointer to a null-delimited string that is part
 * of the internal data structure; it should not be modified or freed.
 */
extern const char *git_attr_name(const struct git_attr *);

/*
 * Retrieve all attributes that apply to the specified path.
 * check holds the attributes and their values.
 */
void git_all_attrs(const char *path, struct git_attr_check *check);

enum git_attr_direction {
	GIT_ATTR_CHECKIN,
	GIT_ATTR_CHECKOUT,
	GIT_ATTR_INDEX
};
void git_attr_set_direction(enum git_attr_direction, struct index_state *);

#endif /* ATTR_H */
