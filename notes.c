#include "cache.h"
#include "commit.h"
#include "notes.h"
#include "refs.h"
#include "utf8.h"
#include "strbuf.h"

static int initialized;

void get_commit_notes(const struct commit *commit, struct strbuf *sb,
		const char *output_encoding)
{
	static const char utf8[] = "utf-8";
	struct strbuf name = STRBUF_INIT;
	unsigned char sha1[20];
	char *msg, *msg_p;
	unsigned long linelen, msglen;
	enum object_type type;

	if (!initialized) {
		const char *env = getenv(GIT_NOTES_REF_ENVIRONMENT);
		if (env)
			notes_ref_name = getenv(GIT_NOTES_REF_ENVIRONMENT);
		else if (!notes_ref_name)
			notes_ref_name = GIT_NOTES_DEFAULT_REF;
		if (notes_ref_name && read_ref(notes_ref_name, sha1))
			notes_ref_name = NULL;
		initialized = 1;
	}

	if (!notes_ref_name)
		return;

	strbuf_addf(&name, "%s:%s", notes_ref_name,
			sha1_to_hex(commit->object.sha1));
	if (get_sha1(name.buf, sha1))
		return;

	if (!(msg = read_sha1_file(sha1, &type, &msglen)) || !msglen ||
			type != OBJ_BLOB) {
		free(msg);
		return;
	}

	if (output_encoding && *output_encoding &&
			strcmp(utf8, output_encoding)) {
		char *reencoded = reencode_string(msg, output_encoding, utf8);
		if (reencoded) {
			free(msg);
			msg = reencoded;
			msglen = strlen(msg);
		}
	}

	/* we will end the annotation by a newline anyway */
	if (msglen && msg[msglen - 1] == '\n')
		msglen--;

	strbuf_addstr(sb, "\nNotes:\n");

	for (msg_p = msg; msg_p < msg + msglen; msg_p += linelen + 1) {
		linelen = strchrnul(msg_p, '\n') - msg_p;

		strbuf_addstr(sb, "    ");
		strbuf_add(sb, msg_p, linelen);
		strbuf_addch(sb, '\n');
	}

	free(msg);
}
