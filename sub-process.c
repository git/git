/*
 * Generic implementation of background process infrastructure.
 */
#include "sub-process.h"
#include "sigchain.h"
#include "pkt-line.h"

static int subprocess_map_initialized;
static struct hashmap subprocess_map;

static int name2process_cmp(const struct subprocess_entry *e1,
		const struct subprocess_entry *e2, const void *unused)
{
	return strcmp(e1->cmd, e2->cmd);
}

static void subprocess_exit_handler(struct child_process *process)
{
	sigchain_push(SIGPIPE, SIG_IGN);
	/* Closing the pipe signals the filter to initiate a shutdown. */
	close(process->in);
	close(process->out);
	sigchain_pop(SIGPIPE);
	/* Finish command will wait until the shutdown is complete. */
	finish_command(process);
}

int subprocess_start(struct subprocess_entry *entry, const char *cmd,
		subprocess_start_fn startfn)
{
	int err;
	const char *argv[] = { cmd, NULL };

	if (!subprocess_map_initialized) {
		hashmap_init(&subprocess_map, (hashmap_cmp_fn)name2process_cmp, 0);
		subprocess_map_initialized = 1;
	}

	entry->cmd = cmd;

	child_process_init(&entry->process);
	entry->process.argv = argv;
	entry->process.use_shell = 1;
	entry->process.in = -1;
	entry->process.out = -1;
	entry->process.clean_on_exit = 1;
	entry->process.clean_on_exit_handler = subprocess_exit_handler;

	err = start_command(&entry->process);
	if (err) {
		error("cannot fork to run sub-process '%s'", entry->cmd);
		return err;
	}

	err = startfn(entry);
	if (err) {
		error("initialization for sub-process '%s' failed", entry->cmd);
		subprocess_stop(entry);
		return err;
	}

	hashmap_entry_init(entry, strhash(entry->cmd));
	hashmap_add(&subprocess_map, entry);

	return 0;
}

void subprocess_stop(struct subprocess_entry *entry)
{
	if (!entry)
		return;

	entry->process.clean_on_exit = 0;
	kill(entry->process.pid, SIGTERM);
	finish_command(&entry->process);

	hashmap_remove(&subprocess_map, entry, NULL);
}

struct subprocess_entry *subprocess_find_entry(const char *cmd)
{
	struct subprocess_entry key;

	if (!subprocess_map_initialized) {
		hashmap_init(&subprocess_map, (hashmap_cmp_fn)name2process_cmp, 0);
		subprocess_map_initialized = 1;
		return NULL;
	}

	hashmap_entry_init(&key, strhash(cmd));
	key.cmd = cmd;
	return hashmap_get(&subprocess_map, &key, NULL);
}

void subprocess_read_status(int fd, struct strbuf *status)
{
	struct strbuf **pair;
	char *line;
	for (;;) {
		line = packet_read_line(fd, NULL);
		if (!line)
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
}
