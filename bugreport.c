#include "cache.h"
#include "parse-options.h"
#include "stdio.h"
#include "strbuf.h"
#include "time.h"
#include "help.h"
#include "compat/compiler.h"
#include "run-command.h"

static void get_git_remote_https_version_info(struct strbuf *version_info)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	cp.git_cmd = 1;
	argv_array_push(&cp.args, "remote-https");
	argv_array_push(&cp.args, "--build-info");
	if (capture_command(&cp, version_info, 0))
	    strbuf_addstr(version_info, "'git-remote-https --build-info' not supported\n");
}

static void get_system_info(struct strbuf *sys_info)
{
	struct utsname uname_info;

	/* get git version from native cmd */
	strbuf_addstr(sys_info, "git version:\n");
	get_version_info(sys_info, 1);
	strbuf_complete_line(sys_info);

	/* system call for other version info */
	strbuf_addstr(sys_info, "uname -a: ");
	if (uname(&uname_info))
		strbuf_addf(sys_info, "uname() failed with code %d\n", errno);
	else
		strbuf_addf(sys_info, "%s %s %s %s\n",
			    uname_info.sysname,
			    uname_info.release,
			    uname_info.version,
			    uname_info.machine);

	strbuf_addstr(sys_info, "compiler info: ");
	get_compiler_info(sys_info);
	strbuf_complete_line(sys_info);

	strbuf_addstr(sys_info, "git-remote-https --build-info:\n");
	get_git_remote_https_version_info(sys_info);
	strbuf_complete_line(sys_info);
}

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

	strbuf_addstr(template, template_text);
	return 0;
}

static void get_header(struct strbuf *buf, const char *title)
{
	strbuf_addf(buf, "\n\n[%s]\n", title);
}

int cmd_main(int argc, const char **argv)
{
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf report_path = STRBUF_INIT;
	FILE *report;
	time_t now = time(NULL);
	char *option_output = NULL;
	char *option_suffix = "%F-%H%M";
	struct stat statbuf;

	const struct option bugreport_options[] = {
		OPT_STRING('o', "output-directory", &option_output, N_("path"),
			   N_("specify a destination for the bugreport file")),
		OPT_STRING('s', "suffix", &option_suffix, N_("format"),
			   N_("specify a strftime format suffix for the filename")),
		OPT_END()
	};
	argc = parse_options(argc, argv, "", bugreport_options,
			     bugreport_usage, 0);

	if (option_output) {
		strbuf_addstr(&report_path, option_output);
		strbuf_complete(&report_path, '/');
	}


	strbuf_addstr(&report_path, "git-bugreport-");
	strbuf_addftime(&report_path, option_suffix, localtime(&now), 0, 0);
	strbuf_addstr(&report_path, ".txt");

	if (!stat(report_path.buf, &statbuf))
		die("'%s' already exists", report_path.buf);

	switch (safe_create_leading_directories(report_path.buf)) {
	case SCLD_OK:
	case SCLD_EXISTS:
		break;
	default:
		die(_("could not create leading directories for '%s'"),
		    report_path.buf);
	}

	get_bug_template(&buffer);

	get_header(&buffer, "System Info");
	get_system_info(&buffer);

	report = fopen_for_writing(report_path.buf);

	if (report == NULL) {
		strbuf_release(&report_path);
		die("couldn't open '%s' for writing", report_path.buf);
	}

	strbuf_write(&buffer, report);
	fclose(report);

	fprintf(stderr, _("Created new report at '%s'.\n"), report_path.buf);

	UNLEAK(buffer);
	UNLEAK(report_path);
	return !!launch_editor(report_path.buf, NULL, NULL);
}
