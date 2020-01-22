#ifndef ARGV_ARRAY_H
#define ARGV_ARRAY_H

/**
 * The argv-array API allows one to dynamically build and store
 * NULL-terminated lists.  An argv-array maintains the invariant that the
 * `argv` member always points to a non-NULL array, and that the array is
 * always NULL-terminated at the element pointed to by `argv[argc]`. This
 * makes the result suitable for passing to functions expecting to receive
 * argv from main().
 *
 * The string-list API (documented in string-list.h) is similar, but cannot be
 * used for these purposes; instead of storing a straight string pointer,
 * it contains an item structure with a `util` field that is not compatible
 * with the traditional argv interface.
 *
 * Each `argv_array` manages its own memory. Any strings pushed into the
 * array are duplicated, and all memory is freed by argv_array_clear().
 */

extern const char *empty_argv[];

/**
 * A single array. This should be initialized by assignment from
 * `ARGV_ARRAY_INIT`, or by calling `argv_array_init`. The `argv`
 * member contains the actual array; the `argc` member contains the
 * number of elements in the array, not including the terminating
 * NULL.
 */
struct argv_array {
	const char **argv;
	int argc;
	int alloc;
};

#define ARGV_ARRAY_INIT { empty_argv, 0, 0 }

/**
 * Initialize an array. This is no different than assigning from
 * `ARGV_ARRAY_INIT`.
 */
void argv_array_init(struct argv_array *);

/* Push a copy of a string onto the end of the array. */
const char *argv_array_push(struct argv_array *, const char *);

/**
 * Format a string and push it onto the end of the array. This is a
 * convenience wrapper combining `strbuf_addf` and `argv_array_push`.
 */
__attribute__((format (printf,2,3)))
const char *argv_array_pushf(struct argv_array *, const char *fmt, ...);

/**
 * Push a list of strings onto the end of the array. The arguments
 * should be a list of `const char *` strings, terminated by a NULL
 * argument.
 */
LAST_ARG_MUST_BE_NULL
void argv_array_pushl(struct argv_array *, ...);

/* Push a null-terminated array of strings onto the end of the array. */
void argv_array_pushv(struct argv_array *, const char **);

/**
 * Remove the final element from the array. If there are no
 * elements in the array, do nothing.
 */
void argv_array_pop(struct argv_array *);

/* Splits by whitespace; does not handle quoted arguments! */
void argv_array_split(struct argv_array *, const char *);

/**
 * Free all memory associated with the array and return it to the
 * initial, empty state.
 */
void argv_array_clear(struct argv_array *);

/**
 * Disconnect the `argv` member from the `argv_array` struct and
 * return it. The caller is responsible for freeing the memory used
 * by the array, and by the strings it references. After detaching,
 * the `argv_array` is in a reinitialized state and can be pushed
 * into again.
 */
const char **argv_array_detach(struct argv_array *);

#endif /* ARGV_ARRAY_H */
