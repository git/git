#include "builtin.h"
#include "config.h"
#include "mailmap.h"
#include "parse-options.h"
#include "string-list.h"

static int use_stdin;
static const char * const check_mailmap_usage[] = {
N_("git check-mailmap [<options>] <contact>..."),
NULL
};

static const struct option check_mailmap_options[] = {
	OPT_BOOL(0, "stdin", &use_stdin, N_("also read contacts from stdin")),
	OPT_END()
};

static void check_mailmap(struct string_list *mailmap, const char *contact)
{
	const char *name, *mail;
	size_t namelen, maillen;
	struct ident_split ident;

	if (split_ident_line(&ident, contact, strlen(contact)))
		die(_("unable to parse contact: %s"), contact);

	name = ident.name_begin;
	namelen = ident.name_end - ident.name_begin;
	mail = ident.mail_begin;
	maillen = ident.mail_end - ident.mail_begin;

	map_user(mailmap, &mail, &maillen, &name, &namelen);

	if (namelen)
		printf("%.*s ", (int)namelen, name);
	printf("<%.*s>\n", (int)maillen, mail);
}

int cmd_check_mailmap(int argc, const char **argv, const char *prefix)
{
	int i;
	struct string_list mailmap = STRING_LIST_INIT_NODUP;

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, check_mailmap_options,
			     check_mailmap_usage, 0);
	if (argc == 0 && !use_stdin)
		die(_("no contacts specified"));

	read_mailmap(&mailmap);

	for (i = 0; i < argc; ++i)
		check_mailmap(&mailmap, argv[i]);
	maybe_flush_or_die(stdout, "stdout");

	if (use_stdin) {
		struct strbuf buf = STRBUF_INIT;
		while (strbuf_getline_lf(&buf, stdin) != EOF) {
			check_mailmap(&mailmap, buf.buf);
			maybe_flush_or_die(stdout, "stdout");
		}
		strbuf_release(&buf);
	}

	clear_mailmap(&mailmap);
	return 0;
}
