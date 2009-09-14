#include "cache.h"
#include "transport.h"

#include "run-command.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"

struct helper_data
{
	const char *name;
	struct child_process *helper;
	unsigned fetch : 1;
};

static struct child_process *get_helper(struct transport *transport)
{
	struct helper_data *data = transport->data;
	struct strbuf buf = STRBUF_INIT;
	struct child_process *helper;
	FILE *file;

	if (data->helper)
		return data->helper;

	helper = xcalloc(1, sizeof(*helper));
	helper->in = -1;
	helper->out = -1;
	helper->err = 0;
	helper->argv = xcalloc(4, sizeof(*helper->argv));
	strbuf_addf(&buf, "remote-%s", data->name);
	helper->argv[0] = strbuf_detach(&buf, NULL);
	helper->argv[1] = transport->remote->name;
	helper->argv[2] = transport->url;
	helper->git_cmd = 1;
	if (start_command(helper))
		die("Unable to run helper: git %s", helper->argv[0]);
	data->helper = helper;

	write_str_in_full(helper->in, "capabilities\n");

	file = xfdopen(helper->out, "r");
	while (1) {
		if (strbuf_getline(&buf, file, '\n') == EOF)
			exit(128); /* child died, message supplied already */

		if (!*buf.buf)
			break;
		if (!strcmp(buf.buf, "fetch"))
			data->fetch = 1;
	}
	return data->helper;
}

static int disconnect_helper(struct transport *transport)
{
	struct helper_data *data = transport->data;
	if (data->helper) {
		write_str_in_full(data->helper->in, "\n");
		close(data->helper->in);
		finish_command(data->helper);
		free((char *)data->helper->argv[0]);
		free(data->helper->argv);
		free(data->helper);
		data->helper = NULL;
	}
	return 0;
}

static int fetch_with_fetch(struct transport *transport,
			    int nr_heads, const struct ref **to_fetch)
{
	struct child_process *helper = get_helper(transport);
	FILE *file = xfdopen(helper->out, "r");
	int i;
	struct strbuf buf = STRBUF_INIT;

	for (i = 0; i < nr_heads; i++) {
		const struct ref *posn = to_fetch[i];
		if (posn->status & REF_STATUS_UPTODATE)
			continue;

		strbuf_addf(&buf, "fetch %s %s\n",
			    sha1_to_hex(posn->old_sha1), posn->name);
		write_in_full(helper->in, buf.buf, buf.len);
		strbuf_reset(&buf);

		if (strbuf_getline(&buf, file, '\n') == EOF)
			exit(128); /* child died, message supplied already */
	}
	return 0;
}

static int fetch(struct transport *transport,
		 int nr_heads, const struct ref **to_fetch)
{
	struct helper_data *data = transport->data;
	int i, count;

	count = 0;
	for (i = 0; i < nr_heads; i++)
		if (!(to_fetch[i]->status & REF_STATUS_UPTODATE))
			count++;

	if (!count)
		return 0;

	if (data->fetch)
		return fetch_with_fetch(transport, nr_heads, to_fetch);

	return -1;
}

static struct ref *get_refs_list(struct transport *transport, int for_push)
{
	struct child_process *helper;
	struct ref *ret = NULL;
	struct ref **tail = &ret;
	struct ref *posn;
	struct strbuf buf = STRBUF_INIT;
	FILE *file;

	helper = get_helper(transport);

	write_str_in_full(helper->in, "list\n");

	file = xfdopen(helper->out, "r");
	while (1) {
		char *eov, *eon;
		if (strbuf_getline(&buf, file, '\n') == EOF)
			exit(128); /* child died, message supplied already */

		if (!*buf.buf)
			break;

		eov = strchr(buf.buf, ' ');
		if (!eov)
			die("Malformed response in ref list: %s", buf.buf);
		eon = strchr(eov + 1, ' ');
		*eov = '\0';
		if (eon)
			*eon = '\0';
		*tail = alloc_ref(eov + 1);
		if (buf.buf[0] == '@')
			(*tail)->symref = xstrdup(buf.buf + 1);
		else if (buf.buf[0] != '?')
			get_sha1_hex(buf.buf, (*tail)->old_sha1);
		tail = &((*tail)->next);
	}
	strbuf_release(&buf);

	for (posn = ret; posn; posn = posn->next)
		resolve_remote_symref(posn, ret);

	return ret;
}

int transport_helper_init(struct transport *transport, const char *name)
{
	struct helper_data *data = xcalloc(sizeof(*data), 1);
	data->name = name;

	transport->data = data;
	transport->get_refs_list = get_refs_list;
	transport->fetch = fetch;
	transport->disconnect = disconnect_helper;
	return 0;
}
