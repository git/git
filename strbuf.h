#ifndef STRBUF_H
#define STRBUF_H

/*
 * NOTE FOR STRBUF DEVELOPERS
 *
 * strbuf is a low-level primitive; as such it should interact only
 * with other low-level primitives. Do not introduce new functions
 * which interact with higher-level APIs.
 */

struct string_list;

/**
 * strbufs are meant to be used with all the usual C string and memory
 * APIs. Given that the length of the buffer is known, it's often better to
 * use the mem* functions than a str* one (e.g., memchr vs. strchr).
 * Though, one has to be careful about the fact that str* functions often
 * stop on NULs and that strbufs may have embedded NULs.
 *
 * A strbuf is NUL terminated for convenience, but no function in the
 * strbuf API actually relies on the string being free of NULs.
 *
 * strbufs have some invariants that are very important to keep in mind:
 *
 *  - The `buf` member is never NULL, so it can be used in any usual C
 *    string operations safely. strbufs _have_ to be initialized either by
 *    `strbuf_init()` or by `= STRBUF_INIT` before the invariants, though.
 *
 *    Do *not* assume anything on what `buf` really is (e.g. if it is
 *    allocated memory or not), use `strbuf_detach()` to unwrap a memory
 *    buffer from its strbuf shell in a safe way. That is the sole supported
 *    way. This will give you a malloced buffer that you can later `free()`.
 *
 *    However, it is totally safe to modify anything in the string pointed by
 *    the `buf` member, between the indices `0` and `len-1` (inclusive).
 *
 *  - The `buf` member is a byte array that has at least `len + 1` bytes
 *    allocated. The extra byte is used to store a `'\0'`, allowing the
 *    `buf` member to be a valid C-string. All strbuf functions ensure this
 *    invariant is preserved.
 *
 *    NOTE: It is OK to "play" with the buffer directly if you work it this
 *    way:
 *
 *        strbuf_grow(sb, SOME_SIZE); <1>
 *        strbuf_setlen(sb, sb->len + SOME_OTHER_SIZE);
 *
 *    <1> Here, the memory array starting at `sb->buf`, and of length
 *    `strbuf_avail(sb)` is all yours, and you can be sure that
 *    `strbuf_avail(sb)` is at least `SOME_SIZE`.
 *
 *    NOTE: `SOME_OTHER_SIZE` must be smaller or equal to `strbuf_avail(sb)`.
 *
 *    Doing so is safe, though if it has to be done in many places, adding the
 *    missing API to the strbuf module is the way to go.
 *
 *    WARNING: Do _not_ assume that the area that is yours is of size `alloc
 *    - 1` even if it's true in the current implementation. Alloc is somehow a
 *    "private" member that should not be messed with. Use `strbuf_avail()`
 *    instead.
*/

/**
 * Data Structures
 * ---------------
 */

/**
 * This is the string buffer structure. The `len` member can be used to
 * determine the current length of the string, and `buf` member provides
 * access to the string itself.
 */
struct strbuf {
	size_t alloc;
	size_t len;
	char *buf;
};

extern char strbuf_slopbuf[];
#define STRBUF_INIT  { .buf = strbuf_slopbuf }

struct object_id;

/**
 * Life Cycle Functions
 * --------------------
 */

/**
 * Initialize the structure. The second parameter can be zero or a bigger
 * number to allocate memory, in case you want to prevent further reallocs.
 */
void strbuf_init(struct strbuf *sb, size_t alloc);

/**
 * Release a string buffer and the memory it used. After this call, the
 * strbuf points to an empty string that does not need to be free()ed, as
 * if it had been set to `STRBUF_INIT` and never modified.
 *
 * To clear a strbuf in preparation for further use without the overhead
 * of free()ing and malloc()ing again, use strbuf_reset() instead.
 */
void strbuf_release(struct strbuf *sb);

/**
 * Detach the string from the strbuf and returns it; you now own the
 * storage the string occupies and it is your responsibility from then on
 * to release it with `free(3)` when you are done with it.
 *
 * The strbuf that previously held the string is reset to `STRBUF_INIT` so
 * it can be reused after calling this function.
 */
char *strbuf_detach(struct strbuf *sb, size_t *sz);

/**
 * Attach a string to a buffer. You should specify the string to attach,
 * the current length of the string and the amount of allocated memory.
 * The amount must be larger than the string length, because the string you
 * pass is supposed to be a NUL-terminated string.  This string _must_ be
 * malloc()ed, and after attaching, the pointer cannot be relied upon
 * anymore, and neither be free()d directly.
 */
void strbuf_attach(struct strbuf *sb, void *str, size_t len, size_t mem);

/**
 * Swap the contents of two string buffers.
 */
static inline void strbuf_swap(struct strbuf *a, struct strbuf *b)
{
	SWAP(*a, *b);
}


/**
 * Functions related to the size of the buffer
 * -------------------------------------------
 */

/**
 * Determine the amount of allocated but unused memory.
 */
static inline size_t strbuf_avail(const struct strbuf *sb)
{
	return sb->alloc ? sb->alloc - sb->len - 1 : 0;
}

/**
 * Ensure that at least this amount of unused memory is available after
 * `len`. This is used when you know a typical size for what you will add
 * and want to avoid repetitive automatic resizing of the underlying buffer.
 * This is never a needed operation, but can be critical for performance in
 * some cases.
 */
void strbuf_grow(struct strbuf *sb, size_t amount);

/**
 * Set the length of the buffer to a given value. This function does *not*
 * allocate new memory, so you should not perform a `strbuf_setlen()` to a
 * length that is larger than `len + strbuf_avail()`. `strbuf_setlen()` is
 * just meant as a 'please fix invariants from this strbuf I just messed
 * with'.
 */
static inline void strbuf_setlen(struct strbuf *sb, size_t len)
{
	if (len > (sb->alloc ? sb->alloc - 1 : 0))
		BUG("strbuf_setlen() beyond buffer");
	sb->len = len;
	if (sb->buf != strbuf_slopbuf)
		sb->buf[len] = '\0';
	else
		assert(!strbuf_slopbuf[0]);
}

/**
 * Empty the buffer by setting the size of it to zero.
 */
#define strbuf_reset(sb)  strbuf_setlen(sb, 0)


/**
 * Functions related to the contents of the buffer
 * -----------------------------------------------
 */

/**
 * Strip whitespace from the beginning (`ltrim`), end (`rtrim`), or both side
 * (`trim`) of a string.
 */
void strbuf_trim(struct strbuf *sb);
void strbuf_rtrim(struct strbuf *sb);
void strbuf_ltrim(struct strbuf *sb);

/* Strip trailing directory separators */
void strbuf_trim_trailing_dir_sep(struct strbuf *sb);

/* Strip trailing LF or CR/LF */
void strbuf_trim_trailing_newline(struct strbuf *sb);

/* Strip trailing character c */
void strbuf_trim_trailing_ch(struct strbuf *sb, int c);

/**
 * Replace the contents of the strbuf with a reencoded form.  Returns -1
 * on error, 0 on success.
 */
int strbuf_reencode(struct strbuf *sb, const char *from, const char *to);

/**
 * Lowercase each character in the buffer using `tolower`.
 */
void strbuf_tolower(struct strbuf *sb);

/**
 * Compare two buffers. Returns an integer less than, equal to, or greater
 * than zero if the first buffer is found, respectively, to be less than,
 * to match, or be greater than the second buffer.
 */
int strbuf_cmp(const struct strbuf *first, const struct strbuf *second);


/**
 * Adding data to the buffer
 * -------------------------
 *
 * NOTE: All of the functions in this section will grow the buffer as
 * necessary.  If they fail for some reason other than memory shortage and the
 * buffer hadn't been allocated before (i.e. the `struct strbuf` was set to
 * `STRBUF_INIT`), then they will free() it.
 */

/**
 * Add a single character to the buffer.
 */
static inline void strbuf_addch(struct strbuf *sb, int c)
{
	if (!strbuf_avail(sb))
		strbuf_grow(sb, 1);
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

/**
 * Add a character the specified number of times to the buffer.
 */
void strbuf_addchars(struct strbuf *sb, int c, size_t n);

/**
 * Insert data to the given position of the buffer. The remaining contents
 * will be shifted, not overwritten.
 */
void strbuf_insert(struct strbuf *sb, size_t pos, const void *, size_t);

/**
 * Insert a NUL-terminated string to the given position of the buffer.
 * The remaining contents will be shifted, not overwritten.  It's an
 * inline function to allow the compiler to resolve strlen() calls on
 * constants at compile time.
 */
static inline void strbuf_insertstr(struct strbuf *sb, size_t pos,
				    const char *s)
{
	strbuf_insert(sb, pos, s, strlen(s));
}

/**
 * Insert data to the given position of the buffer giving a printf format
 * string. The contents will be shifted, not overwritten.
 */
void strbuf_vinsertf(struct strbuf *sb, size_t pos, const char *fmt,
		     va_list ap);

__attribute__((format (printf, 3, 4)))
void strbuf_insertf(struct strbuf *sb, size_t pos, const char *fmt, ...);

/**
 * Remove given amount of data from a given position of the buffer.
 */
void strbuf_remove(struct strbuf *sb, size_t pos, size_t len);

/**
 * Remove the bytes between `pos..pos+len` and replace it with the given
 * data.
 */
void strbuf_splice(struct strbuf *sb, size_t pos, size_t len,
		   const void *data, size_t data_len);

/**
 * Add a NUL-terminated string to the buffer. Each line will be prepended
 * by a comment character and a blank.
 */
void strbuf_add_commented_lines(struct strbuf *out,
				const char *buf, size_t size,
				const char *comment_prefix);


/**
 * Add data of given length to the buffer.
 */
void strbuf_add(struct strbuf *sb, const void *data, size_t len);

/**
 * Add a NUL-terminated string to the buffer.
 *
 * NOTE: This function will *always* be implemented as an inline or a macro
 * using strlen, meaning that this is efficient to write things like:
 *
 *     strbuf_addstr(sb, "immediate string");
 *
 */
static inline void strbuf_addstr(struct strbuf *sb, const char *s)
{
	strbuf_add(sb, s, strlen(s));
}

/**
 * Add a NUL-terminated string the specified number of times to the buffer.
 */
void strbuf_addstrings(struct strbuf *sb, const char *s, size_t n);

/**
 * Copy the contents of another buffer at the end of the current one.
 */
void strbuf_addbuf(struct strbuf *sb, const struct strbuf *sb2);

/**
 * Join the arguments into a buffer. `delim` is put between every
 * two arguments.
 */
const char *strbuf_join_argv(struct strbuf *buf, int argc,
			     const char **argv, char delim);

/**
 * Used with `strbuf_expand_step` to expand the literals %n and %x
 * followed by two hexadecimal digits. Returns the number of recognized
 * characters.
 */
size_t strbuf_expand_literal(struct strbuf *sb, const char *placeholder);

/**
 * If the string pointed to by `formatp` contains a percent sign ("%"),
 * advance it to point to the character following the next one and
 * return 1, otherwise return 0.  Append the substring before that
 * percent sign to `sb`, or the whole string if there is none.
 */
int strbuf_expand_step(struct strbuf *sb, const char **formatp);

/**
 * Used with `strbuf_expand_step` to report unknown placeholders.
 */
void strbuf_expand_bad_format(const char *format, const char *command);

/**
 * Append the contents of one strbuf to another, quoting any
 * percent signs ("%") into double-percents ("%%") in the
 * destination. This is useful for literal data to be fed to either
 * strbuf_expand or to the *printf family of functions.
 */
void strbuf_addbuf_percentquote(struct strbuf *dst, const struct strbuf *src);

#define STRBUF_ENCODE_SLASH 1

/**
 * Append the contents of a string to a strbuf, percent-encoding any characters
 * that are needed to be encoded for a URL.
 *
 * If STRBUF_ENCODE_SLASH is set in flags, percent-encode slashes.  Otherwise,
 * slashes are not percent-encoded.
 */
void strbuf_add_percentencode(struct strbuf *dst, const char *src, int flags);

/**
 * Append the given byte size as a human-readable string (i.e. 12.23 KiB,
 * 3.50 MiB).
 */
void strbuf_humanise_bytes(struct strbuf *buf, off_t bytes);

/**
 * Append the given byte rate as a human-readable string (i.e. 12.23 KiB/s,
 * 3.50 MiB/s).
 */
void strbuf_humanise_rate(struct strbuf *buf, off_t bytes);

/**
 * Add a formatted string to the buffer.
 */
__attribute__((format (printf,2,3)))
void strbuf_addf(struct strbuf *sb, const char *fmt, ...);

/**
 * Add a formatted string prepended by a comment character and a
 * blank to the buffer.
 */
__attribute__((format (printf, 3, 4)))
void strbuf_commented_addf(struct strbuf *sb, const char *comment_prefix, const char *fmt, ...);

__attribute__((format (printf,2,0)))
void strbuf_vaddf(struct strbuf *sb, const char *fmt, va_list ap);

/**
 * Add the time specified by `tm`, as formatted by `strftime`.
 * `tz_offset` is in decimal hhmm format, e.g. -600 means six hours west
 * of Greenwich, and it's used to expand %z internally.  However, tokens
 * with modifiers (e.g. %Ez) are passed to `strftime`.
 * `suppress_tz_name`, when set, expands %Z internally to the empty
 * string rather than passing it to `strftime`.
 */
void strbuf_addftime(struct strbuf *sb, const char *fmt,
		    const struct tm *tm, int tz_offset,
		    int suppress_tz_name);

/**
 * Read a given size of data from a FILE* pointer to the buffer.
 *
 * NOTE: The buffer is rewound if the read fails. If -1 is returned,
 * `errno` must be consulted, like you would do for `read(3)`.
 * `strbuf_read()`, `strbuf_read_file()` and `strbuf_getline_*()`
 * family of functions have the same behaviour as well.
 */
size_t strbuf_fread(struct strbuf *sb, size_t size, FILE *file);

/**
 * Read the contents of a given file descriptor. The third argument can be
 * used to give a hint about the file size, to avoid reallocs.  If read fails,
 * any partial read is undone.
 */
ssize_t strbuf_read(struct strbuf *sb, int fd, size_t hint);

/**
 * Read the contents of a given file descriptor partially by using only one
 * attempt of xread. The third argument can be used to give a hint about the
 * file size, to avoid reallocs. Returns the number of new bytes appended to
 * the sb.
 */
ssize_t strbuf_read_once(struct strbuf *sb, int fd, size_t hint);

/**
 * Read the contents of a file, specified by its path. The third argument
 * can be used to give a hint about the file size, to avoid reallocs.
 * Return the number of bytes read or a negative value if some error
 * occurred while opening or reading the file.
 */
ssize_t strbuf_read_file(struct strbuf *sb, const char *path, size_t hint);

/**
 * Read the target of a symbolic link, specified by its path.  The third
 * argument can be used to give a hint about the size, to avoid reallocs.
 */
int strbuf_readlink(struct strbuf *sb, const char *path, size_t hint);

/**
 * Write the whole content of the strbuf to the stream not stopping at
 * NUL bytes.
 */
ssize_t strbuf_write(struct strbuf *sb, FILE *stream);

/**
 * Read from a FILE * until the specified terminator is encountered,
 * overwriting the existing contents of the strbuf.
 *
 * Reading stops after the terminator or at EOF.  The terminator is
 * removed from the buffer before returning.  If the terminator is LF
 * and if it is preceded by a CR, then the whole CRLF is stripped.
 * Returns 0 unless there was nothing left before EOF, in which case
 * it returns `EOF`.
 */
int strbuf_getdelim_strip_crlf(struct strbuf *sb, FILE *fp, int term);

/**
 * Read a line from a FILE *, overwriting the existing contents of
 * the strbuf.  The strbuf_getline*() family of functions share
 * this signature, but have different line termination conventions.
 *
 * Reading stops after the terminator or at EOF.  The terminator
 * is removed from the buffer before returning.  Returns 0 unless
 * there was nothing left before EOF, in which case it returns `EOF`.
 */
typedef int (*strbuf_getline_fn)(struct strbuf *, FILE *);

/* Uses LF as the line terminator */
int strbuf_getline_lf(struct strbuf *sb, FILE *fp);

/* Uses NUL as the line terminator */
int strbuf_getline_nul(struct strbuf *sb, FILE *fp);

/*
 * Similar to strbuf_getline_lf(), but additionally treats a CR that
 * comes immediately before the LF as part of the terminator.
 * This is the most friendly version to be used to read "text" files
 * that can come from platforms whose native text format is CRLF
 * terminated.
 */
int strbuf_getline(struct strbuf *sb, FILE *file);


/**
 * Like `strbuf_getline`, but keeps the trailing terminator (if
 * any) in the buffer.
 */
int strbuf_getwholeline(struct strbuf *sb, FILE *file, int term);

/**
 * Like `strbuf_getwholeline`, but appends the line instead of
 * resetting the buffer first.
 */
int strbuf_appendwholeline(struct strbuf *sb, FILE *file, int term);

/**
 * Like `strbuf_getwholeline`, but operates on a file descriptor.
 * It reads one character at a time, so it is very slow.  Do not
 * use it unless you need the correct position in the file
 * descriptor.
 */
int strbuf_getwholeline_fd(struct strbuf *sb, int fd, int term);

/**
 * Set the buffer to the path of the current working directory.
 */
int strbuf_getcwd(struct strbuf *sb);

/**
 * Normalize in-place the path contained in the strbuf. See
 * normalize_path_copy() for details. If an error occurs, the contents of "sb"
 * are left untouched, and -1 is returned.
 */
int strbuf_normalize_path(struct strbuf *sb);

/**
 * Strip whitespace from a buffer. If comment_prefix is non-NULL,
 * then lines beginning with that character are considered comments,
 * thus removed.
 */
void strbuf_stripspace(struct strbuf *buf, const char *comment_prefix);

static inline int strbuf_strip_suffix(struct strbuf *sb, const char *suffix)
{
	if (strip_suffix_mem(sb->buf, &sb->len, suffix)) {
		strbuf_setlen(sb, sb->len);
		return 1;
	} else
		return 0;
}

/**
 * Split str (of length slen) at the specified terminator character.
 * Return a null-terminated array of pointers to strbuf objects
 * holding the substrings.  The substrings include the terminator,
 * except for the last substring, which might be unterminated if the
 * original string did not end with a terminator.  If max is positive,
 * then split the string into at most max substrings (with the last
 * substring containing everything following the (max-1)th terminator
 * character).
 *
 * The most generic form is `strbuf_split_buf`, which takes an arbitrary
 * pointer/len buffer. The `_str` variant takes a NUL-terminated string,
 * the `_max` variant takes a strbuf, and just `strbuf_split` is a convenience
 * wrapper to drop the `max` parameter.
 *
 * For lighter-weight alternatives, see string_list_split() and
 * string_list_split_in_place().
 */
struct strbuf **strbuf_split_buf(const char *str, size_t len,
				 int terminator, int max);

static inline struct strbuf **strbuf_split_str(const char *str,
					       int terminator, int max)
{
	return strbuf_split_buf(str, strlen(str), terminator, max);
}

static inline struct strbuf **strbuf_split_max(const struct strbuf *sb,
					       int terminator, int max)
{
	return strbuf_split_buf(sb->buf, sb->len, terminator, max);
}

static inline struct strbuf **strbuf_split(const struct strbuf *sb,
					   int terminator)
{
	return strbuf_split_max(sb, terminator, 0);
}

/*
 * Adds all strings of a string list to the strbuf, separated by the given
 * separator.  For example, if sep is
 *   ', '
 * and slist contains
 *   ['element1', 'element2', ..., 'elementN'],
 * then write:
 *   'element1, element2, ..., elementN'
 * to str.  If only one element, just write "element1" to str.
 */
void strbuf_add_separated_string_list(struct strbuf *str,
				      const char *sep,
				      struct string_list *slist);

/**
 * Free a NULL-terminated list of strbufs (for example, the return
 * values of the strbuf_split*() functions).
 */
void strbuf_list_free(struct strbuf **list);

/*
 * Remove the filename from the provided path string. If the path
 * contains a trailing separator, then the path is considered a directory
 * and nothing is modified.
 *
 * Examples:
 * - "/path/to/file" -> "/path/to/"
 * - "/path/to/dir/" -> "/path/to/dir/"
 */
void strbuf_strip_file_from_path(struct strbuf *sb);

void strbuf_add_lines(struct strbuf *sb,
		      const char *prefix,
		      const char *buf,
		      size_t size);

/**
 * Append s to sb, with the characters '<', '>', '&' and '"' converted
 * into XML entities.
 */
void strbuf_addstr_xml_quoted(struct strbuf *sb,
			      const char *s);

/**
 * "Complete" the contents of `sb` by ensuring that either it ends with the
 * character `term`, or it is empty.  This can be used, for example,
 * to ensure that text ends with a newline, but without creating an empty
 * blank line if there is no content in the first place.
 */
static inline void strbuf_complete(struct strbuf *sb, char term)
{
	if (sb->len && sb->buf[sb->len - 1] != term)
		strbuf_addch(sb, term);
}

static inline void strbuf_complete_line(struct strbuf *sb)
{
	strbuf_complete(sb, '\n');
}

/*
 * Copy "name" to "sb", expanding any special @-marks as handled by
 * repo_interpret_branch_name(). The result is a non-qualified branch name
 * (so "foo" or "origin/master" instead of "refs/heads/foo" or
 * "refs/remotes/origin/master").
 *
 * Note that the resulting name may not be a syntactically valid refname.
 *
 * If "allowed" is non-zero, restrict the set of allowed expansions. See
 * repo_interpret_branch_name() for details.
 */
void strbuf_branchname(struct strbuf *sb, const char *name,
		       unsigned allowed);

/*
 * Like strbuf_branchname() above, but confirm that the result is
 * syntactically valid to be used as a local branch name in refs/heads/.
 *
 * The return value is "0" if the result is valid, and "-1" otherwise.
 */
int strbuf_check_branch_ref(struct strbuf *sb, const char *name);

typedef int (*char_predicate)(char ch);

void strbuf_addstr_urlencode(struct strbuf *sb, const char *name,
			     char_predicate allow_unencoded_fn);

/*
 * Trim and replace each character with ascii code below 32 or above
 * 127 (included) using a dot '.' character. Useful for sending
 * capabilities.
 */
void strbuf_sanitize(struct strbuf *sb);

__attribute__((format (printf,1,2)))
int printf_ln(const char *fmt, ...);
__attribute__((format (printf,2,3)))
int fprintf_ln(FILE *fp, const char *fmt, ...);

char *xstrdup_tolower(const char *);
char *xstrdup_toupper(const char *);

/**
 * Create a newly allocated string using printf format. You can do this easily
 * with a strbuf, but this provides a shortcut to save a few lines.
 */
__attribute__((format (printf, 1, 0)))
char *xstrvfmt(const char *fmt, va_list ap);
__attribute__((format (printf, 1, 2)))
char *xstrfmt(const char *fmt, ...);

int starts_with(const char *str, const char *prefix);
int istarts_with(const char *str, const char *prefix);
int starts_with_mem(const char *str, size_t len, const char *prefix);

/*
 * If the string "str" is the same as the string in "prefix", then the "arg"
 * parameter is set to the "def" parameter and 1 is returned.
 * If the string "str" begins with the string found in "prefix" and then a
 * "=" sign, then the "arg" parameter is set to "str + strlen(prefix) + 1"
 * (i.e., to the point in the string right after the prefix and the "=" sign),
 * and 1 is returned.
 *
 * Otherwise, return 0 and leave "arg" untouched.
 *
 * When we accept both a "--key" and a "--key=<val>" option, this function
 * can be used instead of !strcmp(arg, "--key") and then
 * skip_prefix(arg, "--key=", &arg) to parse such an option.
 */
int skip_to_optional_arg_default(const char *str, const char *prefix,
				 const char **arg, const char *def);

static inline int skip_to_optional_arg(const char *str, const char *prefix,
				       const char **arg)
{
	return skip_to_optional_arg_default(str, prefix, arg, "");
}

static inline int ends_with(const char *str, const char *suffix)
{
	size_t len;
	return strip_suffix(str, suffix, &len);
}

#endif /* STRBUF_H */
