#include "builtin.h"
#include "parse-options.h"
#include "stdio.h"
#include "strbuf.h"
#include "time.h"

static const char * const bugreport_usage[] = {
	N_("git bugreport [-o|--output-directory <file>] [-s|--suffix <format>]"),
	NULL
};

static int get_bug_template(struct strbuf *template)
{
	const char template_text[] = N_(
"Thank you for filling out a Git bug report!\n"
"Please answer the following questions to help us understand your issue.\n"
"\n"
"What did you do before the bug happened? (Steps to reproduce your issue)\n"
"\n"
"What did you expect to happen? (Expected behavior)\n"
"\n"
"What happened instead? (Actual behavior)\n"
"\n"
"What's different between what you expected and what actually happened?\n"
"\n"
"Anything else you want to add:\n"
"\n"
"Please review the rest of the bug report below.\n"
"You can delete any lines you don't wish to share.\n");

	strbuf_addstr(template, _(template_text));
	return 0;
}

int cmd_main(int argc, const char **argv)
{
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf report_path = STRBUF_INIT;
	int report = -1;
	time_t now = time(NULL);
	char *option_output = NULL;
	char *option_suffix = "%Y-%m-%d-%H%M";
	int nongit_ok = 0;
	const char *prefix = NULL;
	const char *user_relative_path = NULL;

	const struct option bugreport_options[] = {
		OPT_STRING('o', "output-directory", &option_output, N_("path"),
			   N_("specify a destination for the bugreport file")),
		OPT_STRING('s', "suffix", &option_suffix, N_("format"),
			   N_("specify a strftime format suffix for the filename")),
		OPT_END()
	};

	prefix = setup_git_directory_gently(&nongit_ok);

	argc = parse_options(argc, argv, prefix, bugreport_options,
			     bugreport_usage, 0);

	/* Prepare the path to put the result */
	strbuf_addstr(&report_path,
		      prefix_filename(prefix,
				      option_output ? option_output : ""));
	strbuf_complete(&report_path, '/');

	strbuf_addstr(&report_path, "git-bugreport-");
	strbuf_addftime(&report_path, option_suffix, localtime(&now), 0, 0);
	strbuf_addstr(&report_path, ".txt");

	switch (safe_create_leading_directories(report_path.buf)) {
	case SCLD_OK:
	case SCLD_EXISTS:
		break;
	default:
		die(_("could not create leading directories for '%s'"),
		    report_path.buf);
	}

	/* Prepare the report contents */
	get_bug_template(&buffer);

	/* fopen doesn't offer us an O_EXCL alternative, except with glibc. */
	report = open(report_path.buf, O_CREAT | O_EXCL | O_WRONLY, 0666);

	if (report < 0) {
		UNLEAK(report_path);
		die(_("couldn't create a new file at '%s'"), report_path.buf);
	}

	strbuf_write_fd(&buffer, report);
	close(report);

	/*
	 * We want to print the path relative to the user, but we still need the
	 * path relative to us to give to the editor.
	 */
	if (!(prefix && skip_prefix(report_path.buf, prefix, &user_relative_path)))
		user_relative_path = report_path.buf;
	fprintf(stderr, _("Created new report at '%s'.\n"),
		user_relative_path);

	UNLEAK(buffer);
	UNLEAK(report_path);
	return !!launch_editor(report_path.buf, NULL, NULL);
}
