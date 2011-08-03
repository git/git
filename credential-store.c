#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "parse-options.h"

static int lookup_credential(const char *fn, struct credential *c)
{
	config_exclusive_filename = fn;
	credential_from_config(c);
	return c->username && c->password;
}

static void store_item(const char *fn, const char *unique,
		       const char *item, const char *value)
{
	struct strbuf key = STRBUF_INIT;

	if (!unique)
		return;

	config_exclusive_filename = fn;
	umask(077);

	strbuf_addf(&key, "credential.%s.%s", unique, item);
	git_config_set(key.buf, value);
	strbuf_release(&key);
}

static void store_credential(const char *fn, struct credential *c)
{
	store_item(fn, c->unique, "username", c->username);
	store_item(fn, c->unique, "password", c->password);
}

static void remove_credential(const char *fn, struct credential *c)
{
	store_item(fn, c->unique, "username", NULL);
	store_item(fn, c->unique, "password", NULL);
}

int main(int argc, const char **argv)
{
	const char * const usage[] = {
		"git credential-store [options]",
		NULL
	};
	struct credential c = { NULL };
	struct string_list chain = STRING_LIST_INIT_NODUP;
	char *store = NULL;
	int reject = 0;
	struct option options[] = {
		OPT_STRING_LIST(0, "store", &store, "file",
				"fetch and store credentials in <file>"),
		OPT_STRING_LIST(0, "chain", &chain, "helper",
				"use <helper> to get non-cached credentials"),
		OPT_BOOLEAN(0, "reject", &reject,
			    "reject a stored credential"),
		OPT_STRING(0, "username", &c.username, "name",
			   "an existing username"),
		OPT_STRING(0, "description", &c.description, "desc",
			   "human-readable description of the credential"),
		OPT_STRING(0, "unique", &c.unique, "token",
			   "a unique context for the credential"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc)
		usage_with_options(usage, options);

	if (!store)
		store = expand_user_path("~/.git-credentials");
	if (!store)
		die("unable to set up default store; use --store");

	if (reject)
		remove_credential(store, &c);
	else {
		if (!lookup_credential(store, &c)) {
			credential_fill(&c, &chain);
			store_credential(store, &c);
		}
		printf("username=%s\n", c.username);
		printf("password=%s\n", c.password);
	}
	return 0;
}
