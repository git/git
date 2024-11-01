#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "repository.h"
#include "object-store-ll.h"
#include "oid-array.h"
#include "object.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects-filter-options.h"
#include "run-command.h"
#include "connect.h"
#include "sigchain.h"
#include "version.h"
#include "string-list.h"
#include "strvec.h"
#include "trace2.h"
#include "protocol.h"
#include "upload-pack.h"
#include "commit-graph.h"
#include "commit-reach.h"
#include "shallow.h"
#include "write-or-die.h"
#include "json-writer.h"
#include "strmap.h"
#include "promisor-remote.h"

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

/* Enum for allowed unadvertised object request (UOR) */
enum allow_uor {
	/* Allow specifying sha1 if it is a ref tip. */
	ALLOW_TIP_SHA1 = 0x01,
	/* Allow request of a sha1 if it is reachable from a ref (possibly hidden ref). */
	ALLOW_REACHABLE_SHA1 = 0x02,
	/* Allow request of any sha1. Implies ALLOW_TIP_SHA1 and ALLOW_REACHABLE_SHA1. */
	ALLOW_ANY_SHA1 = 0x07
};

/*
 * Please annotate, and if possible group together, fields used only
 * for protocol v0 or only for protocol v2.
 */
struct upload_pack_data {
	struct string_list symref;				/* v0 only */
	struct object_array want_obj;
	struct object_array have_obj;
	struct strmap wanted_refs;				/* v2 only */
	struct strvec hidden_refs;

	struct object_array shallows;
	struct oidset deepen_not;
	struct object_array extra_edge_obj;
	int depth;
	timestamp_t deepen_since;
	int deepen_rev_list;
	int deepen_relative;
	int keepalive;
	int shallow_nr;
	timestamp_t oldest_have;

	unsigned int timeout;					/* v0 only */
	enum {
		NO_MULTI_ACK = 0,
		MULTI_ACK = 1,
		MULTI_ACK_DETAILED = 2
	} multi_ack;						/* v0 only */

	/* 0 for no sideband, otherwise DEFAULT_PACKET_MAX or LARGE_PACKET_MAX */
	int use_sideband;

	struct string_list uri_protocols;
	enum allow_uor allow_uor;

	struct list_objects_filter_options filter_options;
	struct string_list allowed_filters;

	struct packet_writer writer;

	char *pack_objects_hook;

	unsigned stateless_rpc : 1;				/* v0 only */
	unsigned no_done : 1;					/* v0 only */
	unsigned daemon_mode : 1;				/* v0 only */
	unsigned filter_capability_requested : 1;		/* v0 only */

	unsigned use_thin_pack : 1;
	unsigned use_ofs_delta : 1;
	unsigned no_progress : 1;
	unsigned use_include_tag : 1;
	unsigned wait_for_done : 1;
	unsigned allow_filter : 1;
	unsigned allow_filter_fallback : 1;
	unsigned long tree_filter_max_depth;

	unsigned done : 1;					/* v2 only */
	unsigned allow_ref_in_want : 1;				/* v2 only */
	unsigned allow_sideband_all : 1;			/* v2 only */
	unsigned seen_haves : 1;				/* v2 only */
	unsigned allow_packfile_uris : 1;			/* v2 only */
	unsigned advertise_sid : 1;
	unsigned sent_capabilities : 1;
};

static void upload_pack_data_init(struct upload_pack_data *data)
{
	struct string_list symref = STRING_LIST_INIT_DUP;
	struct strmap wanted_refs = STRMAP_INIT;
	struct strvec hidden_refs = STRVEC_INIT;
	struct object_array want_obj = OBJECT_ARRAY_INIT;
	struct object_array have_obj = OBJECT_ARRAY_INIT;
	struct object_array shallows = OBJECT_ARRAY_INIT;
	struct oidset deepen_not = OID_ARRAY_INIT;
	struct string_list uri_protocols = STRING_LIST_INIT_DUP;
	struct object_array extra_edge_obj = OBJECT_ARRAY_INIT;
	struct string_list allowed_filters = STRING_LIST_INIT_DUP;

	memset(data, 0, sizeof(*data));
	data->symref = symref;
	data->wanted_refs = wanted_refs;
	data->hidden_refs = hidden_refs;
	data->want_obj = want_obj;
	data->have_obj = have_obj;
	data->shallows = shallows;
	data->deepen_not = deepen_not;
	data->uri_protocols = uri_protocols;
	data->extra_edge_obj = extra_edge_obj;
	data->allowed_filters = allowed_filters;
	data->allow_filter_fallback = 1;
	data->tree_filter_max_depth = ULONG_MAX;
	packet_writer_init(&data->writer, 1);
	list_objects_filter_init(&data->filter_options);

	data->keepalive = 5;
	data->advertise_sid = 0;
}

static void upload_pack_data_clear(struct upload_pack_data *data)
{
	string_list_clear(&data->symref, 1);
	strmap_clear(&data->wanted_refs, 1);
	strvec_clear(&data->hidden_refs);
	object_array_clear(&data->want_obj);
	object_array_clear(&data->have_obj);
	object_array_clear(&data->shallows);
	oidset_clear(&data->deepen_not);
	object_array_clear(&data->extra_edge_obj);
	list_objects_filter_release(&data->filter_options);
	string_list_clear(&data->allowed_filters, 0);
	string_list_clear(&data->uri_protocols, 0);

	free((char *)data->pack_objects_hook);
}

static void reset_timeout(unsigned int timeout)
{
	alarm(timeout);
}

static void send_client_data(int fd, const char *data, ssize_t sz,
			     int use_sideband)
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

struct output_state {
	/*
	 * We do writes no bigger than LARGE_PACKET_DATA_MAX - 1, because with
	 * sideband-64k the band designator takes up 1 byte of space. Because
	 * relay_pack_data keeps the last byte to itself, we make the buffer 1
	 * byte bigger than the intended maximum write size.
	 */
	char buffer[(LARGE_PACKET_DATA_MAX - 1) + 1];
	int used;
	unsigned packfile_uris_started : 1;
	unsigned packfile_started : 1;
};

static int relay_pack_data(int pack_objects_out, struct output_state *os,
			   int use_sideband, int write_packfile_line)
{
	/*
	 * We keep the last byte to ourselves
	 * in case we detect broken rev-list, so that we
	 * can leave the stream corrupted.  This is
	 * unfortunate -- unpack-objects would happily
	 * accept a valid packdata with trailing garbage,
	 * so appending garbage after we pass all the
	 * pack data is not good enough to signal
	 * breakage to downstream.
	 */
	ssize_t readsz;

	readsz = xread(pack_objects_out, os->buffer + os->used,
		       sizeof(os->buffer) - os->used);
	if (readsz < 0) {
		return readsz;
	}
	os->used += readsz;

	while (!os->packfile_started) {
		char *p;
		if (os->used >= 4 && !memcmp(os->buffer, "PACK", 4)) {
			os->packfile_started = 1;
			if (write_packfile_line) {
				if (os->packfile_uris_started)
					packet_delim(1);
				packet_write_fmt(1, "\1packfile\n");
			}
			break;
		}
		if ((p = memchr(os->buffer, '\n', os->used))) {
			if (!os->packfile_uris_started) {
				os->packfile_uris_started = 1;
				if (!write_packfile_line)
					BUG("packfile_uris requires sideband-all");
				packet_write_fmt(1, "\1packfile-uris\n");
			}
			*p = '\0';
			packet_write_fmt(1, "\1%s\n", os->buffer);

			os->used -= p - os->buffer + 1;
			memmove(os->buffer, p + 1, os->used);
		} else {
			/*
			 * Incomplete line.
			 */
			return readsz;
		}
	}

	if (os->used > 1) {
		send_client_data(1, os->buffer, os->used - 1, use_sideband);
		os->buffer[0] = os->buffer[os->used - 1];
		os->used = 1;
	} else {
		send_client_data(1, os->buffer, os->used, use_sideband);
		os->used = 0;
	}

	return readsz;
}

static void create_pack_file(struct upload_pack_data *pack_data,
			     const struct string_list *uri_protocols)
{
	struct child_process pack_objects = CHILD_PROCESS_INIT;
	struct output_state *output_state = xcalloc(1, sizeof(struct output_state));
	char progress[128];
	char abort_msg[] = "aborting due to possible repository "
		"corruption on the remote side.";
	ssize_t sz;
	int i;
	FILE *pipe_fd;

	if (!pack_data->pack_objects_hook)
		pack_objects.git_cmd = 1;
	else {
		strvec_push(&pack_objects.args, pack_data->pack_objects_hook);
		strvec_push(&pack_objects.args, "git");
		pack_objects.use_shell = 1;
	}

	if (pack_data->shallow_nr) {
		strvec_push(&pack_objects.args, "--shallow-file");
		strvec_push(&pack_objects.args, "");
	}
	strvec_push(&pack_objects.args, "pack-objects");
	strvec_push(&pack_objects.args, "--revs");
	if (pack_data->use_thin_pack)
		strvec_push(&pack_objects.args, "--thin");

	strvec_push(&pack_objects.args, "--stdout");
	if (pack_data->shallow_nr)
		strvec_push(&pack_objects.args, "--shallow");
	if (!pack_data->no_progress)
		strvec_push(&pack_objects.args, "--progress");
	if (pack_data->use_ofs_delta)
		strvec_push(&pack_objects.args, "--delta-base-offset");
	if (pack_data->use_include_tag)
		strvec_push(&pack_objects.args, "--include-tag");
	if (repo_has_accepted_promisor_remote(the_repository))
		strvec_push(&pack_objects.args, "--missing=allow-promisor");
	if (pack_data->filter_options.choice) {
		const char *spec =
			expand_list_objects_filter_spec(&pack_data->filter_options);
		strvec_pushf(&pack_objects.args, "--filter=%s", spec);
	}
	if (uri_protocols) {
		for (i = 0; i < uri_protocols->nr; i++)
			strvec_pushf(&pack_objects.args, "--uri-protocol=%s",
					 uri_protocols->items[i].string);
	}

	pack_objects.in = -1;
	pack_objects.out = -1;
	pack_objects.err = -1;
	pack_objects.clean_on_exit = 1;

	if (start_command(&pack_objects))
		die("git upload-pack: unable to fork git-pack-objects");

	pipe_fd = xfdopen(pack_objects.in, "w");

	if (pack_data->shallow_nr)
		for_each_commit_graft(write_one_shallow, pipe_fd);

	for (i = 0; i < pack_data->want_obj.nr; i++)
		fprintf(pipe_fd, "%s\n",
			oid_to_hex(&pack_data->want_obj.objects[i].item->oid));
	fprintf(pipe_fd, "--not\n");
	for (i = 0; i < pack_data->have_obj.nr; i++)
		fprintf(pipe_fd, "%s\n",
			oid_to_hex(&pack_data->have_obj.objects[i].item->oid));
	for (i = 0; i < pack_data->extra_edge_obj.nr; i++)
		fprintf(pipe_fd, "%s\n",
			oid_to_hex(&pack_data->extra_edge_obj.objects[i].item->oid));
	fprintf(pipe_fd, "\n");
	fflush(pipe_fd);
	fclose(pipe_fd);

	/* We read from pack_objects.err to capture stderr output for
	 * progress bar, and pack_objects.out to capture the pack data.
	 */

	while (1) {
		struct pollfd pfd[2];
		int pe, pu, pollsize, polltimeout;
		int ret;

		reset_timeout(pack_data->timeout);

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

		polltimeout = pack_data->keepalive < 0
			? -1
			: 1000 * pack_data->keepalive;

		ret = poll(pfd, pollsize, polltimeout);

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
				send_client_data(2, progress, sz,
						 pack_data->use_sideband);
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
			int result = relay_pack_data(pack_objects.out,
						     output_state,
						     pack_data->use_sideband,
						     !!uri_protocols);

			if (result == 0) {
				close(pack_objects.out);
				pack_objects.out = -1;
			} else if (result < 0) {
				goto fail;
			}
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
		if (!ret && pack_data->use_sideband) {
			static const char buf[] = "0005\1";
			write_or_die(1, buf, 5);
		}
	}

	if (finish_command(&pack_objects)) {
		error("git upload-pack: git-pack-objects died with error.");
		goto fail;
	}

	/* flush the data */
	if (output_state->used > 0) {
		send_client_data(1, output_state->buffer, output_state->used,
				 pack_data->use_sideband);
		fprintf(stderr, "flushed.\n");
	}
	free(output_state);
	if (pack_data->use_sideband)
		packet_flush(1);
	return;

 fail:
	free(output_state);
	send_client_data(3, abort_msg, strlen(abort_msg),
			 pack_data->use_sideband);
	die("git upload-pack: %s", abort_msg);
}

static int do_got_oid(struct upload_pack_data *data, const struct object_id *oid)
{
	int we_knew_they_have = 0;
	struct object *o = parse_object_with_flags(the_repository, oid,
						   PARSE_OBJECT_SKIP_HASH_CHECK |
						   PARSE_OBJECT_DISCARD_TREE);

	if (!o)
		die("oops (%s)", oid_to_hex(oid));
	if (o->type == OBJ_COMMIT) {
		struct commit_list *parents;
		struct commit *commit = (struct commit *)o;
		if (o->flags & THEY_HAVE)
			we_knew_they_have = 1;
		else
			o->flags |= THEY_HAVE;
		if (!data->oldest_have || (commit->date < data->oldest_have))
			data->oldest_have = commit->date;
		for (parents = commit->parents;
		     parents;
		     parents = parents->next)
			parents->item->object.flags |= THEY_HAVE;
	}
	if (!we_knew_they_have) {
		add_object_array(o, NULL, &data->have_obj);
		return 1;
	}
	return 0;
}

static int got_oid(struct upload_pack_data *data,
		   const char *hex, struct object_id *oid)
{
	if (get_oid_hex(hex, oid))
		die("git upload-pack: expected SHA1 object, got '%s'", hex);
	if (!repo_has_object_file_with_flags(the_repository, oid,
					     OBJECT_INFO_QUICK | OBJECT_INFO_SKIP_FETCH_OBJECT))
		return -1;
	return do_got_oid(data, oid);
}

static int ok_to_give_up(struct upload_pack_data *data)
{
	timestamp_t min_generation = GENERATION_NUMBER_ZERO;

	if (!data->have_obj.nr)
		return 0;

	return can_all_from_reach_with_flag(&data->want_obj, THEY_HAVE,
					    COMMON_KNOWN, data->oldest_have,
					    min_generation);
}

static int get_common_commits(struct upload_pack_data *data,
			      struct packet_reader *reader)
{
	struct object_id oid;
	char last_hex[GIT_MAX_HEXSZ + 1];
	int got_common = 0;
	int got_other = 0;
	int sent_ready = 0;

	for (;;) {
		const char *arg;

		reset_timeout(data->timeout);

		if (packet_reader_read(reader) != PACKET_READ_NORMAL) {
			if (data->multi_ack == MULTI_ACK_DETAILED
			    && got_common
			    && !got_other
			    && ok_to_give_up(data)) {
				sent_ready = 1;
				packet_write_fmt(1, "ACK %s ready\n", last_hex);
			}
			if (data->have_obj.nr == 0 || data->multi_ack)
				packet_write_fmt(1, "NAK\n");

			if (data->no_done && sent_ready) {
				packet_write_fmt(1, "ACK %s\n", last_hex);
				return 0;
			}
			if (data->stateless_rpc)
				exit(0);
			got_common = 0;
			got_other = 0;
			continue;
		}
		if (skip_prefix(reader->line, "have ", &arg)) {
			switch (got_oid(data, arg, &oid)) {
			case -1: /* they have what we do not */
				got_other = 1;
				if (data->multi_ack
				    && ok_to_give_up(data)) {
					const char *hex = oid_to_hex(&oid);
					if (data->multi_ack == MULTI_ACK_DETAILED) {
						sent_ready = 1;
						packet_write_fmt(1, "ACK %s ready\n", hex);
					} else
						packet_write_fmt(1, "ACK %s continue\n", hex);
				}
				break;
			default:
				got_common = 1;
				oid_to_hex_r(last_hex, &oid);
				if (data->multi_ack == MULTI_ACK_DETAILED)
					packet_write_fmt(1, "ACK %s common\n", last_hex);
				else if (data->multi_ack)
					packet_write_fmt(1, "ACK %s continue\n", last_hex);
				else if (data->have_obj.nr == 1)
					packet_write_fmt(1, "ACK %s\n", last_hex);
				break;
			}
			continue;
		}
		if (!strcmp(reader->line, "done")) {
			if (data->have_obj.nr > 0) {
				if (data->multi_ack)
					packet_write_fmt(1, "ACK %s\n", last_hex);
				return 0;
			}
			packet_write_fmt(1, "NAK\n");
			return -1;
		}
		die("git upload-pack: expected SHA1 list, got '%s'", reader->line);
	}
}

static int allow_hidden_refs(enum allow_uor allow_uor)
{
	if ((allow_uor & ALLOW_ANY_SHA1) == ALLOW_ANY_SHA1)
		return 1;
	return !(allow_uor & (ALLOW_TIP_SHA1 | ALLOW_REACHABLE_SHA1));
}

static void for_each_namespaced_ref_1(each_ref_fn fn,
				      struct upload_pack_data *data)
{
	const char **excludes = NULL;
	/*
	 * If `data->allow_uor` allows fetching hidden refs, we need to
	 * mark all references (including hidden ones), to check in
	 * `is_our_ref()` below.
	 *
	 * Otherwise, we only care about whether each reference's object
	 * has the OUR_REF bit set or not, so do not need to visit
	 * hidden references.
	 */
	if (allow_hidden_refs(data->allow_uor))
		excludes = hidden_refs_to_excludes(&data->hidden_refs);

	refs_for_each_namespaced_ref(get_main_ref_store(the_repository),
				     excludes, fn, data);
}


static int is_our_ref(struct object *o, enum allow_uor allow_uor)
{
	return o->flags & ((allow_hidden_refs(allow_uor) ? 0 : HIDDEN_REF) | OUR_REF);
}

/*
 * on successful case, it's up to the caller to close cmd->out
 */
static int do_reachable_revlist(struct child_process *cmd,
				struct object_array *src,
				struct object_array *reachable,
				enum allow_uor allow_uor)
{
	struct object *o;
	FILE *cmd_in = NULL;
	int i;

	strvec_pushl(&cmd->args, "rev-list", "--stdin", NULL);
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

	cmd_in = xfdopen(cmd->in, "w");

	for (i = get_max_object_index(); 0 < i; ) {
		o = get_indexed_object(--i);
		if (!o)
			continue;
		if (reachable && o->type == OBJ_COMMIT)
			o->flags &= ~TMP_MARK;
		if (!is_our_ref(o, allow_uor))
			continue;
		if (fprintf(cmd_in, "^%s\n", oid_to_hex(&o->oid)) < 0)
			goto error;
	}
	for (i = 0; i < src->nr; i++) {
		o = src->objects[i].item;
		if (is_our_ref(o, allow_uor)) {
			if (reachable)
				add_object_array(o, NULL, reachable);
			continue;
		}
		if (reachable && o->type == OBJ_COMMIT)
			o->flags |= TMP_MARK;
		if (fprintf(cmd_in, "%s\n", oid_to_hex(&o->oid)) < 0)
			goto error;
	}
	if (ferror(cmd_in) || fflush(cmd_in))
		goto error;
	fclose(cmd_in);
	cmd->in = -1;
	sigchain_pop(SIGPIPE);

	return 0;

error:
	sigchain_pop(SIGPIPE);

	if (cmd_in)
		fclose(cmd_in);
	if (cmd->out >= 0)
		close(cmd->out);
	return -1;
}

static int get_reachable_list(struct upload_pack_data *data,
			      struct object_array *reachable)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int i;
	struct object *o;
	char namebuf[GIT_MAX_HEXSZ + 2]; /* ^ + hash + LF */
	const unsigned hexsz = the_hash_algo->hexsz;
	int ret;

	if (do_reachable_revlist(&cmd, &data->shallows, reachable,
				 data->allow_uor) < 0) {
		ret = -1;
		goto out;
	}

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

	if (finish_command(&cmd)) {
		ret = -1;
		goto out;
	}

	ret = 0;

out:
	child_process_clear(&cmd);
	return ret;
}

static int has_unreachable(struct object_array *src, enum allow_uor allow_uor)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	char buf[1];
	int i;

	if (do_reachable_revlist(&cmd, src, NULL, allow_uor) < 0)
		goto error;

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
	if (cmd.out >= 0)
		close(cmd.out);
	child_process_clear(&cmd);
	return 1;
}

static void check_non_tip(struct upload_pack_data *data)
{
	int i;

	/*
	 * In the normal in-process case without
	 * uploadpack.allowReachableSHA1InWant,
	 * non-tip requests can never happen.
	 */
	if (!data->stateless_rpc && !(data->allow_uor & ALLOW_REACHABLE_SHA1))
		goto error;
	if (!has_unreachable(&data->want_obj, data->allow_uor))
		/* All the non-tip ones are ancestors of what we advertised */
		return;

error:
	/* Pick one of them (we know there at least is one) */
	for (i = 0; i < data->want_obj.nr; i++) {
		struct object *o = data->want_obj.objects[i].item;
		if (!is_our_ref(o, data->allow_uor)) {
			error("git upload-pack: not our ref %s",
			      oid_to_hex(&o->oid));
			packet_writer_error(&data->writer,
					    "upload-pack: not our ref %s",
					    oid_to_hex(&o->oid));
			exit(128);
		}
	}
}

static void send_shallow(struct upload_pack_data *data,
			 struct commit_list *result)
{
	while (result) {
		struct object *object = &result->item->object;
		if (!(object->flags & (CLIENT_SHALLOW|NOT_SHALLOW))) {
			packet_writer_write(&data->writer, "shallow %s",
					    oid_to_hex(&object->oid));
			register_shallow(the_repository, &object->oid);
			data->shallow_nr++;
		}
		result = result->next;
	}
}

static void send_unshallow(struct upload_pack_data *data)
{
	int i;

	for (i = 0; i < data->shallows.nr; i++) {
		struct object *object = data->shallows.objects[i].item;
		if (object->flags & NOT_SHALLOW) {
			struct commit_list *parents;
			packet_writer_write(&data->writer, "unshallow %s",
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
						 NULL, &data->want_obj);
				parents = parents->next;
			}
			add_object_array(object, NULL, &data->extra_edge_obj);
		}
		/* make sure commit traversal conforms to client */
		register_shallow(the_repository, &object->oid);
	}
}

static int check_ref(const char *refname_full, const char *referent UNUSED, const struct object_id *oid,
		     int flag, void *cb_data);
static void deepen(struct upload_pack_data *data, int depth)
{
	if (depth == INFINITE_DEPTH && !is_repository_shallow(the_repository)) {
		int i;

		for (i = 0; i < data->shallows.nr; i++) {
			struct object *object = data->shallows.objects[i].item;
			object->flags |= NOT_SHALLOW;
		}
	} else if (data->deepen_relative) {
		struct object_array reachable_shallows = OBJECT_ARRAY_INIT;
		struct commit_list *result;

		/*
		 * Checking for reachable shallows requires that our refs be
		 * marked with OUR_REF.
		 */
		refs_head_ref_namespaced(get_main_ref_store(the_repository),
					 check_ref, data);
		for_each_namespaced_ref_1(check_ref, data);

		get_reachable_list(data, &reachable_shallows);
		result = get_shallow_commits(&reachable_shallows,
					     depth + 1,
					     SHALLOW, NOT_SHALLOW);
		send_shallow(data, result);
		free_commit_list(result);
		object_array_clear(&reachable_shallows);
	} else {
		struct commit_list *result;

		result = get_shallow_commits(&data->want_obj, depth,
					     SHALLOW, NOT_SHALLOW);
		send_shallow(data, result);
		free_commit_list(result);
	}

	send_unshallow(data);
}

static void deepen_by_rev_list(struct upload_pack_data *data,
			       int ac,
			       const char **av)
{
	struct commit_list *result;

	disable_commit_graph(the_repository);
	result = get_shallow_commits_by_rev_list(ac, av, SHALLOW, NOT_SHALLOW);
	send_shallow(data, result);
	free_commit_list(result);
	send_unshallow(data);
}

/* Returns 1 if a shallow list is sent or 0 otherwise */
static int send_shallow_list(struct upload_pack_data *data)
{
	int ret = 0;

	if (data->depth > 0 && data->deepen_rev_list)
		die("git upload-pack: deepen and deepen-since (or deepen-not) cannot be used together");
	if (data->depth > 0) {
		deepen(data, data->depth);
		ret = 1;
	} else if (data->deepen_rev_list) {
		struct strvec av = STRVEC_INIT;
		int i;

		strvec_push(&av, "rev-list");
		if (data->deepen_since)
			strvec_pushf(&av, "--max-age=%"PRItime, data->deepen_since);
		if (oidset_size(&data->deepen_not)) {
			const struct object_id *oid;
			struct oidset_iter iter;
			strvec_push(&av, "--not");
			oidset_iter_init(&data->deepen_not, &iter);
			while ((oid = oidset_iter_next(&iter)))
				strvec_push(&av, oid_to_hex(oid));
			strvec_push(&av, "--not");
		}
		for (i = 0; i < data->want_obj.nr; i++) {
			struct object *o = data->want_obj.objects[i].item;
			strvec_push(&av, oid_to_hex(&o->oid));
		}
		deepen_by_rev_list(data, av.nr, av.v);
		strvec_clear(&av);
		ret = 1;
	} else {
		if (data->shallows.nr > 0) {
			int i;
			for (i = 0; i < data->shallows.nr; i++)
				register_shallow(the_repository,
						 &data->shallows.objects[i].item->oid);
		}
	}

	data->shallow_nr += data->shallows.nr;
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

static int process_deepen_not(const char *line, struct oidset *deepen_not, int *deepen_rev_list)
{
	const char *arg;
	if (skip_prefix(line, "deepen-not ", &arg)) {
		char *ref = NULL;
		struct object_id oid;
		if (expand_ref(the_repository, arg, strlen(arg), &oid, &ref) != 1)
			die("git upload-pack: ambiguous deepen-not: %s", line);
		oidset_insert(deepen_not, &oid);
		free(ref);
		*deepen_rev_list = 1;
		return 1;
	}
	return 0;
}

NORETURN __attribute__((format(printf,2,3)))
static void send_err_and_die(struct upload_pack_data *data,
			     const char *fmt, ...)
{
	struct strbuf buf = STRBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&buf, fmt, ap);
	va_end(ap);

	packet_writer_error(&data->writer, "%s", buf.buf);
	die("%s", buf.buf);
}

static void check_one_filter(struct upload_pack_data *data,
			     struct list_objects_filter_options *opts)
{
	const char *key = list_object_filter_config_name(opts->choice);
	struct string_list_item *item = string_list_lookup(&data->allowed_filters,
							   key);
	int allowed;

	if (item)
		allowed = (intptr_t)item->util;
	else
		allowed = data->allow_filter_fallback;

	if (!allowed)
		send_err_and_die(data, "filter '%s' not supported", key);

	if (opts->choice == LOFC_TREE_DEPTH &&
	    opts->tree_exclude_depth > data->tree_filter_max_depth)
		send_err_and_die(data,
				 "tree filter allows max depth %lu, but got %lu",
				 data->tree_filter_max_depth,
				 opts->tree_exclude_depth);
}

static void check_filter_recurse(struct upload_pack_data *data,
				 struct list_objects_filter_options *opts)
{
	size_t i;

	check_one_filter(data, opts);
	if (opts->choice != LOFC_COMBINE)
		return;

	for (i = 0; i < opts->sub_nr; i++)
		check_filter_recurse(data, &opts->sub[i]);
}

static void die_if_using_banned_filter(struct upload_pack_data *data)
{
	check_filter_recurse(data, &data->filter_options);
}

static void receive_needs(struct upload_pack_data *data,
			  struct packet_reader *reader)
{
	int has_non_tip = 0;

	data->shallow_nr = 0;
	for (;;) {
		struct object *o;
		const char *features;
		struct object_id oid_buf;
		const char *arg;
		size_t feature_len;

		reset_timeout(data->timeout);
		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		if (process_shallow(reader->line, &data->shallows))
			continue;
		if (process_deepen(reader->line, &data->depth))
			continue;
		if (process_deepen_since(reader->line, &data->deepen_since, &data->deepen_rev_list))
			continue;
		if (process_deepen_not(reader->line, &data->deepen_not, &data->deepen_rev_list))
			continue;

		if (skip_prefix(reader->line, "filter ", &arg)) {
			if (!data->filter_capability_requested)
				die("git upload-pack: filtering capability not negotiated");
			list_objects_filter_die_if_populated(&data->filter_options);
			parse_list_objects_filter(&data->filter_options, arg);
			die_if_using_banned_filter(data);
			continue;
		}

		if (!skip_prefix(reader->line, "want ", &arg) ||
		    parse_oid_hex(arg, &oid_buf, &features))
			die("git upload-pack: protocol error, "
			    "expected to get object ID, not '%s'", reader->line);

		if (parse_feature_request(features, "deepen-relative"))
			data->deepen_relative = 1;
		if (parse_feature_request(features, "multi_ack_detailed"))
			data->multi_ack = MULTI_ACK_DETAILED;
		else if (parse_feature_request(features, "multi_ack"))
			data->multi_ack = MULTI_ACK;
		if (parse_feature_request(features, "no-done"))
			data->no_done = 1;
		if (parse_feature_request(features, "thin-pack"))
			data->use_thin_pack = 1;
		if (parse_feature_request(features, "ofs-delta"))
			data->use_ofs_delta = 1;
		if (parse_feature_request(features, "side-band-64k"))
			data->use_sideband = LARGE_PACKET_MAX;
		else if (parse_feature_request(features, "side-band"))
			data->use_sideband = DEFAULT_PACKET_MAX;
		if (parse_feature_request(features, "no-progress"))
			data->no_progress = 1;
		if (parse_feature_request(features, "include-tag"))
			data->use_include_tag = 1;
		if (data->allow_filter &&
		    parse_feature_request(features, "filter"))
			data->filter_capability_requested = 1;

		arg = parse_feature_value(features, "session-id", &feature_len, NULL);
		if (arg) {
			char *client_sid = xstrndup(arg, feature_len);
			trace2_data_string("transfer", NULL, "client-sid", client_sid);
			free(client_sid);
		}

		o = parse_object_with_flags(the_repository, &oid_buf,
					    PARSE_OBJECT_SKIP_HASH_CHECK |
					    PARSE_OBJECT_DISCARD_TREE);
		if (!o) {
			packet_writer_error(&data->writer,
					    "upload-pack: not our ref %s",
					    oid_to_hex(&oid_buf));
			die("git upload-pack: not our ref %s",
			    oid_to_hex(&oid_buf));
		}
		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			if (!((data->allow_uor & ALLOW_ANY_SHA1) == ALLOW_ANY_SHA1
			      || is_our_ref(o, data->allow_uor)))
				has_non_tip = 1;
			add_object_array(o, NULL, &data->want_obj);
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
		check_non_tip(data);

	if (!data->use_sideband && data->daemon_mode)
		data->no_progress = 1;

	if (data->depth == 0 && !data->deepen_rev_list && data->shallows.nr == 0)
		return;

	if (send_shallow_list(data))
		packet_flush(1);
}

/* return non-zero if the ref is hidden, otherwise 0 */
static int mark_our_ref(const char *refname, const char *refname_full,
			const struct object_id *oid, const struct strvec *hidden_refs)
{
	struct object *o = lookup_unknown_object(the_repository, oid);

	if (ref_is_hidden(refname, refname_full, hidden_refs)) {
		o->flags |= HIDDEN_REF;
		return 1;
	}
	o->flags |= OUR_REF;
	return 0;
}

static int check_ref(const char *refname_full, const char *referent UNUSED,const struct object_id *oid,
		     int flag UNUSED, void *cb_data)
{
	const char *refname = strip_namespace(refname_full);
	struct upload_pack_data *data = cb_data;

	mark_our_ref(refname, refname_full, oid, &data->hidden_refs);
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

static void format_session_id(struct strbuf *buf, struct upload_pack_data *d) {
	if (d->advertise_sid)
		strbuf_addf(buf, " session-id=%s", trace2_session_id());
}

static void write_v0_ref(struct upload_pack_data *data,
			const char *refname, const char *refname_nons,
			const struct object_id *oid)
{
	static const char *capabilities = "multi_ack thin-pack side-band"
		" side-band-64k ofs-delta shallow deepen-since deepen-not"
		" deepen-relative no-progress include-tag multi_ack_detailed";
	struct object_id peeled;

	if (mark_our_ref(refname_nons, refname, oid, &data->hidden_refs))
		return;

	if (capabilities) {
		struct strbuf symref_info = STRBUF_INIT;
		struct strbuf session_id = STRBUF_INIT;

		format_symref_info(&symref_info, &data->symref);
		format_session_id(&session_id, data);
		packet_fwrite_fmt(stdout, "%s %s%c%s%s%s%s%s%s%s object-format=%s agent=%s\n",
			     oid_to_hex(oid), refname_nons,
			     0, capabilities,
			     (data->allow_uor & ALLOW_TIP_SHA1) ?
				     " allow-tip-sha1-in-want" : "",
			     (data->allow_uor & ALLOW_REACHABLE_SHA1) ?
				     " allow-reachable-sha1-in-want" : "",
			     data->no_done ? " no-done" : "",
			     symref_info.buf,
			     data->allow_filter ? " filter" : "",
			     session_id.buf,
			     the_hash_algo->name,
			     git_user_agent_sanitized());
		strbuf_release(&symref_info);
		strbuf_release(&session_id);
		data->sent_capabilities = 1;
	} else {
		packet_fwrite_fmt(stdout, "%s %s\n", oid_to_hex(oid), refname_nons);
	}
	capabilities = NULL;
	if (!peel_iterated_oid(the_repository, oid, &peeled))
		packet_fwrite_fmt(stdout, "%s %s^{}\n", oid_to_hex(&peeled), refname_nons);
	return;
}

static int send_ref(const char *refname, const char *referent UNUSED, const struct object_id *oid,
		    int flag UNUSED, void *cb_data)
{
	write_v0_ref(cb_data, refname, strip_namespace(refname), oid);
	return 0;
}

static int find_symref(const char *refname, const char *referent UNUSED,
		       const struct object_id *oid UNUSED,
		       int flag, void *cb_data)
{
	const char *symref_target;
	struct string_list_item *item;

	if ((flag & REF_ISSYMREF) == 0)
		return 0;
	symref_target = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
						refname, 0, NULL, &flag);
	if (!symref_target || (flag & REF_ISSYMREF) == 0)
		die("'%s' is a symref but it is not?", refname);
	item = string_list_append(cb_data, strip_namespace(refname));
	item->util = xstrdup(strip_namespace(symref_target));
	return 0;
}

static int parse_object_filter_config(const char *var, const char *value,
				      const struct key_value_info *kvi,
				      struct upload_pack_data *data)
{
	struct strbuf buf = STRBUF_INIT;
	const char *sub, *key;
	size_t sub_len;

	if (parse_config_key(var, "uploadpackfilter", &sub, &sub_len, &key))
		return 0;

	if (!sub) {
		if (!strcmp(key, "allow"))
			data->allow_filter_fallback = git_config_bool(var, value);
		return 0;
	}

	strbuf_add(&buf, sub, sub_len);

	if (!strcmp(key, "allow"))
		string_list_insert(&data->allowed_filters, buf.buf)->util =
			(void *)(intptr_t)git_config_bool(var, value);
	else if (!strcmp(buf.buf, "tree") && !strcmp(key, "maxdepth")) {
		if (!value) {
			strbuf_release(&buf);
			return config_error_nonbool(var);
		}
		string_list_insert(&data->allowed_filters, buf.buf)->util =
			(void *)(intptr_t)1;
		data->tree_filter_max_depth = git_config_ulong(var, value,
							       kvi);
	}

	strbuf_release(&buf);
	return 0;
}

static int upload_pack_config(const char *var, const char *value,
			      const struct config_context *ctx,
			      void *cb_data)
{
	struct upload_pack_data *data = cb_data;

	if (!strcmp("uploadpack.allowtipsha1inwant", var)) {
		if (git_config_bool(var, value))
			data->allow_uor |= ALLOW_TIP_SHA1;
		else
			data->allow_uor &= ~ALLOW_TIP_SHA1;
	} else if (!strcmp("uploadpack.allowreachablesha1inwant", var)) {
		if (git_config_bool(var, value))
			data->allow_uor |= ALLOW_REACHABLE_SHA1;
		else
			data->allow_uor &= ~ALLOW_REACHABLE_SHA1;
	} else if (!strcmp("uploadpack.allowanysha1inwant", var)) {
		if (git_config_bool(var, value))
			data->allow_uor |= ALLOW_ANY_SHA1;
		else
			data->allow_uor &= ~ALLOW_ANY_SHA1;
	} else if (!strcmp("uploadpack.keepalive", var)) {
		data->keepalive = git_config_int(var, value, ctx->kvi);
		if (!data->keepalive)
			data->keepalive = -1;
	} else if (!strcmp("uploadpack.allowfilter", var)) {
		data->allow_filter = git_config_bool(var, value);
	} else if (!strcmp("uploadpack.allowrefinwant", var)) {
		data->allow_ref_in_want = git_config_bool(var, value);
	} else if (!strcmp("uploadpack.allowsidebandall", var)) {
		data->allow_sideband_all = git_config_bool(var, value);
	} else if (!strcmp("uploadpack.blobpackfileuri", var)) {
		if (value)
			data->allow_packfile_uris = 1;
	} else if (!strcmp("core.precomposeunicode", var)) {
		precomposed_unicode = git_config_bool(var, value);
	} else if (!strcmp("transfer.advertisesid", var)) {
		data->advertise_sid = git_config_bool(var, value);
	}

	if (parse_object_filter_config(var, value, ctx->kvi, data) < 0)
		return -1;

	return parse_hide_refs_config(var, value, "uploadpack", &data->hidden_refs);
}

static int upload_pack_protected_config(const char *var, const char *value,
					const struct config_context *ctx UNUSED,
					void *cb_data)
{
	struct upload_pack_data *data = cb_data;

	if (!strcmp("uploadpack.packobjectshook", var))
		return git_config_string(&data->pack_objects_hook, var, value);
	return 0;
}

static void get_upload_pack_config(struct repository *r,
				   struct upload_pack_data *data)
{
	repo_config(r, upload_pack_config, data);
	git_protected_config(upload_pack_protected_config, data);

	data->allow_sideband_all |= git_env_bool("GIT_TEST_SIDEBAND_ALL", 0);
}

void upload_pack(const int advertise_refs, const int stateless_rpc,
		 const int timeout)
{
	struct packet_reader reader;
	struct upload_pack_data data;

	upload_pack_data_init(&data);
	get_upload_pack_config(the_repository, &data);

	data.stateless_rpc = stateless_rpc;
	data.timeout = timeout;
	if (data.timeout)
		data.daemon_mode = 1;

	refs_head_ref_namespaced(get_main_ref_store(the_repository),
				 find_symref, &data.symref);

	if (advertise_refs || !data.stateless_rpc) {
		reset_timeout(data.timeout);
		if (advertise_refs)
			data.no_done = 1;
		refs_head_ref_namespaced(get_main_ref_store(the_repository),
					 send_ref, &data);
		for_each_namespaced_ref_1(send_ref, &data);
		if (!data.sent_capabilities) {
			const char *refname = "capabilities^{}";
			write_v0_ref(&data, refname, refname, null_oid());
		}
		/*
		 * fflush stdout before calling advertise_shallow_grafts because send_ref
		 * uses stdio.
		 */
		fflush_or_die(stdout);
		advertise_shallow_grafts(1);
		packet_flush(1);
	} else {
		refs_head_ref_namespaced(get_main_ref_store(the_repository),
					 check_ref, &data);
		for_each_namespaced_ref_1(check_ref, &data);
	}

	if (!advertise_refs) {
		packet_reader_init(&reader, 0, NULL, 0,
				   PACKET_READ_CHOMP_NEWLINE |
				   PACKET_READ_DIE_ON_ERR_PACKET);

		receive_needs(&data, &reader);

		/*
		 * An EOF at this exact point in negotiation should be
		 * acceptable from stateless clients as they will consume the
		 * shallow list before doing subsequent rpc with haves/etc.
		 */
		if (data.stateless_rpc)
			reader.options |= PACKET_READ_GENTLE_ON_EOF;

		if (data.want_obj.nr &&
		    packet_reader_peek(&reader) != PACKET_READ_EOF) {
			reader.options &= ~PACKET_READ_GENTLE_ON_EOF;
			get_common_commits(&data, &reader);
			create_pack_file(&data, NULL);
		}
	}

	upload_pack_data_clear(&data);
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

		o = parse_object_with_flags(the_repository, &oid,
					    PARSE_OBJECT_SKIP_HASH_CHECK |
					    PARSE_OBJECT_DISCARD_TREE);

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
			  struct strmap *wanted_refs,
			  struct strvec *hidden_refs,
			  struct object_array *want_obj)
{
	const char *refname_nons;
	if (skip_prefix(line, "want-ref ", &refname_nons)) {
		struct object_id oid;
		struct object *o = NULL;
		struct strbuf refname = STRBUF_INIT;

		strbuf_addf(&refname, "%s%s", get_git_namespace(), refname_nons);
		if (ref_is_hidden(refname_nons, refname.buf, hidden_refs) ||
		    refs_read_ref(get_main_ref_store(the_repository), refname.buf, &oid)) {
			packet_writer_error(writer, "unknown ref %s", refname_nons);
			die("unknown ref %s", refname_nons);
		}
		strbuf_release(&refname);

		if (strmap_put(wanted_refs, refname_nons, oiddup(&oid))) {
			packet_writer_error(writer, "duplicate want-ref %s",
					    refname_nons);
			die("duplicate want-ref %s", refname_nons);
		}

		if (!starts_with(refname_nons, "refs/tags/")) {
			struct commit *commit = lookup_commit_in_graph(the_repository, &oid);
			if (commit)
				o = &commit->object;
		}

		if (!o)
			o = parse_object_or_die(&oid, refname_nons);

		if (!(o->flags & WANTED)) {
			o->flags |= WANTED;
			add_object_array(o, NULL, want_obj);
		}

		return 1;
	}

	return 0;
}

static int parse_have(const char *line, struct upload_pack_data *data)
{
	const char *arg;
	if (skip_prefix(line, "have ", &arg)) {
		struct object_id oid;

		got_oid(data, arg, &oid);
		data->seen_haves = 1;
		return 1;
	}

	return 0;
}

static void trace2_fetch_info(struct upload_pack_data *data)
{
	struct json_writer jw = JSON_WRITER_INIT;

	jw_object_begin(&jw, 0);
	jw_object_intmax(&jw, "haves", data->have_obj.nr);
	jw_object_intmax(&jw, "wants", data->want_obj.nr);
	jw_object_intmax(&jw, "want-refs", strmap_get_size(&data->wanted_refs));
	jw_object_intmax(&jw, "depth", data->depth);
	jw_object_intmax(&jw, "shallows", data->shallows.nr);
	jw_object_bool(&jw, "deepen-since", data->deepen_since);
	jw_object_intmax(&jw, "deepen-not", oidset_size(&data->deepen_not));
	jw_object_bool(&jw, "deepen-relative", data->deepen_relative);
	if (data->filter_options.choice)
		jw_object_string(&jw, "filter", list_object_filter_config_name(data->filter_options.choice));
	else
		jw_object_null(&jw, "filter");
	jw_end(&jw);

	trace2_data_json("upload-pack", the_repository, "fetch-info", &jw);

	jw_release(&jw);
}

static void process_args(struct packet_reader *request,
			 struct upload_pack_data *data)
{
	while (packet_reader_read(request) == PACKET_READ_NORMAL) {
		const char *arg = request->line;
		const char *p;

		/* process want */
		if (parse_want(&data->writer, arg, &data->want_obj))
			continue;
		if (data->allow_ref_in_want &&
		    parse_want_ref(&data->writer, arg, &data->wanted_refs,
				   &data->hidden_refs, &data->want_obj))
			continue;
		/* process have line */
		if (parse_have(arg, data))
			continue;

		/* process args like thin-pack */
		if (!strcmp(arg, "thin-pack")) {
			data->use_thin_pack = 1;
			continue;
		}
		if (!strcmp(arg, "ofs-delta")) {
			data->use_ofs_delta = 1;
			continue;
		}
		if (!strcmp(arg, "no-progress")) {
			data->no_progress = 1;
			continue;
		}
		if (!strcmp(arg, "include-tag")) {
			data->use_include_tag = 1;
			continue;
		}
		if (!strcmp(arg, "done")) {
			data->done = 1;
			continue;
		}
		if (!strcmp(arg, "wait-for-done")) {
			data->wait_for_done = 1;
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

		if (data->allow_filter && skip_prefix(arg, "filter ", &p)) {
			list_objects_filter_die_if_populated(&data->filter_options);
			parse_list_objects_filter(&data->filter_options, p);
			die_if_using_banned_filter(data);
			continue;
		}

		if (data->allow_sideband_all &&
		    !strcmp(arg, "sideband-all")) {
			data->writer.use_sideband = 1;
			continue;
		}

		if (data->allow_packfile_uris &&
		    skip_prefix(arg, "packfile-uris ", &p)) {
			if (data->uri_protocols.nr)
				send_err_and_die(data,
						 "multiple packfile-uris lines forbidden");
			string_list_split(&data->uri_protocols, p, ',', -1);
			continue;
		}

		/* ignore unknown lines maybe? */
		die("unexpected line: '%s'", arg);
	}

	if (data->uri_protocols.nr && !data->writer.use_sideband)
		string_list_clear(&data->uri_protocols, 0);

	if (request->status != PACKET_READ_FLUSH)
		die(_("expected flush after fetch arguments"));

	if (trace2_is_enabled())
		trace2_fetch_info(data);
}

static int send_acks(struct upload_pack_data *data, struct object_array *acks)
{
	int i;

	packet_writer_write(&data->writer, "acknowledgments\n");

	/* Send Acks */
	if (!acks->nr)
		packet_writer_write(&data->writer, "NAK\n");

	for (i = 0; i < acks->nr; i++) {
		packet_writer_write(&data->writer, "ACK %s\n",
				    oid_to_hex(&acks->objects[i].item->oid));
	}

	if (!data->wait_for_done && ok_to_give_up(data)) {
		/* Send Ready */
		packet_writer_write(&data->writer, "ready\n");
		return 1;
	}

	return 0;
}

static int process_haves_and_send_acks(struct upload_pack_data *data)
{
	int ret = 0;

	if (data->done) {
		ret = 1;
	} else if (send_acks(data, &data->have_obj)) {
		packet_writer_delim(&data->writer);
		ret = 1;
	} else {
		/* Add Flush */
		packet_writer_flush(&data->writer);
		ret = 0;
	}

	return ret;
}

static void send_wanted_ref_info(struct upload_pack_data *data)
{
	struct hashmap_iter iter;
	const struct strmap_entry *e;

	if (strmap_empty(&data->wanted_refs))
		return;

	packet_writer_write(&data->writer, "wanted-refs\n");

	strmap_for_each_entry(&data->wanted_refs, &iter, e) {
		packet_writer_write(&data->writer, "%s %s\n",
				    oid_to_hex(e->value),
				    e->key);
	}

	packet_writer_delim(&data->writer);
}

static void send_shallow_info(struct upload_pack_data *data)
{
	/* No shallow info needs to be sent */
	if (!data->depth && !data->deepen_rev_list && !data->shallows.nr &&
	    !is_repository_shallow(the_repository))
		return;

	packet_writer_write(&data->writer, "shallow-info\n");

	if (!send_shallow_list(data) &&
	    is_repository_shallow(the_repository))
		deepen(data, INFINITE_DEPTH);

	packet_delim(1);
}

enum fetch_state {
	FETCH_PROCESS_ARGS = 0,
	FETCH_SEND_ACKS,
	FETCH_SEND_PACK,
	FETCH_DONE,
};

int upload_pack_v2(struct repository *r, struct packet_reader *request)
{
	enum fetch_state state = FETCH_PROCESS_ARGS;
	struct upload_pack_data data;

	clear_object_flags(ALL_FLAGS);

	upload_pack_data_init(&data);
	data.use_sideband = LARGE_PACKET_MAX;
	get_upload_pack_config(r, &data);

	while (state != FETCH_DONE) {
		switch (state) {
		case FETCH_PROCESS_ARGS:
			process_args(request, &data);

			if (!data.want_obj.nr && !data.wait_for_done) {
				/*
				 * Request didn't contain any 'want' lines (and
				 * the request does not contain
				 * "wait-for-done", in which it is reasonable
				 * to just send 'have's without 'want's); guess
				 * they didn't want anything.
				 */
				state = FETCH_DONE;
			} else if (data.seen_haves) {
				/*
				 * Request had 'have' lines, so lets ACK them.
				 */
				state = FETCH_SEND_ACKS;
			} else {
				/*
				 * Request had 'want's but no 'have's so we can
				 * immediately go to construct and send a pack.
				 */
				state = FETCH_SEND_PACK;
			}
			break;
		case FETCH_SEND_ACKS:
			if (process_haves_and_send_acks(&data))
				state = FETCH_SEND_PACK;
			else
				state = FETCH_DONE;
			break;
		case FETCH_SEND_PACK:
			send_wanted_ref_info(&data);
			send_shallow_info(&data);

			if (data.uri_protocols.nr) {
				create_pack_file(&data, &data.uri_protocols);
			} else {
				packet_writer_write(&data.writer, "packfile\n");
				create_pack_file(&data, NULL);
			}
			state = FETCH_DONE;
			break;
		case FETCH_DONE:
			continue;
		}
	}

	upload_pack_data_clear(&data);
	return 0;
}

int upload_pack_advertise(struct repository *r,
			  struct strbuf *value)
{
	struct upload_pack_data data;

	upload_pack_data_init(&data);
	get_upload_pack_config(r, &data);

	if (value) {
		strbuf_addstr(value, "shallow wait-for-done");

		if (data.allow_filter)
			strbuf_addstr(value, " filter");

		if (data.allow_ref_in_want)
			strbuf_addstr(value, " ref-in-want");

		if (data.allow_sideband_all)
			strbuf_addstr(value, " sideband-all");

		if (data.allow_packfile_uris)
			strbuf_addstr(value, " packfile-uris");
	}

	upload_pack_data_clear(&data);

	return 1;
}
