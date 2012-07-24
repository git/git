#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "run-command.h"
#include "url.h"
#include "prompt.h"

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

int credential_match(const struct credential *want,
		     const struct credential *have)
{
#define CHECK(x) (!want->x || (have->x && !strcmp(want->x, have->x)))
	return CHECK(protocol) &&
	       CHECK(host) &&
	       CHECK(path) &&
	       CHECK(username);
#undef CHECK
}

static int credential_config_callback(const char *var, const char *value,
				      void *data)
{
	struct credential *c = data;
	const char *key, *dot;

	key = skip_prefix(var, "credential.");
	if (!key)
		return 0;

	if (!value)
		return config_error_nonbool(var);

	dot = strrchr(key, '.');
	if (dot) {
		struct credential want = CREDENTIAL_INIT;
		char *url = xmemdupz(key, dot - key);
		int matched;

		credential_from_url(&want, url);
		matched = credential_match(&want, c);

		credential_clear(&want);
		free(url);

		if (!matched)
			return 0;
		key = dot + 1;
	}

	if (!strcmp(key, "helper"))
		string_list_append(&c->helpers, value);
	else if (!strcmp(key, "username")) {
		if (!c->username)
			c->username = xstrdup(value);
	}
	else if (!strcmp(key, "usehttppath"))
		c->use_http_path = git_config_bool(var, value);

	return 0;
}

static int proto_is_http(const char *s)
{
	if (!s)
		return 0;
	return !strcmp(s, "https") || !strcmp(s, "http");
}

static void credential_apply_config(struct credential *c)
{
	if (c->configured)
		return;
	git_config(credential_config_callback, c);
	c->configured = 1;

	if (!c->use_http_path && proto_is_http(c->protocol)) {
		free(c->path);
		c->path = NULL;
	}
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

static char *credential_ask_one(const char *what, struct credential *c,
				int flags)
{
	struct strbuf desc = STRBUF_INIT;
	struct strbuf prompt = STRBUF_INIT;
	char *r;

	credential_describe(c, &desc);
	if (desc.len)
		strbuf_addf(&prompt, "%s for '%s': ", what, desc.buf);
	else
		strbuf_addf(&prompt, "%s: ", what);

	r = git_prompt(prompt.buf, flags);

	strbuf_release(&desc);
	strbuf_release(&prompt);
	return xstrdup(r);
}

static void credential_getpass(struct credential *c)
{
	if (!c->username)
		c->username = credential_ask_one("Username", c,
						 PROMPT_ASKPASS|PROMPT_ECHO);
	if (!c->password)
		c->password = credential_ask_one("Password", c,
						 PROMPT_ASKPASS);
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
		} else if (!strcmp(key, "url")) {
			credential_from_url(c, value);
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

void credential_write(const struct credential *c, FILE *fp)
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

	credential_apply_config(c);

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

	credential_apply_config(c);

	for (i = 0; i < c->helpers.nr; i++)
		credential_do(c, c->helpers.items[i].string, "store");
	c->approved = 1;
}

void credential_reject(struct credential *c)
{
	int i;

	credential_apply_config(c);

	for (i = 0; i < c->helpers.nr; i++)
		credential_do(c, c->helpers.items[i].string, "erase");

	free(c->username);
	c->username = NULL;
	free(c->password);
	c->password = NULL;
	c->approved = 0;
}

void credential_from_url(struct credential *c, const char *url)
{
	const char *at, *colon, *cp, *slash, *host, *proto_end;

	credential_clear(c);

	/*
	 * Match one of:
	 *   (1) proto://<host>/...
	 *   (2) proto://<user>@<host>/...
	 *   (3) proto://<user>:<pass>@<host>/...
	 */
	proto_end = strstr(url, "://");
	if (!proto_end)
		return;
	cp = proto_end + 3;
	at = strchr(cp, '@');
	colon = strchr(cp, ':');
	slash = strchrnul(cp, '/');

	if (!at || slash <= at) {
		/* Case (1) */
		host = cp;
	}
	else if (!colon || at <= colon) {
		/* Case (2) */
		c->username = url_decode_mem(cp, at - cp);
		host = at + 1;
	} else {
		/* Case (3) */
		c->username = url_decode_mem(cp, colon - cp);
		c->password = url_decode_mem(colon + 1, at - (colon + 1));
		host = at + 1;
	}

	if (proto_end - url > 0)
		c->protocol = xmemdupz(url, proto_end - url);
	if (slash - host > 0)
		c->host = url_decode_mem(host, slash - host);
	/* Trim leading and trailing slashes from path */
	while (*slash == '/')
		slash++;
	if (*slash) {
		char *p;
		c->path = url_decode(slash);
		p = c->path + strlen(c->path) - 1;
		while (p > c->path && *p == '/')
			*p-- = '\0';
	}
}
