/*
 * Copyright (C) 2011 John Szakmeister <john@szakmeister.net>
 *               2012 Philipp A. Hartmann <pah@qo.cx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Credits:
 * - GNOME Keyring API handling originally written by John Szakmeister
 * - ported to credential helper API by Philipp A. Hartmann
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gnome-keyring.h>

#ifdef GNOME_KEYRING_DEFAULT

   /* Modern gnome-keyring */

#include <gnome-keyring-memory.h>

#else

   /*
    * Support ancient gnome-keyring, circ. RHEL 5.X.
    * GNOME_KEYRING_DEFAULT seems to have been introduced with Gnome 2.22,
    * and the other features roughly around Gnome 2.20, 6 months before.
    * Ubuntu 8.04 used Gnome 2.22 (I think).  Not sure any distro used 2.20.
    * So the existence/non-existence of GNOME_KEYRING_DEFAULT seems like
    * a decent thing to use as an indicator.
    */

#define GNOME_KEYRING_DEFAULT NULL

/*
 * ancient gnome-keyring returns DENIED when an entry is not found.
 * Setting NO_MATCH to DENIED will prevent us from reporting DENIED
 * errors during get and erase operations, but we will still report
 * DENIED errors during a store.
 */
#define GNOME_KEYRING_RESULT_NO_MATCH GNOME_KEYRING_RESULT_DENIED

#define gnome_keyring_memory_alloc g_malloc
#define gnome_keyring_memory_free gnome_keyring_free_password
#define gnome_keyring_memory_strdup g_strdup

static const char *gnome_keyring_result_to_message(GnomeKeyringResult result)
{
	switch (result) {
	case GNOME_KEYRING_RESULT_OK:
		return "OK";
	case GNOME_KEYRING_RESULT_DENIED:
		return "Denied";
	case GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON:
		return "No Keyring Daemon";
	case GNOME_KEYRING_RESULT_ALREADY_UNLOCKED:
		return "Already UnLocked";
	case GNOME_KEYRING_RESULT_NO_SUCH_KEYRING:
		return "No Such Keyring";
	case GNOME_KEYRING_RESULT_BAD_ARGUMENTS:
		return "Bad Arguments";
	case GNOME_KEYRING_RESULT_IO_ERROR:
		return "IO Error";
	case GNOME_KEYRING_RESULT_CANCELLED:
		return "Cancelled";
	case GNOME_KEYRING_RESULT_ALREADY_EXISTS:
		return "Already Exists";
	default:
		return "Unknown Error";
	}
}

/*
 * Support really ancient gnome-keyring, circ. RHEL 4.X.
 * Just a guess for the Glib version.  Glib 2.8 was roughly Gnome 2.12 ?
 * Which was released with gnome-keyring 0.4.3 ??
 */
#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 8

static void gnome_keyring_done_cb(GnomeKeyringResult result, gpointer user_data)
{
	gpointer *data = (gpointer *)user_data;
	int *done = (int *)data[0];
	GnomeKeyringResult *r = (GnomeKeyringResult *)data[1];

	*r = result;
	*done = 1;
}

static void wait_for_request_completion(int *done)
{
	GMainContext *mc = g_main_context_default();
	while (!*done)
		g_main_context_iteration(mc, TRUE);
}

static GnomeKeyringResult gnome_keyring_item_delete_sync(const char *keyring, guint32 id)
{
	int done = 0;
	GnomeKeyringResult result;
	gpointer data[] = { &done, &result };

	gnome_keyring_item_delete(keyring, id, gnome_keyring_done_cb, data,
		NULL);

	wait_for_request_completion(&done);

	return result;
}

#endif
#endif

/*
 * This credential struct and API is simplified from git's credential.{h,c}
 */
struct credential {
	char *protocol;
	char *host;
	unsigned short port;
	char *path;
	char *username;
	char *password;
};

#define CREDENTIAL_INIT { NULL, NULL, 0, NULL, NULL, NULL }

typedef int (*credential_op_cb)(struct credential *);

struct credential_operation {
	char *name;
	credential_op_cb op;
};

#define CREDENTIAL_OP_END { NULL, NULL }

/* ----------------- GNOME Keyring functions ----------------- */

/* create a special keyring option string, if path is given */
static char *keyring_object(struct credential *c)
{
	if (!c->path)
		return NULL;

	if (c->port)
		return g_strdup_printf("%s:%hd/%s", c->host, c->port, c->path);

	return g_strdup_printf("%s/%s", c->host, c->path);
}

static int keyring_get(struct credential *c)
{
	char *object = NULL;
	GList *entries;
	GnomeKeyringNetworkPasswordData *password_data;
	GnomeKeyringResult result;

	if (!c->protocol || !(c->host || c->path))
		return EXIT_FAILURE;

	object = keyring_object(c);

	result = gnome_keyring_find_network_password_sync(
				c->username,
				NULL /* domain */,
				c->host,
				object,
				c->protocol,
				NULL /* authtype */,
				c->port,
				&entries);

	g_free(object);

	if (result == GNOME_KEYRING_RESULT_NO_MATCH)
		return EXIT_SUCCESS;

	if (result == GNOME_KEYRING_RESULT_CANCELLED)
		return EXIT_SUCCESS;

	if (result != GNOME_KEYRING_RESULT_OK) {
		g_critical("%s", gnome_keyring_result_to_message(result));
		return EXIT_FAILURE;
	}

	/* pick the first one from the list */
	password_data = (GnomeKeyringNetworkPasswordData *)entries->data;

	gnome_keyring_memory_free(c->password);
	c->password = gnome_keyring_memory_strdup(password_data->password);

	if (!c->username)
		c->username = g_strdup(password_data->user);

	gnome_keyring_network_password_list_free(entries);

	return EXIT_SUCCESS;
}


static int keyring_store(struct credential *c)
{
	guint32 item_id;
	char *object = NULL;
	GnomeKeyringResult result;

	/*
	 * Sanity check that what we are storing is actually sensible.
	 * In particular, we can't make a URL without a protocol field.
	 * Without either a host or pathname (depending on the scheme),
	 * we have no primary key. And without a username and password,
	 * we are not actually storing a credential.
	 */
	if (!c->protocol || !(c->host || c->path) ||
	    !c->username || !c->password)
		return EXIT_FAILURE;

	object = keyring_object(c);

	result = gnome_keyring_set_network_password_sync(
				GNOME_KEYRING_DEFAULT,
				c->username,
				NULL /* domain */,
				c->host,
				object,
				c->protocol,
				NULL /* authtype */,
				c->port,
				c->password,
				&item_id);

	g_free(object);

	if (result != GNOME_KEYRING_RESULT_OK &&
	    result != GNOME_KEYRING_RESULT_CANCELLED) {
		g_critical("%s", gnome_keyring_result_to_message(result));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int keyring_erase(struct credential *c)
{
	char *object = NULL;
	GList *entries;
	GnomeKeyringNetworkPasswordData *password_data;
	GnomeKeyringResult result;

	/*
	 * Sanity check that we actually have something to match
	 * against. The input we get is a restrictive pattern,
	 * so technically a blank credential means "erase everything".
	 * But it is too easy to accidentally send this, since it is equivalent
	 * to empty input. So explicitly disallow it, and require that the
	 * pattern have some actual content to match.
	 */
	if (!c->protocol && !c->host && !c->path && !c->username)
		return EXIT_FAILURE;

	object = keyring_object(c);

	result = gnome_keyring_find_network_password_sync(
				c->username,
				NULL /* domain */,
				c->host,
				object,
				c->protocol,
				NULL /* authtype */,
				c->port,
				&entries);

	g_free(object);

	if (result == GNOME_KEYRING_RESULT_NO_MATCH)
		return EXIT_SUCCESS;

	if (result == GNOME_KEYRING_RESULT_CANCELLED)
		return EXIT_SUCCESS;

	if (result != GNOME_KEYRING_RESULT_OK) {
		g_critical("%s", gnome_keyring_result_to_message(result));
		return EXIT_FAILURE;
	}

	/* pick the first one from the list (delete all matches?) */
	password_data = (GnomeKeyringNetworkPasswordData *)entries->data;

	result = gnome_keyring_item_delete_sync(
		password_data->keyring, password_data->item_id);

	gnome_keyring_network_password_list_free(entries);

	if (result != GNOME_KEYRING_RESULT_OK) {
		g_critical("%s", gnome_keyring_result_to_message(result));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/*
 * Table with helper operation callbacks, used by generic
 * credential helper main function.
 */
static struct credential_operation const credential_helper_ops[] = {
	{ "get",   keyring_get },
	{ "store", keyring_store },
	{ "erase", keyring_erase },
	CREDENTIAL_OP_END
};

/* ------------------ credential functions ------------------ */

static void credential_init(struct credential *c)
{
	memset(c, 0, sizeof(*c));
}

static void credential_clear(struct credential *c)
{
	g_free(c->protocol);
	g_free(c->host);
	g_free(c->path);
	g_free(c->username);
	gnome_keyring_memory_free(c->password);

	credential_init(c);
}

static int credential_read(struct credential *c)
{
	char *buf;
	size_t line_len;
	char *key;
	char *value;

	key = buf = gnome_keyring_memory_alloc(1024);

	while (fgets(buf, 1024, stdin)) {
		line_len = strlen(buf);

		if (line_len && buf[line_len-1] == '\n')
			buf[--line_len] = '\0';

		if (!line_len)
			break;

		value = strchr(buf, '=');
		if (!value) {
			g_warning("invalid credential line: %s", key);
			gnome_keyring_memory_free(buf);
			return -1;
		}
		*value++ = '\0';

		if (!strcmp(key, "protocol")) {
			g_free(c->protocol);
			c->protocol = g_strdup(value);
		} else if (!strcmp(key, "host")) {
			g_free(c->host);
			c->host = g_strdup(value);
			value = strrchr(c->host, ':');
			if (value) {
				*value++ = '\0';
				c->port = atoi(value);
			}
		} else if (!strcmp(key, "path")) {
			g_free(c->path);
			c->path = g_strdup(value);
		} else if (!strcmp(key, "username")) {
			g_free(c->username);
			c->username = g_strdup(value);
		} else if (!strcmp(key, "password")) {
			gnome_keyring_memory_free(c->password);
			c->password = gnome_keyring_memory_strdup(value);
			while (*value)
				*value++ = '\0';
		}
		/*
		 * Ignore other lines; we don't know what they mean, but
		 * this future-proofs us when later versions of git do
		 * learn new lines, and the helpers are updated to match.
		 */
	}

	gnome_keyring_memory_free(buf);

	return 0;
}

static void credential_write_item(FILE *fp, const char *key, const char *value)
{
	if (!value)
		return;
	fprintf(fp, "%s=%s\n", key, value);
}

static void credential_write(const struct credential *c)
{
	/* only write username/password, if set */
	credential_write_item(stdout, "username", c->username);
	credential_write_item(stdout, "password", c->password);
}

static void usage(const char *name)
{
	struct credential_operation const *try_op = credential_helper_ops;
	const char *basename = strrchr(name, '/');

	basename = (basename) ? basename + 1 : name;
	fprintf(stderr, "usage: %s <", basename);
	while (try_op->name) {
		fprintf(stderr, "%s", (try_op++)->name);
		if (try_op->name)
			fprintf(stderr, "%s", "|");
	}
	fprintf(stderr, "%s", ">\n");
}

int main(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;

	struct credential_operation const *try_op = credential_helper_ops;
	struct credential cred = CREDENTIAL_INIT;

	if (!argv[1]) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	g_set_application_name("Git Credential Helper");

	/* lookup operation callback */
	while (try_op->name && strcmp(argv[1], try_op->name))
		try_op++;

	/* unsupported operation given -- ignore silently */
	if (!try_op->name || !try_op->op)
		goto out;

	ret = credential_read(&cred);
	if (ret)
		goto out;

	/* perform credential operation */
	ret = (*try_op->op)(&cred);

	credential_write(&cred);

out:
	credential_clear(&cred);
	return ret;
}
