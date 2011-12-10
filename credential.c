#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "run-command.h"

void credential_init(struct credential *c)
{
	memset(c, 0, sizeof(*c));
	c->helpers.strdup_strings = 1;
}

void credential_clear(struct credential *c)
{
	free(c->protocol);
	free(c->host);
	free(c->path);
	free(c->username);
	free(c->password);
	string_list_clear(&c->helpers, 0);

	credential_init(c);
}

static void credential_describe(struct credential *c, struct strbuf *out)
{
	if (!c->protocol)
		return;
	strbuf_addf(out, "%s://", c->protocol);
	if (c->username && *c->username)
		strbuf_addf(out, "%s@", c->username);
	if (c->host)
		strbuf_addstr(out, c->host);
	if (c->path)
		strbuf_addf(out, "/%s", c->path);
}

static char *credential_ask_one(const char *what, struct credential *c)
{
	struct strbuf desc = STRBUF_INIT;
	struct strbuf prompt = STRBUF_INIT;
	char *r;

	credential_describe(c, &desc);
	if (desc.len)
		strbuf_addf(&prompt, "%s for '%s': ", what, desc.buf);
	else
		strbuf_addf(&prompt, "%s: ", what);

	/* FIXME: for usernames, we should do something less magical that
	 * actually echoes the characters. However, we need to read from
	 * /dev/tty and not stdio, which is not portable (but getpass will do
	 * it for us). http.c uses the same workaround. */
	r = git_getpass(prompt.buf);

	strbuf_release(&desc);
	strbuf_release(&prompt);
	return xstrdup(r);
}

static void credential_getpass(struct credential *c)
{
	if (!c->username)
		c->username = credential_ask_one("Username", c);
	if (!c->password)
		c->password = credential_ask_one("Password", c);
}

int credential_read(struct credential *c, FILE *fp)
{
	struct strbuf line = STRBUF_INIT;

	while (strbuf_getline(&line, fp, '\n') != EOF) {
		char *key = line.buf;
		char *value = strchr(key, '=');

		if (!line.len)
			break;

		if (!value) {
			warning("invalid credential line: %s", key);
			strbuf_release(&line);
			return -1;
		}
		*value++ = '\0';

		if (!strcmp(key, "username")) {
			free(c->username);
			c->username = xstrdup(value);
		} else if (!strcmp(key, "password")) {
			free(c->password);
			c->password = xstrdup(value);
		} else if (!strcmp(key, "protocol")) {
			free(c->protocol);
			c->protocol = xstrdup(value);
		} else if (!strcmp(key, "host")) {
			free(c->host);
			c->host = xstrdup(value);
		} else if (!strcmp(key, "path")) {
			free(c->path);
			c->path = xstrdup(value);
		}
		/*
		 * Ignore other lines; we don't know what they mean, but
		 * this future-proofs us when later versions of git do
		 * learn new lines, and the helpers are updated to match.
		 */
	}

	strbuf_release(&line);
	return 0;
}

static void credential_write_item(FILE *fp, const char *key, const char *value)
{
	if (!value)
		return;
	fprintf(fp, "%s=%s\n", key, value);
}

static void credential_write(const struct credential *c, FILE *fp)
{
	credential_write_item(fp, "protocol", c->protocol);
	credential_write_item(fp, "host", c->host);
	credential_write_item(fp, "path", c->path);
	credential_write_item(fp, "username", c->username);
	credential_write_item(fp, "password", c->password);
}

static int run_credential_helper(struct credential *c,
				 const char *cmd,
				 int want_output)
{
	struct child_process helper;
	const char *argv[] = { NULL, NULL };
	FILE *fp;

	memset(&helper, 0, sizeof(helper));
	argv[0] = cmd;
	helper.argv = argv;
	helper.use_shell = 1;
	helper.in = -1;
	if (want_output)
		helper.out = -1;
	else
		helper.no_stdout = 1;

	if (start_command(&helper) < 0)
		return -1;

	fp = xfdopen(helper.in, "w");
	credential_write(c, fp);
	fclose(fp);

	if (want_output) {
		int r;
		fp = xfdopen(helper.out, "r");
		r = credential_read(c, fp);
		fclose(fp);
		if (r < 0) {
			finish_command(&helper);
			return -1;
		}
	}

	if (finish_command(&helper))
		return -1;
	return 0;
}

static int credential_do(struct credential *c, const char *helper,
			 const char *operation)
{
	struct strbuf cmd = STRBUF_INIT;
	int r;

	if (helper[0] == '!')
		strbuf_addstr(&cmd, helper + 1);
	else if (is_absolute_path(helper))
		strbuf_addstr(&cmd, helper);
	else
		strbuf_addf(&cmd, "git credential-%s", helper);

	strbuf_addf(&cmd, " %s", operation);
	r = run_credential_helper(c, cmd.buf, !strcmp(operation, "get"));

	strbuf_release(&cmd);
	return r;
}

void credential_fill(struct credential *c)
{
	int i;

	if (c->username && c->password)
		return;

	for (i = 0; i < c->helpers.nr; i++) {
		credential_do(c, c->helpers.items[i].string, "get");
		if (c->username && c->password)
			return;
	}

	credential_getpass(c);
	if (!c->username && !c->password)
		die("unable to get password from user");
}

void credential_approve(struct credential *c)
{
	int i;

	if (c->approved)
		return;
	if (!c->username || !c->password)
		return;

	for (i = 0; i < c->helpers.nr; i++)
		credential_do(c, c->helpers.items[i].string, "store");
	c->approved = 1;
}

void credential_reject(struct credential *c)
{
	int i;

	for (i = 0; i < c->helpers.nr; i++)
		credential_do(c, c->helpers.items[i].string, "erase");

	free(c->username);
	c->username = NULL;
	free(c->password);
	c->password = NULL;
	c->approved = 0;
}
