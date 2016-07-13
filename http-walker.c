#include "cache.h"
#include "commit.h"
#include "walker.h"
#include "http.h"
#include "list.h"

struct alt_base {
	char *base;
	int got_indices;
	struct packed_git *packs;
	struct alt_base *next;
};

enum object_request_state {
	WAITING,
	ABORTED,
	ACTIVE,
	COMPLETE
};

struct object_request {
	struct walker *walker;
	unsigned char sha1[20];
	struct alt_base *repo;
	enum object_request_state state;
	struct http_object_request *req;
	struct list_head node;
};

struct alternates_request {
	struct walker *walker;
	const char *base;
	struct strbuf *url;
	struct strbuf *buffer;
	struct active_request_slot *slot;
	int http_specific;
};

struct walker_data {
	const char *url;
	int got_alternates;
	struct alt_base *alt;
};

static LIST_HEAD(object_queue_head);

static void fetch_alternates(struct walker *walker, const char *base);

static void process_object_response(void *callback_data);

static void start_object_request(struct walker *walker,
				 struct object_request *obj_req)
{
	struct active_request_slot *slot;
	struct http_object_request *req;

	req = new_http_object_request(obj_req->repo->base, obj_req->sha1);
	if (req == NULL) {
		obj_req->state = ABORTED;
		return;
	}
	obj_req->req = req;

	slot = req->slot;
	slot->callback_func = process_object_response;
	slot->callback_data = obj_req;

	/* Try to get the request started, abort the request on error */
	obj_req->state = ACTIVE;
	if (!start_active_slot(slot)) {
		obj_req->state = ABORTED;
		release_http_object_request(req);
		return;
	}
}

static void finish_object_request(struct object_request *obj_req)
{
	if (finish_http_object_request(obj_req->req))
		return;

	if (obj_req->req->rename == 0)
		walker_say(obj_req->walker, "got %s\n", sha1_to_hex(obj_req->sha1));
}

static void process_object_response(void *callback_data)
{
	struct object_request *obj_req =
		(struct object_request *)callback_data;
	struct walker *walker = obj_req->walker;
	struct walker_data *data = walker->data;
	struct alt_base *alt = data->alt;

	process_http_object_request(obj_req->req);
	obj_req->state = COMPLETE;

	/* Use alternates if necessary */
	if (missing_target(obj_req->req)) {
		fetch_alternates(walker, alt->base);
		if (obj_req->repo->next != NULL) {
			obj_req->repo =
				obj_req->repo->next;
			release_http_object_request(obj_req->req);
			start_object_request(walker, obj_req);
			return;
		}
	}

	finish_object_request(obj_req);
}

static void release_object_request(struct object_request *obj_req)
{
	if (obj_req->req !=NULL && obj_req->req->localfile != -1)
		error("fd leakage in release: %d", obj_req->req->localfile);

	list_del(&obj_req->node);
	free(obj_req);
}

#ifdef USE_CURL_MULTI
static int fill_active_slot(struct walker *walker)
{
	struct object_request *obj_req;
	struct list_head *pos, *tmp, *head = &object_queue_head;

	list_for_each_safe(pos, tmp, head) {
		obj_req = list_entry(pos, struct object_request, node);
		if (obj_req->state == WAITING) {
			if (has_sha1_file(obj_req->sha1))
				obj_req->state = COMPLETE;
			else {
				start_object_request(walker, obj_req);
				return 1;
			}
		}
	}
	return 0;
}
#endif

static void prefetch(struct walker *walker, unsigned char *sha1)
{
	struct object_request *newreq;
	struct walker_data *data = walker->data;

	newreq = xmalloc(sizeof(*newreq));
	newreq->walker = walker;
	hashcpy(newreq->sha1, sha1);
	newreq->repo = data->alt;
	newreq->state = WAITING;
	newreq->req = NULL;

	http_is_verbose = walker->get_verbosely;
	list_add_tail(&newreq->node, &object_queue_head);

#ifdef USE_CURL_MULTI
	fill_active_slots();
	step_active_slots();
#endif
}

static void process_alternates_response(void *callback_data)
{
	struct alternates_request *alt_req =
		(struct alternates_request *)callback_data;
	struct walker *walker = alt_req->walker;
	struct walker_data *cdata = walker->data;
	struct active_request_slot *slot = alt_req->slot;
	struct alt_base *tail = cdata->alt;
	const char *base = alt_req->base;
	const char null_byte = '\0';
	char *data;
	int i = 0;

	if (alt_req->http_specific) {
		if (slot->curl_result != CURLE_OK ||
		    !alt_req->buffer->len) {

			/* Try reusing the slot to get non-http alternates */
			alt_req->http_specific = 0;
			strbuf_reset(alt_req->url);
			strbuf_addf(alt_req->url, "%s/objects/info/alternates",
				    base);
			curl_easy_setopt(slot->curl, CURLOPT_URL,
					 alt_req->url->buf);
			active_requests++;
			slot->in_use = 1;
			if (slot->finished != NULL)
				(*slot->finished) = 0;
			if (!start_active_slot(slot)) {
				cdata->got_alternates = -1;
				slot->in_use = 0;
				if (slot->finished != NULL)
					(*slot->finished) = 1;
			}
			return;
		}
	} else if (slot->curl_result != CURLE_OK) {
		if (!missing_target(slot)) {
			cdata->got_alternates = -1;
			return;
		}
	}

	fwrite_buffer((char *)&null_byte, 1, 1, alt_req->buffer);
	alt_req->buffer->len--;
	data = alt_req->buffer->buf;

	while (i < alt_req->buffer->len) {
		int posn = i;
		while (posn < alt_req->buffer->len && data[posn] != '\n')
			posn++;
		if (data[posn] == '\n') {
			int okay = 0;
			int serverlen = 0;
			struct alt_base *newalt;
			if (data[i] == '/') {
				/*
				 * This counts
				 * http://git.host/pub/scm/linux.git/
				 * -----------here^
				 * so memcpy(dst, base, serverlen) will
				 * copy up to "...git.host".
				 */
				const char *colon_ss = strstr(base,"://");
				if (colon_ss) {
					serverlen = (strchr(colon_ss + 3, '/')
						     - base);
					okay = 1;
				}
			} else if (!memcmp(data + i, "../", 3)) {
				/*
				 * Relative URL; chop the corresponding
				 * number of subpath from base (and ../
				 * from data), and concatenate the result.
				 *
				 * The code first drops ../ from data, and
				 * then drops one ../ from data and one path
				 * from base.  IOW, one extra ../ is dropped
				 * from data than path is dropped from base.
				 *
				 * This is not wrong.  The alternate in
				 *     http://git.host/pub/scm/linux.git/
				 * to borrow from
				 *     http://git.host/pub/scm/linus.git/
				 * is ../../linus.git/objects/.  You need
				 * two ../../ to borrow from your direct
				 * neighbour.
				 */
				i += 3;
				serverlen = strlen(base);
				while (i + 2 < posn &&
				       !memcmp(data + i, "../", 3)) {
					do {
						serverlen--;
					} while (serverlen &&
						 base[serverlen - 1] != '/');
					i += 3;
				}
				/* If the server got removed, give up. */
				okay = strchr(base, ':') - base + 3 <
				       serverlen;
			} else if (alt_req->http_specific) {
				char *colon = strchr(data + i, ':');
				char *slash = strchr(data + i, '/');
				if (colon && slash && colon < data + posn &&
				    slash < data + posn && colon < slash) {
					okay = 1;
				}
			}
			/* skip "objects\n" at end */
			if (okay) {
				struct strbuf target = STRBUF_INIT;
				strbuf_add(&target, base, serverlen);
				strbuf_add(&target, data + i, posn - i - 7);
				if (walker->get_verbosely)
					fprintf(stderr, "Also look at %s\n",
						target.buf);
				newalt = xmalloc(sizeof(*newalt));
				newalt->next = NULL;
				newalt->base = strbuf_detach(&target, NULL);
				newalt->got_indices = 0;
				newalt->packs = NULL;

				while (tail->next != NULL)
					tail = tail->next;
				tail->next = newalt;
			}
		}
		i = posn + 1;
	}

	cdata->got_alternates = 1;
}

static void fetch_alternates(struct walker *walker, const char *base)
{
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf url = STRBUF_INIT;
	struct active_request_slot *slot;
	struct alternates_request alt_req;
	struct walker_data *cdata = walker->data;

	/*
	 * If another request has already started fetching alternates,
	 * wait for them to arrive and return to processing this request's
	 * curl message
	 */
#ifdef USE_CURL_MULTI
	while (cdata->got_alternates == 0) {
		step_active_slots();
	}
#endif

	/* Nothing to do if they've already been fetched */
	if (cdata->got_alternates == 1)
		return;

	/* Start the fetch */
	cdata->got_alternates = 0;

	if (walker->get_verbosely)
		fprintf(stderr, "Getting alternates list for %s\n", base);

	strbuf_addf(&url, "%s/objects/info/http-alternates", base);

	/*
	 * Use a callback to process the result, since another request
	 * may fail and need to have alternates loaded before continuing
	 */
	slot = get_active_slot();
	slot->callback_func = process_alternates_response;
	alt_req.walker = walker;
	slot->callback_data = &alt_req;

	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url.buf);

	alt_req.base = base;
	alt_req.url = &url;
	alt_req.buffer = &buffer;
	alt_req.http_specific = 1;
	alt_req.slot = slot;

	if (start_active_slot(slot))
		run_active_slot(slot);
	else
		cdata->got_alternates = -1;

	strbuf_release(&buffer);
	strbuf_release(&url);
}

static int fetch_indices(struct walker *walker, struct alt_base *repo)
{
	int ret;

	if (repo->got_indices)
		return 0;

	if (walker->get_verbosely)
		fprintf(stderr, "Getting pack list for %s\n", repo->base);

	switch (http_get_info_packs(repo->base, &repo->packs)) {
	case HTTP_OK:
	case HTTP_MISSING_TARGET:
		repo->got_indices = 1;
		ret = 0;
		break;
	default:
		repo->got_indices = 0;
		ret = -1;
	}

	return ret;
}

static int http_fetch_pack(struct walker *walker, struct alt_base *repo, unsigned char *sha1)
{
	struct packed_git *target;
	int ret;
	struct slot_results results;
	struct http_pack_request *preq;

	if (fetch_indices(walker, repo))
		return -1;
	target = find_sha1_pack(sha1, repo->packs);
	if (!target)
		return -1;

	if (walker->get_verbosely) {
		fprintf(stderr, "Getting pack %s\n",
			sha1_to_hex(target->sha1));
		fprintf(stderr, " which contains %s\n",
			sha1_to_hex(sha1));
	}

	preq = new_http_pack_request(target, repo->base);
	if (preq == NULL)
		goto abort;
	preq->lst = &repo->packs;
	preq->slot->results = &results;

	if (start_active_slot(preq->slot)) {
		run_active_slot(preq->slot);
		if (results.curl_result != CURLE_OK) {
			error("Unable to get pack file %s\n%s", preq->url,
			      curl_errorstr);
			goto abort;
		}
	} else {
		error("Unable to start request");
		goto abort;
	}

	ret = finish_http_pack_request(preq);
	release_http_pack_request(preq);
	if (ret)
		return ret;

	return 0;

abort:
	return -1;
}

static void abort_object_request(struct object_request *obj_req)
{
	release_object_request(obj_req);
}

static int fetch_object(struct walker *walker, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	int ret = 0;
	struct object_request *obj_req = NULL;
	struct http_object_request *req;
	struct list_head *pos, *head = &object_queue_head;

	list_for_each(pos, head) {
		obj_req = list_entry(pos, struct object_request, node);
		if (!hashcmp(obj_req->sha1, sha1))
			break;
	}
	if (obj_req == NULL)
		return error("Couldn't find request for %s in the queue", hex);

	if (has_sha1_file(obj_req->sha1)) {
		if (obj_req->req != NULL)
			abort_http_object_request(obj_req->req);
		abort_object_request(obj_req);
		return 0;
	}

#ifdef USE_CURL_MULTI
	while (obj_req->state == WAITING)
		step_active_slots();
#else
	start_object_request(walker, obj_req);
#endif

	/*
	 * obj_req->req might change when fetching alternates in the callback
	 * process_object_response; therefore, the "shortcut" variable, req,
	 * is used only after we're done with slots.
	 */
	while (obj_req->state == ACTIVE)
		run_active_slot(obj_req->req->slot);

	req = obj_req->req;

	if (req->localfile != -1) {
		close(req->localfile);
		req->localfile = -1;
	}

	/*
	 * we turned off CURLOPT_FAILONERROR to avoid losing a
	 * persistent connection and got CURLE_OK.
	 */
	if (req->http_code == 404 && req->curl_result == CURLE_OK &&
			(starts_with(req->url, "http://") ||
			 starts_with(req->url, "https://")))
		req->curl_result = CURLE_HTTP_RETURNED_ERROR;

	if (obj_req->state == ABORTED) {
		ret = error("Request for %s aborted", hex);
	} else if (req->curl_result != CURLE_OK &&
		   req->http_code != 416) {
		if (missing_target(req))
			ret = -1; /* Be silent, it is probably in a pack. */
		else
			ret = error("%s (curl_result = %d, http_code = %ld, sha1 = %s)",
				    req->errorstr, req->curl_result,
				    req->http_code, hex);
	} else if (req->zret != Z_STREAM_END) {
		walker->corrupt_object_found++;
		ret = error("File %s (%s) corrupt", hex, req->url);
	} else if (hashcmp(obj_req->sha1, req->real_sha1)) {
		ret = error("File %s has bad hash", hex);
	} else if (req->rename < 0) {
		ret = error("unable to write sha1 filename %s",
			    sha1_file_name(req->sha1));
	}

	release_http_object_request(req);
	release_object_request(obj_req);
	return ret;
}

static int fetch(struct walker *walker, unsigned char *sha1)
{
	struct walker_data *data = walker->data;
	struct alt_base *altbase = data->alt;

	if (!fetch_object(walker, sha1))
		return 0;
	while (altbase) {
		if (!http_fetch_pack(walker, altbase, sha1))
			return 0;
		fetch_alternates(walker, data->alt->base);
		altbase = altbase->next;
	}
	return error("Unable to find %s under %s", sha1_to_hex(sha1),
		     data->alt->base);
}

static int fetch_ref(struct walker *walker, struct ref *ref)
{
	struct walker_data *data = walker->data;
	return http_fetch_ref(data->alt->base, ref);
}

static void cleanup(struct walker *walker)
{
	struct walker_data *data = walker->data;
	struct alt_base *alt, *alt_next;

	if (data) {
		alt = data->alt;
		while (alt) {
			alt_next = alt->next;

			free(alt->base);
			free(alt);

			alt = alt_next;
		}
		free(data);
		walker->data = NULL;
	}
}

struct walker *get_http_walker(const char *url)
{
	char *s;
	struct walker_data *data = xmalloc(sizeof(struct walker_data));
	struct walker *walker = xmalloc(sizeof(struct walker));

	data->alt = xmalloc(sizeof(*data->alt));
	data->alt->base = xstrdup(url);
	for (s = data->alt->base + strlen(data->alt->base) - 1; *s == '/'; --s)
		*s = 0;

	data->alt->got_indices = 0;
	data->alt->packs = NULL;
	data->alt->next = NULL;
	data->got_alternates = -1;

	walker->corrupt_object_found = 0;
	walker->fetch = fetch;
	walker->fetch_ref = fetch_ref;
	walker->prefetch = prefetch;
	walker->cleanup = cleanup;
	walker->data = data;

#ifdef USE_CURL_MULTI
	add_fill_function(walker, (int (*)(void *)) fill_active_slot);
#endif

	return walker;
}
