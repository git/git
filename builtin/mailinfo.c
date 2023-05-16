/*
 * Another stupid program, this one parsing the headers of an
 * email to figure out authorship and subject
 */
#include "cache.h"
#include "abspath.h"
#include "builtin.h"
#include "environment.h"
#include "gettext.h"
#include "utf8.h"
#include "strbuf.h"
#include "mailinfo.h"
#include "parse-options.h"

static const char * const mailinfo_usage[] = {
	/* TRANSLATORS: keep <> in "<" mail ">" info. */
	N_("git mailinfo [<options>] <msg> <patch> < mail >info"),
	NULL,
};

struct metainfo_charset
{
	enum {
		CHARSET_DEFAULT,
		CHARSET_NO_REENCODE,
		CHARSET_EXPLICIT,
	} policy;
	const char *charset;
};

static int parse_opt_explicit_encoding(const struct option *opt,
				       const char *arg, int unset)
{
	struct metainfo_charset *meta_charset = opt->value;

	BUG_ON_OPT_NEG(unset);

	meta_charset->policy = CHARSET_EXPLICIT;
	meta_charset->charset = arg;

	return 0;
}

static int parse_opt_quoted_cr(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);

	if (mailinfo_parse_quoted_cr_action(arg, opt->value) != 0)
		return error(_("bad action '%s' for '%s'"), arg, "--quoted-cr");
	return 0;
}

int cmd_mailinfo(int argc, const char **argv, const char *prefix)
{
	struct metainfo_charset meta_charset;
	struct mailinfo mi;
	int status;
	char *msgfile, *patchfile;

	struct option options[] = {
		OPT_BOOL('k', NULL, &mi.keep_subject, N_("keep subject")),
		OPT_BOOL('b', NULL, &mi.keep_non_patch_brackets_in_subject,
			 N_("keep non patch brackets in subject")),
		OPT_BOOL('m', "message-id", &mi.add_message_id,
			 N_("copy Message-ID to the end of commit message")),
		OPT_SET_INT_F('u', NULL, &meta_charset.policy,
			      N_("re-code metadata to i18n.commitEncoding"),
			      CHARSET_DEFAULT, PARSE_OPT_NONEG),
		OPT_SET_INT_F('n', NULL, &meta_charset.policy,
			      N_("disable charset re-coding of metadata"),
			      CHARSET_NO_REENCODE, PARSE_OPT_NONEG),
		OPT_CALLBACK_F(0, "encoding", &meta_charset, N_("encoding"),
			       N_("re-code metadata to this encoding"),
			       PARSE_OPT_NONEG, parse_opt_explicit_encoding),
		OPT_BOOL(0, "scissors", &mi.use_scissors, N_("use scissors")),
		OPT_CALLBACK_F(0, "quoted-cr", &mi.quoted_cr, N_("<action>"),
			       N_("action when quoted CR is found"),
			       PARSE_OPT_NONEG, parse_opt_quoted_cr),
		OPT_HIDDEN_BOOL(0, "inbody-headers", &mi.use_inbody_headers,
			 N_("use headers in message's body")),
		OPT_END()
	};

	setup_mailinfo(&mi);
	meta_charset.policy = CHARSET_DEFAULT;

	argc = parse_options(argc, argv, prefix, options, mailinfo_usage, 0);

	if (argc != 2)
		usage_with_options(mailinfo_usage, options);

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

	msgfile = prefix_filename(prefix, argv[0]);
	patchfile = prefix_filename(prefix, argv[1]);

	status = !!mailinfo(&mi, msgfile, patchfile);
	clear_mailinfo(&mi);

	free(msgfile);
	free(patchfile);
	return status;
}
