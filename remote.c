#include "cache.h"
#include "remote.h"
#include "refs.h"

static struct remote **remotes;
static int allocated_remotes;

#define BUF_SIZE (2048)
static char buffer[BUF_SIZE];

static void add_push_refspec(struct remote *remote, const char *ref)
{
	int nr = remote->push_refspec_nr + 1;
	remote->push_refspec =
		xrealloc(remote->push_refspec, nr * sizeof(char *));
	remote->push_refspec[nr-1] = ref;
	remote->push_refspec_nr = nr;
}

static void add_uri(struct remote *remote, const char *uri)
{
	int nr = remote->uri_nr + 1;
	remote->uri =
		xrealloc(remote->uri, nr * sizeof(char *));
	remote->uri[nr-1] = uri;
	remote->uri_nr = nr;
}

static struct remote *make_remote(const char *name, int len)
{
	int i, empty = -1;

	for (i = 0; i < allocated_remotes; i++) {
		if (!remotes[i]) {
			if (empty < 0)
				empty = i;
		} else {
			if (len ? (!strncmp(name, remotes[i]->name, len) &&
				   !remotes[i]->name[len]) :
			    !strcmp(name, remotes[i]->name))
				return remotes[i];
		}
	}

	if (empty < 0) {
		empty = allocated_remotes;
		allocated_remotes += allocated_remotes ? allocated_remotes : 1;
		remotes = xrealloc(remotes,
				   sizeof(*remotes) * allocated_remotes);
		memset(remotes + empty, 0,
		       (allocated_remotes - empty) * sizeof(*remotes));
	}
	remotes[empty] = xcalloc(1, sizeof(struct remote));
	if (len)
		remotes[empty]->name = xstrndup(name, len);
	else
		remotes[empty]->name = xstrdup(name);
	return remotes[empty];
}

static void read_remotes_file(struct remote *remote)
{
	FILE *f = fopen(git_path("remotes/%s", remote->name), "r");

	if (!f)
		return;
	while (fgets(buffer, BUF_SIZE, f)) {
		int value_list;
		char *s, *p;

		if (!prefixcmp(buffer, "URL:")) {
			value_list = 0;
			s = buffer + 4;
		} else if (!prefixcmp(buffer, "Push:")) {
			value_list = 1;
			s = buffer + 5;
		} else
			continue;

		while (isspace(*s))
			s++;
		if (!*s)
			continue;

		p = s + strlen(s);
		while (isspace(p[-1]))
			*--p = 0;

		switch (value_list) {
		case 0:
			add_uri(remote, xstrdup(s));
			break;
		case 1:
			add_push_refspec(remote, xstrdup(s));
			break;
		}
	}
	fclose(f);
}

static void read_branches_file(struct remote *remote)
{
	const char *slash = strchr(remote->name, '/');
	int n = slash ? slash - remote->name : 1000;
	FILE *f = fopen(git_path("branches/%.*s", n, remote->name), "r");
	char *s, *p;
	int len;

	if (!f)
		return;
	s = fgets(buffer, BUF_SIZE, f);
	fclose(f);
	if (!s)
		return;
	while (isspace(*s))
		s++;
	if (!*s)
		return;
	p = s + strlen(s);
	while (isspace(p[-1]))
		*--p = 0;
	len = p - s;
	if (slash)
		len += strlen(slash);
	p = xmalloc(len + 1);
	strcpy(p, s);
	if (slash)
		strcat(p, slash);
	add_uri(remote, p);
}

static char *default_remote_name = NULL;
static const char *current_branch = NULL;
static int current_branch_len = 0;

static int handle_config(const char *key, const char *value)
{
	const char *name;
	const char *subkey;
	struct remote *remote;
	if (!prefixcmp(key, "branch.") && current_branch &&
	    !strncmp(key + 7, current_branch, current_branch_len) &&
	    !strcmp(key + 7 + current_branch_len, ".remote")) {
		free(default_remote_name);
		default_remote_name = xstrdup(value);
	}
	if (prefixcmp(key,  "remote."))
		return 0;
	name = key + 7;
	subkey = strrchr(name, '.');
	if (!subkey)
		return error("Config with no key for remote %s", name);
	if (*subkey == '/') {
		warning("Config remote shorthand cannot begin with '/': %s", name);
		return 0;
	}
	remote = make_remote(name, subkey - name);
	if (!value) {
		/* if we ever have a boolean variable, e.g. "remote.*.disabled"
		 * [remote "frotz"]
		 *      disabled
		 * is a valid way to set it to true; we get NULL in value so
		 * we need to handle it here.
		 *
		 * if (!strcmp(subkey, ".disabled")) {
		 *      val = git_config_bool(key, value);
		 *      return 0;
		 * } else
		 *
		 */
		return 0; /* ignore unknown booleans */
	}
	if (!strcmp(subkey, ".url")) {
		add_uri(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".push")) {
		add_push_refspec(remote, xstrdup(value));
	} else if (!strcmp(subkey, ".receivepack")) {
		if (!remote->receivepack)
			remote->receivepack = xstrdup(value);
		else
			error("more than one receivepack given, using the first");
	}
	return 0;
}

static void read_config(void)
{
	unsigned char sha1[20];
	const char *head_ref;
	int flag;
	if (default_remote_name) // did this already
		return;
	default_remote_name = xstrdup("origin");
	current_branch = NULL;
	head_ref = resolve_ref("HEAD", sha1, 0, &flag);
	if (head_ref && (flag & REF_ISSYMREF) &&
	    !prefixcmp(head_ref, "refs/heads/")) {
		current_branch = head_ref + strlen("refs/heads/");
		current_branch_len = strlen(current_branch);
	}
	git_config(handle_config);
}

struct remote *remote_get(const char *name)
{
	struct remote *ret;

	read_config();
	if (!name)
		name = default_remote_name;
	ret = make_remote(name, 0);
	if (name[0] != '/') {
		if (!ret->uri)
			read_remotes_file(ret);
		if (!ret->uri)
			read_branches_file(ret);
	}
	if (!ret->uri)
		add_uri(ret, name);
	if (!ret->uri)
		return NULL;
	return ret;
}
