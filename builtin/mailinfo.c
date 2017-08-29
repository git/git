/*
 * Another stupid program, this one parsing the headers of an
 * email to figure out authorship and subject
 */
#include "cache.h"
#include "builtin.h"
#include "utf8.h"
#include "strbuf.h"
#include "mailinfo.h"
#include "config.h"

static const char mailinfo_usage[] =
	"git mailinfo [-k | -b] [-m | --message-id] [-u | --encoding=<encoding> | -n] [--scissors | --no-scissors] <msg> <patch> < mail >info";

int cmd_mailinfo(int argc, const char **argv, const char *prefix)
{
	const char *def_charset;
	struct mailinfo mi;
	int status;
	char *msgfile, *patchfile;

	git_config(git_default_config, NULL);
	setup_mailinfo(&mi);

	def_charset = get_commit_output_encoding();
	mi.metainfo_charset = def_charset;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-k"))
			mi.keep_subject = 1;
		else if (!strcmp(argv[1], "-b"))
			mi.keep_non_patch_brackets_in_subject = 1;
		else if (!strcmp(argv[1], "-m") || !strcmp(argv[1], "--message-id"))
			mi.add_message_id = 1;
		else if (!strcmp(argv[1], "-u"))
			mi.metainfo_charset = def_charset;
		else if (!strcmp(argv[1], "-n"))
			mi.metainfo_charset = NULL;
		else if (starts_with(argv[1], "--encoding="))
			mi.metainfo_charset = argv[1] + 11;
		else if (!strcmp(argv[1], "--scissors"))
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
