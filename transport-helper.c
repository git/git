#include "cache.h"
#include "transport.h"
#include "quote.h"
#include "run-command.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "remote.h"
#include "string-list.h"
#include "thread-utils.h"
#include "sigchain.h"
#include "argv-array.h"
#include "refs.h"
#include "refspec.h"
#include "transport-internal.h"
#include "protocol.h"

static int debug;

struct helper_data {
	const char *name;
	struct child_process *helper;
	FILE *out;
	unsigned fetch : 1,
		import : 1,
		bidi_import : 1,
		export : 1,
		option : 1,
		push : 1,
		connect : 1,
		stateless_connect : 1,
		signed_tags : 1,
		check_connectivity : 1,
		no_disconnect_req : 1,
		no_private_update : 1;
	char *export_marks;
	char *import_marks;
	/* These go from remote name (as in "list") to private name */
	struct refspec rs;
	/* Transport options for fetch-pack/send-pack (should one of
	 * those be invoked).
	 */
	struct git_transport_options transport_options;
};

static void sendline(struct helper_data *helper, struct strbuf *buffer)
{
	if (debug)
		fprintf(stderr, "Debug: Remote helper: -> %s", buffer->buf);
	if (write_in_full(helper->helper->in, buffer->buf, buffer->len) < 0)
		die_errno(_("full write to remote helper failed"));
}

static int recvline_fh(FILE *helper, struct strbuf *buffer)
{
	strbuf_reset(buffer);
	if (debug)
		fprintf(stderr, "Debug: Remote helper: Waiting...\n");
	if (strbuf_getline(buffer, helper) == EOF) {
		if (debug)
			fprintf(stderr, "Debug: Remote helper quit.\n");
		return 1;
	}

	if (debug)
		fprintf(stderr, "Debug: Remote helper: <- %s\n", buffer->buf);
	return 0;
}

static int recvline(struct helper_data *helper, struct strbuf *buffer)
{
	return recvline_fh(helper->out, buffer);
}

static void write_constant(int fd, const char *str)
{
	if (debug)
		fprintf(stderr, "Debug: Remote helper: -> %s", str);
	if (write_in_full(fd, str, strlen(str)) < 0)
		die_errno(_("full write to remote helper failed"));
}

static const char *remove_ext_force(const char *url)
{
	if (url) {
		const char *colon = strchr(url, ':');
		if (colon && colon[1] == ':')
			return colon + 2;
	}
	return url;
}

static void do_take_over(struct transport *transport)
{
	struct helper_data *data;
	data = (struct helper_data *)transport->data;
	transport_take_over(transport, data->helper);
	fclose(data->out);
	free(data);
}

static void standard_options(struct transport *t);

static struct child_process *get_helper(struct transport *transport)
{
	struct helper_data *data = transport->data;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf version_advert = STRBUF_INIT;
	struct child_process *helper;
	int duped;
	int code;

	if (data->helper)
		return data->helper;

	helper = xmalloc(sizeof(*helper));
	child_process_init(helper);
	helper->in = -1;
	helper->out = -1;
	helper->err = 0;
	argv_array_pushf(&helper->args, "git-remote-%s", data->name);
	argv_array_push(&helper->args, transport->remote->name);
	argv_array_push(&helper->args, remove_ext_force(transport->url));
	helper->git_cmd = 0;
	helper->silent_exec_failure = 1;

	if (have_git_dir())
		argv_array_pushf(&helper->env_array, "%s=%s",
				 GIT_DIR_ENVIRONMENT, get_git_dir());

	get_client_protocol_version_advertisement(&version_advert);
	if (version_advert.len > 0)
		argv_array_pushf(&helper->env_array, "%s=%s",
				 GIT_PROTOCOL_ENVIRONMENT, version_advert.buf);

	helper->trace2_child_class = helper->args.argv[0]; /* "remote-<name>" */

	code = start_command(helper);
	if (code < 0 && errno == ENOENT)
		die(_("unable to find remote helper for '%s'"), data->name);
	else if (code != 0)
		exit(code);

	data->helper = helper;
	data->no_disconnect_req = 0;
	refspec_init(&data->rs, REFSPEC_FETCH);

	/*
	 * Open the output as FILE* so strbuf_getline_*() family of
	 * functions can be used.
	 * Do this with duped fd because fclose() will close the fd,
	 * and stuff like taking over will require the fd to remain.
	 */
	duped = dup(helper->out);
	if (duped < 0)
		die_errno(_("can't dup helper output fd"));
	data->out = xfdopen(duped, "r");

	write_constant(helper->in, "capabilities\n");

	while (1) {
		const char *capname, *arg;
		int mandatory = 0;
		if (recvline(data, &buf))
			exit(128);

		if (!*buf.buf)
			break;

		if (*buf.buf == '*') {
			capname = buf.buf + 1;
			mandatory = 1;
		} else
			capname = buf.buf;

		if (debug)
			fprintf(stderr, "Debug: Got cap %s\n", capname);
		if (!strcmp(capname, "fetch"))
			data->fetch = 1;
		else if (!strcmp(capname, "option"))
			data->option = 1;
		else if (!strcmp(capname, "push"))
			data->push = 1;
		else if (!strcmp(capname, "import"))
			data->import = 1;
		else if (!strcmp(capname, "bidi-import"))
			data->bidi_import = 1;
		else if (!strcmp(capname, "export"))
			data->export = 1;
		else if (!strcmp(capname, "check-connectivity"))
			data->check_connectivity = 1;
		else if (skip_prefix(capname, "refspec ", &arg)) {
			refspec_append(&data->rs, arg);
		} else if (!strcmp(capname, "connect")) {
			data->connect = 1;
		} else if (!strcmp(capname, "stateless-connect")) {
			data->stateless_connect = 1;
		} else if (!strcmp(capname, "signed-tags")) {
			data->signed_tags = 1;
		} else if (skip_prefix(capname, "export-marks ", &arg)) {
			data->export_marks = xstrdup(arg);
		} else if (skip_prefix(capname, "import-marks ", &arg)) {
			data->import_marks = xstrdup(arg);
		} else if (starts_with(capname, "no-private-update")) {
			data->no_private_update = 1;
		} else if (mandatory) {
			die(_("unknown mandatory capability %s; this remote "
			      "helper probably needs newer version of Git"),
			    capname);
		}
	}
	if (!data->rs.nr && (data->import || data->bidi_import || data->export)) {
		warning(_("this remote helper should implement refspec capability"));
	}
	strbuf_release(&buf);
	if (debug)
		fprintf(stderr, "Debug: Capabilities complete.\n");
	standard_options(transport);
	return data->helper;
}

static int disconnect_helper(struct transport *transport)
{
	struct helper_data *data = transport->data;
	int res = 0;

	if (data->helper) {
		if (debug)
			fprintf(stderr, "Debug: Disconnecting.\n");
		if (!data->no_disconnect_req) {
			/*
			 * Ignore write errors; there's nothing we can do,
			 * since we're about to close the pipe anyway. And the
			 * most likely error is EPIPE due to the helper dying
			 * to report an error itself.
			 */
			sigchain_push(SIGPIPE, SIG_IGN);
			xwrite(data->helper->in, "\n", 1);
			sigchain_pop(SIGPIPE);
		}
		close(data->helper->in);
		close(data->helper->out);
		fclose(data->out);
		res = finish_command(data->helper);
		FREE_AND_NULL(data->helper);
	}
	return res;
}

static const char *unsupported_options[] = {
	TRANS_OPT_UPLOADPACK,
	TRANS_OPT_RECEIVEPACK,
	TRANS_OPT_THIN,
	TRANS_OPT_KEEP
	};

static const char *boolean_options[] = {
	TRANS_OPT_THIN,
	TRANS_OPT_KEEP,
	TRANS_OPT_FOLLOWTAGS,
	TRANS_OPT_DEEPEN_RELATIVE
	};

static int strbuf_set_helper_option(struct helper_data *data,
				    struct strbuf *buf)
{
	int ret;

	sendline(data, buf);
	if (recvline(data, buf))
		exit(128);

	if (!strcmp(buf->buf, "ok"))
		ret = 0;
	else if (starts_with(buf->buf, "error"))
		ret = -1;
	else if (!strcmp(buf->buf, "unsupported"))
		ret = 1;
	else {
		warning(_("%s unexpectedly said: '%s'"), data->name, buf->buf);
		ret = 1;
	}
	return ret;
}

static int string_list_set_helper_option(struct helper_data *data,
					 const char *name,
					 struct string_list *list)
{
	struct strbuf buf = STRBUF_INIT;
	int i, ret = 0;

	for (i = 0; i < list->nr; i++) {
		strbuf_addf(&buf, "option %s ", name);
		quote_c_style(list->items[i].string, &buf, NULL, 0);
		strbuf_addch(&buf, '\n');

		if ((ret = strbuf_set_helper_option(data, &buf)))
			break;
		strbuf_reset(&buf);
	}
	strbuf_release(&buf);
	return ret;
}

static int set_helper_option(struct transport *transport,
			  const char *name, const char *value)
{
	struct helper_data *data = transport->data;
	struct strbuf buf = STRBUF_INIT;
	int i, ret, is_bool = 0;

	get_helper(transport);

	if (!data->option)
		return 1;

	if (!strcmp(name, "deepen-not"))
		return string_list_set_helper_option(data, name,
						     (struct string_list *)value);

	for (i = 0; i < ARRAY_SIZE(unsupported_options); i++) {
		if (!strcmp(name, unsupported_options[i]))
			return 1;
	}

	for (i = 0; i < ARRAY_SIZE(boolean_options); i++) {
		if (!strcmp(name, boolean_options[i])) {
			is_bool = 1;
			break;
		}
	}

	strbuf_addf(&buf, "option %s ", name);
	if (is_bool)
		strbuf_addstr(&buf, value ? "true" : "false");
	else
		quote_c_style(value, &buf, NULL, 0);
	strbuf_addch(&buf, '\n');

	ret = strbuf_set_helper_option(data, &buf);
	strbuf_release(&buf);
	return ret;
}

static void standard_options(struct transport *t)
{
	char buf[16];
	int v = t->verbose;

	set_helper_option(t, "progress", t->progress ? "true" : "false");

	xsnprintf(buf, sizeof(buf), "%d", v + 1);
	set_helper_option(t, "verbosity", buf);

	switch (t->family) {
	case TRANSPORT_FAMILY_ALL:
		/*
		 * this is already the default,
		 * do not break old remote helpers by setting "all" here
		 */
		break;
	case TRANSPORT_FAMILY_IPV4:
		set_helper_option(t, "family", "ipv4");
		break;
	case TRANSPORT_FAMILY_IPV6:
		set_helper_option(t, "family", "ipv6");
		break;
	}
}

static int release_helper(struct transport *transport)
{
	int res = 0;
	struct helper_data *data = transport->data;
	refspec_clear(&data->rs);
	res = disconnect_helper(transport);
	free(transport->data);
	return res;
}

static int fetch_with_fetch(struct transport *transport,
			    int nr_heads, struct ref **to_fetch)
{
	struct helper_data *data = transport->data;
	int i;
	struct strbuf buf = STRBUF_INIT;

	for (i = 0; i < nr_heads; i++) {
		const struct ref *posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;

		strbuf_addf(&buf, "fetch %s %s\n",
			    oid_to_hex(&posn->old_oid),
			    posn->symref ? posn->symref : posn->name);
	}

	strbuf_addch(&buf, '\n');
	sendline(data, &buf);

	while (1) {
		if (recvline(data, &buf))
			exit(128);

		if (starts_with(buf.buf, "lock ")) {
			const char *name = buf.buf + 5;
			if (transport->pack_lockfile)
				warning(_("%s also locked %s"), data->name, name);
			else
				transport->pack_lockfile = xstrdup(name);
		}
		else if (data->check_connectivity &&
			 data->transport_options.check_self_contained_and_connected &&
			 !strcmp(buf.buf, "connectivity-ok"))
			data->transport_options.self_contained_and_connected = 1;
		else if (!buf.len)
			break;
		else
			warning(_("%s unexpectedly said: '%s'"), data->name, buf.buf);
	}
	strbuf_release(&buf);
	return 0;
}

static int get_importer(struct transport *transport, struct child_process *fastimport)
{
	struct child_process *helper = get_helper(transport);
	struct helper_data *data = transport->data;
	int cat_blob_fd, code;
	child_process_init(fastimport);
	fastimport->in = xdup(helper->out);
	argv_array_push(&fastimport->args, "fast-import");
	argv_array_push(&fastimport->args, debug ? "--stats" : "--quiet");

	if (data->bidi_import) {
		cat_blob_fd = xdup(helper->in);
		argv_array_pushf(&fastimport->args, "--cat-blob-fd=%d", cat_blob_fd);
	}
	fastimport->git_cmd = 1;

	code = start_command(fastimport);
	return code;
}

static int get_exporter(struct transport *transport,
			struct child_process *fastexport,
			struct string_list *revlist_args)
{
	struct helper_data *data = transport->data;
	struct child_process *helper = get_helper(transport);
	int i;

	child_process_init(fastexport);

	/* we need to duplicate helper->in because we want to use it after
	 * fastexport is done with it. */
	fastexport->out = dup(helper->in);
	argv_array_push(&fastexport->args, "fast-export");
	argv_array_push(&fastexport->args, "--use-done-feature");
	argv_array_push(&fastexport->args, data->signed_tags ?
		"--signed-tags=verbatim" : "--signed-tags=warn-strip");
	if (data->export_marks)
		argv_array_pushf(&fastexport->args, "--export-marks=%s.tmp", data->export_marks);
	if (data->import_marks)
		argv_array_pushf(&fastexport->args, "--import-marks=%s", data->import_marks);

	for (i = 0; i < revlist_args->nr; i++)
		argv_array_push(&fastexport->args, revlist_args->items[i].string);

	fastexport->git_cmd = 1;
	return start_command(fastexport);
}

static int fetch_with_import(struct transport *transport,
			     int nr_heads, struct ref **to_fetch)
{
	struct child_process fastimport;
	struct helper_data *data = transport->data;
	int i;
	struct ref *posn;
	struct strbuf buf = STRBUF_INIT;

	get_helper(transport);

	if (get_importer(transport, &fastimport))
		die(_("couldn't run fast-import"));

	for (i = 0; i < nr_heads; i++) {
		posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;

		strbuf_addf(&buf, "import %s\n",
			    posn->symref ? posn->symref : posn->name);
		sendline(data, &buf);
		strbuf_reset(&buf);
	}

	write_constant(data->helper->in, "\n");
	/*
	 * remote-helpers that advertise the bidi-import capability are required to
	 * buffer the complete batch of import commands until this newline before
	 * sending data to fast-import.
	 * These helpers read back data from fast-import on their stdin, which could
	 * be mixed with import commands, otherwise.
	 */

	if (finish_command(&fastimport))
		die(_("error while running fast-import"));

	/*
	 * The fast-import stream of a remote helper that advertises
	 * the "refspec" capability writes to the refs named after the
	 * right hand side of the first refspec matching each ref we
	 * were fetching.
	 *
	 * (If no "refspec" capability was specified, for historical
	 * reasons we default to the equivalent of *:*.)
	 *
	 * Store the result in to_fetch[i].old_sha1.  Callers such
	 * as "git fetch" can use the value to write feedback to the
	 * terminal, populate FETCH_HEAD, and determine what new value
	 * should be written to peer_ref if the update is a
	 * fast-forward or this is a forced update.
	 */
	for (i = 0; i < nr_heads; i++) {
		char *private, *name;
		posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;
		name = posn->symref ? posn->symref : posn->name;
		if (data->rs.nr)
			private = apply_refspecs(&data->rs, name);
		else
			private = xstrdup(name);
		if (private) {
			if (read_ref(private, &posn->old_oid) < 0)
				die(_("could not read ref %s"), private);
			free(private);
		}
	}
	strbuf_release(&buf);
	return 0;
}

static int run_connect(struct transport *transport, struct strbuf *cmdbuf)
{
	struct helper_data *data = transport->data;
	int ret = 0;
	int duped;
	FILE *input;
	struct child_process *helper;

	helper = get_helper(transport);

	/*
	 * Yes, dup the pipe another time, as we need unbuffered version
	 * of input pipe as FILE*. fclose() closes the underlying fd and
	 * stream buffering only can be changed before first I/O operation
	 * on it.
	 */
	duped = dup(helper->out);
	if (duped < 0)
		die_errno(_("can't dup helper output fd"));
	input = xfdopen(duped, "r");
	setvbuf(input, NULL, _IONBF, 0);

	sendline(data, cmdbuf);
	if (recvline_fh(input, cmdbuf))
		exit(128);

	if (!strcmp(cmdbuf->buf, "")) {
		data->no_disconnect_req = 1;
		if (debug)
			fprintf(stderr, "Debug: Smart transport connection "
				"ready.\n");
		ret = 1;
	} else if (!strcmp(cmdbuf->buf, "fallback")) {
		if (debug)
			fprintf(stderr, "Debug: Falling back to dumb "
				"transport.\n");
	} else {
		die(_("unknown response to connect: %s"),
		    cmdbuf->buf);
	}

	fclose(input);
	return ret;
}

static int process_connect_service(struct transport *transport,
				   const char *name, const char *exec)
{
	struct helper_data *data = transport->data;
	struct strbuf cmdbuf = STRBUF_INIT;
	int ret = 0;

	/*
	 * Handle --upload-pack and friends. This is fire and forget...
	 * just warn if it fails.
	 */
	if (strcmp(name, exec)) {
		int r = set_helper_option(transport, "servpath", exec);
		if (r > 0)
			warning(_("setting remote service path not supported by protocol"));
		else if (r < 0)
			warning(_("invalid remote service path"));
	}

	if (data->connect) {
		strbuf_addf(&cmdbuf, "connect %s\n", name);
		ret = run_connect(transport, &cmdbuf);
	} else if (data->stateless_connect &&
		   (get_protocol_version_config() == protocol_v2) &&
		   !strcmp("git-upload-pack", name)) {
		strbuf_addf(&cmdbuf, "stateless-connect %s\n", name);
		ret = run_connect(transport, &cmdbuf);
		if (ret)
			transport->stateless_rpc = 1;
	}

	strbuf_release(&cmdbuf);
	return ret;
}

static int process_connect(struct transport *transport,
				     int for_push)
{
	struct helper_data *data = transport->data;
	const char *name;
	const char *exec;

	name = for_push ? "git-receive-pack" : "git-upload-pack";
	if (for_push)
		exec = data->transport_options.receivepack;
	else
		exec = data->transport_options.uploadpack;

	return process_connect_service(transport, name, exec);
}

static int connect_helper(struct transport *transport, const char *name,
		   const char *exec, int fd[2])
{
	struct helper_data *data = transport->data;

	/* Get_helper so connect is inited. */
	get_helper(transport);
	if (!data->connect)
		die(_("operation not supported by protocol"));

	if (!process_connect_service(transport, name, exec))
		die(_("can't connect to subservice %s"), name);

	fd[0] = data->helper->out;
	fd[1] = data->helper->in;
	return 0;
}

static int fetch(struct transport *transport,
		 int nr_heads, struct ref **to_fetch)
{
	struct helper_data *data = transport->data;
	int i, count;

	if (process_connect(transport, 0)) {
		do_take_over(transport);
		return transport->vtable->fetch(transport, nr_heads, to_fetch);
	}

	count = 0;
	for (i = 0; i < nr_heads; i++)
		if (!(to_fetch[i]->status & REF_STATUS_UPTODATE))
			count++;

	if (!count)
		return 0;

	if (data->check_connectivity &&
	    data->transport_options.check_self_contained_and_connected)
		set_helper_option(transport, "check-connectivity", "true");

	if (transport->cloning)
		set_helper_option(transport, "cloning", "true");

	if (data->transport_options.update_shallow)
		set_helper_option(transport, "update-shallow", "true");

	if (data->transport_options.filter_options.choice) {
		const char *spec = expand_list_objects_filter_spec(
			&data->transport_options.filter_options);
		set_helper_option(transport, "filter", spec);
	}

	if (data->transport_options.negotiation_tips)
		warning("Ignoring --negotiation-tip because the protocol does not support it.");

	if (data->fetch)
		return fetch_with_fetch(transport, nr_heads, to_fetch);

	if (data->import)
		return fetch_with_import(transport, nr_heads, to_fetch);

	return -1;
}

static int push_update_ref_status(struct strbuf *buf,
				   struct ref **ref,
				   struct ref *remote_refs)
{
	char *refname, *msg;
	int status, forced = 0;

	if (starts_with(buf->buf, "ok ")) {
		status = REF_STATUS_OK;
		refname = buf->buf + 3;
	} else if (starts_with(buf->buf, "error ")) {
		status = REF_STATUS_REMOTE_REJECT;
		refname = buf->buf + 6;
	} else
		die(_("expected ok/error, helper said '%s'"), buf->buf);

	msg = strchr(refname, ' ');
	if (msg) {
		struct strbuf msg_buf = STRBUF_INIT;
		const char *end;

		*msg++ = '\0';
		if (!unquote_c_style(&msg_buf, msg, &end))
			msg = strbuf_detach(&msg_buf, NULL);
		else
			msg = xstrdup(msg);
		strbuf_release(&msg_buf);

		if (!strcmp(msg, "no match")) {
			status = REF_STATUS_NONE;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "up to date")) {
			status = REF_STATUS_UPTODATE;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "non-fast forward")) {
			status = REF_STATUS_REJECT_NONFASTFORWARD;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "already exists")) {
			status = REF_STATUS_REJECT_ALREADY_EXISTS;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "fetch first")) {
			status = REF_STATUS_REJECT_FETCH_FIRST;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "needs force")) {
			status = REF_STATUS_REJECT_NEEDS_FORCE;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "stale info")) {
			status = REF_STATUS_REJECT_STALE;
			FREE_AND_NULL(msg);
		}
		else if (!strcmp(msg, "forced update")) {
			forced = 1;
			FREE_AND_NULL(msg);
		}
	}

	if (*ref)
		*ref = find_ref_by_name(*ref, refname);
	if (!*ref)
		*ref = find_ref_by_name(remote_refs, refname);
	if (!*ref) {
		warning(_("helper reported unexpected status of %s"), refname);
		return 1;
	}

	if ((*ref)->status != REF_STATUS_NONE) {
		/*
		 * Earlier, the ref was marked not to be pushed, so ignore the ref
		 * status reported by the remote helper if the latter is 'no match'.
		 */
		if (status == REF_STATUS_NONE)
			return 1;
	}

	(*ref)->status = status;
	(*ref)->forced_update |= forced;
	(*ref)->remote_status = msg;
	return !(status == REF_STATUS_OK);
}

static int push_update_refs_status(struct helper_data *data,
				    struct ref *remote_refs,
				    int flags)
{
	struct strbuf buf = STRBUF_INIT;
	struct ref *ref = remote_refs;
	int ret = 0;

	for (;;) {
		char *private;

		if (recvline(data, &buf)) {
			ret = 1;
			break;
		}

		if (!buf.len)
			break;

		if (push_update_ref_status(&buf, &ref, remote_refs))
			continue;

		if (flags & TRANSPORT_PUSH_DRY_RUN || !data->rs.nr || data->no_private_update)
			continue;

		/* propagate back the update to the remote namespace */
		private = apply_refspecs(&data->rs, ref->name);
		if (!private)
			continue;
		update_ref("update by helper", private, &ref->new_oid, NULL,
			   0, 0);
		free(private);
	}
	strbuf_release(&buf);
	return ret;
}

static void set_common_push_options(struct transport *transport,
				   const char *name, int flags)
{
	if (flags & TRANSPORT_PUSH_DRY_RUN) {
		if (set_helper_option(transport, "dry-run", "true") != 0)
			die(_("helper %s does not support dry-run"), name);
	} else if (flags & TRANSPORT_PUSH_CERT_ALWAYS) {
		if (set_helper_option(transport, TRANS_OPT_PUSH_CERT, "true") != 0)
			die(_("helper %s does not support --signed"), name);
	} else if (flags & TRANSPORT_PUSH_CERT_IF_ASKED) {
		if (set_helper_option(transport, TRANS_OPT_PUSH_CERT, "if-asked") != 0)
			die(_("helper %s does not support --signed=if-asked"), name);
	}

	if (flags & TRANSPORT_PUSH_OPTIONS) {
		struct string_list_item *item;
		for_each_string_list_item(item, transport->push_options)
			if (set_helper_option(transport, "push-option", item->string) != 0)
				die(_("helper %s does not support 'push-option'"), name);
	}
}

static int push_refs_with_push(struct transport *transport,
			       struct ref *remote_refs, int flags)
{
	int force_all = flags & TRANSPORT_PUSH_FORCE;
	int mirror = flags & TRANSPORT_PUSH_MIRROR;
	int atomic = flags & TRANSPORT_PUSH_ATOMIC;
	struct helper_data *data = transport->data;
	struct strbuf buf = STRBUF_INIT;
	struct ref *ref;
	struct string_list cas_options = STRING_LIST_INIT_DUP;
	struct string_list_item *cas_option;

	get_helper(transport);
	if (!data->push)
		return 1;

	for (ref = remote_refs; ref; ref = ref->next) {
		if (!ref->peer_ref && !mirror)
			continue;

		/* Check for statuses set by set_ref_status_for_push() */
		switch (ref->status) {
		case REF_STATUS_REJECT_NONFASTFORWARD:
		case REF_STATUS_REJECT_STALE:
		case REF_STATUS_REJECT_ALREADY_EXISTS:
			if (atomic) {
				string_list_clear(&cas_options, 0);
				return 0;
			} else
				continue;
		case REF_STATUS_UPTODATE:
			continue;
		default:
			; /* do nothing */
		}

		if (force_all)
			ref->force = 1;

		strbuf_addstr(&buf, "push ");
		if (!ref->deletion) {
			if (ref->force)
				strbuf_addch(&buf, '+');
			if (ref->peer_ref)
				strbuf_addstr(&buf, ref->peer_ref->name);
			else
				strbuf_addstr(&buf, oid_to_hex(&ref->new_oid));
		}
		strbuf_addch(&buf, ':');
		strbuf_addstr(&buf, ref->name);
		strbuf_addch(&buf, '\n');

		/*
		 * The "--force-with-lease" options without explicit
		 * values to expect have already been expanded into
		 * the ref->old_oid_expect[] field; we can ignore
		 * transport->smart_options->cas altogether and instead
		 * can enumerate them from the refs.
		 */
		if (ref->expect_old_sha1) {
			struct strbuf cas = STRBUF_INIT;
			strbuf_addf(&cas, "%s:%s",
				    ref->name, oid_to_hex(&ref->old_oid_expect));
			string_list_append_nodup(&cas_options,
						 strbuf_detach(&cas, NULL));
		}
	}
	if (buf.len == 0) {
		string_list_clear(&cas_options, 0);
		return 0;
	}

	for_each_string_list_item(cas_option, &cas_options)
		set_helper_option(transport, "cas", cas_option->string);
	set_common_push_options(transport, data->name, flags);

	strbuf_addch(&buf, '\n');
	sendline(data, &buf);
	strbuf_release(&buf);
	string_list_clear(&cas_options, 0);

	return push_update_refs_status(data, remote_refs, flags);
}

static int push_refs_with_export(struct transport *transport,
		struct ref *remote_refs, int flags)
{
	struct ref *ref;
	struct child_process *helper, exporter;
	struct helper_data *data = transport->data;
	struct string_list revlist_args = STRING_LIST_INIT_DUP;
	struct strbuf buf = STRBUF_INIT;

	if (!data->rs.nr)
		die(_("remote-helper doesn't support push; refspec needed"));

	set_common_push_options(transport, data->name, flags);
	if (flags & TRANSPORT_PUSH_FORCE) {
		if (set_helper_option(transport, "force", "true") != 0)
			warning(_("helper %s does not support 'force'"), data->name);
	}

	helper = get_helper(transport);

	write_constant(helper->in, "export\n");

	for (ref = remote_refs; ref; ref = ref->next) {
		char *private;
		struct object_id oid;

		private = apply_refspecs(&data->rs, ref->name);
		if (private && !get_oid(private, &oid)) {
			strbuf_addf(&buf, "^%s", private);
			string_list_append_nodup(&revlist_args,
						 strbuf_detach(&buf, NULL));
			oidcpy(&ref->old_oid, &oid);
		}
		free(private);

		if (ref->peer_ref) {
			if (strcmp(ref->name, ref->peer_ref->name)) {
				if (!ref->deletion) {
					const char *name;
					int flag;

					/* Follow symbolic refs (mainly for HEAD). */
					name = resolve_ref_unsafe(ref->peer_ref->name,
								  RESOLVE_REF_READING,
								  &oid, &flag);
					if (!name || !(flag & REF_ISSYMREF))
						name = ref->peer_ref->name;

					strbuf_addf(&buf, "%s:%s", name, ref->name);
				} else
					strbuf_addf(&buf, ":%s", ref->name);

				string_list_append(&revlist_args, "--refspec");
				string_list_append(&revlist_args, buf.buf);
				strbuf_release(&buf);
			}
			if (!ref->deletion)
				string_list_append(&revlist_args, ref->peer_ref->name);
		}
	}

	if (get_exporter(transport, &exporter, &revlist_args))
		die(_("couldn't run fast-export"));

	string_list_clear(&revlist_args, 1);

	if (finish_command(&exporter))
		die(_("error while running fast-export"));
	if (push_update_refs_status(data, remote_refs, flags))
		return 1;

	if (data->export_marks) {
		strbuf_addf(&buf, "%s.tmp", data->export_marks);
		rename(buf.buf, data->export_marks);
		strbuf_release(&buf);
	}

	return 0;
}

static int push_refs(struct transport *transport,
		struct ref *remote_refs, int flags)
{
	struct helper_data *data = transport->data;

	if (process_connect(transport, 1)) {
		do_take_over(transport);
		return transport->vtable->push_refs(transport, remote_refs, flags);
	}

	if (!remote_refs) {
		fprintf(stderr,
			_("No refs in common and none specified; doing nothing.\n"
			  "Perhaps you should specify a branch such as 'master'.\n"));
		return 0;
	}

	if (data->push)
		return push_refs_with_push(transport, remote_refs, flags);

	if (data->export)
		return push_refs_with_export(transport, remote_refs, flags);

	return -1;
}


static int has_attribute(const char *attrs, const char *attr)
{
	int len;
	if (!attrs)
		return 0;

	len = strlen(attr);
	for (;;) {
		const char *space = strchrnul(attrs, ' ');
		if (len == space - attrs && !strncmp(attrs, attr, len))
			return 1;
		if (!*space)
			return 0;
		attrs = space + 1;
	}
}

static struct ref *get_refs_list(struct transport *transport, int for_push,
				 const struct argv_array *ref_prefixes)
{
	struct helper_data *data = transport->data;
	struct child_process *helper;
	struct ref *ret = NULL;
	struct ref **tail = &ret;
	struct ref *posn;
	struct strbuf buf = STRBUF_INIT;

	helper = get_helper(transport);

	if (process_connect(transport, for_push)) {
		do_take_over(transport);
		return transport->vtable->get_refs_list(transport, for_push, ref_prefixes);
	}

	if (data->push && for_push)
		write_str_in_full(helper->in, "list for-push\n");
	else
		write_str_in_full(helper->in, "list\n");

	while (1) {
		char *eov, *eon;
		if (recvline(data, &buf))
			exit(128);

		if (!*buf.buf)
			break;

		eov = strchr(buf.buf, ' ');
		if (!eov)
			die(_("malformed response in ref list: %s"), buf.buf);
		eon = strchr(eov + 1, ' ');
		*eov = '\0';
		if (eon)
			*eon = '\0';
		*tail = alloc_ref(eov + 1);
		if (buf.buf[0] == '@')
			(*tail)->symref = xstrdup(buf.buf + 1);
		else if (buf.buf[0] != '?')
			get_oid_hex(buf.buf, &(*tail)->old_oid);
		if (eon) {
			if (has_attribute(eon + 1, "unchanged")) {
				(*tail)->status |= REF_STATUS_UPTODATE;
				if (read_ref((*tail)->name, &(*tail)->old_oid) < 0)
					die(_("could not read ref %s"),
					    (*tail)->name);
			}
		}
		tail = &((*tail)->next);
	}
	if (debug)
		fprintf(stderr, "Debug: Read ref listing.\n");
	strbuf_release(&buf);

	for (posn = ret; posn; posn = posn->next)
		resolve_remote_symref(posn, ret);

	return ret;
}

static struct transport_vtable vtable = {
	0,
	set_helper_option,
	get_refs_list,
	fetch,
	push_refs,
	connect_helper,
	release_helper
};

int transport_helper_init(struct transport *transport, const char *name)
{
	struct helper_data *data = xcalloc(1, sizeof(*data));
	data->name = name;

	transport_check_allowed(name);

	if (getenv("GIT_TRANSPORT_HELPER_DEBUG"))
		debug = 1;

	transport->data = data;
	transport->vtable = &vtable;
	transport->smart_options = &(data->transport_options);
	return 0;
}

/*
 * Linux pipes can buffer 65536 bytes at once (and most platforms can
 * buffer less), so attempt reads and writes with up to that size.
 */
#define BUFFERSIZE 65536
/* This should be enough to hold debugging message. */
#define PBUFFERSIZE 8192

/* Print bidirectional transfer loop debug message. */
__attribute__((format (printf, 1, 2)))
static void transfer_debug(const char *fmt, ...)
{
	/*
	 * NEEDSWORK: This function is sometimes used from multiple threads, and
	 * we end up using debug_enabled racily. That "should not matter" since
	 * we always write the same value, but it's still wrong. This function
	 * is listed in .tsan-suppressions for the time being.
	 */

	va_list args;
	char msgbuf[PBUFFERSIZE];
	static int debug_enabled = -1;

	if (debug_enabled < 0)
		debug_enabled = getenv("GIT_TRANSLOOP_DEBUG") ? 1 : 0;
	if (!debug_enabled)
		return;

	va_start(args, fmt);
	vsnprintf(msgbuf, PBUFFERSIZE, fmt, args);
	va_end(args);
	fprintf(stderr, "Transfer loop debugging: %s\n", msgbuf);
}

/* Stream state: More data may be coming in this direction. */
#define SSTATE_TRANSFERRING 0
/*
 * Stream state: No more data coming in this direction, flushing rest of
 * data.
 */
#define SSTATE_FLUSHING 1
/* Stream state: Transfer in this direction finished. */
#define SSTATE_FINISHED 2

#define STATE_NEEDS_READING(state) ((state) <= SSTATE_TRANSFERRING)
#define STATE_NEEDS_WRITING(state) ((state) <= SSTATE_FLUSHING)
#define STATE_NEEDS_CLOSING(state) ((state) == SSTATE_FLUSHING)

/* Unidirectional transfer. */
struct unidirectional_transfer {
	/* Source */
	int src;
	/* Destination */
	int dest;
	/* Is source socket? */
	int src_is_sock;
	/* Is destination socket? */
	int dest_is_sock;
	/* Transfer state (TRANSFERRING/FLUSHING/FINISHED) */
	int state;
	/* Buffer. */
	char buf[BUFFERSIZE];
	/* Buffer used. */
	size_t bufuse;
	/* Name of source. */
	const char *src_name;
	/* Name of destination. */
	const char *dest_name;
};

/* Closes the target (for writing) if transfer has finished. */
static void udt_close_if_finished(struct unidirectional_transfer *t)
{
	if (STATE_NEEDS_CLOSING(t->state) && !t->bufuse) {
		t->state = SSTATE_FINISHED;
		if (t->dest_is_sock)
			shutdown(t->dest, SHUT_WR);
		else
			close(t->dest);
		transfer_debug("Closed %s.", t->dest_name);
	}
}

/*
 * Tries to read data from source into buffer. If buffer is full,
 * no data is read. Returns 0 on success, -1 on error.
 */
static int udt_do_read(struct unidirectional_transfer *t)
{
	ssize_t bytes;

	if (t->bufuse == BUFFERSIZE)
		return 0;	/* No space for more. */

	transfer_debug("%s is readable", t->src_name);
	bytes = xread(t->src, t->buf + t->bufuse, BUFFERSIZE - t->bufuse);
	if (bytes < 0) {
		error_errno(_("read(%s) failed"), t->src_name);
		return -1;
	} else if (bytes == 0) {
		transfer_debug("%s EOF (with %i bytes in buffer)",
			t->src_name, (int)t->bufuse);
		t->state = SSTATE_FLUSHING;
	} else if (bytes > 0) {
		t->bufuse += bytes;
		transfer_debug("Read %i bytes from %s (buffer now at %i)",
			(int)bytes, t->src_name, (int)t->bufuse);
	}
	return 0;
}

/* Tries to write data from buffer into destination. If buffer is empty,
 * no data is written. Returns 0 on success, -1 on error.
 */
static int udt_do_write(struct unidirectional_transfer *t)
{
	ssize_t bytes;

	if (t->bufuse == 0)
		return 0;	/* Nothing to write. */

	transfer_debug("%s is writable", t->dest_name);
	bytes = xwrite(t->dest, t->buf, t->bufuse);
	if (bytes < 0) {
		error_errno(_("write(%s) failed"), t->dest_name);
		return -1;
	} else if (bytes > 0) {
		t->bufuse -= bytes;
		if (t->bufuse)
			memmove(t->buf, t->buf + bytes, t->bufuse);
		transfer_debug("Wrote %i bytes to %s (buffer now at %i)",
			(int)bytes, t->dest_name, (int)t->bufuse);
	}
	return 0;
}


/* State of bidirectional transfer loop. */
struct bidirectional_transfer_state {
	/* Direction from program to git. */
	struct unidirectional_transfer ptg;
	/* Direction from git to program. */
	struct unidirectional_transfer gtp;
};

static void *udt_copy_task_routine(void *udt)
{
	struct unidirectional_transfer *t = (struct unidirectional_transfer *)udt;
	while (t->state != SSTATE_FINISHED) {
		if (STATE_NEEDS_READING(t->state))
			if (udt_do_read(t))
				return NULL;
		if (STATE_NEEDS_WRITING(t->state))
			if (udt_do_write(t))
				return NULL;
		if (STATE_NEEDS_CLOSING(t->state))
			udt_close_if_finished(t);
	}
	return udt;	/* Just some non-NULL value. */
}

#ifndef NO_PTHREADS

/*
 * Join thread, with appropriate errors on failure. Name is name for the
 * thread (for error messages). Returns 0 on success, 1 on failure.
 */
static int tloop_join(pthread_t thread, const char *name)
{
	int err;
	void *tret;
	err = pthread_join(thread, &tret);
	if (!tret) {
		error(_("%s thread failed"), name);
		return 1;
	}
	if (err) {
		error(_("%s thread failed to join: %s"), name, strerror(err));
		return 1;
	}
	return 0;
}

/*
 * Spawn the transfer tasks and then wait for them. Returns 0 on success,
 * -1 on failure.
 */
static int tloop_spawnwait_tasks(struct bidirectional_transfer_state *s)
{
	pthread_t gtp_thread;
	pthread_t ptg_thread;
	int err;
	int ret = 0;
	err = pthread_create(&gtp_thread, NULL, udt_copy_task_routine,
		&s->gtp);
	if (err)
		die(_("can't start thread for copying data: %s"), strerror(err));
	err = pthread_create(&ptg_thread, NULL, udt_copy_task_routine,
		&s->ptg);
	if (err)
		die(_("can't start thread for copying data: %s"), strerror(err));

	ret |= tloop_join(gtp_thread, "Git to program copy");
	ret |= tloop_join(ptg_thread, "Program to git copy");
	return ret;
}
#else

/* Close the source and target (for writing) for transfer. */
static void udt_kill_transfer(struct unidirectional_transfer *t)
{
	t->state = SSTATE_FINISHED;
	/*
	 * Socket read end left open isn't a disaster if nobody
	 * attempts to read from it (mingw compat headers do not
	 * have SHUT_RD)...
	 *
	 * We can't fully close the socket since otherwise gtp
	 * task would first close the socket it sends data to
	 * while closing the ptg file descriptors.
	 */
	if (!t->src_is_sock)
		close(t->src);
	if (t->dest_is_sock)
		shutdown(t->dest, SHUT_WR);
	else
		close(t->dest);
}

/*
 * Join process, with appropriate errors on failure. Name is name for the
 * process (for error messages). Returns 0 on success, 1 on failure.
 */
static int tloop_join(pid_t pid, const char *name)
{
	int tret;
	if (waitpid(pid, &tret, 0) < 0) {
		error_errno(_("%s process failed to wait"), name);
		return 1;
	}
	if (!WIFEXITED(tret) || WEXITSTATUS(tret)) {
		error(_("%s process failed"), name);
		return 1;
	}
	return 0;
}

/*
 * Spawn the transfer tasks and then wait for them. Returns 0 on success,
 * -1 on failure.
 */
static int tloop_spawnwait_tasks(struct bidirectional_transfer_state *s)
{
	pid_t pid1, pid2;
	int ret = 0;

	/* Fork thread #1: git to program. */
	pid1 = fork();
	if (pid1 < 0)
		die_errno(_("can't start thread for copying data"));
	else if (pid1 == 0) {
		udt_kill_transfer(&s->ptg);
		exit(udt_copy_task_routine(&s->gtp) ? 0 : 1);
	}

	/* Fork thread #2: program to git. */
	pid2 = fork();
	if (pid2 < 0)
		die_errno(_("can't start thread for copying data"));
	else if (pid2 == 0) {
		udt_kill_transfer(&s->gtp);
		exit(udt_copy_task_routine(&s->ptg) ? 0 : 1);
	}

	/*
	 * Close both streams in parent as to not interfere with
	 * end of file detection and wait for both tasks to finish.
	 */
	udt_kill_transfer(&s->gtp);
	udt_kill_transfer(&s->ptg);
	ret |= tloop_join(pid1, "Git to program copy");
	ret |= tloop_join(pid2, "Program to git copy");
	return ret;
}
#endif

/*
 * Copies data from stdin to output and from input to stdout simultaneously.
 * Additionally filtering through given filter. If filter is NULL, uses
 * identity filter.
 */
int bidirectional_transfer_loop(int input, int output)
{
	struct bidirectional_transfer_state state;

	/* Fill the state fields. */
	state.ptg.src = input;
	state.ptg.dest = 1;
	state.ptg.src_is_sock = (input == output);
	state.ptg.dest_is_sock = 0;
	state.ptg.state = SSTATE_TRANSFERRING;
	state.ptg.bufuse = 0;
	state.ptg.src_name = "remote input";
	state.ptg.dest_name = "stdout";

	state.gtp.src = 0;
	state.gtp.dest = output;
	state.gtp.src_is_sock = 0;
	state.gtp.dest_is_sock = (input == output);
	state.gtp.state = SSTATE_TRANSFERRING;
	state.gtp.bufuse = 0;
	state.gtp.src_name = "stdin";
	state.gtp.dest_name = "remote output";

	return tloop_spawnwait_tasks(&state);
}
