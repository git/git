#include "cache.h"
#include "transport.h"
#include "run-command.h"

static const struct transport_ops rsync_transport;

static int curl_transport_push(struct transport *transport, int refspec_nr, const char **refspec, int flags) {
	const char **argv;
	int argc;
	int err;

	argv = xmalloc((refspec_nr + 11) * sizeof(char *));
	argv[0] = "http-push";
	argc = 1;
	if (flags & TRANSPORT_PUSH_ALL)
		argv[argc++] = "--all";
	if (flags & TRANSPORT_PUSH_FORCE)
		argv[argc++] = "--force";
	argv[argc++] = transport->url;
	while (refspec_nr--)
		argv[argc++] = *refspec++;
	argv[argc] = NULL;
	err = run_command_v_opt(argv, RUN_GIT_CMD);
	switch (err) {
	case -ERR_RUN_COMMAND_FORK:
		error("unable to fork for %s", argv[0]);
	case -ERR_RUN_COMMAND_EXEC:
		error("unable to exec %s", argv[0]);
		break;
	case -ERR_RUN_COMMAND_WAITPID:
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		error("%s died with strange error", argv[0]);
	}
	return !!err;
}

static const struct transport_ops curl_transport = {
	/* set_option */	NULL,
	/* push */		curl_transport_push
};

static const struct transport_ops bundle_transport = {
};

struct git_transport_data {
	unsigned thin : 1;

	const char *receivepack;
};

static int set_git_option(struct transport *connection,
			  const char *name, const char *value)
{
	struct git_transport_data *data = connection->data;
	if (!strcmp(name, TRANS_OPT_RECEIVEPACK)) {
		data->receivepack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_THIN)) {
		data->thin = !!value;
		return 0;
	}
	return 1;
}

static int git_transport_push(struct transport *transport, int refspec_nr, const char **refspec, int flags) {
	struct git_transport_data *data = transport->data;
	const char **argv;
	char *rem;
	int argc;
	int err;

	argv = xmalloc((refspec_nr + 11) * sizeof(char *));
	argv[0] = "send-pack";
	argc = 1;
	if (flags & TRANSPORT_PUSH_ALL)
		argv[argc++] = "--all";
	if (flags & TRANSPORT_PUSH_FORCE)
		argv[argc++] = "--force";
	if (data->receivepack) {
		char *rp = xmalloc(strlen(data->receivepack) + 16);
		sprintf(rp, "--receive-pack=%s", data->receivepack);
		argv[argc++] = rp;
	}
	if (data->thin)
		argv[argc++] = "--thin";
	rem = xmalloc(strlen(transport->remote->name) + 10);
	sprintf(rem, "--remote=%s", transport->remote->name);
	argv[argc++] = rem;
	argv[argc++] = transport->url;
	while (refspec_nr--)
		argv[argc++] = *refspec++;
	argv[argc] = NULL;
	err = run_command_v_opt(argv, RUN_GIT_CMD);
	switch (err) {
	case -ERR_RUN_COMMAND_FORK:
		error("unable to fork for %s", argv[0]);
	case -ERR_RUN_COMMAND_EXEC:
		error("unable to exec %s", argv[0]);
		break;
	case -ERR_RUN_COMMAND_WAITPID:
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		error("%s died with strange error", argv[0]);
	}
	return !!err;
}

static const struct transport_ops git_transport = {
	/* set_option */	set_git_option,
	/* push */		git_transport_push
};

static int is_local(const char *url)
{
	const char *colon = strchr(url, ':');
	const char *slash = strchr(url, '/');
	return !colon || (slash && slash < colon);
}

static int is_file(const char *url)
{
	struct stat buf;
	if (stat(url, &buf))
		return 0;
	return S_ISREG(buf.st_mode);
}

struct transport *transport_get(struct remote *remote, const char *url,
				int fetch)
{
	struct transport *ret = NULL;
	if (!prefixcmp(url, "rsync://")) {
		ret = xmalloc(sizeof(*ret));
		ret->data = NULL;
		ret->ops = &rsync_transport;
	} else if (!prefixcmp(url, "http://") || !prefixcmp(url, "https://") ||
		   !prefixcmp(url, "ftp://")) {
		ret = xmalloc(sizeof(*ret));
		ret->ops = &curl_transport;
		ret->data = NULL;
	} else if (is_local(url) && is_file(url)) {
		ret = xmalloc(sizeof(*ret));
		ret->data = NULL;
		ret->ops = &bundle_transport;
	} else {
		struct git_transport_data *data = xcalloc(1, sizeof(*data));
		ret = xcalloc(1, sizeof(*ret));
		ret->data = data;
		data->thin = 1;
		data->receivepack = "git-receive-pack";
		if (remote && remote->receivepack)
			data->receivepack = remote->receivepack;
		ret->ops = &git_transport;
	}
	if (ret) {
		ret->remote = remote;
		ret->url = url;
		ret->fetch = !!fetch;
	}
	return ret;
}

int transport_set_option(struct transport *transport,
			 const char *name, const char *value)
{
	int ret = 1;
	if (transport->ops->set_option)
		ret = transport->ops->set_option(transport, name, value);
	if (ret < 0)
		fprintf(stderr, "For '%s' option %s cannot be set to '%s'\n",
			transport->url, name, value);
	if (ret > 0)
		fprintf(stderr, "For '%s' option %s is ignored\n",
			transport->url, name);
	return ret;
}

int transport_push(struct transport *transport,
		   int refspec_nr, const char **refspec, int flags)
{
	if (!transport->ops->push)
		return 1;
	return transport->ops->push(transport, refspec_nr, refspec, flags);
}

int transport_disconnect(struct transport *transport)
{
	int ret = 0;
	if (transport->ops->disconnect)
		ret = transport->ops->disconnect(transport);
	free(transport);
	return ret;
}
