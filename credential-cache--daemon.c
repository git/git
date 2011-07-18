#include "cache.h"
#include "credential.h"
#include "unix-socket.h"

struct credential_cache_entry {
	struct credential item;
	unsigned long expiration;
};
static struct credential_cache_entry *entries;
static int entries_nr;
static int entries_alloc;

static void cache_credential(const struct credential *c, int timeout)
{
	struct credential_cache_entry *e;

	ALLOC_GROW(entries, entries_nr + 1, entries_alloc);
	e = &entries[entries_nr++];

	memcpy(&e->item, c, sizeof(*c));
	e->expiration = time(NULL) + timeout;
}

static struct credential_cache_entry *lookup_credential(const struct credential *c)
{
	int i;
	for (i = 0; i < entries_nr; i++) {
		struct credential *e = &entries[i].item;

		/* We must either both have the same unique token,
		 * or we must not be using unique tokens at all. */
		if (e->unique) {
			if (!c->unique || strcmp(e->unique, c->unique))
				continue;
		}
		else if (c->unique)
			continue;

		/* If we have a username, it must match. Otherwise,
		 * we will fill in the username. */
		if (c->username && strcmp(e->username, c->username))
			continue;

		return &entries[i];
	}
	return NULL;
}

static void remove_credential(const struct credential *c)
{
	struct credential_cache_entry *e;

	e = lookup_credential(c);
	if (e)
		e->expiration = 0;
}

static int check_expirations(void)
{
	int i = 0;
	unsigned long now = time(NULL);
	unsigned long next = (unsigned long)-1;

	while (i < entries_nr) {
		if (entries[i].expiration <= now) {
			entries_nr--;
			if (!entries_nr)
				return 0;
			free(entries[i].item.description);
			free(entries[i].item.unique);
			free(entries[i].item.username);
			free(entries[i].item.password);
			memcpy(&entries[i], &entries[entries_nr], sizeof(*entries));
		}
		else {
			if (entries[i].expiration < next)
				next = entries[i].expiration;
			i++;
		}
	}

	return next - now;
}

static int read_credential_request(FILE *fh, struct credential *c,
				   char **action, int *timeout) {
	struct strbuf item = STRBUF_INIT;

	while (strbuf_getline(&item, fh, '\0') != EOF) {
		char *key = item.buf;
		char *value = strchr(key, '=');

		if (!value) {
			warning("cache client sent bogus input: %s", key);
			strbuf_release(&item);
			return -1;
		}
		*value++ = '\0';

		if (!strcmp(key, "action"))
			*action = xstrdup(value);
		else if (!strcmp(key, "unique"))
			c->unique = xstrdup(value);
		else if (!strcmp(key, "username"))
			c->username = xstrdup(value);
		else if (!strcmp(key, "password"))
			c->password = xstrdup(value);
		else if (!strcmp(key, "timeout"))
			*timeout = atoi(value);
		else {
			warning("cache client sent bogus key: %s", key);
			strbuf_release(&item);
			return -1;
		}
	}
	strbuf_release(&item);
	return 0;
}

static void serve_one_client(FILE *in, FILE *out)
{
	struct credential c = { NULL };
	int timeout = -1;
	char *action = NULL;

	if (read_credential_request(in, &c, &action, &timeout) < 0)
		return;

	if (!action) {
		warning("cache client didn't specify an action");
		return;
	}

	if (!strcmp(action, "exit"))
		exit(0);

	if (!strcmp(action, "get")) {
		struct credential_cache_entry *e = lookup_credential(&c);
		if (e) {
			fprintf(out, "username=%s\n", e->item.username);
			fprintf(out, "password=%s\n", e->item.password);
		}
		return;
	}

	if (!strcmp(action, "erase")) {
		remove_credential(&c);
		return;
	}

	if (!strcmp(action, "store")) {
		if (timeout < 0) {
			warning("cache client didn't specify a timeout");
			return;
		}

		remove_credential(&c);
		cache_credential(&c, timeout);
		return;
	}

	warning("cache client sent unknown action: %s", action);
	return;
}

static int serve_cache_loop(int fd)
{
	struct pollfd pfd;
	unsigned long wakeup;

	wakeup = check_expirations();
	if (!wakeup)
		return 0;

	pfd.fd = fd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 1000 * wakeup) < 0) {
		if (errno != EINTR)
			die_errno("poll failed");
		return 1;
	}

	if (pfd.revents & POLLIN) {
		int client, client2;
		FILE *in, *out;

		client = accept(fd, NULL, NULL);
		if (client < 0) {
			warning("accept failed: %s", strerror(errno));
			return 1;
		}
		client2 = dup(client);
		if (client2 < 0) {
			warning("dup failed: %s", strerror(errno));
			close(client);
			return 1;
		}

		in = xfdopen(client, "r");
		out = xfdopen(client2, "w");
		serve_one_client(in, out);
		fclose(in);
		fclose(out);
	}
	return 1;
}

static void serve_cache(const char *socket_path)
{
	int fd;

	fd = unix_stream_listen(socket_path);
	if (fd < 0)
		die_errno("unable to bind to '%s'", socket_path);

	printf("ok\n");
	fclose(stdout);

	while (serve_cache_loop(fd))
		; /* nothing */

	close(fd);
	unlink(socket_path);
}

static const char permissions_advice[] =
"The permissions on your socket directory are too loose; other\n"
"users may be able to read your cached credentials. Consider running:\n"
"\n"
"	chmod 0700 %s";
static void check_socket_directory(const char *path)
{
	struct stat st;
	char *path_copy = xstrdup(path);
	char *dir = dirname(path_copy);

	if (!stat(dir, &st)) {
		if (st.st_mode & 077)
			die(permissions_advice, dir);
		free(path_copy);
		return;
	}

	/*
	 * We must be sure to create the directory with the correct mode,
	 * not just chmod it after the fact; otherwise, there is a race
	 * condition in which somebody can chdir to it, sleep, then try to open
	 * our protected socket.
	 */
	if (safe_create_leading_directories_const(dir) < 0)
		die_errno("unable to create directories for '%s'", dir);
	if (mkdir(dir, 0700) < 0)
		die_errno("unable to mkdir '%s'", dir);
	free(path_copy);
}

int main(int argc, const char **argv)
{
	const char *socket_path = argv[1];

	if (!socket_path)
		die("usage: git-credential-cache--daemon <socket_path>");
	check_socket_directory(socket_path);

	serve_cache(socket_path);

	return 0;
}
