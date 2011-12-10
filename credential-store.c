#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "parse-options.h"

static struct lock_file credential_lock;

static void parse_credential_file(const char *fn,
				  struct credential *c,
				  void (*match_cb)(struct credential *),
				  void (*other_cb)(struct strbuf *))
{
	FILE *fh;
	struct strbuf line = STRBUF_INIT;
	struct credential entry = CREDENTIAL_INIT;

	fh = fopen(fn, "r");
	if (!fh) {
		if (errno != ENOENT)
			die_errno("unable to open %s", fn);
		return;
	}

	while (strbuf_getline(&line, fh, '\n') != EOF) {
		credential_from_url(&entry, line.buf);
		if (entry.username && entry.password &&
		    credential_match(c, &entry)) {
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
}

static void print_entry(struct credential *c)
{
	printf("username=%s\n", c->username);
	printf("password=%s\n", c->password);
}

static void print_line(struct strbuf *buf)
{
	strbuf_addch(buf, '\n');
	write_or_die(credential_lock.fd, buf->buf, buf->len);
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

static void store_credential(const char *fn, struct credential *c)
{
	struct strbuf buf = STRBUF_INIT;

	/*
	 * Sanity check that what we are storing is actually sensible.
	 * In particular, we can't make a URL without a protocol field.
	 * Without either a host or pathname (depending on the scheme),
	 * we have no primary key. And without a username and password,
	 * we are not actually storing a credential.
	 */
	if (!c->protocol || !(c->host || c->path) ||
	    !c->username || !c->password)
		return;

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

static void remove_credential(const char *fn, struct credential *c)
{
	/*
	 * Sanity check that we actually have something to match
	 * against. The input we get is a restrictive pattern,
	 * so technically a blank credential means "erase everything".
	 * But it is too easy to accidentally send this, since it is equivalent
	 * to empty input. So explicitly disallow it, and require that the
	 * pattern have some actual content to match.
	 */
	if (c->protocol || c->host || c->path || c->username)
		rewrite_credential_file(fn, c, NULL);
}

static int lookup_credential(const char *fn, struct credential *c)
{
	parse_credential_file(fn, c, print_entry, NULL);
	return c->username && c->password;
}

int main(int argc, const char **argv)
{
	const char * const usage[] = {
		"git credential-store [options] <action>",
		NULL
	};
	const char *op;
	struct credential c = CREDENTIAL_INIT;
	char *file = NULL;
	struct option options[] = {
		OPT_STRING(0, "file", &file, "path",
			   "fetch and store credentials in <path>"),
		OPT_END()
	};

	umask(077);

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc != 1)
		usage_with_options(usage, options);
	op = argv[0];

	if (!file)
		file = expand_user_path("~/.git-credentials");
	if (!file)
		die("unable to set up default path; use --file");

	if (credential_read(&c, stdin) < 0)
		die("unable to read credential");

	if (!strcmp(op, "get"))
		lookup_credential(file, &c);
	else if (!strcmp(op, "erase"))
		remove_credential(file, &c);
	else if (!strcmp(op, "store"))
		store_credential(file, &c);
	else
		; /* Ignore unknown operation. */

	return 0;
}
