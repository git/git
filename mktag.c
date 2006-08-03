#include "cache.h"
#include "tag.h"

/*
 * A signature file has a very simple fixed format: four lines
 * of "object <sha1>" + "type <typename>" + "tag <tagname>" +
 * "tagger <committer>", followed by a blank line, a free-form tag
 * message and a signature block that git itself doesn't care about,
 * but that can be verified with gpg or similar.
 *
 * The first three lines are guaranteed to be at least 63 bytes:
 * "object <sha1>\n" is 48 bytes, "type tag\n" at 9 bytes is the
 * shortest possible type-line, and "tag .\n" at 6 bytes is the
 * shortest single-character-tag line. 
 *
 * We also artificially limit the size of the full object to 8kB.
 * Just because I'm a lazy bastard, and if you can't fit a signature
 * in that size, you're doing something wrong.
 */

/* Some random size */
#define MAXSIZE (8192)

/*
 * We refuse to tag something we can't verify. Just because.
 */
static int verify_object(unsigned char *sha1, const char *expected_type)
{
	int ret = -1;
	char type[100];
	unsigned long size;
	void *buffer = read_sha1_file(sha1, type, &size);

	if (buffer) {
		if (!strcmp(type, expected_type))
			ret = check_sha1_signature(sha1, buffer, size, type);
		free(buffer);
	}
	return ret;
}

#ifdef NO_C99_FORMAT
#define PD_FMT "%d"
#else
#define PD_FMT "%td"
#endif

static int verify_tag(char *buffer, unsigned long size)
{
	int typelen;
	char type[20];
	unsigned char sha1[20];
	const char *object, *type_line, *tag_line, *tagger_line;

	if (size < 64)
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
		return error("char" PD_FMT ": could not find next \"\\n\"", type_line - buffer);
	tag_line++;
	if (memcmp(tag_line, "tag ", 4) || tag_line[4] == '\n')
		return error("char" PD_FMT ": no \"tag \" found", tag_line - buffer);

	/* Get the actual type */
	typelen = tag_line - type_line - strlen("type \n");
	if (typelen >= sizeof(type))
		return error("char" PD_FMT ": type too long", type_line+5 - buffer);

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
		return error("char" PD_FMT ": could not verify tag name", tag_line - buffer);
	}

	/* Verify the tagger line */
	tagger_line = tag_line;

	if (memcmp(tagger_line, "tagger", 6) || (tagger_line[6] == '\n'))
		return error("char" PD_FMT ": could not find \"tagger\"", tagger_line - buffer);

	/* TODO: check for committer info + blank line? */
	/* Also, the minimum length is probably + "tagger .", or 63+8=71 */

	/* The actual stuff afterwards we don't care about.. */
	return 0;
}

#undef PD_FMT

int main(int argc, char **argv)
{
	unsigned long size = 4096;
	char *buffer = malloc(size);
	unsigned char result_sha1[20];

	if (argc != 1)
		usage("git-mktag < signaturefile");

	setup_git_directory();

	if (read_pipe(0, &buffer, &size)) {
		free(buffer);
		die("could not read from stdin");
	}

	/* Verify it for some basic sanity: it needs to start with
	   "object <sha1>\ntype\ntagger " */
	if (verify_tag(buffer, size) < 0)
		die("invalid tag signature file");

	if (write_sha1_file(buffer, size, tag_type, result_sha1) < 0)
		die("unable to write tag file");

	free(buffer);

	printf("%s\n", sha1_to_hex(result_sha1));
	return 0;
}
