#include "git-compat-util.h"
#include "alloc.h"
#include "repository.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "refs.h"
#include "pkt-line.h"
#include "commit.h"
#include "tag.h"
#include "exec-cmd.h"
#include "pack.h"
#include "sideband.h"
#include "fetch-pack.h"
#include "remote.h"
#include "run-command.h"
#include "connect.h"
#include "trace2.h"
#include "transport.h"
#include "version.h"
#include "oid-array.h"
#include "oidset.h"
#include "packfile.h"
#include "object-store.h"
#include "connected.h"
#include "fetch-negotiator.h"
#include "fsck.h"
#include "shallow.h"
#include "commit-reach.h"
#include "commit-graph.h"
#include "sigchain.h"
#include "mergesort.h"
#include "wrapper.h"

static int transfer_unpack_limit = -1;
static int fetch_unpack_limit = -1;
static int unpack_limit = 100;
static int prefer_ofs_delta = 1;
static int no_done;
static int deepen_since_ok;
static int deepen_not_ok;
static int fetch_fsck_objects = -1;
static int transfer_fsck_objects = -1;
static int agent_supported;
static int server_supports_filtering;
static int advertise_sid;
static struct shallow_lock shallow_lock;
static const char *alternate_shallow_file;
static struct fsck_options fsck_options = FSCK_OPTIONS_MISSING_GITMODULES;
static struct strbuf fsck_msg_types = STRBUF_INIT;
static struct string_list uri_protocols = STRING_LIST_INIT_DUP;

/* Remember to update object flag allocation in object.h */
#define COMPLETE	(1U << 0)
#define ALTERNATE	(1U << 1)
#define COMMON		(1U << 6)
#define REACH_SCRATCH	(1U << 7)

/*
 * After sending this many "have"s if we do not get any new ACK , we
 * give up traversing our history.
 */
#define MAX_IN_VAIN 256

static int multi_ack, use_sideband;
/* Allow specifying sha1 if it is a ref tip. */
#define ALLOW_TIP_SHA1	01
/* Allow request of a sha1 if it is reachable from a ref (possibly hidden ref). */
#define ALLOW_REACHABLE_SHA1	02
static unsigned int allow_unadvertised_object_request;

__attribute__((format (printf, 2, 3)))
static inline void print_verbose(const struct fetch_pack_args *args,
				 const char *fmt, ...)
{
	va_list params;

	if (!args->verbose)
		return;

	va_start(params, fmt);
	vfprintf(stderr, fmt, params);
	va_end(params);
	fputc('\n', stderr);
}

struct alternate_object_cache {
	struct object **items;
	size_t nr, alloc;
};

static void cache_one_alternate(const struct object_id *oid,
				void *vcache)
{
	struct alternate_object_cache *cache = vcache;
	struct object *obj = parse_object(the_repository, oid);

	if (!obj || (obj->flags & ALTERNATE))
		return;

	obj->flags |= ALTERNATE;
	ALLOC_GROW(cache->items, cache->nr + 1, cache->alloc);
	cache->items[cache->nr++] = obj;
}

static void for_each_cached_alternate(struct fetch_negotiator *negotiator,
				      void (*cb)(struct fetch_negotiator *,
						 struct object *))
{
	static int initialized;
	static struct alternate_object_cache cache;
	size_t i;

	if (!initialized) {
		for_each_alternate_ref(cache_one_alternate, &cache);
		initialized = 1;
	}

	for (i = 0; i < cache.nr; i++)
		cb(negotiator, cache.items[i]);
}

static struct commit *deref_without_lazy_fetch_extended(const struct object_id *oid,
							int mark_tags_complete,
							enum object_type *type,
							unsigned int oi_flags)
{
	struct object_info info = { .typep = type };
	struct commit *commit;

	commit = lookup_commit_in_graph(the_repository, oid);
	if (commit)
		return commit;

	while (1) {
		if (oid_object_info_extended(the_repository, oid, &info,
					     oi_flags))
			return NULL;
		if (*type == OBJ_TAG) {
			struct tag *tag = (struct tag *)
				parse_object(the_repository, oid);

			if (!tag->tagged)
				return NULL;
			if (mark_tags_complete)
				tag->object.flags |= COMPLETE;
			oid = &tag->tagged->oid;
		} else {
			break;
		}
	}

	if (*type == OBJ_COMMIT) {
		struct commit *commit = lookup_commit(the_repository, oid);
		if (!commit || repo_parse_commit(the_repository, commit))
			return NULL;
		return commit;
	}

	return NULL;
}


static struct commit *deref_without_lazy_fetch(const struct object_id *oid,
					       int mark_tags_complete)
{
	enum object_type type;
	unsigned flags = OBJECT_INFO_SKIP_FETCH_OBJECT | OBJECT_INFO_QUICK;
	return deref_without_lazy_fetch_extended(oid, mark_tags_complete,
						 &type, flags);
}

static int rev_list_insert_ref(struct fetch_negotiator *negotiator,
			       const struct object_id *oid)
{
	struct commit *c = deref_without_lazy_fetch(oid, 0);

	if (c)
		negotiator->add_tip(negotiator, c);
	return 0;
}

static int rev_list_insert_ref_oid(const char *refname UNUSED,
				   const struct object_id *oid,
				   int flag UNUSED,
				   void *cb_data)
{
	return rev_list_insert_ref(cb_data, oid);
}

enum ack_type {
	NAK = 0,
	ACK,
	ACK_continue,
	ACK_common,
	ACK_ready
};

static void consume_shallow_list(struct fetch_pack_args *args,
				 struct packet_reader *reader)
{
	if (args->stateless_rpc && args->deepen) {
		/* If we sent a depth we will get back "duplicate"
		 * shallow and unshallow commands every time there
		 * is a block of have lines exchanged.
		 */
		while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
			if (starts_with(reader->line, "shallow "))
				continue;
			if (starts_with(reader->line, "unshallow "))
				continue;
			die(_("git fetch-pack: expected shallow list"));
		}
		if (reader->status != PACKET_READ_FLUSH)
			die(_("git fetch-pack: expected a flush packet after shallow list"));
	}
}

static enum ack_type get_ack(struct packet_reader *reader,
			     struct object_id *result_oid)
{
	int len;
	const char *arg;

	if (packet_reader_read(reader) != PACKET_READ_NORMAL)
		die(_("git fetch-pack: expected ACK/NAK, got a flush packet"));
	len = reader->pktlen;

	if (!strcmp(reader->line, "NAK"))
		return NAK;
	if (skip_prefix(reader->line, "ACK ", &arg)) {
		const char *p;
		if (!parse_oid_hex(arg, result_oid, &p)) {
			len -= p - reader->line;
			if (len < 1)
				return ACK;
			if (strstr(p, "continue"))
				return ACK_continue;
			if (strstr(p, "common"))
				return ACK_common;
			if (strstr(p, "ready"))
				return ACK_ready;
			return ACK;
		}
	}
	die(_("git fetch-pack: expected ACK/NAK, got '%s'"), reader->line);
}

static void send_request(struct fetch_pack_args *args,
			 int fd, struct strbuf *buf)
{
	if (args->stateless_rpc) {
		send_sideband(fd, -1, buf->buf, buf->len, LARGE_PACKET_MAX);
		packet_flush(fd);
	} else {
		if (write_in_full(fd, buf->buf, buf->len) < 0)
			die_errno(_("unable to write to remote"));
	}
}

static void insert_one_alternate_object(struct fetch_negotiator *negotiator,
					struct object *obj)
{
	rev_list_insert_ref(negotiator, &obj->oid);
}

#define INITIAL_FLUSH 16
#define PIPESAFE_FLUSH 32
#define LARGE_FLUSH 16384

static int next_flush(int stateless_rpc, int count)
{
	if (stateless_rpc) {
		if (count < LARGE_FLUSH)
			count <<= 1;
		else
			count = count * 11 / 10;
	} else {
		if (count < PIPESAFE_FLUSH)
			count <<= 1;
		else
			count += PIPESAFE_FLUSH;
	}
	return count;
}

static void mark_tips(struct fetch_negotiator *negotiator,
		      const struct oid_array *negotiation_tips)
{
	int i;

	if (!negotiation_tips) {
		for_each_rawref(rev_list_insert_ref_oid, negotiator);
		return;
	}

	for (i = 0; i < negotiation_tips->nr; i++)
		rev_list_insert_ref(negotiator, &negotiation_tips->oid[i]);
	return;
}

static void send_filter(struct fetch_pack_args *args,
			struct strbuf *req_buf,
			int server_supports_filter)
{
	if (args->filter_options.choice) {
		const char *spec =
			expand_list_objects_filter_spec(&args->filter_options);
		if (server_supports_filter) {
			print_verbose(args, _("Server supports filter"));
			packet_buf_write(req_buf, "filter %s", spec);
			trace2_data_string("fetch", the_repository,
					   "filter/effective", spec);
		} else {
			warning("filtering not recognized by server, ignoring");
			trace2_data_string("fetch", the_repository,
					   "filter/unsupported", spec);
		}
	} else {
		trace2_data_string("fetch", the_repository,
				   "filter/none", "");
	}
}

static int find_common(struct fetch_negotiator *negotiator,
		       struct fetch_pack_args *args,
		       int fd[2], struct object_id *result_oid,
		       struct ref *refs)
{
	int fetching;
	int count = 0, flushes = 0, flush_at = INITIAL_FLUSH, retval;
	int negotiation_round = 0, haves = 0;
	const struct object_id *oid;
	unsigned in_vain = 0;
	int got_continue = 0;
	int got_ready = 0;
	struct strbuf req_buf = STRBUF_INIT;
	size_t state_len = 0;
	struct packet_reader reader;

	if (args->stateless_rpc && multi_ack == 1)
		die(_("the option '%s' requires '%s'"), "--stateless-rpc", "multi_ack_detailed");

	packet_reader_init(&reader, fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	mark_tips(negotiator, args->negotiation_tips);
	for_each_cached_alternate(negotiator, insert_one_alternate_object);

	fetching = 0;
	for ( ; refs ; refs = refs->next) {
		struct object_id *remote = &refs->old_oid;
		const char *remote_hex;
		struct object *o;

		if (!args->refetch) {
			/*
			* If that object is complete (i.e. it is an ancestor of a
			* local ref), we tell them we have it but do not have to
			* tell them about its ancestors, which they already know
			* about.
			*
			* We use lookup_object here because we are only
			* interested in the case we *know* the object is
			* reachable and we have already scanned it.
			*/
			if (((o = lookup_object(the_repository, remote)) != NULL) &&
					(o->flags & COMPLETE)) {
				continue;
			}
		}

		remote_hex = oid_to_hex(remote);
		if (!fetching) {
			struct strbuf c = STRBUF_INIT;
			if (multi_ack == 2)     strbuf_addstr(&c, " multi_ack_detailed");
			if (multi_ack == 1)     strbuf_addstr(&c, " multi_ack");
			if (no_done)            strbuf_addstr(&c, " no-done");
			if (use_sideband == 2)  strbuf_addstr(&c, " side-band-64k");
			if (use_sideband == 1)  strbuf_addstr(&c, " side-band");
			if (args->deepen_relative) strbuf_addstr(&c, " deepen-relative");
			if (args->use_thin_pack) strbuf_addstr(&c, " thin-pack");
			if (args->no_progress)   strbuf_addstr(&c, " no-progress");
			if (args->include_tag)   strbuf_addstr(&c, " include-tag");
			if (prefer_ofs_delta)   strbuf_addstr(&c, " ofs-delta");
			if (deepen_since_ok)    strbuf_addstr(&c, " deepen-since");
			if (deepen_not_ok)      strbuf_addstr(&c, " deepen-not");
			if (agent_supported)    strbuf_addf(&c, " agent=%s",
							    git_user_agent_sanitized());
			if (advertise_sid)
				strbuf_addf(&c, " session-id=%s", trace2_session_id());
			if (args->filter_options.choice)
				strbuf_addstr(&c, " filter");
			packet_buf_write(&req_buf, "want %s%s\n", remote_hex, c.buf);
			strbuf_release(&c);
		} else
			packet_buf_write(&req_buf, "want %s\n", remote_hex);
		fetching++;
	}

	if (!fetching) {
		strbuf_release(&req_buf);
		packet_flush(fd[1]);
		return 1;
	}

	if (is_repository_shallow(the_repository))
		write_shallow_commits(&req_buf, 1, NULL);
	if (args->depth > 0)
		packet_buf_write(&req_buf, "deepen %d", args->depth);
	if (args->deepen_since) {
		timestamp_t max_age = approxidate(args->deepen_since);
		packet_buf_write(&req_buf, "deepen-since %"PRItime, max_age);
	}
	if (args->deepen_not) {
		int i;
		for (i = 0; i < args->deepen_not->nr; i++) {
			struct string_list_item *s = args->deepen_not->items + i;
			packet_buf_write(&req_buf, "deepen-not %s", s->string);
		}
	}
	send_filter(args, &req_buf, server_supports_filtering);
	packet_buf_flush(&req_buf);
	state_len = req_buf.len;

	if (args->deepen) {
		const char *arg;
		struct object_id oid;

		send_request(args, fd[1], &req_buf);
		while (packet_reader_read(&reader) == PACKET_READ_NORMAL) {
			if (skip_prefix(reader.line, "shallow ", &arg)) {
				if (get_oid_hex(arg, &oid))
					die(_("invalid shallow line: %s"), reader.line);
				register_shallow(the_repository, &oid);
				continue;
			}
			if (skip_prefix(reader.line, "unshallow ", &arg)) {
				if (get_oid_hex(arg, &oid))
					die(_("invalid unshallow line: %s"), reader.line);
				if (!lookup_object(the_repository, &oid))
					die(_("object not found: %s"), reader.line);
				/* make sure that it is parsed as shallow */
				if (!parse_object(the_repository, &oid))
					die(_("error in object: %s"), reader.line);
				if (unregister_shallow(&oid))
					die(_("no shallow found: %s"), reader.line);
				continue;
			}
			die(_("expected shallow/unshallow, got %s"), reader.line);
		}
	} else if (!args->stateless_rpc)
		send_request(args, fd[1], &req_buf);

	if (!args->stateless_rpc) {
		/* If we aren't using the stateless-rpc interface
		 * we don't need to retain the headers.
		 */
		strbuf_setlen(&req_buf, 0);
		state_len = 0;
	}

	trace2_region_enter("fetch-pack", "negotiation_v0_v1", the_repository);
	flushes = 0;
	retval = -1;
	while ((oid = negotiator->next(negotiator))) {
		packet_buf_write(&req_buf, "have %s\n", oid_to_hex(oid));
		print_verbose(args, "have %s", oid_to_hex(oid));
		in_vain++;
		haves++;
		if (flush_at <= ++count) {
			int ack;

			negotiation_round++;
			trace2_region_enter_printf("negotiation_v0_v1", "round",
						   the_repository, "%d",
						   negotiation_round);
			trace2_data_intmax("negotiation_v0_v1", the_repository,
					   "haves_added", haves);
			trace2_data_intmax("negotiation_v0_v1", the_repository,
					   "in_vain", in_vain);
			haves = 0;
			packet_buf_flush(&req_buf);
			send_request(args, fd[1], &req_buf);
			strbuf_setlen(&req_buf, state_len);
			flushes++;
			flush_at = next_flush(args->stateless_rpc, count);

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (!args->stateless_rpc && count == INITIAL_FLUSH)
				continue;

			consume_shallow_list(args, &reader);
			do {
				ack = get_ack(&reader, result_oid);
				if (ack)
					print_verbose(args, _("got %s %d %s"), "ack",
						      ack, oid_to_hex(result_oid));
				switch (ack) {
				case ACK:
					trace2_region_leave_printf("negotiation_v0_v1", "round",
								   the_repository, "%d",
								   negotiation_round);
					flushes = 0;
					multi_ack = 0;
					retval = 0;
					goto done;
				case ACK_common:
				case ACK_ready:
				case ACK_continue: {
					struct commit *commit =
						lookup_commit(the_repository,
							      result_oid);
					int was_common;

					if (!commit)
						die(_("invalid commit %s"), oid_to_hex(result_oid));
					was_common = negotiator->ack(negotiator, commit);
					if (args->stateless_rpc
					 && ack == ACK_common
					 && !was_common) {
						/* We need to replay the have for this object
						 * on the next RPC request so the peer knows
						 * it is in common with us.
						 */
						const char *hex = oid_to_hex(result_oid);
						packet_buf_write(&req_buf, "have %s\n", hex);
						state_len = req_buf.len;
						haves++;
						/*
						 * Reset in_vain because an ack
						 * for this commit has not been
						 * seen.
						 */
						in_vain = 0;
					} else if (!args->stateless_rpc
						   || ack != ACK_common)
						in_vain = 0;
					retval = 0;
					got_continue = 1;
					if (ack == ACK_ready)
						got_ready = 1;
					break;
					}
				}
			} while (ack);
			flushes--;
			trace2_region_leave_printf("negotiation_v0_v1", "round",
						   the_repository, "%d",
						   negotiation_round);
			if (got_continue && MAX_IN_VAIN < in_vain) {
				print_verbose(args, _("giving up"));
				break; /* give up */
			}
			if (got_ready)
				break;
		}
	}
done:
	trace2_region_leave("fetch-pack", "negotiation_v0_v1", the_repository);
	trace2_data_intmax("negotiation_v0_v1", the_repository, "total_rounds",
			   negotiation_round);
	if (!got_ready || !no_done) {
		packet_buf_write(&req_buf, "done\n");
		send_request(args, fd[1], &req_buf);
	}
	print_verbose(args, _("done"));
	if (retval != 0) {
		multi_ack = 0;
		flushes++;
	}
	strbuf_release(&req_buf);

	if (!got_ready || !no_done)
		consume_shallow_list(args, &reader);
	while (flushes || multi_ack) {
		int ack = get_ack(&reader, result_oid);
		if (ack) {
			print_verbose(args, _("got %s (%d) %s"), "ack",
				      ack, oid_to_hex(result_oid));
			if (ack == ACK)
				return 0;
			multi_ack = 1;
			continue;
		}
		flushes--;
	}
	/* it is no error to fetch into a completely empty repo */
	return count ? retval : 0;
}

static struct commit_list *complete;

static int mark_complete(const struct object_id *oid)
{
	struct commit *commit = deref_without_lazy_fetch(oid, 1);

	if (commit && !(commit->object.flags & COMPLETE)) {
		commit->object.flags |= COMPLETE;
		commit_list_insert(commit, &complete);
	}
	return 0;
}

static int mark_complete_oid(const char *refname UNUSED,
			     const struct object_id *oid,
			     int flag UNUSED,
			     void *cb_data UNUSED)
{
	return mark_complete(oid);
}

static void mark_recent_complete_commits(struct fetch_pack_args *args,
					 timestamp_t cutoff)
{
	while (complete && cutoff <= complete->item->date) {
		print_verbose(args, _("Marking %s as complete"),
			      oid_to_hex(&complete->item->object.oid));
		pop_most_recent_commit(&complete, COMPLETE);
	}
}

static void add_refs_to_oidset(struct oidset *oids, struct ref *refs)
{
	for (; refs; refs = refs->next)
		oidset_insert(oids, &refs->old_oid);
}

static int is_unmatched_ref(const struct ref *ref)
{
	struct object_id oid;
	const char *p;
	return	ref->match_status == REF_NOT_MATCHED &&
		!parse_oid_hex(ref->name, &oid, &p) &&
		*p == '\0' &&
		oideq(&oid, &ref->old_oid);
}

static void filter_refs(struct fetch_pack_args *args,
			struct ref **refs,
			struct ref **sought, int nr_sought)
{
	struct ref *newlist = NULL;
	struct ref **newtail = &newlist;
	struct ref *unmatched = NULL;
	struct ref *ref, *next;
	struct oidset tip_oids = OIDSET_INIT;
	int i;
	int strict = !(allow_unadvertised_object_request &
		       (ALLOW_TIP_SHA1 | ALLOW_REACHABLE_SHA1));

	i = 0;
	for (ref = *refs; ref; ref = next) {
		int keep = 0;
		next = ref->next;

		if (starts_with(ref->name, "refs/") &&
		    check_refname_format(ref->name, 0)) {
			/*
			 * trash or a peeled value; do not even add it to
			 * unmatched list
			 */
			free_one_ref(ref);
			continue;
		} else {
			while (i < nr_sought) {
				int cmp = strcmp(ref->name, sought[i]->name);
				if (cmp < 0)
					break; /* definitely do not have it */
				else if (cmp == 0) {
					keep = 1; /* definitely have it */
					sought[i]->match_status = REF_MATCHED;
				}
				i++;
			}

			if (!keep && args->fetch_all &&
			    (!args->deepen || !starts_with(ref->name, "refs/tags/")))
				keep = 1;
		}

		if (keep) {
			*newtail = ref;
			ref->next = NULL;
			newtail = &ref->next;
		} else {
			ref->next = unmatched;
			unmatched = ref;
		}
	}

	if (strict) {
		for (i = 0; i < nr_sought; i++) {
			ref = sought[i];
			if (!is_unmatched_ref(ref))
				continue;

			add_refs_to_oidset(&tip_oids, unmatched);
			add_refs_to_oidset(&tip_oids, newlist);
			break;
		}
	}

	/* Append unmatched requests to the list */
	for (i = 0; i < nr_sought; i++) {
		ref = sought[i];
		if (!is_unmatched_ref(ref))
			continue;

		if (!strict || oidset_contains(&tip_oids, &ref->old_oid)) {
			ref->match_status = REF_MATCHED;
			*newtail = copy_ref(ref);
			newtail = &(*newtail)->next;
		} else {
			ref->match_status = REF_UNADVERTISED_NOT_ALLOWED;
		}
	}

	oidset_clear(&tip_oids);
	free_refs(unmatched);

	*refs = newlist;
}

static void mark_alternate_complete(struct fetch_negotiator *negotiator UNUSED,
				    struct object *obj)
{
	mark_complete(&obj->oid);
}

struct loose_object_iter {
	struct oidset *loose_object_set;
	struct ref *refs;
};

/*
 * Mark recent commits available locally and reachable from a local ref as
 * COMPLETE.
 *
 * The cutoff time for recency is determined by this heuristic: it is the
 * earliest commit time of the objects in refs that are commits and that we know
 * the commit time of.
 */
static void mark_complete_and_common_ref(struct fetch_negotiator *negotiator,
					 struct fetch_pack_args *args,
					 struct ref **refs)
{
	struct ref *ref;
	int old_save_commit_buffer = save_commit_buffer;
	timestamp_t cutoff = 0;

	if (args->refetch)
		return;

	save_commit_buffer = 0;

	trace2_region_enter("fetch-pack", "parse_remote_refs_and_find_cutoff", NULL);
	for (ref = *refs; ref; ref = ref->next) {
		struct commit *commit;

		commit = lookup_commit_in_graph(the_repository, &ref->old_oid);
		if (!commit) {
			struct object *o;

			if (!repo_has_object_file_with_flags(the_repository, &ref->old_oid,
							     OBJECT_INFO_QUICK |
							     OBJECT_INFO_SKIP_FETCH_OBJECT))
				continue;
			o = parse_object(the_repository, &ref->old_oid);
			if (!o || o->type != OBJ_COMMIT)
				continue;

			commit = (struct commit *)o;
		}

		/*
		 * We already have it -- which may mean that we were
		 * in sync with the other side at some time after
		 * that (it is OK if we guess wrong here).
		 */
		if (!cutoff || cutoff < commit->date)
			cutoff = commit->date;
	}
	trace2_region_leave("fetch-pack", "parse_remote_refs_and_find_cutoff", NULL);

	/*
	 * This block marks all local refs as COMPLETE, and then recursively marks all
	 * parents of those refs as COMPLETE.
	 */
	trace2_region_enter("fetch-pack", "mark_complete_local_refs", NULL);
	if (!args->deepen) {
		for_each_rawref(mark_complete_oid, NULL);
		for_each_cached_alternate(NULL, mark_alternate_complete);
		commit_list_sort_by_date(&complete);
		if (cutoff)
			mark_recent_complete_commits(args, cutoff);
	}
	trace2_region_leave("fetch-pack", "mark_complete_local_refs", NULL);

	/*
	 * Mark all complete remote refs as common refs.
	 * Don't mark them common yet; the server has to be told so first.
	 */
	trace2_region_enter("fetch-pack", "mark_common_remote_refs", NULL);
	for (ref = *refs; ref; ref = ref->next) {
		struct commit *c = deref_without_lazy_fetch(&ref->old_oid, 0);

		if (!c || !(c->object.flags & COMPLETE))
			continue;

		negotiator->known_common(negotiator, c);
	}
	trace2_region_leave("fetch-pack", "mark_common_remote_refs", NULL);

	save_commit_buffer = old_save_commit_buffer;
}

/*
 * Returns 1 if every object pointed to by the given remote refs is available
 * locally and reachable from a local ref, and 0 otherwise.
 */
static int everything_local(struct fetch_pack_args *args,
			    struct ref **refs)
{
	struct ref *ref;
	int retval;

	for (retval = 1, ref = *refs; ref ; ref = ref->next) {
		const struct object_id *remote = &ref->old_oid;
		struct object *o;

		o = lookup_object(the_repository, remote);
		if (!o || !(o->flags & COMPLETE)) {
			retval = 0;
			print_verbose(args, "want %s (%s)", oid_to_hex(remote),
				      ref->name);
			continue;
		}
		print_verbose(args, _("already have %s (%s)"), oid_to_hex(remote),
			      ref->name);
	}

	return retval;
}

static int sideband_demux(int in UNUSED, int out, void *data)
{
	int *xd = data;
	int ret;

	ret = recv_sideband("fetch-pack", xd[0], out);
	close(out);
	return ret;
}

static void create_promisor_file(const char *keep_name,
				 struct ref **sought, int nr_sought)
{
	struct strbuf promisor_name = STRBUF_INIT;
	int suffix_stripped;

	strbuf_addstr(&promisor_name, keep_name);
	suffix_stripped = strbuf_strip_suffix(&promisor_name, ".keep");
	if (!suffix_stripped)
		BUG("name of pack lockfile should end with .keep (was '%s')",
		    keep_name);
	strbuf_addstr(&promisor_name, ".promisor");

	write_promisor_file(promisor_name.buf, sought, nr_sought);

	strbuf_release(&promisor_name);
}

static void parse_gitmodules_oids(int fd, struct oidset *gitmodules_oids)
{
	int len = the_hash_algo->hexsz + 1; /* hash + NL */

	do {
		char hex_hash[GIT_MAX_HEXSZ + 1];
		int read_len = read_in_full(fd, hex_hash, len);
		struct object_id oid;
		const char *end;

		if (!read_len)
			return;
		if (read_len != len)
			die("invalid length read %d", read_len);
		if (parse_oid_hex(hex_hash, &oid, &end) || *end != '\n')
			die("invalid hash");
		oidset_insert(gitmodules_oids, &oid);
	} while (1);
}

static void add_index_pack_keep_option(struct strvec *args)
{
	char hostname[HOST_NAME_MAX + 1];

	if (xgethostname(hostname, sizeof(hostname)))
		xsnprintf(hostname, sizeof(hostname), "localhost");
	strvec_pushf(args, "--keep=fetch-pack %"PRIuMAX " on %s",
		     (uintmax_t)getpid(), hostname);
}

/*
 * If packfile URIs were provided, pass a non-NULL pointer to index_pack_args.
 * The strings to pass as the --index-pack-arg arguments to http-fetch will be
 * stored there. (It must be freed by the caller.)
 */
static int get_pack(struct fetch_pack_args *args,
		    int xd[2], struct string_list *pack_lockfiles,
		    struct strvec *index_pack_args,
		    struct ref **sought, int nr_sought,
		    struct oidset *gitmodules_oids)
{
	struct async demux;
	int do_keep = args->keep_pack;
	const char *cmd_name;
	struct pack_header header;
	int pass_header = 0;
	struct child_process cmd = CHILD_PROCESS_INIT;
	int fsck_objects = 0;
	int ret;

	memset(&demux, 0, sizeof(demux));
	if (use_sideband) {
		/* xd[] is talking with upload-pack; subprocess reads from
		 * xd[0], spits out band#2 to stderr, and feeds us band#1
		 * through demux->out.
		 */
		demux.proc = sideband_demux;
		demux.data = xd;
		demux.out = -1;
		demux.isolate_sigpipe = 1;
		if (start_async(&demux))
			die(_("fetch-pack: unable to fork off sideband demultiplexer"));
	}
	else
		demux.out = xd[0];

	if (!args->keep_pack && unpack_limit && !index_pack_args) {

		if (read_pack_header(demux.out, &header))
			die(_("protocol error: bad pack header"));
		pass_header = 1;
		if (ntohl(header.hdr_entries) < unpack_limit)
			do_keep = 0;
		else
			do_keep = 1;
	}

	if (alternate_shallow_file) {
		strvec_push(&cmd.args, "--shallow-file");
		strvec_push(&cmd.args, alternate_shallow_file);
	}

	if (fetch_fsck_objects >= 0
	    ? fetch_fsck_objects
	    : transfer_fsck_objects >= 0
	    ? transfer_fsck_objects
	    : 0)
		fsck_objects = 1;

	if (do_keep || args->from_promisor || index_pack_args || fsck_objects) {
		if (pack_lockfiles || fsck_objects)
			cmd.out = -1;
		cmd_name = "index-pack";
		strvec_push(&cmd.args, cmd_name);
		strvec_push(&cmd.args, "--stdin");
		if (!args->quiet && !args->no_progress)
			strvec_push(&cmd.args, "-v");
		if (args->use_thin_pack)
			strvec_push(&cmd.args, "--fix-thin");
		if ((do_keep || index_pack_args) && (args->lock_pack || unpack_limit))
			add_index_pack_keep_option(&cmd.args);
		if (!index_pack_args && args->check_self_contained_and_connected)
			strvec_push(&cmd.args, "--check-self-contained-and-connected");
		else
			/*
			 * We cannot perform any connectivity checks because
			 * not all packs have been downloaded; let the caller
			 * have this responsibility.
			 */
			args->check_self_contained_and_connected = 0;

		if (args->from_promisor)
			/*
			 * create_promisor_file() may be called afterwards but
			 * we still need index-pack to know that this is a
			 * promisor pack. For example, if transfer.fsckobjects
			 * is true, index-pack needs to know that .gitmodules
			 * is a promisor object (so that it won't complain if
			 * it is missing).
			 */
			strvec_push(&cmd.args, "--promisor");
	}
	else {
		cmd_name = "unpack-objects";
		strvec_push(&cmd.args, cmd_name);
		if (args->quiet || args->no_progress)
			strvec_push(&cmd.args, "-q");
		args->check_self_contained_and_connected = 0;
	}

	if (pass_header)
		strvec_pushf(&cmd.args, "--pack_header=%"PRIu32",%"PRIu32,
			     ntohl(header.hdr_version),
				 ntohl(header.hdr_entries));
	if (fsck_objects) {
		if (args->from_promisor || index_pack_args)
			/*
			 * We cannot use --strict in index-pack because it
			 * checks both broken objects and links, but we only
			 * want to check for broken objects.
			 */
			strvec_push(&cmd.args, "--fsck-objects");
		else
			strvec_pushf(&cmd.args, "--strict%s",
				     fsck_msg_types.buf);
	}

	if (index_pack_args) {
		int i;

		for (i = 0; i < cmd.args.nr; i++)
			strvec_push(index_pack_args, cmd.args.v[i]);
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	cmd.in = demux.out;
	cmd.git_cmd = 1;
	if (start_command(&cmd))
		die(_("fetch-pack: unable to fork off %s"), cmd_name);
	if (do_keep && (pack_lockfiles || fsck_objects)) {
		int is_well_formed;
		char *pack_lockfile = index_pack_lockfile(cmd.out, &is_well_formed);

		if (!is_well_formed)
			die(_("fetch-pack: invalid index-pack output"));
		if (pack_lockfile)
			string_list_append_nodup(pack_lockfiles, pack_lockfile);
		parse_gitmodules_oids(cmd.out, gitmodules_oids);
		close(cmd.out);
	}

	if (!use_sideband)
		/* Closed by start_command() */
		xd[0] = -1;

	ret = finish_command(&cmd);
	if (!ret || (args->check_self_contained_and_connected && ret == 1))
		args->self_contained_and_connected =
			args->check_self_contained_and_connected &&
			ret == 0;
	else
		die(_("%s failed"), cmd_name);
	if (use_sideband && finish_async(&demux))
		die(_("error in sideband demultiplexer"));

	sigchain_pop(SIGPIPE);

	/*
	 * Now that index-pack has succeeded, write the promisor file using the
	 * obtained .keep filename if necessary
	 */
	if (do_keep && pack_lockfiles && pack_lockfiles->nr && args->from_promisor)
		create_promisor_file(pack_lockfiles->items[0].string, sought, nr_sought);

	return 0;
}

static int ref_compare_name(const struct ref *a, const struct ref *b)
{
	return strcmp(a->name, b->name);
}

DEFINE_LIST_SORT(static, sort_ref_list, struct ref, next);

static int cmp_ref_by_name(const void *a_, const void *b_)
{
	const struct ref *a = *((const struct ref **)a_);
	const struct ref *b = *((const struct ref **)b_);
	return strcmp(a->name, b->name);
}

static struct ref *do_fetch_pack(struct fetch_pack_args *args,
				 int fd[2],
				 const struct ref *orig_ref,
				 struct ref **sought, int nr_sought,
				 struct shallow_info *si,
				 struct string_list *pack_lockfiles)
{
	struct repository *r = the_repository;
	struct ref *ref = copy_ref_list(orig_ref);
	struct object_id oid;
	const char *agent_feature;
	int agent_len;
	struct fetch_negotiator negotiator_alloc;
	struct fetch_negotiator *negotiator;

	negotiator = &negotiator_alloc;
	if (args->refetch) {
		fetch_negotiator_init_noop(negotiator);
	} else {
		fetch_negotiator_init(r, negotiator);
	}

	sort_ref_list(&ref, ref_compare_name);
	QSORT(sought, nr_sought, cmp_ref_by_name);

	if ((agent_feature = server_feature_value("agent", &agent_len))) {
		agent_supported = 1;
		if (agent_len)
			print_verbose(args, _("Server version is %.*s"),
				      agent_len, agent_feature);
	}

	if (!server_supports("session-id"))
		advertise_sid = 0;

	if (server_supports("shallow"))
		print_verbose(args, _("Server supports %s"), "shallow");
	else if (args->depth > 0 || is_repository_shallow(r))
		die(_("Server does not support shallow clients"));
	if (args->depth > 0 || args->deepen_since || args->deepen_not)
		args->deepen = 1;
	if (server_supports("multi_ack_detailed")) {
		print_verbose(args, _("Server supports %s"), "multi_ack_detailed");
		multi_ack = 2;
		if (server_supports("no-done")) {
			print_verbose(args, _("Server supports %s"), "no-done");
			if (args->stateless_rpc)
				no_done = 1;
		}
	}
	else if (server_supports("multi_ack")) {
		print_verbose(args, _("Server supports %s"), "multi_ack");
		multi_ack = 1;
	}
	if (server_supports("side-band-64k")) {
		print_verbose(args, _("Server supports %s"), "side-band-64k");
		use_sideband = 2;
	}
	else if (server_supports("side-band")) {
		print_verbose(args, _("Server supports %s"), "side-band");
		use_sideband = 1;
	}
	if (server_supports("allow-tip-sha1-in-want")) {
		print_verbose(args, _("Server supports %s"), "allow-tip-sha1-in-want");
		allow_unadvertised_object_request |= ALLOW_TIP_SHA1;
	}
	if (server_supports("allow-reachable-sha1-in-want")) {
		print_verbose(args, _("Server supports %s"), "allow-reachable-sha1-in-want");
		allow_unadvertised_object_request |= ALLOW_REACHABLE_SHA1;
	}
	if (server_supports("thin-pack"))
		print_verbose(args, _("Server supports %s"), "thin-pack");
	else
		args->use_thin_pack = 0;
	if (server_supports("no-progress"))
		print_verbose(args, _("Server supports %s"), "no-progress");
	else
		args->no_progress = 0;
	if (server_supports("include-tag"))
		print_verbose(args, _("Server supports %s"), "include-tag");
	else
		args->include_tag = 0;
	if (server_supports("ofs-delta"))
		print_verbose(args, _("Server supports %s"), "ofs-delta");
	else
		prefer_ofs_delta = 0;

	if (server_supports("filter")) {
		server_supports_filtering = 1;
		print_verbose(args, _("Server supports %s"), "filter");
	} else if (args->filter_options.choice) {
		warning("filtering not recognized by server, ignoring");
	}

	if (server_supports("deepen-since")) {
		print_verbose(args, _("Server supports %s"), "deepen-since");
		deepen_since_ok = 1;
	} else if (args->deepen_since)
		die(_("Server does not support --shallow-since"));
	if (server_supports("deepen-not")) {
		print_verbose(args, _("Server supports %s"), "deepen-not");
		deepen_not_ok = 1;
	} else if (args->deepen_not)
		die(_("Server does not support --shallow-exclude"));
	if (server_supports("deepen-relative"))
		print_verbose(args, _("Server supports %s"), "deepen-relative");
	else if (args->deepen_relative)
		die(_("Server does not support --deepen"));
	if (!server_supports_hash(the_hash_algo->name, NULL))
		die(_("Server does not support this repository's object format"));

	mark_complete_and_common_ref(negotiator, args, &ref);
	filter_refs(args, &ref, sought, nr_sought);
	if (!args->refetch && everything_local(args, &ref)) {
		packet_flush(fd[1]);
		goto all_done;
	}
	if (find_common(negotiator, args, fd, &oid, ref) < 0)
		if (!args->keep_pack)
			/* When cloning, it is not unusual to have
			 * no common commit.
			 */
			warning(_("no common commits"));

	if (args->stateless_rpc)
		packet_flush(fd[1]);
	if (args->deepen)
		setup_alternate_shallow(&shallow_lock, &alternate_shallow_file,
					NULL);
	else if (si->nr_ours || si->nr_theirs) {
		if (args->reject_shallow_remote)
			die(_("source repository is shallow, reject to clone."));
		alternate_shallow_file = setup_temporary_shallow(si->shallow);
	} else
		alternate_shallow_file = NULL;
	if (get_pack(args, fd, pack_lockfiles, NULL, sought, nr_sought,
		     &fsck_options.gitmodules_found))
		die(_("git fetch-pack: fetch failed."));
	if (fsck_finish(&fsck_options))
		die("fsck failed");

 all_done:
	if (negotiator)
		negotiator->release(negotiator);
	return ref;
}

static void add_shallow_requests(struct strbuf *req_buf,
				 const struct fetch_pack_args *args)
{
	if (is_repository_shallow(the_repository))
		write_shallow_commits(req_buf, 1, NULL);
	if (args->depth > 0)
		packet_buf_write(req_buf, "deepen %d", args->depth);
	if (args->deepen_since) {
		timestamp_t max_age = approxidate(args->deepen_since);
		packet_buf_write(req_buf, "deepen-since %"PRItime, max_age);
	}
	if (args->deepen_not) {
		int i;
		for (i = 0; i < args->deepen_not->nr; i++) {
			struct string_list_item *s = args->deepen_not->items + i;
			packet_buf_write(req_buf, "deepen-not %s", s->string);
		}
	}
	if (args->deepen_relative)
		packet_buf_write(req_buf, "deepen-relative\n");
}

static void add_wants(const struct ref *wants, struct strbuf *req_buf)
{
	int use_ref_in_want = server_supports_feature("fetch", "ref-in-want", 0);

	for ( ; wants ; wants = wants->next) {
		const struct object_id *remote = &wants->old_oid;
		struct object *o;

		/*
		 * If that object is complete (i.e. it is an ancestor of a
		 * local ref), we tell them we have it but do not have to
		 * tell them about its ancestors, which they already know
		 * about.
		 *
		 * We use lookup_object here because we are only
		 * interested in the case we *know* the object is
		 * reachable and we have already scanned it.
		 */
		if (((o = lookup_object(the_repository, remote)) != NULL) &&
		    (o->flags & COMPLETE)) {
			continue;
		}

		if (!use_ref_in_want || wants->exact_oid)
			packet_buf_write(req_buf, "want %s\n", oid_to_hex(remote));
		else
			packet_buf_write(req_buf, "want-ref %s\n", wants->name);
	}
}

static void add_common(struct strbuf *req_buf, struct oidset *common)
{
	struct oidset_iter iter;
	const struct object_id *oid;
	oidset_iter_init(common, &iter);

	while ((oid = oidset_iter_next(&iter))) {
		packet_buf_write(req_buf, "have %s\n", oid_to_hex(oid));
	}
}

static int add_haves(struct fetch_negotiator *negotiator,
		     struct strbuf *req_buf,
		     int *haves_to_send)
{
	int haves_added = 0;
	const struct object_id *oid;

	while ((oid = negotiator->next(negotiator))) {
		packet_buf_write(req_buf, "have %s\n", oid_to_hex(oid));
		if (++haves_added >= *haves_to_send)
			break;
	}

	/* Increase haves to send on next round */
	*haves_to_send = next_flush(1, *haves_to_send);

	return haves_added;
}

static void write_fetch_command_and_capabilities(struct strbuf *req_buf,
						 const struct string_list *server_options)
{
	const char *hash_name;

	ensure_server_supports_v2("fetch");
	packet_buf_write(req_buf, "command=fetch");
	if (server_supports_v2("agent"))
		packet_buf_write(req_buf, "agent=%s", git_user_agent_sanitized());
	if (advertise_sid && server_supports_v2("session-id"))
		packet_buf_write(req_buf, "session-id=%s", trace2_session_id());
	if (server_options && server_options->nr) {
		int i;
		ensure_server_supports_v2("server-option");
		for (i = 0; i < server_options->nr; i++)
			packet_buf_write(req_buf, "server-option=%s",
					 server_options->items[i].string);
	}

	if (server_feature_v2("object-format", &hash_name)) {
		int hash_algo = hash_algo_by_name(hash_name);
		if (hash_algo_by_ptr(the_hash_algo) != hash_algo)
			die(_("mismatched algorithms: client %s; server %s"),
			    the_hash_algo->name, hash_name);
		packet_buf_write(req_buf, "object-format=%s", the_hash_algo->name);
	} else if (hash_algo_by_ptr(the_hash_algo) != GIT_HASH_SHA1) {
		die(_("the server does not support algorithm '%s'"),
		    the_hash_algo->name);
	}
	packet_buf_delim(req_buf);
}

static int send_fetch_request(struct fetch_negotiator *negotiator, int fd_out,
			      struct fetch_pack_args *args,
			      const struct ref *wants, struct oidset *common,
			      int *haves_to_send, int *in_vain,
			      int sideband_all, int seen_ack)
{
	int haves_added;
	int done_sent = 0;
	struct strbuf req_buf = STRBUF_INIT;

	write_fetch_command_and_capabilities(&req_buf, args->server_options);

	if (args->use_thin_pack)
		packet_buf_write(&req_buf, "thin-pack");
	if (args->no_progress)
		packet_buf_write(&req_buf, "no-progress");
	if (args->include_tag)
		packet_buf_write(&req_buf, "include-tag");
	if (prefer_ofs_delta)
		packet_buf_write(&req_buf, "ofs-delta");
	if (sideband_all)
		packet_buf_write(&req_buf, "sideband-all");

	/* Add shallow-info and deepen request */
	if (server_supports_feature("fetch", "shallow", 0))
		add_shallow_requests(&req_buf, args);
	else if (is_repository_shallow(the_repository) || args->deepen)
		die(_("Server does not support shallow requests"));

	/* Add filter */
	send_filter(args, &req_buf,
		    server_supports_feature("fetch", "filter", 0));

	if (server_supports_feature("fetch", "packfile-uris", 0)) {
		int i;
		struct strbuf to_send = STRBUF_INIT;

		for (i = 0; i < uri_protocols.nr; i++) {
			const char *s = uri_protocols.items[i].string;

			if (!strcmp(s, "https") || !strcmp(s, "http")) {
				if (to_send.len)
					strbuf_addch(&to_send, ',');
				strbuf_addstr(&to_send, s);
			}
		}
		if (to_send.len) {
			packet_buf_write(&req_buf, "packfile-uris %s",
					 to_send.buf);
			strbuf_release(&to_send);
		}
	}

	/* add wants */
	add_wants(wants, &req_buf);

	/* Add all of the common commits we've found in previous rounds */
	add_common(&req_buf, common);

	haves_added = add_haves(negotiator, &req_buf, haves_to_send);
	*in_vain += haves_added;
	trace2_data_intmax("negotiation_v2", the_repository, "haves_added", haves_added);
	trace2_data_intmax("negotiation_v2", the_repository, "in_vain", *in_vain);
	if (!haves_added || (seen_ack && *in_vain >= MAX_IN_VAIN)) {
		/* Send Done */
		packet_buf_write(&req_buf, "done\n");
		done_sent = 1;
	}

	/* Send request */
	packet_buf_flush(&req_buf);
	if (write_in_full(fd_out, req_buf.buf, req_buf.len) < 0)
		die_errno(_("unable to write request to remote"));

	strbuf_release(&req_buf);
	return done_sent;
}

/*
 * Processes a section header in a server's response and checks if it matches
 * `section`.  If the value of `peek` is 1, the header line will be peeked (and
 * not consumed); if 0, the line will be consumed and the function will die if
 * the section header doesn't match what was expected.
 */
static int process_section_header(struct packet_reader *reader,
				  const char *section, int peek)
{
	int ret = 0;

	if (packet_reader_peek(reader) == PACKET_READ_NORMAL &&
	    !strcmp(reader->line, section))
		ret = 1;

	if (!peek) {
		if (!ret) {
			if (reader->line)
				die(_("expected '%s', received '%s'"),
				    section, reader->line);
			else
				die(_("expected '%s'"), section);
		}
		packet_reader_read(reader);
	}

	return ret;
}

static int process_ack(struct fetch_negotiator *negotiator,
		       struct packet_reader *reader,
		       struct object_id *common_oid,
		       int *received_ready)
{
	while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
		const char *arg;

		if (!strcmp(reader->line, "NAK"))
			continue;

		if (skip_prefix(reader->line, "ACK ", &arg)) {
			if (!get_oid_hex(arg, common_oid)) {
				struct commit *commit;
				commit = lookup_commit(the_repository, common_oid);
				if (negotiator)
					negotiator->ack(negotiator, commit);
			}
			return 1;
		}

		if (!strcmp(reader->line, "ready")) {
			*received_ready = 1;
			continue;
		}

		die(_("unexpected acknowledgment line: '%s'"), reader->line);
	}

	if (reader->status != PACKET_READ_FLUSH &&
	    reader->status != PACKET_READ_DELIM)
		die(_("error processing acks: %d"), reader->status);

	/*
	 * If an "acknowledgments" section is sent, a packfile is sent if and
	 * only if "ready" was sent in this section. The other sections
	 * ("shallow-info" and "wanted-refs") are sent only if a packfile is
	 * sent. Therefore, a DELIM is expected if "ready" is sent, and a FLUSH
	 * otherwise.
	 */
	if (*received_ready && reader->status != PACKET_READ_DELIM)
		/*
		 * TRANSLATORS: The parameter will be 'ready', a protocol
		 * keyword.
		 */
		die(_("expected packfile to be sent after '%s'"), "ready");
	if (!*received_ready && reader->status != PACKET_READ_FLUSH)
		/*
		 * TRANSLATORS: The parameter will be 'ready', a protocol
		 * keyword.
		 */
		die(_("expected no other sections to be sent after no '%s'"), "ready");

	return 0;
}

static void receive_shallow_info(struct fetch_pack_args *args,
				 struct packet_reader *reader,
				 struct oid_array *shallows,
				 struct shallow_info *si)
{
	int unshallow_received = 0;

	process_section_header(reader, "shallow-info", 0);
	while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
		const char *arg;
		struct object_id oid;

		if (skip_prefix(reader->line, "shallow ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die(_("invalid shallow line: %s"), reader->line);
			oid_array_append(shallows, &oid);
			continue;
		}
		if (skip_prefix(reader->line, "unshallow ", &arg)) {
			if (get_oid_hex(arg, &oid))
				die(_("invalid unshallow line: %s"), reader->line);
			if (!lookup_object(the_repository, &oid))
				die(_("object not found: %s"), reader->line);
			/* make sure that it is parsed as shallow */
			if (!parse_object(the_repository, &oid))
				die(_("error in object: %s"), reader->line);
			if (unregister_shallow(&oid))
				die(_("no shallow found: %s"), reader->line);
			unshallow_received = 1;
			continue;
		}
		die(_("expected shallow/unshallow, got %s"), reader->line);
	}

	if (reader->status != PACKET_READ_FLUSH &&
	    reader->status != PACKET_READ_DELIM)
		die(_("error processing shallow info: %d"), reader->status);

	if (args->deepen || unshallow_received) {
		/*
		 * Treat these as shallow lines caused by our depth settings.
		 * In v0, these lines cannot cause refs to be rejected; do the
		 * same.
		 */
		int i;

		for (i = 0; i < shallows->nr; i++)
			register_shallow(the_repository, &shallows->oid[i]);
		setup_alternate_shallow(&shallow_lock, &alternate_shallow_file,
					NULL);
		args->deepen = 1;
	} else if (shallows->nr) {
		/*
		 * Treat these as shallow lines caused by the remote being
		 * shallow. In v0, remote refs that reach these objects are
		 * rejected (unless --update-shallow is set); do the same.
		 */
		prepare_shallow_info(si, shallows);
		if (si->nr_ours || si->nr_theirs) {
			if (args->reject_shallow_remote)
				die(_("source repository is shallow, reject to clone."));
			alternate_shallow_file =
				setup_temporary_shallow(si->shallow);
		} else
			alternate_shallow_file = NULL;
	} else {
		alternate_shallow_file = NULL;
	}
}

static int cmp_name_ref(const void *name, const void *ref)
{
	return strcmp(name, (*(struct ref **)ref)->name);
}

static void receive_wanted_refs(struct packet_reader *reader,
				struct ref **sought, int nr_sought)
{
	process_section_header(reader, "wanted-refs", 0);
	while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
		struct object_id oid;
		const char *end;
		struct ref **found;

		if (parse_oid_hex(reader->line, &oid, &end) || *end++ != ' ')
			die(_("expected wanted-ref, got '%s'"), reader->line);

		found = bsearch(end, sought, nr_sought, sizeof(*sought),
				cmp_name_ref);
		if (!found)
			die(_("unexpected wanted-ref: '%s'"), reader->line);
		oidcpy(&(*found)->old_oid, &oid);
	}

	if (reader->status != PACKET_READ_DELIM)
		die(_("error processing wanted refs: %d"), reader->status);
}

static void receive_packfile_uris(struct packet_reader *reader,
				  struct string_list *uris)
{
	process_section_header(reader, "packfile-uris", 0);
	while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
		if (reader->pktlen < the_hash_algo->hexsz ||
		    reader->line[the_hash_algo->hexsz] != ' ')
			die("expected '<hash> <uri>', got: %s\n", reader->line);

		string_list_append(uris, reader->line);
	}
	if (reader->status != PACKET_READ_DELIM)
		die("expected DELIM");
}

enum fetch_state {
	FETCH_CHECK_LOCAL = 0,
	FETCH_SEND_REQUEST,
	FETCH_PROCESS_ACKS,
	FETCH_GET_PACK,
	FETCH_DONE,
};

static void do_check_stateless_delimiter(int stateless_rpc,
					 struct packet_reader *reader)
{
	check_stateless_delimiter(stateless_rpc, reader,
				  _("git fetch-pack: expected response end packet"));
}

static struct ref *do_fetch_pack_v2(struct fetch_pack_args *args,
				    int fd[2],
				    const struct ref *orig_ref,
				    struct ref **sought, int nr_sought,
				    struct oid_array *shallows,
				    struct shallow_info *si,
				    struct string_list *pack_lockfiles)
{
	struct repository *r = the_repository;
	struct ref *ref = copy_ref_list(orig_ref);
	enum fetch_state state = FETCH_CHECK_LOCAL;
	struct oidset common = OIDSET_INIT;
	struct packet_reader reader;
	int in_vain = 0, negotiation_started = 0;
	int negotiation_round = 0;
	int haves_to_send = INITIAL_FLUSH;
	struct fetch_negotiator negotiator_alloc;
	struct fetch_negotiator *negotiator;
	int seen_ack = 0;
	struct object_id common_oid;
	int received_ready = 0;
	struct string_list packfile_uris = STRING_LIST_INIT_DUP;
	int i;
	struct strvec index_pack_args = STRVEC_INIT;

	negotiator = &negotiator_alloc;
	if (args->refetch)
		fetch_negotiator_init_noop(negotiator);
	else
		fetch_negotiator_init(r, negotiator);

	packet_reader_init(&reader, fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);
	if (git_env_bool("GIT_TEST_SIDEBAND_ALL", 1) &&
	    server_supports_feature("fetch", "sideband-all", 0)) {
		reader.use_sideband = 1;
		reader.me = "fetch-pack";
	}

	while (state != FETCH_DONE) {
		switch (state) {
		case FETCH_CHECK_LOCAL:
			sort_ref_list(&ref, ref_compare_name);
			QSORT(sought, nr_sought, cmp_ref_by_name);

			/* v2 supports these by default */
			allow_unadvertised_object_request |= ALLOW_REACHABLE_SHA1;
			use_sideband = 2;
			if (args->depth > 0 || args->deepen_since || args->deepen_not)
				args->deepen = 1;

			/* Filter 'ref' by 'sought' and those that aren't local */
			mark_complete_and_common_ref(negotiator, args, &ref);
			filter_refs(args, &ref, sought, nr_sought);
			if (!args->refetch && everything_local(args, &ref))
				state = FETCH_DONE;
			else
				state = FETCH_SEND_REQUEST;

			mark_tips(negotiator, args->negotiation_tips);
			for_each_cached_alternate(negotiator,
						  insert_one_alternate_object);
			break;
		case FETCH_SEND_REQUEST:
			if (!negotiation_started) {
				negotiation_started = 1;
				trace2_region_enter("fetch-pack",
						    "negotiation_v2",
						    the_repository);
			}
			negotiation_round++;
			trace2_region_enter_printf("negotiation_v2", "round",
						   the_repository, "%d",
						   negotiation_round);
			if (send_fetch_request(negotiator, fd[1], args, ref,
					       &common,
					       &haves_to_send, &in_vain,
					       reader.use_sideband,
					       seen_ack)) {
				trace2_region_leave_printf("negotiation_v2", "round",
							   the_repository, "%d",
							   negotiation_round);
				state = FETCH_GET_PACK;
			}
			else
				state = FETCH_PROCESS_ACKS;
			break;
		case FETCH_PROCESS_ACKS:
			/* Process ACKs/NAKs */
			process_section_header(&reader, "acknowledgments", 0);
			while (process_ack(negotiator, &reader, &common_oid,
					   &received_ready)) {
				in_vain = 0;
				seen_ack = 1;
				oidset_insert(&common, &common_oid);
			}
			trace2_region_leave_printf("negotiation_v2", "round",
						   the_repository, "%d",
						   negotiation_round);
			if (received_ready) {
				/*
				 * Don't check for response delimiter; get_pack() will
				 * read the rest of this response.
				 */
				state = FETCH_GET_PACK;
			} else {
				do_check_stateless_delimiter(args->stateless_rpc, &reader);
				state = FETCH_SEND_REQUEST;
			}
			break;
		case FETCH_GET_PACK:
			trace2_region_leave("fetch-pack",
					    "negotiation_v2",
					    the_repository);
			trace2_data_intmax("negotiation_v2", the_repository,
					   "total_rounds", negotiation_round);
			/* Check for shallow-info section */
			if (process_section_header(&reader, "shallow-info", 1))
				receive_shallow_info(args, &reader, shallows, si);

			if (process_section_header(&reader, "wanted-refs", 1))
				receive_wanted_refs(&reader, sought, nr_sought);

			/* get the pack(s) */
			if (git_env_bool("GIT_TRACE_REDACT", 1))
				reader.options |= PACKET_READ_REDACT_URI_PATH;
			if (process_section_header(&reader, "packfile-uris", 1))
				receive_packfile_uris(&reader, &packfile_uris);
			/* We don't expect more URIs. Reset to avoid expensive URI check. */
			reader.options &= ~PACKET_READ_REDACT_URI_PATH;

			process_section_header(&reader, "packfile", 0);

			/*
			 * this is the final request we'll make of the server;
			 * do a half-duplex shutdown to indicate that they can
			 * hang up as soon as the pack is sent.
			 */
			close(fd[1]);
			fd[1] = -1;

			if (get_pack(args, fd, pack_lockfiles,
				     packfile_uris.nr ? &index_pack_args : NULL,
				     sought, nr_sought, &fsck_options.gitmodules_found))
				die(_("git fetch-pack: fetch failed."));
			do_check_stateless_delimiter(args->stateless_rpc, &reader);

			state = FETCH_DONE;
			break;
		case FETCH_DONE:
			continue;
		}
	}

	for (i = 0; i < packfile_uris.nr; i++) {
		int j;
		struct child_process cmd = CHILD_PROCESS_INIT;
		char packname[GIT_MAX_HEXSZ + 1];
		const char *uri = packfile_uris.items[i].string +
			the_hash_algo->hexsz + 1;

		strvec_push(&cmd.args, "http-fetch");
		strvec_pushf(&cmd.args, "--packfile=%.*s",
			     (int) the_hash_algo->hexsz,
			     packfile_uris.items[i].string);
		for (j = 0; j < index_pack_args.nr; j++)
			strvec_pushf(&cmd.args, "--index-pack-arg=%s",
				     index_pack_args.v[j]);
		strvec_push(&cmd.args, uri);
		cmd.git_cmd = 1;
		cmd.no_stdin = 1;
		cmd.out = -1;
		if (start_command(&cmd))
			die("fetch-pack: unable to spawn http-fetch");

		if (read_in_full(cmd.out, packname, 5) < 0 ||
		    memcmp(packname, "keep\t", 5))
			die("fetch-pack: expected keep then TAB at start of http-fetch output");

		if (read_in_full(cmd.out, packname,
				 the_hash_algo->hexsz + 1) < 0 ||
		    packname[the_hash_algo->hexsz] != '\n')
			die("fetch-pack: expected hash then LF at end of http-fetch output");

		packname[the_hash_algo->hexsz] = '\0';

		parse_gitmodules_oids(cmd.out, &fsck_options.gitmodules_found);

		close(cmd.out);

		if (finish_command(&cmd))
			die("fetch-pack: unable to finish http-fetch");

		if (memcmp(packfile_uris.items[i].string, packname,
			   the_hash_algo->hexsz))
			die("fetch-pack: pack downloaded from %s does not match expected hash %.*s",
			    uri, (int) the_hash_algo->hexsz,
			    packfile_uris.items[i].string);

		string_list_append_nodup(pack_lockfiles,
					 xstrfmt("%s/pack/pack-%s.keep",
						 get_object_directory(),
						 packname));
	}
	string_list_clear(&packfile_uris, 0);
	strvec_clear(&index_pack_args);

	if (fsck_finish(&fsck_options))
		die("fsck failed");

	if (negotiator)
		negotiator->release(negotiator);

	oidset_clear(&common);
	return ref;
}

static int fetch_pack_config_cb(const char *var, const char *value, void *cb)
{
	if (strcmp(var, "fetch.fsck.skiplist") == 0) {
		const char *path;

		if (git_config_pathname(&path, var, value))
			return 1;
		strbuf_addf(&fsck_msg_types, "%cskiplist=%s",
			fsck_msg_types.len ? ',' : '=', path);
		free((char *)path);
		return 0;
	}

	if (skip_prefix(var, "fetch.fsck.", &var)) {
		if (is_valid_msg_type(var, value))
			strbuf_addf(&fsck_msg_types, "%c%s=%s",
				fsck_msg_types.len ? ',' : '=', var, value);
		else
			warning("Skipping unknown msg id '%s'", var);
		return 0;
	}

	return git_default_config(var, value, cb);
}

static void fetch_pack_config(void)
{
	git_config_get_int("fetch.unpacklimit", &fetch_unpack_limit);
	git_config_get_int("transfer.unpacklimit", &transfer_unpack_limit);
	git_config_get_bool("repack.usedeltabaseoffset", &prefer_ofs_delta);
	git_config_get_bool("fetch.fsckobjects", &fetch_fsck_objects);
	git_config_get_bool("transfer.fsckobjects", &transfer_fsck_objects);
	git_config_get_bool("transfer.advertisesid", &advertise_sid);
	if (!uri_protocols.nr) {
		char *str;

		if (!git_config_get_string("fetch.uriprotocols", &str) && str) {
			string_list_split(&uri_protocols, str, ',', -1);
			free(str);
		}
	}

	git_config(fetch_pack_config_cb, NULL);
}

static void fetch_pack_setup(void)
{
	static int did_setup;
	if (did_setup)
		return;
	fetch_pack_config();
	if (0 <= transfer_unpack_limit)
		unpack_limit = transfer_unpack_limit;
	else if (0 <= fetch_unpack_limit)
		unpack_limit = fetch_unpack_limit;
	did_setup = 1;
}

static int remove_duplicates_in_refs(struct ref **ref, int nr)
{
	struct string_list names = STRING_LIST_INIT_NODUP;
	int src, dst;

	for (src = dst = 0; src < nr; src++) {
		struct string_list_item *item;
		item = string_list_insert(&names, ref[src]->name);
		if (item->util)
			continue; /* already have it */
		item->util = ref[src];
		if (src != dst)
			ref[dst] = ref[src];
		dst++;
	}
	for (src = dst; src < nr; src++)
		ref[src] = NULL;
	string_list_clear(&names, 0);
	return dst;
}

static void update_shallow(struct fetch_pack_args *args,
			   struct ref **sought, int nr_sought,
			   struct shallow_info *si)
{
	struct oid_array ref = OID_ARRAY_INIT;
	int *status;
	int i;

	if (args->deepen && alternate_shallow_file) {
		if (*alternate_shallow_file == '\0') { /* --unshallow */
			unlink_or_warn(git_path_shallow(the_repository));
			rollback_shallow_file(the_repository, &shallow_lock);
		} else
			commit_shallow_file(the_repository, &shallow_lock);
		alternate_shallow_file = NULL;
		return;
	}

	if (!si->shallow || !si->shallow->nr)
		return;

	if (args->cloning) {
		/*
		 * remote is shallow, but this is a clone, there are
		 * no objects in repo to worry about. Accept any
		 * shallow points that exist in the pack (iow in repo
		 * after get_pack() and reprepare_packed_git())
		 */
		struct oid_array extra = OID_ARRAY_INIT;
		struct object_id *oid = si->shallow->oid;
		for (i = 0; i < si->shallow->nr; i++)
			if (repo_has_object_file(the_repository, &oid[i]))
				oid_array_append(&extra, &oid[i]);
		if (extra.nr) {
			setup_alternate_shallow(&shallow_lock,
						&alternate_shallow_file,
						&extra);
			commit_shallow_file(the_repository, &shallow_lock);
			alternate_shallow_file = NULL;
		}
		oid_array_clear(&extra);
		return;
	}

	if (!si->nr_ours && !si->nr_theirs)
		return;

	remove_nonexistent_theirs_shallow(si);
	if (!si->nr_ours && !si->nr_theirs)
		return;
	for (i = 0; i < nr_sought; i++)
		oid_array_append(&ref, &sought[i]->old_oid);
	si->ref = &ref;

	if (args->update_shallow) {
		/*
		 * remote is also shallow, .git/shallow may be updated
		 * so all refs can be accepted. Make sure we only add
		 * shallow roots that are actually reachable from new
		 * refs.
		 */
		struct oid_array extra = OID_ARRAY_INIT;
		struct object_id *oid = si->shallow->oid;
		assign_shallow_commits_to_refs(si, NULL, NULL);
		if (!si->nr_ours && !si->nr_theirs) {
			oid_array_clear(&ref);
			return;
		}
		for (i = 0; i < si->nr_ours; i++)
			oid_array_append(&extra, &oid[si->ours[i]]);
		for (i = 0; i < si->nr_theirs; i++)
			oid_array_append(&extra, &oid[si->theirs[i]]);
		setup_alternate_shallow(&shallow_lock,
					&alternate_shallow_file,
					&extra);
		commit_shallow_file(the_repository, &shallow_lock);
		oid_array_clear(&extra);
		oid_array_clear(&ref);
		alternate_shallow_file = NULL;
		return;
	}

	/*
	 * remote is also shallow, check what ref is safe to update
	 * without updating .git/shallow
	 */
	CALLOC_ARRAY(status, nr_sought);
	assign_shallow_commits_to_refs(si, NULL, status);
	if (si->nr_ours || si->nr_theirs) {
		for (i = 0; i < nr_sought; i++)
			if (status[i])
				sought[i]->status = REF_STATUS_REJECT_SHALLOW;
	}
	free(status);
	oid_array_clear(&ref);
}

static const struct object_id *iterate_ref_map(void *cb_data)
{
	struct ref **rm = cb_data;
	struct ref *ref = *rm;

	if (!ref)
		return NULL;
	*rm = ref->next;
	return &ref->old_oid;
}

struct ref *fetch_pack(struct fetch_pack_args *args,
		       int fd[],
		       const struct ref *ref,
		       struct ref **sought, int nr_sought,
		       struct oid_array *shallow,
		       struct string_list *pack_lockfiles,
		       enum protocol_version version)
{
	struct ref *ref_cpy;
	struct shallow_info si;
	struct oid_array shallows_scratch = OID_ARRAY_INIT;

	fetch_pack_setup();
	if (nr_sought)
		nr_sought = remove_duplicates_in_refs(sought, nr_sought);

	if (version != protocol_v2 && !ref) {
		packet_flush(fd[1]);
		die(_("no matching remote head"));
	}
	if (version == protocol_v2) {
		if (shallow->nr)
			BUG("Protocol V2 does not provide shallows at this point in the fetch");
		memset(&si, 0, sizeof(si));
		ref_cpy = do_fetch_pack_v2(args, fd, ref, sought, nr_sought,
					   &shallows_scratch, &si,
					   pack_lockfiles);
	} else {
		prepare_shallow_info(&si, shallow);
		ref_cpy = do_fetch_pack(args, fd, ref, sought, nr_sought,
					&si, pack_lockfiles);
	}
	reprepare_packed_git(the_repository);

	if (!args->cloning && args->deepen) {
		struct check_connected_options opt = CHECK_CONNECTED_INIT;
		struct ref *iterator = ref_cpy;
		opt.shallow_file = alternate_shallow_file;
		if (args->deepen)
			opt.is_deepening_fetch = 1;
		if (check_connected(iterate_ref_map, &iterator, &opt)) {
			error(_("remote did not send all necessary objects"));
			free_refs(ref_cpy);
			ref_cpy = NULL;
			rollback_shallow_file(the_repository, &shallow_lock);
			goto cleanup;
		}
		args->connectivity_checked = 1;
	}

	update_shallow(args, sought, nr_sought, &si);
cleanup:
	clear_shallow_info(&si);
	oid_array_clear(&shallows_scratch);
	return ref_cpy;
}

static int add_to_object_array(const struct object_id *oid, void *data)
{
	struct object_array *a = data;

	add_object_array(lookup_object(the_repository, oid), "", a);
	return 0;
}

static void clear_common_flag(struct oidset *s)
{
	struct oidset_iter iter;
	const struct object_id *oid;
	oidset_iter_init(s, &iter);

	while ((oid = oidset_iter_next(&iter))) {
		struct object *obj = lookup_object(the_repository, oid);
		obj->flags &= ~COMMON;
	}
}

void negotiate_using_fetch(const struct oid_array *negotiation_tips,
			   const struct string_list *server_options,
			   int stateless_rpc,
			   int fd[],
			   struct oidset *acked_commits)
{
	struct fetch_negotiator negotiator;
	struct packet_reader reader;
	struct object_array nt_object_array = OBJECT_ARRAY_INIT;
	struct strbuf req_buf = STRBUF_INIT;
	int haves_to_send = INITIAL_FLUSH;
	int in_vain = 0;
	int seen_ack = 0;
	int last_iteration = 0;
	int negotiation_round = 0;
	timestamp_t min_generation = GENERATION_NUMBER_INFINITY;

	fetch_negotiator_init(the_repository, &negotiator);
	mark_tips(&negotiator, negotiation_tips);

	packet_reader_init(&reader, fd[0], NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	oid_array_for_each((struct oid_array *) negotiation_tips,
			   add_to_object_array,
			   &nt_object_array);

	trace2_region_enter("fetch-pack", "negotiate_using_fetch", the_repository);
	while (!last_iteration) {
		int haves_added;
		struct object_id common_oid;
		int received_ready = 0;

		negotiation_round++;

		trace2_region_enter_printf("negotiate_using_fetch", "round",
					   the_repository, "%d",
					   negotiation_round);
		strbuf_reset(&req_buf);
		write_fetch_command_and_capabilities(&req_buf, server_options);

		packet_buf_write(&req_buf, "wait-for-done");

		haves_added = add_haves(&negotiator, &req_buf, &haves_to_send);
		in_vain += haves_added;
		if (!haves_added || (seen_ack && in_vain >= MAX_IN_VAIN))
			last_iteration = 1;

		trace2_data_intmax("negotiate_using_fetch", the_repository,
				   "haves_added", haves_added);
		trace2_data_intmax("negotiate_using_fetch", the_repository,
				   "in_vain", in_vain);

		/* Send request */
		packet_buf_flush(&req_buf);
		if (write_in_full(fd[1], req_buf.buf, req_buf.len) < 0)
			die_errno(_("unable to write request to remote"));

		/* Process ACKs/NAKs */
		process_section_header(&reader, "acknowledgments", 0);
		while (process_ack(&negotiator, &reader, &common_oid,
				   &received_ready)) {
			struct commit *commit = lookup_commit(the_repository,
							      &common_oid);
			if (commit) {
				timestamp_t generation;

				parse_commit_or_die(commit);
				commit->object.flags |= COMMON;
				generation = commit_graph_generation(commit);
				if (generation < min_generation)
					min_generation = generation;
			}
			in_vain = 0;
			seen_ack = 1;
			oidset_insert(acked_commits, &common_oid);
		}
		if (received_ready)
			die(_("unexpected 'ready' from remote"));
		else
			do_check_stateless_delimiter(stateless_rpc, &reader);
		if (can_all_from_reach_with_flag(&nt_object_array, COMMON,
						 REACH_SCRATCH, 0,
						 min_generation))
			last_iteration = 1;
		trace2_region_leave_printf("negotiation", "round",
					   the_repository, "%d",
					   negotiation_round);
	}
	trace2_region_enter("fetch-pack", "negotiate_using_fetch", the_repository);
	trace2_data_intmax("negotiate_using_fetch", the_repository,
			   "total_rounds", negotiation_round);
	clear_common_flag(acked_commits);
	strbuf_release(&req_buf);
}

int report_unmatched_refs(struct ref **sought, int nr_sought)
{
	int i, ret = 0;

	for (i = 0; i < nr_sought; i++) {
		if (!sought[i])
			continue;
		switch (sought[i]->match_status) {
		case REF_MATCHED:
			continue;
		case REF_NOT_MATCHED:
			error(_("no such remote ref %s"), sought[i]->name);
			break;
		case REF_UNADVERTISED_NOT_ALLOWED:
			error(_("Server does not allow request for unadvertised object %s"),
			      sought[i]->name);
			break;
		}
		ret = 1;
	}
	return ret;
}
