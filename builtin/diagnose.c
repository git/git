#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "abspath.h"
#include "gettext.h"
#include "parse-options.h"
#include "path.h"
#include "diagnose.h"

static const char * const diagnose_usage[] = {
	N_("git diagnose [(-o | --output-directory) <path>] [(-s | --suffix) <format>]\n"
	   "             [--mode=<mode>]"),
	NULL
};

int cmd_diagnose(int argc,
		 const char **argv,
		 const char *prefix,
		 struct repository *repo UNUSED)
{
	struct strbuf zip_path = STRBUF_INIT;
	time_t now = time(NULL);
	struct tm tm;
	enum diagnose_mode mode = DIAGNOSE_STATS;
	char *option_output = NULL;
	const char *option_suffix = "%Y-%m-%d-%H%M";
	char *prefixed_filename;

	const struct option diagnose_options[] = {
		OPT_STRING('o', "output-directory", &option_output, N_("path"),
			   N_("specify a destination for the diagnostics archive")),
		OPT_STRING('s', "suffix", &option_suffix, N_("format"),
			   N_("specify a strftime format suffix for the filename")),
		OPT_CALLBACK_F(0, "mode", &mode, "(stats|all)",
			       N_("specify the content of the diagnostic archive"),
			       PARSE_OPT_NONEG, option_parse_diagnose),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, diagnose_options,
			     diagnose_usage, 0);

	/* Prepare the path to put the result */
	prefixed_filename = prefix_filename(prefix,
					    option_output ? option_output : "");
	strbuf_addstr(&zip_path, prefixed_filename);
	strbuf_complete(&zip_path, '/');

	strbuf_addstr(&zip_path, "git-diagnostics-");
	strbuf_addftime(&zip_path, option_suffix, localtime_r(&now, &tm), 0, 0);
	strbuf_addstr(&zip_path, ".zip");

	switch (safe_create_leading_directories(the_repository, zip_path.buf)) {
	case SCLD_OK:
	case SCLD_EXISTS:
		break;
	default:
		die_errno(_("could not create leading directories for '%s'"),
			  zip_path.buf);
	}

	/* Prepare diagnostics */
	if (create_diagnostics_archive(the_repository, &zip_path, mode))
		die_errno(_("unable to create diagnostics archive %s"),
			  zip_path.buf);

	free(prefixed_filename);
	strbuf_release(&zip_path);
	return 0;
}
