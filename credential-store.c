#include "cache.h"
#include "lockfile.h"
#include "credential.h"
#include "string-list.h"
#include "parse-options.h"

static struct lock_file credential_lock;

static int parse_credential_file(const char *fn,
				  struct credential *c,
				  void (*match_cb)(struct credential *),
				  void (*other_cb)(struct strbuf *))
{
	FILE *fh;
	struct strbuf line = STRBUF_INIT;
	struct credential entry = CREDENTIAL_INIT;
	int found_credential = 0;

	fh = fopen(fn, "r");
	if (!fh) {
		if (errno != ENOENT && errno != EACCES)
			die_errno("unable to open %s", fn);
		return found_credential;
	}

	while (strbuf_getline(&line, fh, '\n') != EOF) {
		credential_from_url(&entry, line.buf);
		if (entry.username && entry.password &&
		    credential_match(c, &entry)) {
			found_credential = 1;
			if (match_cb) {
				match_cb(&entry);
				break;
			}
		}
		else if (other_cb)
			other_cb(&line);
	}

	credential_clear(&entry);
	strbuf_release(&line);
	fclose(fh);
	return found_credential;
}

static void print_entry(struct credential *c)
{
	printf("username=%s\n", c->username);
	printf("password=%s\n", c->password);
}

static void print_line(struct strbuf *buf)
{
	strbuf_addch(buf, '\n');
	write_or_die(get_lock_file_fd(&credential_lock), buf->buf, buf->len);
}

static void rewrite_credential_file(const char *fn, struct credential *c,
				    struct strbuf *extra)
{
	if (hold_lock_file_for_update(&credential_lock, fn, 0) < 0)
		die_errno("unable to get credential storage lock");
	if (extra)
		print_line(extra);
	parse_credential_file(fn, c, NULL, print_line);
	if (commit_lock_file(&credential_lock) < 0)
		die_errno("unable to commit credential store");
}

static void store_credential_file(const char *fn, struct credential *c)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_addf(&buf, "%s://", c->protocol);
	strbuf_addstr_urlencode(&buf, c->username, 1);
	strbuf_addch(&buf, ':');
	strbuf_addstr_urlencode(&buf, c->password, 1);
	strbuf_addch(&buf, '@');
	if (c->host)
		strbuf_addstr_urlencode(&buf, c->host, 1);
	if (c->path) {
		strbuf_addch(&buf, '/');
		strbuf_addstr_urlencode(&buf, c->path, 0);
	}

	rewrite_credential_file(fn, c, &buf);
	strbuf_release(&buf);
}

static void store_credential(const struct string_list *fns, struct credential *c)
{
	struct string_list_item *fn;

	/*
	 * Sanity check that what we are storing is actually sensible.
	 * In particular, we can't make a URL without a protocol field.
	 * Without either a host or pathname (depending on the scheme),
	 * we have no primary key. And without a username and password,
	 * we are not actually storing a credential.
	 */
	if (!c->protocol || !(c->host || c->path) || !c->username || !c->password)
		return;

	for_each_string_list_item(fn, fns)
		if (!access(fn->string, F_OK)) {
			store_credential_file(fn->string, c);
			return;
		}
	/*
	 * Write credential to the filename specified by fns->items[0], thus
	 * creating it
	 */
	if (fns->nr)
		store_credential_file(fns->items[0].string, c);
}

static void remove_credential(const struct string_list *fns, struct credential *c)
{
	struct string_list_item *fn;

	/*
	 * Sanity check that we actually have something to match
	 * against. The input we get is a restrictive pattern,
	 * so technically a blank credential means "erase everything".
	 * But it is too easy to accidentally send this, since it is equivalent
	 * to empty input. So explicitly disallow it, and require that the
	 * pattern have some actual content to match.
	 */
	if (!c->protocol && !c->host && !c->path && !c->username)
		return;
	for_each_string_list_item(fn, fns)
		if (!access(fn->string, F_OK))
			rewrite_credential_file(fn->string, c, NULL);
}

static void lookup_credential(const struct string_list *fns, struct credential *c)
{
	struct string_list_item *fn;

	for_each_string_list_item(fn, fns)
		if (parse_credential_file(fn->string, c, print_entry, NULL))
			return; /* Found credential */
}

int main(int argc, char **argv)
{
	const char * const usage[] = {
		"git credential-store [<options>] <action>",
		NULL
	};
	const char *op;
	struct credential c = CREDENTIAL_INIT;
	struct string_list fns = STRING_LIST_INIT_DUP;
	char *file = NULL;
	struct option options[] = {
		OPT_STRING(0, "file", &file, "path",
			   "fetch and store credentials in <path>"),
		OPT_END()
	};

	umask(077);

	argc = parse_options(argc, (const char **)argv, NULL, options, usage, 0);
	if (argc != 1)
		usage_with_options(usage, options);
	op = argv[0];

	if (file) {
		string_list_append(&fns, file);
	} else {
		if ((file = expand_user_path("~/.git-credentials")))
			string_list_append_nodup(&fns, file);
		file = xdg_config_home("credentials");
		if (file)
			string_list_append_nodup(&fns, file);
	}
	if (!fns.nr)
		die("unable to set up default path; use --file");

	if (credential_read(&c, stdin) < 0)
		die("unable to read credential");

	if (!strcmp(op, "get"))
		lookup_credential(&fns, &c);
	else if (!strcmp(op, "erase"))
		remove_credential(&fns, &c);
	else if (!strcmp(op, "store"))
		store_credential(&fns, &c);
	else
		; /* Ignore unknown operation. */

	string_list_clear(&fns, 0);
	return 0;
}
