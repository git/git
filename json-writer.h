#ifndef JSON_WRITER_H
#define JSON_WRITER_H

/*
 * JSON data structures are defined at:
 * [1] http://www.ietf.org/rfc/rfc7159.txt
 * [2] http://json.org/
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

#define JSON_WRITER_INIT { STRBUF_INIT, STRBUF_INIT, 0, 0 }

void jw_init(struct json_writer *jw);
void jw_release(struct json_writer *jw);

void jw_object_begin(struct json_writer *jw, int pretty);
void jw_array_begin(struct json_writer *jw, int pretty);

void jw_object_string(struct json_writer *jw, const char *key,
		      const char *value);
void jw_object_intmax(struct json_writer *jw, const char *key, intmax_t value);
void jw_object_double(struct json_writer *jw, const char *key, int precision,
		      double value);
void jw_object_true(struct json_writer *jw, const char *key);
void jw_object_false(struct json_writer *jw, const char *key);
void jw_object_bool(struct json_writer *jw, const char *key, int value);
void jw_object_null(struct json_writer *jw, const char *key);
void jw_object_sub_jw(struct json_writer *jw, const char *key,
		      const struct json_writer *value);

void jw_object_inline_begin_object(struct json_writer *jw, const char *key);
void jw_object_inline_begin_array(struct json_writer *jw, const char *key);

void jw_array_string(struct json_writer *jw, const char *value);
void jw_array_intmax(struct json_writer *jw, intmax_t value);
void jw_array_double(struct json_writer *jw, int precision, double value);
void jw_array_true(struct json_writer *jw);
void jw_array_false(struct json_writer *jw);
void jw_array_bool(struct json_writer *jw, int value);
void jw_array_null(struct json_writer *jw);
void jw_array_sub_jw(struct json_writer *jw, const struct json_writer *value);
void jw_array_argc_argv(struct json_writer *jw, int argc, const char **argv);
void jw_array_argv(struct json_writer *jw, const char **argv);

void jw_array_inline_begin_object(struct json_writer *jw);
void jw_array_inline_begin_array(struct json_writer *jw);

int jw_is_terminated(const struct json_writer *jw);
void jw_end(struct json_writer *jw);

#endif /* JSON_WRITER_H */
