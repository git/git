#ifndef STRVEC_H
#define STRVEC_H

/**
 * The strvec API allows one to dynamically build and store
 * NULL-terminated arrays of strings. A strvec maintains the invariant that the
 * `v` member always points to a non-NULL array, and that the array is
 * always NULL-terminated at the element pointed to by `v[nr]`. This
 * makes the result suitable for passing to functions expecting to receive
 * argv from main().
 *
 * The string-list API (documented in string-list.h) is similar, but cannot be
 * used for these purposes; instead of storing a straight string pointer,
 * it contains an item structure with a `util` field that is not compatible
 * with the traditional argv interface.
 *
 * Each `strvec` manages its own memory. Any strings pushed into the
 * array are duplicated, and all memory is freed by strvec_clear().
 */

extern const char *empty_strvec[];

/**
 * A single array. This should be initialized by assignment from
 * `STRVEC_INIT`, or by calling `strvec_init`. The `v`
 * member contains the actual array; the `nr` member contains the
 * number of elements in the array, not including the terminating
 * NULL.
 */
struct strvec {
	const char **v;
	size_t nr;
	size_t alloc;
};

#define STRVEC_INIT { \
	.v = empty_strvec, \
}

/**
 * Initialize an array. This is no different than assigning from
 * `STRVEC_INIT`.
 */
void strvec_init(struct strvec *);

/* Push a copy of a string onto the end of the array. */
const char *strvec_push(struct strvec *, const char *);

/* Push an allocated string onto the end of the array, taking ownership. */
void strvec_push_nodup(struct strvec *array, char *value);

/**
 * Format a string and push it onto the end of the array. This is a
 * convenience wrapper combining `strbuf_addf` and `strvec_push`.
 */
__attribute__((format (printf,2,3)))
const char *strvec_pushf(struct strvec *, const char *fmt, ...);

/**
 * Push a list of strings onto the end of the array. The arguments
 * should be a list of `const char *` strings, terminated by a NULL
 * argument.
 */
LAST_ARG_MUST_BE_NULL
void strvec_pushl(struct strvec *, ...);

/* Push a null-terminated array of strings onto the end of the array. */
void strvec_pushv(struct strvec *, const char **);

/*
 * Replace `len` values starting at `idx` with the provided replacement
 * strings. If `len` is zero this is effectively an insert at the given `idx`.
 * If `replacement_len` is zero this is effectively a delete of `len` items
 * starting at `idx`.
 */
void strvec_splice(struct strvec *array, size_t idx, size_t len,
		   const char **replacement, size_t replacement_len);

/**
 * Replace the value at the given index with a new value. The index must be
 * valid. Returns a pointer to the inserted value.
 */
const char *strvec_replace(struct strvec *array, size_t idx, const char *replacement);

/*
 * Remove the value at the given index. The remainder of the array will be
 * moved to fill the resulting gap. The provided index must point into the
 * array.
 */
void strvec_remove(struct strvec *array, size_t idx);

/**
 * Remove the final element from the array. If there are no
 * elements in the array, do nothing.
 */
void strvec_pop(struct strvec *);

/* Splits by whitespace; does not handle quoted arguments! */
void strvec_split(struct strvec *, const char *);

/**
 * Free all memory associated with the array and return it to the
 * initial, empty state.
 */
void strvec_clear(struct strvec *);

/**
 * Disconnect the `v` member from the `strvec` struct and
 * return it. The caller is responsible for freeing the memory used
 * by the array, and by the strings it references. After detaching,
 * the `strvec` is in a reinitialized state and can be pushed
 * into again.
 */
const char **strvec_detach(struct strvec *);

#endif /* STRVEC_H */
