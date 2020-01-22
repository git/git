#include "cache.h"
#include "config.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "repository.h"
#include "object-store.h"
#include "tag.h"
#include "object.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"
#include "run-command.h"
#include "connect.h"
#include "sigchain.h"
#include "version.h"
#include "string-list.h"
#include "argv-array.h"
#include "prio-queue.h"
#include "protocol.h"
#include "quote.h"
#include "upload-pack.h"
#include "serve.h"
#include "commit-graph.h"
#include "commit-reach.h"

/* Remember to update object flag allocation in object.h */
#define THEY_HAVE	(1u << 11)
#define OUR_REF		(1u << 12)
#define WANTED		(1u << 13)
#define COMMON_KNOWN	(1u << 14)

#define SHALLOW		(1u << 16)
#define NOT_SHALLOW	(1u << 17)
#define CLIENT_SHALLOW	(1u << 18)
#define HIDDEN_REF	(1u << 19)

#define ALL_FLAGS (THEY_HAVE | OUR_REF | WANTED | COMMON_KNOWN | SHALLOW | \
		NOT_SHALLOW | CLIENT_SHALLOW | HIDDEN_REF)

static timestamp_t oldest_have;

static int multi_ack;
static int no_done;
static int use_thin_pack, use_ofs_delta, use_include_tag;
static int no_progress, daemon_mode;
/* Allow specifying sha1 if it is a ref tip. */
#define ALLOW_TIP_SHA1	01
/* Allow request of a sha1 if it is reachable from a ref (possibly hidden ref). */
#define ALLOW_REACHABLE_SHA1	02
/* Allow request of any sha1. Implies ALLOW_TIP_SHA1 and ALLOW_REACHABLE_SHA1. */
#define ALLOW_ANY_SHA1	07
static unsigned int allow_unadvertised_object_request;
static int shallow_nr;
static struct object_array extra_edge_obj;
static unsigned int timeout;
static int keepalive = 5;
/* 0 for no sideband,
 * otherwise maximum packet size (up to 65520 bytes).
 */
static int use_sideband;
static int stateless_rpc;
static const char *pack_objects_hook;

static int filter_capability_requested;
static int allow_filter;
static int allow_ref_in_want;
static struct list_objects_filter_options filter_options;

static int allow_sideband_all;

static void reset_timeout(void)
{
	alarm(timeout);
}

static void send_client_data(int fd, const char *data, ssize_t sz)
{
	if (use_sideband) {
		send_sideband(1, fd, data, sz, use_sideband);
		return;
	}
	if (fd == 3)
		/* emergency quit */
		fd = 2;
	if (fd == 2) {
		/* XXX: are we happy to lose stuff here? */
		xwrite(fd, data, sz);
		return;
	}
	write_or_die(fd, data, sz);
}

static int write_one_shallow(const struct commit_graft *graft, void *cb_data)
{
	FILE *fp = cb_data;
	if (graft->nr_parent == -1)
		fprintf(fp, "--shallow %s\n", oid_to_hex(&graft->oid));
	return 0;
}

static void create_pack_file(const struct object_array *have_obj,
			     const struct object_array *want_obj)
{
	struct child_process pack_objects = CHILD_PROCESS_INIT;
	char data[8193], progress[128];
	char abort_msg[] = "aborting due to possible repository "
		"corruption on the remote side.";
	int buffered = -1;
	ssize_t sz;
	int i;
	FILE *pipe_fd;

	if (!pack_objects_hook)
		pack_objects.git_cmd = 1;
	else {
		argv_array_push(&pack_objects.args, pack_objects_hook);
		argv_array_push(&pack_objects.args, "git");
		pack_objects.use_shell = 1;
	}

	if (shallow_nr) {
		argv_array_push(&pack_objects.args, "--shallow-file");
		argv_array_push(&pack_objects.args, "");
	}
	argv_array_push(&pack_objects.args, "pack-objects");
	argv_array_push(&pack_objects.args, "--revs");
	if (use_thin_pack)
		argv_array_push(&pack_objects.args, "--thin");

	argv_array_push(&pack_objects.args, "--stdout");
	if (shallow_nr)
		argv_array_push(&pack_objects.args, "--shallow");
	if (!no_progress)
		argv_array_push(&pack_objects.args, "--progress");
	if (use_ofs_delta)
		argv_array_push(&pack_objects.args, "--delta-base-offset");
	if (use_include_tag)
		argv_array_push(&pack_objects.args, "--include-tag");
	if (filter_options.choice) {
		const char *spec =
			expand_list_objects_filter_spec(&filter_options);
		if (pack_objects.use_shell) {
			struct strbuf buf = STRBUF_INIT;
			sq_quote_buf(&buf, spec);
			argv_array_pushf(&pack_objects.args, "--filter=%s", buf.buf);
			strbuf_release(&buf);
		} else {
			argv_array_pushf(&pack_objects.args, "--filter=%s",
					 spec);
		}
	}

	pack_objects.in = -1;
	pack_objects.out = -1;
	pack_objects.err = -1;

	if (start_command(&pack_objects))
		die("git upload-pack: unable to fork git-pack-objects");

	pipe_fd = xfdopen(pack_objects.in, "w");

	if (shallow_nr)
		for_each_commit_graft(write_one_shallow, pipe_fd);

	for (i = 0; i < want_obj->nr; i++)
		fprintf(pipe_fd, "%s\n",
			oid_to_hex(&want_obj->objects[i].item->oid));
	fprintf(pipe_fd, "--not\n");
	for (i = 0; i < have_obj->nr; i++)
		fprintf(pipe_fd, "%s\n",
			oid_to_hex(&have_obj->objects[i].item->oid));
	for (i = 0; i < extra_edge_obj.nr; i++)
		fprintf(pipe_fd, "%s\n",
			oid_to_hex(&extra_edge_obj.objects[i].item->oid));
	fprintf(pipe_fd, "\n");
	fflush(pipe_fd);
	fclose(pipe_fd);

	/* We read from pack_objects.err to capture stderr output for
	 * progress bar, and pack_objects.out to capture the pack data.
	 */

	while (1) {
		struct pollfd pfd[2];
		int pe, pu, pollsize;
		int ret;

		reset_timeout();

		pollsize = 0;
		pe = pu = -1;

		if (0 <= pack_objects.out) {
			pfd[pollsize].fd = pack_objects.out;
			pfd[pollsize].events = POLLIN;
			pu = pollsize;
			pollsize++;
		}
		if (0 <= pack_objects.err) {
			pfd[pollsize].fd = pack_objects.err;
			pfd[pollsize].events = POLLIN;
			pe = pollsize;
			pollsize++;
		}

		if (!pollsize)
			break;

		ret = poll(pfd, pollsize,
			keepalive < 0 ? -1 : 1000 * keepalive);

		if (ret < 0) {
			if (errno != EINTR) {
				error_errno("poll failed, resuming");
				sleep(1);
			}
			continue;
		}
		if (0 <= pe && (pfd[pe].revents & (POLLIN|POLLHUP))) {
			/* Status ready; we ship that in the side-band
			 * or dump to the standard error.
			 */
			sz = xread(pack_objects.err, progress,
				  sizeof(progress));
			if (0 < sz)
				send_client_data(2, progress, sz);
			else if (sz == 0) {
				close(pack_objects.err);
				pack_objects.err = -1;
			}
			else
				goto fail;
			/* give priority to status messages */
			continue;
		}
		if (0 <= pu && (pfd[pu].revents & (POLLIN|POLLHUP))) {
			/* Data ready; we keep the last byte to ourselves
			 * in case we detect broken rev-list, so that we
			 * can leave the stream corrupted.  This is
			 * unfortunate -- unpack-objects would happily
			 * accept a valid packdata with trailing garbage,
			 * so appending garbage after we pass all the
			 * pack data is not good enough to signal
			 * breakage to downstream.
			 */
			char *cp = data;
			ssize_t outsz = 0;
			if (0 <= buffered) {
				*cp++ = buffered;
				outsz++;
			}
			sz = xread(pack_objects.out, cp,
				  sizeof(data) - outsz);
			if (0 < sz)
				;
			else if (sz == 0) {
				close(pack_objects.out);
				pack_objects.out = -1;
			}
			else
				goto fail;
			sz += outsz;
			if (1 < sz) {
				buffered = data[sz-1] & 0xFF;
				sz--;
			}
			else
				buffered = -1;
			send_client_data(1, data, sz);
		}

		/*
		 * We hit the keepalive timeout without saying anything; send
		 * an empty message on the data sideband just to let the other
		 * side know we're still working on it, but don't have any data
		 * yet.
		 *
		 * If we don't have a sideband channel, there's no room in the
		 * protocol to say anything, so those clients are just out of
		 * luck.
		 */
		if (!ret && use_sideband) {
			static const char buf[] = "0005\1";
			write_or_die(1, buf, 5);
		}
	}

	if (finish_command(&pack_objects)) {
		error("git upload-pack: git-pack-objects died with error.");
		goto fail;
	}

	/* flush the data */
	if (0 <= buffered) {
		data[0] = buffered;
		send_client_data(1, data, 1);
		fprintf(stderr, "flushed.\n");
	}
	if (use_sideband)
		packet_flush(1);
	return;

 fail:
	send_client_data(3, abort_msg, sizeof(abort_msg));
	die("git upload-pack: %s", abort_msg);
}

static int got_oid(const char *hex, struct object_id *oid,
		   struct object_array *have_obj)
{
	struct object *o;
	int we_knew_they_have = 0;

	if (get_oid_hex(hex, oid))
		die("git upload-pack: expected SHA1 object, got '%s'", hex);
	if (!has_object_file(oid))
		return -1;

	o = parse_object(the_repository, oid);
	if (!o)
		die("oops (%s)", oid_to_hex(oid));
	if (o->type == OBJ_COMMIT) {
		struct commit_list *parents;
		struct commit *commit = (struct commit *)o;
		if (o->flags & THEY_HAVE)
			we_knew_they_have = 1;
		else
			o->flags |= THEY_HAVE;
		if (!oldest_have || (commit->date < oldest_have))
			oldest_have = commit->date;
		for (parents = commit->parents;
		     parents;
		     parents = parents->next)
			parents->item->object.flags |= THEY_HAVE;
	}
	if (!we_knew_they_have) {
		add_object_array(o, NULL, have_obj);
		return 1;
	}
	return 0;
}

static int ok_to_give_up(const struct object_array *have_obj,
			 struct object_array *want_obj)
{
	uint32_t min_generation = GENERATION_NUMBER_ZERO;

	if (!have_obj->nr)
		return 0;

	return can_all_from_reach_with_flag(want_obj, THEY_HAVE,
					    COMMON_KNOWN, oldest_have,
					    min_generation);
}

static int get_common_commits(struct packet_reader *reader,
			      struct object_array *have_obj,
			      struct object_array *want_obj)
{
	struct object_id oid;
	char last_hex[GIT_MAX_HEXSZ + 1];
	int got_common = 0;
	int got_other = 0;
	int sent_ready = 0;

	save_commit_buffer = 0;

	for (;;) {
		const char *arg;

		reset_timeout();

		if (packet_reader_read(reader) != PACKET_READ_NORMAL) {
			if (multi_ack == 2 && got_common
			    && !got_other && ok_to_give_up(have_obj, want_obj)) {
				sent_ready = 1;
				packet_write_fmt(1, "ACK %s ready\n", last_hex);
			}
			if (have_obj->nr == 0 || multi_ack)
				packet_write_fmt(1, "NAK\n");

			if (no_done && sent_ready) {
				packet_write_fmt(1, "ACK %s\n", last_hex);
				return 0;
			}
			if (stateless_rpc)
				exit(0);
			got_common = 0;
			got_other = 0;
			continue;
		}
		if (skip_prefix(reader->line, "have ", &arg)) {
			switch (got_oid(arg, &oid, have_obj)) {
			case -1: /* they have what we do not */
				got_other = 1;
				if (multi_ack && ok_to_give_up(have_obj, want_obj)) {
					const char *hex = oid_to_hex(&oid);
					if (multi_ack == 2) {
						sent_ready = 1;
						packet_write_fmt(1, "ACK %s ready\n", hex);
					} else
						packet_write_fmt(1, "ACK %s continue\n", hex);
				}
				break;
			default:
				got_common = 1;
				oid_to_hex_r(last_hex, &oid);
				if (multi_ack == 2)
					packet_write_fmt(1, "ACK %s common\n", last_hex);
				else if (multi_ack)
					packet_write_fmt(1, "ACK %s continue\n", last_hex);
				else if (have_obj->nr == 1)
					packet_write_fmt(1, "ACK %s\n", last_hex);
				break;
			}
			continue;
		}
		if (!strcmp(reader->line, "done")) {
			if (have_obj->nr > 0) {
				if (multi_ack)
					packet_write_fmt(1, "ACK %s\n", last_hex);
				return 0;
			}
			packet_write_fmt(1, "NAK\n");
			return -1;
		}
		die("git upload-pack: expected SHA1 list, got '%s'", reader->line);
	}
}

static int is_our_ref(struct object *o)
{
	int allow_hidden_ref = (allow_unadvertised_object_request &
			(ALLOW_TIP_SHA1 | ALLOW_REACHABLE_SHA1));
	return o->flags & ((allow_hidden_ref ? HIDDEN_REF : 0) | OUR_REF);
}

/*
 * on successful case, it's up to the caller to close cmd->out
 */
static int do_reachable_revlist(struct child_process *cmd,
				struct object_array *src,
				struct object_array *reachable)
{
	static const char *argv[] = {
		"rev-list", "--stdin", NULL,
	};
	struct object *o;
	char namebuf[GIT_MAX_HEXSZ + 2]; /* ^ + hash + LF */
	int i;
	const unsigned hexsz = the_hash_algo->hexsz;

	cmd->argv = argv;
	cmd->git_cmd = 1;
	cmd->no_stderr = 1;
	cmd->in = -1;
	cmd->out = -1;

	/*
	 * If the next rev-list --stdin encounters an unknown commit,
	 * it terminates, which will cause SIGPIPE in the write loop
	 * below.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);

	if (start_command(cmd))
		goto error;

	namebuf[0] = '^';
	namebuf[hexsz + 1] = '\n';
	for (i = get_max_object_index(); 0 < i; ) {
		o = get_indexed_object(--i);
		if (!o)
			continue;
		if (reachable && o->type == OBJ_COMMIT)
			o->flags &= ~TMP_MARK;
		if (!is_our_ref(o))
			continue;
		memcpy(namebuf + 1, oid_to_hex(&o->oid), hexsz);
		if (write_in_full(cmd->in, namebuf, hexsz + 2) < 0)
			goto error;
	}
	namebuf[hexsz] = '\n';
	for (i = 0; i < src->nr; i++) {
		o = src->objects[i].item;
		if (is_our_ref(o)) {
			if (reachable)
				add_object_array(o, NULL, reachable);
			continue;
		}
		if (reachable && o->type == OBJ_COMMIT)
			o->flags |= TMP_MARK;
		memcpy(namebuf, oid_to_hex(&o->oid), hexsz);
		if (write_in_full(cmd->in, namebuf, hexsz + 1) < 0)
			goto error;
	}
	close(cmd->in);
	cmd->in = -1;
	sigchain_pop(SIGPIPE);

	return 0;

error:
	sigchain_pop(SIGPIPE);

	if (cmd->in >= 0)
		close(cmd->in);
	if (cmd->out >= 0)
		close(cmd->out);
	return -1;
}

static int get_reachable_list(struct object_array *src,
			      struct object_array *reachable)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int i;
	struct object *o;
	char namebuf[GIT_MAX_HEXSZ + 2]; /* ^ + hash + LF */
	const unsigned hexsz = the_hash_algo->hexsz;

	if (do_reachable_revlist(&cmd, src, reachable) < 0)
		return -1;

	while ((i = read_in_full(cmd.out, namebuf, hexsz + 1)) == hexsz + 1) {
		struct object_id oid;
		const char *p;

		if (parse_oid_hex(namebuf, &oid, &p) || *p != '\n')
			break;

		o = lookup_object(the_repository, &oid);
		if (o && o->type == OBJ_COMMIT) {
			o->flags &= ~TMP_MARK;
		}
	}
	for (i = get_max_object_index(); 0 < i; i--) {
		o = get_indexed_object(i - 1);
		if (o && o->type == OBJ_COMMIT &&
		    (o->flags & TMP_MARK)) {
			add_object_array(o, NULL, reachable);
				o->flags &= ~TMP_MARK;
		}
	}
	close(cmd.out);

	if (finish_command(&cmd))
		return -1;

	return 0;
}

static int has_unreachable(struct object_array *src)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	char buf[1];
	int i;

	if (do_reachable_revlist(&cmd, src, NULL) < 0)
		return 1;

	/*
	 * The commits out of the rev-list are not ancestors of
	 * our ref.
	 */
	i = read_in_full(cmd.out, buf, 1);
	if (i)
		goto error;
	close(cmd.out);
	cmd.out = -1;

	/*
	 * rev-list may have died by encountering a bad commit
	 * in the history, in which case we do want to bail out
	 * even when it showed no commit.
	 */
	if (finish_command(&cmd))
		goto error;

	/* All the non-tip ones are ancestors of what we advertised */
	return 0;

error:
	sigchain_pop(SIGPIPE);
	if (cmd.out >= 0)
		close(cmd.out);
	return 1;
}

static void check_non_tip(struct object_array *want_obj,
			  struct packet_writer *writer)
{
	int i;

	/*
	 * In the normal in-process case without
	 * uploadpack.allowReachableSHA1InWant,
	 * non-tip requests can never happen.
	 */
	if (!stateless_rpc && !(allow_unadvertised_object_request & ALLOW_REACHABLE_SHA1))
		goto error;
	if (!has_unreachable(want_obj))
		/* All the non-tip ones are ancestors of what we advertised */
		return;

error:
	/* Pick one of them (we know there at least is one) */
	for (i = 0; i < want_obj->nr; i++) {
		struct object *o = want_obj->objects[i].item;
		if (!is_our_ref(o)) {
			packet_writer_error(writer,
					    "upload-pack: not our ref %s",
					    oid_to_hex(&o->oid));
			die("git upload-pack: not our ref %s",
			    oid_to_hex(&o->oid));
		}
	}
}

static void send_shallow(struct packet_writer *writer,
			 struct commit_list *result)
{
	while (result) {
		struct object *object = &result->item->object;
		if (!(object->flags & (CLIENT_SHALLOW|NOT_SHALLOW))) {
			packet_writer_write(writer, "shallow %s",
					    oid_to_hex(&object->oid));
			register_shallow(the_repository, &object->oid);
			shallow_nr++;
		}
		result = result->next;
	}
}

static void send_unshallow(struct packet_writer *writer,
			   const struct object_array *shallows,
			   struct object_array *want_obj)
{
	int i;

	for (i = 0; i < shallows->nr; i++) {
		struct object *object = shallows->objects[i].item;
		if (object->flags & NOT_SHALLOW) {
			struct commit_list *parents;
			packet_writer_write(writer, "unshallow %s",
					    oid_to_hex(&object->oid));
			object->flags &= ~CLIENT_SHALLOW;
			/*
			 * We want to _register_ "object" as shallow, but we
			 * also need to traverse object's parents to deepen a
			 * shallow clone. Unregister it for now so we can
			 * parse and add the parents to the want list, then
			 * re-register it.
			 */
			unregister_shallow(&object->oid);
			object->parsed = 0;
			parse_commit_or_die((struct commit *)object);
			parents = ((struct commit *)object)->parents;
			while (parents) {
				add_object_array(&parents->item->object,
						 NULL, want_obj);
				parents = parents->next;
			}
			add_object_array(object, NULL, &extra_edge_obj);
		}
		/* make sure commit traversal conforms to client */
		register_shallow(the_repository, &object->oid);
	}
}

static int check_ref(const char *refname_full, const struct object_id *oid,
		     int flag, void *cb_data);
static void deepen(struct packet_writer *writer, int depth, int deepen_relative,
		   struct object_array *shallows, struct object_array *want_obj)
{
	if (depth == INFINITE_DEPTH && !is_repository_shallow(the_repository)) {
		int i;

		for (i = 0; i < shallows->nr; i++) {
			struct object *object = shallows->objects[i].item;
			object->flags |= NOT_SHALLOW;
		}
	} else if (deepen_relative) {
		struct object_array reachable_shallows = OBJECT_ARRAY_INIT;
		struct commit_list *result;

		/*
		 * Checking for reachable shallows requires that our refs be
		 * marked with OUR_REF.
		 */
		head_ref_namespaced(check_ref, NULL);
		for_each_namespaced_ref(check_ref, NULL);

		get_reachable_list(shallows, &reachable_shallows);
		result = get_shallow_commits(&reachable_shallows,
					     depth + 1,
					     SHALLOW, NOT_SHALLOW);
		send_shallow(writer, result);
		free_commit_list(result);
		object_array_clear(&reachable_shallows);
	} else {
		struct commit_list *result;

		result = get_shallow_commits(want_obj, depth,
					     SHALLOW, NOT_SHALLOW);
		send_shallow(writer, result);
		free_commit_list(result);
	}

	send_unshallow(writer, shallows, want_obj);
}

static void deepen_by_rev_list(struct packet_writer *writer, int ac,
			       const char **av,
			       struct object_array *shallows,
			       struct object_array *want_obj)
{
	struct commit_list *result;

	disable_commit_graph(the_repository);
	result = get_shallow_commits_by_rev_list(ac, av, SHALLOW, NOT_SHALLOW);
	send_shallow(writer, result);
	free_commit_list(result);
	send_unshallow(writer, shallows, want_obj);
}

/* Returns 1 if a shallow list is sent or 0 otherwise */
static int send_shallow_list(struct packet_writer *writer,
			     int depth, int deepen_rev_list,
			     timestamp_t deepen_since,
			     struct string_list *deepen_not,
			     int deepen_relative,
			     struct object_array *shallows,
			     struct object_array *want_obj)
{
	int ret = 0;

	if (depth > 0 && deepen_rev_list)
		die("git upload-pack: deepen and deepen-since (or deepen-not) cannot be used together");
	if (depth > 0) {
		deepen(writer, depth, deepen_relative, shallows, want_obj);
		ret = 1;
	} else if (deepen_rev_list) {
		struct argv_array av = ARGV_ARRAY_INIT;
		int i;

		argv_array_push(&av, "rev-list");
		if (deepen_since)
			argv_array_pushf(&av, "--max-age=%"PRItime, deepen_since);
		if (deepen_not->nr) {
			argv_array_push(&av, "--not");
			for (i = 0; i < deepen_not->nr; i++) {
				struct string_list_item *s = deepen_not->items + i;
				argv_array_push(&av, s->string);
			}
			argv_array_push(&av, "--not");
		}
		for (i = 0; i < want_obj->nr; i++) {
			struct object *o = want_obj->objects[i].item;
			argv_array_push(&av, oid_to_hex(&o->oid));
		}
		deepen_by_rev_list(writer, av.argc, av.argv, shallows, want_obj);
		argv_array_clear(&av);
		ret = 1;
	} else {
		if (shallows->nr > 0) {
			int i;
			for (i = 0; i < shallows->nr; i++)
				register_shallow(the_repository,
						 &shallows->objects[i].item->oid);
		}
	}

	shallow_nr += shallows->nr;
	return ret;
}

static int process_shallow(const char *line, struct object_array *shallows)
{
	const char *arg;
	if (skip_prefix(line, "shallow ", &arg)) {
		struct object_id oid;
		struct object *object;
		if (get_oid_hex(arg, &oid))
			die("invalid shallow line: %s", line);
		object = parse_object(the_repository, &oid);
		if (!object)
			return 1;
		if (object->type != OBJ_COMMIT)
			die("invalid shallow object %s", oid_to_hex(&oid));
		if (!(object->flags & CLIENT_SHALLOW)) {
			object->flags |= CLIENT_SHALLOW;
			add_object_array(object, NULL, shallows);
		}
		return 1;
	}

	return 0;
}

static int process_deepen(const char *line, int *depth)
{
	const char *arg;
	if (skip_prefix(line, "deepen ", &arg)) {
		char *end = NULL;
		*depth = (int)strtol(arg, &end, 0);
		if (!end || *end || *depth <= 0)
			die("Invalid deepen: %s", line);
		return 1;
	}

	return 0;
}

static int process_deepen_since(const char *line, timestamp_t *deepen_since, int *deepen_rev_list)
{
	const char *arg;
	if (skip_prefix(line, "deepen-since ", &arg)) {
		char *end = NULL;
		*deepen_since = parse_timestamp(arg, &end, 0);
		if (!end || *end || !deepen_since ||
		    /* revisions.c's max_age -1 is special */
		    *deepen_since == -1)
			die("Invalid deepen-since: %s", line);
		*deepen_rev_list = 1;
		return 1;
	}
	return 0;
}

static int process_deepen_not(const char *line, struct string_list *deepen_not, int *deepen_rev_list)
{
	const char *arg;
	if (skip_prefix(line, "deepen-not ", &arg)) {
		char *ref = NULL;
		struct object_id oid;
		if (expand_ref(the_repository, arg, strlen(arg), &oid, &ref) != 1)
			die("git upload-pack: ambiguous deepen-not: %s", line);
		string_list_append(deepen_not, ref);
		free(ref);
		*deepen_rev_list = 1;
		return 1;
	}
	return 0;
}

static void receive_needs(struct packet_reader *reader, struct object_array *want_obj)
{
	struct object_array shallows = OBJECT_ARRAY_INIT;
	struct string_list deepen_not = STRING_LIST_INIT_DUP;
	int depth = 0;
	int has_non_tip = 0;
	timestamp_t deepen_since = 0;
	int deepen_rev_list = 0;
	int deepen_relative = 0;
	struct packet_writer writer;

	shallow_nr = 0;
	packet_writer_init(&writer, 1);
	for (;;) {
		struct object *o;
		const char *features;
		struct object_id oid_buf;
		const char *arg;

		reset_timeout();
		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		if (process_shallow(reader->line, &shallows))
			continue;
		if (process_deepen(reader->line, &depth))
			continue;
		if (process_deepen_since(reader->line, &deepen_since, &deepen_rev_list))
			continue;
		if (process_deepen_not(reader->line, &deepen_not, &deepen_rev_list))
			continue;

		if (skip_prefix(reader->line, "filter ", &arg)) {
			if (!filter_capability_requested)
				die("git upload-pack: filtering capability not negotiated");
			list_objects_filter_die_if_populated(&filter_options);
			parse_list_objects_filter(&filter_options, arg);
			continue;
		}

		if (!skip_prefix(reader->line, "want ", &arg) ||
		    parse_oid_hex(arg, &oid_buf, &features))
			die("git upload-pack: protocol error, "
			    "expected to get object ID, not '%s'", reader->line);

		if (parse_feature_request(features, "deepen-relative"))
			deepen_relative = 1;
		if (parse_feature_request(features, "multi_ack_detailed"))
			multi_ack = 2;
		else if (parse_feature_request(features, "multi_ack"))
			multi_ack = 1;
		if (parse_feature_request(features, "no-done"))
			no_done = 1;
		if (parse_feature_request(features, "thin-pack"))
			use_thin_pack = 1;
		if (parse_feature_request(features, "ofs-delta"))
			use_ofs_delta = 1;
		if (parse_feature_request(features, "side-band-64k"))
			use_sideband = LARGE_PACKET_MAX;
		else if (parse_feature_request(features, "side-band"))
			use_sideband = DEFAULT_PACKET_MAX;
		if (parse_feature_request(features, "no-progress"))
			no_progress = 1;
		if (parse_feature_request(features, "include-tag"))
			use_include_tag = 1;
		if (allow_filter && parse_feature_request(features, "filter"))
			filter_capability_requested = 1;

		o = parse_object(the_repository, &oid_buf);
		if (!o) {
			packet_writer_error(&writer,
					    "upload-pack: not our ref %s",
					    oid_to_hex(&oid_buf));
			die("git upload-pack: not our ref %s",
			    oid_to_hex(&oid_buf));
		}
		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			if (!((allow_unadvertised_object_request & ALLOW_ANY_SHA1) == ALLOW_ANY_SHA1
			      || is_our_ref(o)))
				has_non_tip = 1;
			add_object_array(o, NULL, want_obj);
		}
	}

	/*
	 * We have sent all our refs already, and the other end
	 * should have chosen out of them. When we are operating
	 * in the stateless RPC mode, however, their choice may
	 * have been based on the set of older refs advertised
	 * by another process that handled the initial request.
	 */
	if (has_non_tip)
		check_non_tip(want_obj, &writer);

	if (!use_sideband && daemon_mode)
		no_progress = 1;

	if (depth == 0 && !deepen_rev_list && shallows.nr == 0)
		return;

	if (send_shallow_list(&writer, depth, deepen_rev_list, deepen_since,
			      &deepen_not, deepen_relative, &shallows,
			      want_obj))
		packet_flush(1);
	object_array_clear(&shallows);
}

/* return non-zero if the ref is hidden, otherwise 0 */
static int mark_our_ref(const char *refname, const char *refname_full,
			const struct object_id *oid)
{
	struct object *o = lookup_unknown_object(oid);

	if (ref_is_hidden(refname, refname_full)) {
		o->flags |= HIDDEN_REF;
		return 1;
	}
	o->flags |= OUR_REF;
	return 0;
}

static int check_ref(const char *refname_full, const struct object_id *oid,
		     int flag, void *cb_data)
{
	const char *refname = strip_namespace(refname_full);

	mark_our_ref(refname, refname_full, oid);
	return 0;
}

static void format_symref_info(struct strbuf *buf, struct string_list *symref)
{
	struct string_list_item *item;

	if (!symref->nr)
		return;
	for_each_string_list_item(item, symref)
		strbuf_addf(buf, " symref=%s:%s", item->string, (char *)item->util);
}

static int send_ref(const char *refname, const struct object_id *oid,
		    int flag, void *cb_data)
{
	static const char *capabilities = "multi_ack thin-pack side-band"
		" side-band-64k ofs-delta shallow deepen-since deepen-not"
		" deepen-relative no-progress include-tag multi_ack_detailed";
	const char *refname_nons = strip_namespace(refname);
	struct object_id peeled;

	if (mark_our_ref(refname_nons, refname, oid))
		return 0;

	if (capabilities) {
		struct strbuf symref_info = STRBUF_INIT;

		format_symref_info(&symref_info, cb_data);
		packet_write_fmt(1, "%s %s%c%s%s%s%s%s%s agent=%s\n",
			     oid_to_hex(oid), refname_nons,
			     0, capabilities,
			     (allow_unadvertised_object_request & ALLOW_TIP_SHA1) ?
				     " allow-tip-sha1-in-want" : "",
			     (allow_unadvertised_object_request & ALLOW_REACHABLE_SHA1) ?
				     " allow-reachable-sha1-in-want" : "",
			     stateless_rpc ? " no-done" : "",
			     symref_info.buf,
			     allow_filter ? " filter" : "",
			     git_user_agent_sanitized());
		strbuf_release(&symref_info);
	} else {
		packet_write_fmt(1, "%s %s\n", oid_to_hex(oid), refname_nons);
	}
	capabilities = NULL;
	if (!peel_ref(refname, &peeled))
		packet_write_fmt(1, "%s %s^{}\n", oid_to_hex(&peeled), refname_nons);
	return 0;
}

static int find_symref(const char *refname, const struct object_id *oid,
		       int flag, void *cb_data)
{
	const char *symref_target;
	struct string_list_item *item;

	if ((flag & REF_ISSYMREF) == 0)
		return 0;
	symref_target = resolve_ref_unsafe(refname, 0, NULL, &flag);
	if (!symref_target || (flag & REF_ISSYMREF) == 0)
		die("'%s' is a symref but it is not?", refname);
	item = string_list_append(cb_data, strip_namespace(refname));
	item->util = xstrdup(strip_namespace(symref_target));
	return 0;
}

static int upload_pack_config(const char *var, const char *value, void *unused)
{
	if (!strcmp("uploadpack.allowtipsha1inwant", var)) {
		if (git_config_bool(var, value))
			allow_unadvertised_object_request |= ALLOW_TIP_SHA1;
		else
			allow_unadvertised_object_request &= ~ALLOW_TIP_SHA1;
	} else if (!strcmp("uploadpack.allowreachablesha1inwant", var)) {
		if (git_config_bool(var, value))
			allow_unadvertised_object_request |= ALLOW_REACHABLE_SHA1;
		else
			allow_unadvertised_object_request &= ~ALLOW_REACHABLE_SHA1;
	} else if (!strcmp("uploadpack.allowanysha1inwant", var)) {
		if (git_config_bool(var, value))
			allow_unadvertised_object_request |= ALLOW_ANY_SHA1;
		else
			allow_unadvertised_object_request &= ~ALLOW_ANY_SHA1;
	} else if (!strcmp("uploadpack.keepalive", var)) {
		keepalive = git_config_int(var, value);
		if (!keepalive)
			keepalive = -1;
	} else if (!strcmp("uploadpack.allowfilter", var)) {
		allow_filter = git_config_bool(var, value);
	} else if (!strcmp("uploadpack.allowrefinwant", var)) {
		allow_ref_in_want = git_config_bool(var, value);
	} else if (!strcmp("uploadpack.allowsidebandall", var)) {
		allow_sideband_all = git_config_bool(var, value);
	} else if (!strcmp("core.precomposeunicode", var)) {
		precomposed_unicode = git_config_bool(var, value);
	}

	if (current_config_scope() != CONFIG_SCOPE_REPO) {
		if (!strcmp("uploadpack.packobjectshook", var))
			return git_config_string(&pack_objects_hook, var, value);
	}

	return parse_hide_refs_config(var, value, "uploadpack");
}

void upload_pack(struct upload_pack_options *options)
{
	struct string_list symref = STRING_LIST_INIT_DUP;
	struct object_array want_obj = OBJECT_ARRAY_INIT;
	struct packet_reader reader;

	stateless_rpc = options->stateless_rpc;
	timeout = options->timeout;
	daemon_mode = options->daemon_mode;

	git_config(upload_pack_config, NULL);

	head_ref_namespaced(find_symref, &symref);

	if (options->advertise_refs || !stateless_rpc) {
		reset_timeout();
		head_ref_namespaced(send_ref, &symref);
		for_each_namespaced_ref(send_ref, &symref);
		advertise_shallow_grafts(1);
		packet_flush(1);
	} else {
		head_ref_namespaced(check_ref, NULL);
		for_each_namespaced_ref(check_ref, NULL);
	}
	string_list_clear(&symref, 1);
	if (options->advertise_refs)
		return;

	packet_reader_init(&reader, 0, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	receive_needs(&reader, &want_obj);
	if (want_obj.nr) {
		struct object_array have_obj = OBJECT_ARRAY_INIT;
		get_common_commits(&reader, &have_obj, &want_obj);
		create_pack_file(&have_obj, &want_obj);
	}
}

struct upload_pack_data {
	struct object_array wants;
	struct string_list wanted_refs;
	struct oid_array haves;

	struct object_array shallows;
	struct string_list deepen_not;
	int depth;
	timestamp_t deepen_since;
	int deepen_rev_list;
	int deepen_relative;

	struct packet_writer writer;

	unsigned stateless_rpc : 1;

	unsigned use_thin_pack : 1;
	unsigned use_ofs_delta : 1;
	unsigned no_progress : 1;
	unsigned use_include_tag : 1;
	unsigned done : 1;
};

static void upload_pack_data_init(struct upload_pack_data *data)
{
	struct object_array wants = OBJECT_ARRAY_INIT;
	struct string_list wanted_refs = STRING_LIST_INIT_DUP;
	struct oid_array haves = OID_ARRAY_INIT;
	struct object_array shallows = OBJECT_ARRAY_INIT;
	struct string_list deepen_not = STRING_LIST_INIT_DUP;

	memset(data, 0, sizeof(*data));
	data->wants = wants;
	data->wanted_refs = wanted_refs;
	data->haves = haves;
	data->shallows = shallows;
	data->deepen_not = deepen_not;
	packet_writer_init(&data->writer, 1);
}

static void upload_pack_data_clear(struct upload_pack_data *data)
{
	object_array_clear(&data->wants);
	string_list_clear(&data->wanted_refs, 1);
	oid_array_clear(&data->haves);
	object_array_clear(&data->shallows);
	string_list_clear(&data->deepen_not, 0);
}

static int parse_want(struct packet_writer *writer, const char *line,
		      struct object_array *want_obj)
{
	const char *arg;
	if (skip_prefix(line, "want ", &arg)) {
		struct object_id oid;
		struct object *o;

		if (get_oid_hex(arg, &oid))
			die("git upload-pack: protocol error, "
			    "expected to get oid, not '%s'", line);

		o = parse_object(the_repository, &oid);
		if (!o) {
			packet_writer_error(writer,
					    "upload-pack: not our ref %s",
					    oid_to_hex(&oid));
			die("git upload-pack: not our ref %s",
			    oid_to_hex(&oid));
		}

		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			add_object_array(o, NULL, want_obj);
		}

		return 1;
	}

	return 0;
}

static int parse_want_ref(struct packet_writer *writer, const char *line,
			  struct string_list *wanted_refs,
			  struct object_array *want_obj)
{
	const char *arg;
	if (skip_prefix(line, "want-ref ", &arg)) {
		struct object_id oid;
		struct string_list_item *item;
		struct object *o;

		if (read_ref(arg, &oid)) {
			packet_writer_error(writer, "unknown ref %s", arg);
			die("unknown ref %s", arg);
		}

		item = string_list_append(wanted_refs, arg);
		item->util = oiddup(&oid);

		o = parse_object_or_die(&oid, arg);
		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			add_object_array(o, NULL, want_obj);
		}

		return 1;
	}

	return 0;
}

static int parse_have(const char *line, struct oid_array *haves)
{
	const char *arg;
	if (skip_prefix(line, "have ", &arg)) {
		struct object_id oid;

		if (get_oid_hex(arg, &oid))
			die("git upload-pack: expected SHA1 object, got '%s'", arg);
		oid_array_append(haves, &oid);
		return 1;
	}

	return 0;
}

static void process_args(struct packet_reader *request,
			 struct upload_pack_data *data,
			 struct object_array *want_obj)
{
	while (packet_reader_read(request) != PACKET_READ_FLUSH) {
		const char *arg = request->line;
		const char *p;

		/* process want */
		if (parse_want(&data->writer, arg, want_obj))
			continue;
		if (allow_ref_in_want &&
		    parse_want_ref(&data->writer, arg, &data->wanted_refs,
				   want_obj))
			continue;
		/* process have line */
		if (parse_have(arg, &data->haves))
			continue;

		/* process args like thin-pack */
		if (!strcmp(arg, "thin-pack")) {
			use_thin_pack = 1;
			continue;
		}
		if (!strcmp(arg, "ofs-delta")) {
			use_ofs_delta = 1;
			continue;
		}
		if (!strcmp(arg, "no-progress")) {
			no_progress = 1;
			continue;
		}
		if (!strcmp(arg, "include-tag")) {
			use_include_tag = 1;
			continue;
		}
		if (!strcmp(arg, "done")) {
			data->done = 1;
			continue;
		}

		/* Shallow related arguments */
		if (process_shallow(arg, &data->shallows))
			continue;
		if (process_deepen(arg, &data->depth))
			continue;
		if (process_deepen_since(arg, &data->deepen_since,
					 &data->deepen_rev_list))
			continue;
		if (process_deepen_not(arg, &data->deepen_not,
				       &data->deepen_rev_list))
			continue;
		if (!strcmp(arg, "deepen-relative")) {
			data->deepen_relative = 1;
			continue;
		}

		if (allow_filter && skip_prefix(arg, "filter ", &p)) {
			list_objects_filter_die_if_populated(&filter_options);
			parse_list_objects_filter(&filter_options, p);
			continue;
		}

		if ((git_env_bool("GIT_TEST_SIDEBAND_ALL", 0) ||
		     allow_sideband_all) &&
		    !strcmp(arg, "sideband-all")) {
			data->writer.use_sideband = 1;
			continue;
		}

		/* ignore unknown lines maybe? */
		die("unexpected line: '%s'", arg);
	}
}

static int process_haves(struct oid_array *haves, struct oid_array *common,
			 struct object_array *have_obj)
{
	int i;

	/* Process haves */
	for (i = 0; i < haves->nr; i++) {
		const struct object_id *oid = &haves->oid[i];
		struct object *o;
		int we_knew_they_have = 0;

		if (!has_object_file(oid))
			continue;

		oid_array_append(common, oid);

		o = parse_object(the_repository, oid);
		if (!o)
			die("oops (%s)", oid_to_hex(oid));
		if (o->type == OBJ_COMMIT) {
			struct commit_list *parents;
			struct commit *commit = (struct commit *)o;
			if (o->flags & THEY_HAVE)
				we_knew_they_have = 1;
			else
				o->flags |= THEY_HAVE;
			if (!oldest_have || (commit->date < oldest_have))
				oldest_have = commit->date;
			for (parents = commit->parents;
			     parents;
			     parents = parents->next)
				parents->item->object.flags |= THEY_HAVE;
		}
		if (!we_knew_they_have)
			add_object_array(o, NULL, have_obj);
	}

	return 0;
}

static int send_acks(struct packet_writer *writer, struct oid_array *acks,
		     const struct object_array *have_obj,
		     struct object_array *want_obj)
{
	int i;

	packet_writer_write(writer, "acknowledgments\n");

	/* Send Acks */
	if (!acks->nr)
		packet_writer_write(writer, "NAK\n");

	for (i = 0; i < acks->nr; i++) {
		packet_writer_write(writer, "ACK %s\n",
				    oid_to_hex(&acks->oid[i]));
	}

	if (ok_to_give_up(have_obj, want_obj)) {
		/* Send Ready */
		packet_writer_write(writer, "ready\n");
		return 1;
	}

	return 0;
}

static int process_haves_and_send_acks(struct upload_pack_data *data,
				       struct object_array *have_obj,
				       struct object_array *want_obj)
{
	struct oid_array common = OID_ARRAY_INIT;
	int ret = 0;

	process_haves(&data->haves, &common, have_obj);
	if (data->done) {
		ret = 1;
	} else if (send_acks(&data->writer, &common, have_obj, want_obj)) {
		packet_writer_delim(&data->writer);
		ret = 1;
	} else {
		/* Add Flush */
		packet_writer_flush(&data->writer);
		ret = 0;
	}

	oid_array_clear(&data->haves);
	oid_array_clear(&common);
	return ret;
}

static void send_wanted_ref_info(struct upload_pack_data *data)
{
	const struct string_list_item *item;

	if (!data->wanted_refs.nr)
		return;

	packet_writer_write(&data->writer, "wanted-refs\n");

	for_each_string_list_item(item, &data->wanted_refs) {
		packet_writer_write(&data->writer, "%s %s\n",
				    oid_to_hex(item->util),
				    item->string);
	}

	packet_writer_delim(&data->writer);
}

static void send_shallow_info(struct upload_pack_data *data,
			      struct object_array *want_obj)
{
	/* No shallow info needs to be sent */
	if (!data->depth && !data->deepen_rev_list && !data->shallows.nr &&
	    !is_repository_shallow(the_repository))
		return;

	packet_writer_write(&data->writer, "shallow-info\n");

	if (!send_shallow_list(&data->writer, data->depth,
			       data->deepen_rev_list,
			       data->deepen_since, &data->deepen_not,
			       data->deepen_relative,
			       &data->shallows, want_obj) &&
	    is_repository_shallow(the_repository))
		deepen(&data->writer, INFINITE_DEPTH, data->deepen_relative,
		       &data->shallows, want_obj);

	packet_delim(1);
}

enum fetch_state {
	FETCH_PROCESS_ARGS = 0,
	FETCH_SEND_ACKS,
	FETCH_SEND_PACK,
	FETCH_DONE,
};

int upload_pack_v2(struct repository *r, struct argv_array *keys,
		   struct packet_reader *request)
{
	enum fetch_state state = FETCH_PROCESS_ARGS;
	struct upload_pack_data data;
	struct object_array have_obj = OBJECT_ARRAY_INIT;
	struct object_array want_obj = OBJECT_ARRAY_INIT;

	clear_object_flags(ALL_FLAGS);

	git_config(upload_pack_config, NULL);

	upload_pack_data_init(&data);
	use_sideband = LARGE_PACKET_MAX;

	while (state != FETCH_DONE) {
		switch (state) {
		case FETCH_PROCESS_ARGS:
			process_args(request, &data, &want_obj);

			if (!want_obj.nr) {
				/*
				 * Request didn't contain any 'want' lines,
				 * guess they didn't want anything.
				 */
				state = FETCH_DONE;
			} else if (data.haves.nr) {
				/*
				 * Request had 'have' lines, so lets ACK them.
				 */
				state = FETCH_SEND_ACKS;
			} else {
				/*
				 * Request had 'want's but no 'have's so we can
				 * immedietly go to construct and send a pack.
				 */
				state = FETCH_SEND_PACK;
			}
			break;
		case FETCH_SEND_ACKS:
			if (process_haves_and_send_acks(&data, &have_obj,
							&want_obj))
				state = FETCH_SEND_PACK;
			else
				state = FETCH_DONE;
			break;
		case FETCH_SEND_PACK:
			send_wanted_ref_info(&data);
			send_shallow_info(&data, &want_obj);

			packet_writer_write(&data.writer, "packfile\n");
			create_pack_file(&have_obj, &want_obj);
			state = FETCH_DONE;
			break;
		case FETCH_DONE:
			continue;
		}
	}

	upload_pack_data_clear(&data);
	object_array_clear(&have_obj);
	object_array_clear(&want_obj);
	return 0;
}

int upload_pack_advertise(struct repository *r,
			  struct strbuf *value)
{
	if (value) {
		int allow_filter_value;
		int allow_ref_in_want;
		int allow_sideband_all_value;

		strbuf_addstr(value, "shallow");

		if (!repo_config_get_bool(the_repository,
					 "uploadpack.allowfilter",
					 &allow_filter_value) &&
		    allow_filter_value)
			strbuf_addstr(value, " filter");

		if (!repo_config_get_bool(the_repository,
					 "uploadpack.allowrefinwant",
					 &allow_ref_in_want) &&
		    allow_ref_in_want)
			strbuf_addstr(value, " ref-in-want");

		if (git_env_bool("GIT_TEST_SIDEBAND_ALL", 0) ||
		    (!repo_config_get_bool(the_repository,
					   "uploadpack.allowsidebandall",
					   &allow_sideband_all_value) &&
		     allow_sideband_all_value))
			strbuf_addstr(value, " sideband-all");
	}

	return 1;
}
