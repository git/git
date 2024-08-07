/*
 * Generic implementation of background process infrastructure.
 */
#include "git-compat-util.h"
#include "sub-process.h"
#include "sigchain.h"
#include "pkt-line.h"

int cmd2process_cmp(const void *cmp_data UNUSED,
		    const struct hashmap_entry *eptr,
		    const struct hashmap_entry *entry_or_key,
		    const void *keydata UNUSED)
{
	const struct subprocess_entry *e1, *e2;

	e1 = container_of(eptr, const struct subprocess_entry, ent);
	e2 = container_of(entry_or_key, const struct subprocess_entry, ent);

	return strcmp(e1->cmd, e2->cmd);
}

struct subprocess_entry *subprocess_find_entry(struct hashmap *hashmap, const char *cmd)
{
	struct subprocess_entry key;

	hashmap_entry_init(&key.ent, strhash(cmd));
	key.cmd = cmd;
	return hashmap_get_entry(hashmap, &key, ent, NULL);
}

int subprocess_read_status(int fd, struct strbuf *status)
{
	struct strbuf **pair;
	char *line;
	int len;

	for (;;) {
		len = packet_read_line_gently(fd, NULL, &line);
		if ((len < 0) || !line)
			break;
		pair = strbuf_split_str(line, '=', 2);
		if (pair[0] && pair[0]->len && pair[1]) {
			/* the last "status=<foo>" line wins */
			if (!strcmp(pair[0]->buf, "status=")) {
				strbuf_reset(status);
				strbuf_addbuf(status, pair[1]);
			}
		}
		strbuf_list_free(pair);
	}

	return (len < 0) ? len : 0;
}

void subprocess_stop(struct hashmap *hashmap, struct subprocess_entry *entry)
{
	if (!entry)
		return;

	entry->process.clean_on_exit = 0;
	kill(entry->process.pid, SIGTERM);
	finish_command(&entry->process);

	hashmap_remove(hashmap, &entry->ent, NULL);
}

static void subprocess_exit_handler(struct child_process *process)
{
	sigchain_push(SIGPIPE, SIG_IGN);
	/* Closing the pipe signals the subprocess to initiate a shutdown. */
	close(process->in);
	close(process->out);
	sigchain_pop(SIGPIPE);
	/* Finish command will wait until the shutdown is complete. */
	finish_command(process);
}

int subprocess_start(struct hashmap *hashmap, struct subprocess_entry *entry, const char *cmd,
	subprocess_start_fn startfn)
{
	int err;
	struct child_process *process;

	entry->cmd = cmd;
	process = &entry->process;

	child_process_init(process);
	strvec_push(&process->args, cmd);
	process->use_shell = 1;
	process->in = -1;
	process->out = -1;
	process->clean_on_exit = 1;
	process->clean_on_exit_handler = subprocess_exit_handler;
	process->trace2_child_class = "subprocess";

	err = start_command(process);
	if (err) {
		error("cannot fork to run subprocess '%s'", cmd);
		return err;
	}

	hashmap_entry_init(&entry->ent, strhash(cmd));

	err = startfn(entry);
	if (err) {
		error("initialization for subprocess '%s' failed", cmd);
		subprocess_stop(hashmap, entry);
		return err;
	}

	hashmap_add(hashmap, &entry->ent);
	return 0;
}

static int handshake_version(struct child_process *process,
			     const char *welcome_prefix, int *versions,
			     int *chosen_version)
{
	int version_scratch;
	int i;
	char *line;
	const char *p;

	if (!chosen_version)
		chosen_version = &version_scratch;

	if (packet_write_fmt_gently(process->in, "%s-client\n",
				    welcome_prefix))
		return error("Could not write client identification");
	for (i = 0; versions[i]; i++) {
		if (packet_write_fmt_gently(process->in, "version=%d\n",
					    versions[i]))
			return error("Could not write requested version");
	}
	if (packet_flush_gently(process->in))
		return error("Could not write flush packet");

	if (!(line = packet_read_line(process->out, NULL)) ||
	    !skip_prefix(line, welcome_prefix, &p) ||
	    strcmp(p, "-server"))
		return error("Unexpected line '%s', expected %s-server",
			     line ? line : "<flush packet>", welcome_prefix);
	if (!(line = packet_read_line(process->out, NULL)) ||
	    !skip_prefix(line, "version=", &p) ||
	    strtol_i(p, 10, chosen_version))
		return error("Unexpected line '%s', expected version",
			     line ? line : "<flush packet>");
	if ((line = packet_read_line(process->out, NULL)))
		return error("Unexpected line '%s', expected flush", line);

	/* Check to make sure that the version received is supported */
	for (i = 0; versions[i]; i++) {
		if (versions[i] == *chosen_version)
			break;
	}
	if (!versions[i])
		return error("Version %d not supported", *chosen_version);

	return 0;
}

static int handshake_capabilities(struct child_process *process,
				  struct subprocess_capability *capabilities,
				  unsigned int *supported_capabilities)
{
	int i;
	char *line;

	for (i = 0; capabilities[i].name; i++) {
		if (packet_write_fmt_gently(process->in, "capability=%s\n",
					    capabilities[i].name))
			return error("Could not write requested capability");
	}
	if (packet_flush_gently(process->in))
		return error("Could not write flush packet");

	while ((line = packet_read_line(process->out, NULL))) {
		const char *p;
		if (!skip_prefix(line, "capability=", &p))
			continue;

		for (i = 0;
		     capabilities[i].name && strcmp(p, capabilities[i].name);
		     i++)
			;
		if (capabilities[i].name) {
			if (supported_capabilities)
				*supported_capabilities |= capabilities[i].flag;
		} else {
			die("subprocess '%s' requested unsupported capability '%s'",
			    process->args.v[0], p);
		}
	}

	return 0;
}

int subprocess_handshake(struct subprocess_entry *entry,
			 const char *welcome_prefix,
			 int *versions,
			 int *chosen_version,
			 struct subprocess_capability *capabilities,
			 unsigned int *supported_capabilities)
{
	int retval;
	struct child_process *process = &entry->process;

	sigchain_push(SIGPIPE, SIG_IGN);

	retval = handshake_version(process, welcome_prefix, versions,
				   chosen_version) ||
		 handshake_capabilities(process, capabilities,
					supported_capabilities);

	sigchain_pop(SIGPIPE);
	return retval;
}
