#ifndef JSON_WRITER_H
#define JSON_WRITER_H

/*
 * JSON data structures are defined at:
 * [1] https://www.ietf.org/rfc/rfc7159.txt
 * [2] https://www.json.org/
 *
 * The JSON-writer API allows one to build JSON data structures using a
 * simple wrapper around a "struct strbuf" buffer.  It is intended as a
 * simple API to build output strings; it is not intended to be a general
 * object model for JSON data.  In particular, it does not re-order keys
 * in an object (dictionary), it does not de-dup keys in an object, and
 * it does not allow lookup or parsing of JSON data.
 *
 * All string values (both keys and string r-values) are properly quoted
 * and escaped if they contain special characters.
 *
 * These routines create compact JSON data (with no unnecessary whitespace,
 * newlines, or indenting).  If you get an unexpected response, verify
 * that you're not expecting a pretty JSON string.
 *
 * Both "JSON objects" (aka sets of k/v pairs) and "JSON array" can be
 * constructed using a 'begin append* end' model.
 *
 * Nested objects and arrays can either be constructed bottom up (by
 * creating sub object/arrays first and appending them to the super
 * object/array) -or- by building them inline in one pass.  This is a
 * personal style and/or data shape choice.
 *
 * USAGE:
 * ======
 *
 * - Initialize the json_writer with jw_init.
 *
 * - Open an object as the main data structure with jw_object_begin.
 *   Append a key-value pair to it using the jw_object_<type> functions.
 *   Conclude with jw_end.
 *
 * - Alternatively, open an array as the main data structure with
 *   jw_array_begin. Append a value to it using the jw_array_<type>
 *   functions. Conclude with jw_end.
 *
 * - Append a new, unterminated array or object to the current
 *   object using the jw_object_inline_begin_{array, object} functions.
 *   Similarly, append a new, unterminated array or object to
 *   the current array using the jw_array_inline_begin_{array, object}
 *   functions.
 *
 * - Append other json_writer as a value to the current array or object
 *   using the jw_{array, object}_sub_jw functions.
 *
 * - Extend the current array with an null-terminated array of strings
 *   by using jw_array_argv or with a fixed number of elements of a
 *   array of string by using jw_array_argc_argv.
 *
 * - Release the json_writer after using it by calling jw_release.
 *
 * See t/helper/test-json-writer.c for various usage examples.
 *
 * LIMITATIONS:
 * ============
 *
 * The JSON specification [1,2] defines string values as Unicode data
 * and probably UTF-8 encoded.  The current json-writer API does not
 * enforce this and will write any string as received.  However, it will
 * properly quote and backslash-escape them as necessary.  It is up to
 * the caller to UTF-8 encode their strings *before* passing them to this
 * API.  This layer should not have to try to guess the encoding or locale
 * of the given strings.
 */

#include "strbuf.h"

struct json_writer
{
	/*
	 * Buffer of the in-progress JSON currently being composed.
	 */
	struct strbuf json;

	/*
	 * Simple stack of the currently open array and object forms.
	 * This is a string of '{' and '[' characters indicating the
	 * currently unterminated forms.  This is used to ensure the
	 * properly closing character is used when popping a level and
	 * to know when the JSON is completely closed.
	 */
	struct strbuf open_stack;

	unsigned int need_comma:1;
	unsigned int pretty:1;
};

#define JSON_WRITER_INIT { \
	.json = STRBUF_INIT, \
	.open_stack = STRBUF_INIT, \
}

/*
 * Initialize a json_writer with empty values.
 */
void jw_init(struct json_writer *jw);

/*
 * Release the internal buffers of a json_writer.
 */
void jw_release(struct json_writer *jw);

/*
 * Begin the json_writer using an object as the top-level data structure. If
 * pretty is set to 1, the result will be a human-readable and indented JSON,
 * and if it is set to 0 the result will be minified single-line JSON.
 */
void jw_object_begin(struct json_writer *jw, int pretty);

/*
 * Begin the json_writer using an array as the top-level data structure. If
 * pretty is set to 1, the result will be a human-readable and indented JSON,
 * and if it is set to 0 the result will be minified single-line JSON.
 */
void jw_array_begin(struct json_writer *jw, int pretty);

/*
 * Append a string field to the current object of the json_writer, given its key
 * and its value. Trigger a BUG when not in an object.
 */
void jw_object_string(struct json_writer *jw, const char *key,
		      const char *value);

/*
 * Append an int field to the current object of the json_writer, given its key
 * and its value. Trigger a BUG when not in an object.
 */
void jw_object_intmax(struct json_writer *jw, const char *key, intmax_t value);

/*
 * Append a double field to the current object of the json_writer, given its key
 * and its value. The precision parameter defines the number of significant
 * digits, where -1 can be used for maximum precision. Trigger a BUG when not in
 * an object.
 */
void jw_object_double(struct json_writer *jw, const char *key, int precision,
		      double value);

/*
 * Append a boolean field set to true to the current object of the json_writer,
 * given its key. Trigger a BUG when not in an object.
 */
void jw_object_true(struct json_writer *jw, const char *key);

/*
 * Append a boolean field set to false to the current object of the json_writer,
 * given its key. Trigger a BUG when not in an object.
 */
void jw_object_false(struct json_writer *jw, const char *key);

/*
 * Append a boolean field to the current object of the json_writer, given its
 * key and its value. Trigger a BUG when not in an object.
 */
void jw_object_bool(struct json_writer *jw, const char *key, int value);

/*
 * Append a null field to the current object of the json_writer, given its key.
 * Trigger a BUG when not in an object.
 */
void jw_object_null(struct json_writer *jw, const char *key);

/*
 * Append a field to the current object of the json_writer, given its key and
 * another json_writer that represents its content. Trigger a BUG when not in
 * an object.
 */
void jw_object_sub_jw(struct json_writer *jw, const char *key,
		      const struct json_writer *value);

/*
 * Start an object as the value of a field in the current object of the
 * json_writer. Trigger a BUG when not in an object.
 */
void jw_object_inline_begin_object(struct json_writer *jw, const char *key);

/*
 * Start an array as the value of a field in the current object of the
 * json_writer. Trigger a BUG when not in an object.
 */
void jw_object_inline_begin_array(struct json_writer *jw, const char *key);

/*
 * Append a string value to the current array of the json_writer. Trigger a BUG
 * when not in an array.
 */
void jw_array_string(struct json_writer *jw, const char *value);

/*
 * Append an int value to the current array of the json_writer. Trigger a BUG
 * when not in an array.
 */
void jw_array_intmax(struct json_writer *jw, intmax_t value);

/*
 * Append a double value to the current array of the json_writer. The precision
 * parameter defines the number of significant digits, where -1 can be used for
 * maximum precision. Trigger a BUG when not in an array.
 */
void jw_array_double(struct json_writer *jw, int precision, double value);

/*
 * Append a true value to the current array of the json_writer. Trigger a BUG
 * when not in an array.
 */
void jw_array_true(struct json_writer *jw);

/*
 * Append a false value to the current array of the json_writer. Trigger a BUG
 * when not in an array.
 */
void jw_array_false(struct json_writer *jw);

/*
 * Append a boolean value to the current array of the json_writer. Trigger a BUG
 * when not in an array.
 */
void jw_array_bool(struct json_writer *jw, int value);

/*
 * Append a null value to the current array of the json_writer. Trigger a BUG
 * when not in an array.
 */
void jw_array_null(struct json_writer *jw);

/*
 * Append a json_writer as a value to the current array of the
 * json_writer. Trigger a BUG when not in an array.
 */
void jw_array_sub_jw(struct json_writer *jw, const struct json_writer *value);

/*
 * Append the first argc values from the argv array of strings to the current
 * array of the json_writer. Trigger a BUG when not in an array.
 *
 * This function does not provide safety for cases where the array has less than
 * argc values.
 */
void jw_array_argc_argv(struct json_writer *jw, int argc, const char **argv);

/*
 * Append a null-terminated array of strings to the current array of the
 * json_writer. Trigger a BUG when not in an array.
 */
void jw_array_argv(struct json_writer *jw, const char **argv);

/*
 * Start an object as a value in the current array of the json_writer. Trigger a
 * BUG when not in an array.
 */
void jw_array_inline_begin_object(struct json_writer *jw);

/*
 * Start an array as a value in the current array. Trigger a BUG when not in an
 * array.
 */
void jw_array_inline_begin_array(struct json_writer *jw);

/*
 * Return whether the json_writer is terminated. In other words, if the all the
 * objects and arrays are already closed.
 */
int jw_is_terminated(const struct json_writer *jw);

/*
 * Terminates the current object or array of the json_writer. In other words,
 * append a ] if the current array is not closed or } if the current object
 * is not closed.
 *
 * Abort the execution if there's no object or array that can be terminated.
 */
void jw_end(struct json_writer *jw);

#endif /* JSON_WRITER_H */
