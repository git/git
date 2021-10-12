#include "builtin.h"
#include "parse-options.h"
#include "strbuf.h"
#include "help.h"
#include "compat/compiler.h"
#include "run-command.h"


static void get_system_info(struct strbuf *sys_info)
{
	struct utsname uname_info;
	char *shell = NULL;

	/* get git version from native cmd */
	strbuf_addstr(sys_info, _("git version:\n"));
	get_version_info(sys_info, 1);

	/* system call for other version info */
	strbuf_addstr(sys_info, "uname: ");
	if (uname(&uname_info))
		strbuf_addf(sys_info, _("uname() failed with error '%s' (%d)\n"),
			    strerror(errno),
			    errno);
	else
		strbuf_addf(sys_info, "%s %s %s %s\n",
			    uname_info.sysname,
			    uname_info.release,
			    uname_info.version,
			    uname_info.machine);

	strbuf_addstr(sys_info, _("compiler info: "));
	get_compiler_info(sys_info);

	strbuf_addstr(sys_info, _("libc info: "));
	get_libc_info(sys_info);

	shell = getenv("SHELL");
	strbuf_addf(sys_info, "$SHELL (typically, interactive shell): %s\n",
		    shell ? shell : "<unset>");
}

static void get_populated_hooks(struct strbuf *hook_info, int nongit)
{
	/*
	 * NEEDSWORK: Doesn't look like there is a list of all possible hooks;
	 * so below is a transcription of `git help hooks`. Later, this should
	 * be replaced with some programmatically generated list (generated from
	 * doc or else taken from some library which tells us about all the
	 * hooks)
	 */
	static const char *hook[] = {
		"applypatch-msg",
		"pre-applypatch",
		"post-applypatch",
		"pre-commit",
		"pre-merge-commit",
		"prepare-commit-msg",
		"commit-msg",
		"post-commit",
		"pre-rebase",
		"post-checkout",
		"post-merge",
		"pre-push",
		"pre-receive",
		"update",
		"post-receive",
		"post-update",
		"push-to-checkout",
		"pre-auto-gc",
		"post-rewrite",
		"sendemail-validate",
		"fsmonitor-watchman",
		"p4-pre-submit",
		"post-index-change",
	};
	int i;

	if (nongit) {
		strbuf_addstr(hook_info,
			_("not run from a git repository - no hooks to show\n"));
		return;
	}

	for (i = 0; i < ARRAY_SIZE(hook); i++)
		if (find_hook(hook[i]))
			strbuf_addf(hook_info, "%s\n", hook[i]);
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

	strbuf_addstr(template, _(template_text));
	return 0;
}

static void get_header(struct strbuf *buf, const char *title)
{
	strbuf_addf(buf, "\n\n[%s]\n", title);
}

int cmd_bugreport(int argc, const char **argv, const char *prefix)
{
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf report_path = STRBUF_INIT;
	int report = -1;
	time_t now = time(NULL);
	struct tm tm;
	char *option_output = NULL;
	char *option_suffix = "%Y-%m-%d-%H%M";
	const char *user_relative_path = NULL;
	char *prefixed_filename;

	const struct option bugreport_options[] = {
		OPT_STRING('o', "output-directory", &option_output, N_("path"),
			   N_("specify a destination for the bugreport file")),
		OPT_STRING('s', "suffix", &option_suffix, N_("format"),
			   N_("specify a strftime format suffix for the filename")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, bugreport_options,
			     bugreport_usage, 0);

	/* Prepare the path to put the result */
	prefixed_filename = prefix_filename(prefix,
					    option_output ? option_output : "");
	strbuf_addstr(&report_path, prefixed_filename);
	strbuf_complete(&report_path, '/');

	strbuf_addstr(&report_path, "git-bugreport-");
	strbuf_addftime(&report_path, option_suffix, localtime_r(&now, &tm), 0, 0);
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

	get_header(&buffer, _("System Info"));
	get_system_info(&buffer);

	get_header(&buffer, _("Enabled Hooks"));
	get_populated_hooks(&buffer, !startup_info->have_repository);

	/* fopen doesn't offer us an O_EXCL alternative, except with glibc. */
	report = xopen(report_path.buf, O_CREAT | O_EXCL | O_WRONLY, 0666);

	if (write_in_full(report, buffer.buf, buffer.len) < 0)
		die_errno(_("unable to write to %s"), report_path.buf);

	close(report);

	/*
	 * We want to print the path relative to the user, but we still need the
	 * path relative to us to give to the editor.
	 */
	if (!(prefix && skip_prefix(report_path.buf, prefix, &user_relative_path)))
		user_relative_path = report_path.buf;
	fprintf(stderr, _("Created new report at '%s'.\n"),
		user_relative_path);

	free(prefixed_filename);
	UNLEAK(buffer);
	UNLEAK(report_path);
	return !!launch_editor(report_path.buf, NULL, NULL);
}
