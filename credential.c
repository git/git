#include "cache.h"
#include "credential.h"
#include "quote.h"
#include "string-list.h"
#include "run-command.h"

static char *credential_ask_one(const char *what, const char *desc)
{
	struct strbuf prompt = STRBUF_INIT;
	char *r;

	if (desc)
		strbuf_addf(&prompt, "%s for '%s': ", what, desc);
	else
		strbuf_addf(&prompt, "%s: ", what);

	/* FIXME: for usernames, we should do something less magical that
	 * actually echoes the characters. However, we need to read from
	 * /dev/tty and not stdio, which is not portable (but getpass will do
	 * it for us). http.c uses the same workaround. */
	r = git_getpass(prompt.buf);

	strbuf_release(&prompt);
	return xstrdup(r);
}

int credential_getpass(struct credential *c)
{

	if (!c->username)
		c->username = credential_ask_one("Username", c->description);
	if (!c->password)
		c->password = credential_ask_one("Password", c->description);
	return 0;
}

static int read_credential_response(struct credential *c, FILE *fp)
{
	struct strbuf response = STRBUF_INIT;

	while (strbuf_getline(&response, fp, '\n') != EOF) {
		char *key = response.buf;
		char *value = strchr(key, '=');

		if (!value) {
			warning("bad output from credential helper: %s", key);
			strbuf_release(&response);
			return -1;
		}
		*value++ = '\0';

		if (!strcmp(key, "username")) {
			free(c->username);
			c->username = xstrdup(value);
		}
		else if (!strcmp(key, "password")) {
			free(c->password);
			c->password = xstrdup(value);
		}
		/* ignore other responses; we don't know what they mean */
	}

	strbuf_release(&response);
	return 0;
}

static int run_credential_helper(struct credential *c, const char *cmd)
{
	struct child_process helper;
	const char *argv[] = { NULL, NULL };
	FILE *fp;
	int r;

	memset(&helper, 0, sizeof(helper));
	argv[0] = cmd;
	helper.argv = argv;
	helper.use_shell = 1;
	helper.no_stdin = 1;
	helper.out = -1;

	if (start_command(&helper))
		return -1;
	fp = xfdopen(helper.out, "r");

	r = read_credential_response(c, fp);

	fclose(fp);
	if (finish_command(&helper))
		r = -1;

	return r;
}

static void add_item(struct strbuf *out, const char *key, const char *value)
{
	if (!value)
		return;
	strbuf_addf(out, " --%s=", key);
	sq_quote_buf(out, value);
}

static int first_word_is_alnum(const char *s)
{
	for (; *s && *s != ' '; s++)
		if (!isalnum(*s))
			return 0;
	return 1;
}

static int credential_do(struct credential *c, const char *method,
			 const char *extra)
{
	struct strbuf cmd = STRBUF_INIT;
	int r;

	if (first_word_is_alnum(method))
		strbuf_addf(&cmd, "git credential-%s", method);
	else
		strbuf_addstr(&cmd, method);

	if (extra)
		strbuf_addf(&cmd, " %s", extra);

	add_item(&cmd, "description", c->description);
	add_item(&cmd, "unique", c->unique);
	add_item(&cmd, "username", c->username);

	r = run_credential_helper(c, cmd.buf);

	strbuf_release(&cmd);
	return r;
}

void credential_fill(struct credential *c, const struct string_list *methods)
{
	struct strbuf err = STRBUF_INIT;

	if (!credential_fill_gently(c, methods))
		return;

	strbuf_addstr(&err, "unable to get credentials");
	if (c->description)
		strbuf_addf(&err, "for '%s'", c->description);
	if (methods && methods->nr == 1)
		strbuf_addf(&err, "; tried '%s'", methods->items[0].string);
	else if (methods) {
		int i;
		strbuf_addstr(&err, "; tried:");
		for (i = 0; i < methods->nr; i++)
			strbuf_addf(&err, "\n  %s", methods->items[i].string);
	}
	die("%s", err.buf);
}

int credential_fill_gently(struct credential *c,
			   const struct string_list *methods)
{
	int i;

	if (c->username && c->password)
		return 0;

	if (!methods || !methods->nr)
		return credential_getpass(c);

	for (i = 0; i < methods->nr; i++) {
		if (!credential_do(c, methods->items[i].string, NULL) &&
		    c->username && c->password)
			return 0;
	}

	return -1;
}

void credential_reject(struct credential *c, const struct string_list *methods)
{
	int i;

	if (methods && c->username) {
		for (i = 0; i < methods->nr; i++) {
			/* ignore errors, there's nothing we can do */
			credential_do(c, methods->items[i].string, "--reject");
		}
	}

	free(c->username);
	c->username = NULL;
	free(c->password);
	c->password = NULL;
}
