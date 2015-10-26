#include "builtin.h"
#include "tag.h"

/*
 * A signature file has a very simple fixed format: four lines
 * of "object <sha1>" + "type <typename>" + "tag <tagname>" +
 * "tagger <committer>", followed by a blank line, a free-form tag
 * message and a signature block that git itself doesn't care about,
 * but that can be verified with gpg or similar.
 *
 * The first four lines are guaranteed to be at least 83 bytes:
 * "object <sha1>\n" is 48 bytes, "type tag\n" at 9 bytes is the
 * shortest possible type-line, "tag .\n" at 6 bytes is the shortest
 * single-character-tag line, and "tagger . <> 0 +0000\n" at 20 bytes is
 * the shortest possible tagger-line.
 */

/*
 * We refuse to tag something we can't verify. Just because.
 */
static int verify_object(const unsigned char *sha1, const char *expected_type)
{
	int ret = -1;
	enum object_type type;
	unsigned long size;
	void *buffer = read_sha1_file(sha1, &type, &size);
	const unsigned char *repl = lookup_replace_object(sha1);

	if (buffer) {
		if (type == type_from_string(expected_type))
			ret = check_sha1_signature(repl, buffer, size, expected_type);
		free(buffer);
	}
	return ret;
}

static int verify_tag(char *buffer, unsigned long size)
{
	int typelen;
	char type[20];
	unsigned char sha1[20];
	const char *object, *type_line, *tag_line, *tagger_line, *lb, *rb;
	size_t len;

	if (size < 84)
		return error("wanna fool me ? you obviously got the size wrong !");

	buffer[size] = 0;

	/* Verify object line */
	object = buffer;
	if (memcmp(object, "object ", 7))
		return error("char%d: does not start with \"object \"", 0);

	if (get_sha1_hex(object + 7, sha1))
		return error("char%d: could not get SHA1 hash", 7);

	/* Verify type line */
	type_line = object + 48;
	if (memcmp(type_line - 1, "\ntype ", 6))
		return error("char%d: could not find \"\\ntype \"", 47);

	/* Verify tag-line */
	tag_line = strchr(type_line, '\n');
	if (!tag_line)
		return error("char%"PRIuMAX": could not find next \"\\n\"",
				(uintmax_t) (type_line - buffer));
	tag_line++;
	if (memcmp(tag_line, "tag ", 4) || tag_line[4] == '\n')
		return error("char%"PRIuMAX": no \"tag \" found",
				(uintmax_t) (tag_line - buffer));

	/* Get the actual type */
	typelen = tag_line - type_line - strlen("type \n");
	if (typelen >= sizeof(type))
		return error("char%"PRIuMAX": type too long",
				(uintmax_t) (type_line+5 - buffer));

	memcpy(type, type_line+5, typelen);
	type[typelen] = 0;

	/* Verify that the object matches */
	if (verify_object(sha1, type))
		return error("char%d: could not verify object %s", 7, sha1_to_hex(sha1));

	/* Verify the tag-name: we don't allow control characters or spaces in it */
	tag_line += 4;
	for (;;) {
		unsigned char c = *tag_line++;
		if (c == '\n')
			break;
		if (c > ' ')
			continue;
		return error("char%"PRIuMAX": could not verify tag name",
				(uintmax_t) (tag_line - buffer));
	}

	/* Verify the tagger line */
	tagger_line = tag_line;

	if (memcmp(tagger_line, "tagger ", 7))
		return error("char%"PRIuMAX": could not find \"tagger \"",
			(uintmax_t) (tagger_line - buffer));

	/*
	 * Check for correct form for name and email
	 * i.e. " <" followed by "> " on _this_ line
	 * No angle brackets within the name or email address fields.
	 * No spaces within the email address field.
	 */
	tagger_line += 7;
	if (!(lb = strstr(tagger_line, " <")) || !(rb = strstr(lb+2, "> ")) ||
		strpbrk(tagger_line, "<>\n") != lb+1 ||
		strpbrk(lb+2, "><\n ") != rb)
		return error("char%"PRIuMAX": malformed tagger field",
			(uintmax_t) (tagger_line - buffer));

	/* Check for author name, at least one character, space is acceptable */
	if (lb == tagger_line)
		return error("char%"PRIuMAX": missing tagger name",
			(uintmax_t) (tagger_line - buffer));

	/* timestamp, 1 or more digits followed by space */
	tagger_line = rb + 2;
	if (!(len = strspn(tagger_line, "0123456789")))
		return error("char%"PRIuMAX": missing tag timestamp",
			(uintmax_t) (tagger_line - buffer));
	tagger_line += len;
	if (*tagger_line != ' ')
		return error("char%"PRIuMAX": malformed tag timestamp",
			(uintmax_t) (tagger_line - buffer));
	tagger_line++;

	/* timezone, 5 digits [+-]hhmm, max. 1400 */
	if (!((tagger_line[0] == '+' || tagger_line[0] == '-') &&
	      strspn(tagger_line+1, "0123456789") == 4 &&
	      tagger_line[5] == '\n' && atoi(tagger_line+1) <= 1400))
		return error("char%"PRIuMAX": malformed tag timezone",
			(uintmax_t) (tagger_line - buffer));
	tagger_line += 6;

	/* Verify the blank line separating the header from the body */
	if (*tagger_line != '\n')
		return error("char%"PRIuMAX": trailing garbage in tag header",
			(uintmax_t) (tagger_line - buffer));

	/* The actual stuff afterwards we don't care about.. */
	return 0;
}

int cmd_mktag(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	unsigned char result_sha1[20];

	if (argc != 1)
		usage("git mktag");

	if (strbuf_read(&buf, 0, 4096) < 0) {
		die_errno("could not read from stdin");
	}

	/* Verify it for some basic sanity: it needs to start with
	   "object <sha1>\ntype\ntagger " */
	if (verify_tag(buf.buf, buf.len) < 0)
		die("invalid tag signature file");

	if (write_sha1_file(buf.buf, buf.len, tag_type, result_sha1) < 0)
		die("unable to write tag file");

	strbuf_release(&buf);
	printf("%s\n", sha1_to_hex(result_sha1));
	return 0;
}
