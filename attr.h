#ifndef ATTR_H
#define ATTR_H

/*
 * Must be called on platforms that do not support static initialization
 * of mutexes.
 */
extern void attr_start(void);

/* An attribute is a pointer to this opaque structure */
struct git_attr;

/*
 * Return the name of the attribute represented by the argument.  The
 * return value is a pointer to a null-delimited string that is part
 * of the internal data structure; it should not be modified or freed.
 */
extern const char *git_attr_name(const struct git_attr *);

extern int attr_name_valid(const char *name, size_t namelen);
extern void invalid_attr_name_message(struct strbuf *, const char *, int);

/* Internal use */
extern const char git_attr__true[];
extern const char git_attr__false[];

/* For public to check git_attr_check results */
#define ATTR_TRUE(v) ((v) == git_attr__true)
#define ATTR_FALSE(v) ((v) == git_attr__false)
#define ATTR_UNSET(v) ((v) == NULL)

struct git_attr_check {
	struct hashmap_entry entry;
	int finalized;
	int check_nr;
	int check_alloc;
	const struct git_attr **attr;
	struct attr_stack *attr_stack;
};
#define GIT_ATTR_CHECK_INIT {HASHMAP_ENTRY_INIT, 0, 0, 0, NULL, NULL}

struct git_attr_result {
	const char *value;
};

/*
 * Initialize the `git_attr_check` via one of the following three functions:
 *
 * git_all_attrs         allocates a check and fills in all attributes and
 *                       results that are set for the given path.
 * git_attr_check_initl  takes a pointer to where the check will be initialized,
 *                       followed by all attributes that are to be checked.
 * git_attr_check_initv  takes a pointer to where the check will be initialized,
 *                       and a NULL terminated array of attributes.
 *
 * All initialization methods are thread safe.
 * To allocate memory for the result of a given check,
 * use git_attr_result_alloc.
 */
extern void git_attr_check_initl(struct git_attr_check **,
				 const char *, ...);
extern void git_attr_check_initv(struct git_attr_check **,
				 const char **);
extern void git_all_attrs(const char *path,
			  struct git_attr_check *,
			  struct git_attr_result **);
extern struct git_attr_result *git_attr_result_alloc(struct git_attr_check *check);

/* Query a path for its attributes */
extern int git_check_attr(const char *path,
			  struct git_attr_check *,
			  struct git_attr_result *result);

extern void git_attr_check_clear(struct git_attr_check *);
extern void git_attr_result_free(struct git_attr_result *);

enum git_attr_direction {
	GIT_ATTR_CHECKIN,
	GIT_ATTR_CHECKOUT,
	GIT_ATTR_INDEX
};
void git_attr_set_direction(enum git_attr_direction, struct index_state *);

#endif /* ATTR_H */
