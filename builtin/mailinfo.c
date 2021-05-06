/*
 * Another stupid program, this one parsing the headers of an
 * email to figure out authorship and subject
 */
#include "cache.h"
#include "builtin.h"
#include "utf8.h"
#include "strbuf.h"
#include "mailinfo.h"

static const char mailinfo_usage[] =
	"git mailinfo [-k | -b] [-m | --message-id] [-u | --encoding=<encoding> | -n] [--scissors | --no-scissors] <msg> <patch> < mail >info";

struct metainfo_charset
{
	enum {
		CHARSET_DEFAULT,
		CHARSET_NO_REENCODE,
		CHARSET_EXPLICIT,
	} policy;
	const char *charset;
};

int cmd_mailinfo(int argc, const char **argv, const char *prefix)
{
	struct metainfo_charset meta_charset;
	struct mailinfo mi;
	int status;
	char *msgfile, *patchfile;

	setup_mailinfo(&mi);
	meta_charset.policy = CHARSET_DEFAULT;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-k"))
			mi.keep_subject = 1;
		else if (!strcmp(argv[1], "-b"))
			mi.keep_non_patch_brackets_in_subject = 1;
		else if (!strcmp(argv[1], "-m") || !strcmp(argv[1], "--message-id"))
			mi.add_message_id = 1;
		else if (!strcmp(argv[1], "-u"))
			meta_charset.policy = CHARSET_DEFAULT;
		else if (!strcmp(argv[1], "-n"))
			meta_charset.policy = CHARSET_NO_REENCODE;
		else if (starts_with(argv[1], "--encoding=")) {
			meta_charset.policy = CHARSET_EXPLICIT;
			meta_charset.charset = argv[1] + 11;
		} else if (!strcmp(argv[1], "--scissors"))
			mi.use_scissors = 1;
		else if (!strcmp(argv[1], "--no-scissors"))
			mi.use_scissors = 0;
		else if (!strcmp(argv[1], "--no-inbody-headers"))
			mi.use_inbody_headers = 0;
		else
			usage(mailinfo_usage);
		argc--; argv++;
	}

	if (argc != 3)
		usage(mailinfo_usage);

	switch (meta_charset.policy) {
	case CHARSET_DEFAULT:
		mi.metainfo_charset = get_commit_output_encoding();
		break;
	case CHARSET_NO_REENCODE:
		mi.metainfo_charset = NULL;
		break;
	case CHARSET_EXPLICIT:
		break;
	default:
		BUG("invalid meta_charset.policy");
	}

	mi.input = stdin;
	mi.output = stdout;

	msgfile = prefix_filename(prefix, argv[1]);
	patchfile = prefix_filename(prefix, argv[2]);

	status = !!mailinfo(&mi, msgfile, patchfile);
	clear_mailinfo(&mi);

	free(msgfile);
	free(patchfile);
	return status;
}
