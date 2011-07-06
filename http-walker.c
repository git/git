#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "walker.h"
#include "http.h"

#define PREV_BUF_SIZE 4096
#define RANGE_HEADER_SIZE 30

struct alt_base
{
	char *base;
	int got_indices;
	struct packed_git *packs;
	struct alt_base *next;
};

enum object_request_state {
	WAITING,
	ABORTED,
	ACTIVE,
	COMPLETE,
};

struct object_request
{
	struct walker *walker;
	unsigned char sha1[20];
	struct alt_base *repo;
	char *url;
	char filename[PATH_MAX];
	char tmpfile[PATH_MAX];
	int local;
	enum object_request_state state;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	unsigned char real_sha1[20];
	SHA_CTX c;
	z_stream stream;
	int zret;
	int rename;
	struct active_request_slot *slot;
	struct object_request *next;
};

struct alternates_request {
	struct walker *walker;
	const char *base;
	char *url;
	struct strbuf *buffer;
	struct active_request_slot *slot;
	int http_specific;
};

struct walker_data {
	const char *url;
	int got_alternates;
	struct alt_base *alt;
	struct curl_slist *no_pragma_header;
};

static struct object_request *object_queue_head;

static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	struct object_request *obj_req = (struct object_request *)data;
	do {
		ssize_t retval = xwrite(obj_req->local,
				       (char *) ptr + posn, size - posn);
		if (retval < 0)
			return posn;
		posn += retval;
	} while (posn < size);

	obj_req->stream.avail_in = size;
	obj_req->stream.next_in = ptr;
	do {
		obj_req->stream.next_out = expn;
		obj_req->stream.avail_out = sizeof(expn);
		obj_req->zret = inflate(&obj_req->stream, Z_SYNC_FLUSH);
		SHA1_Update(&obj_req->c, expn,
			    sizeof(expn) - obj_req->stream.avail_out);
	} while (obj_req->stream.avail_in && obj_req->zret == Z_OK);
	data_received++;
	return size;
}

static void fetch_alternates(struct walker *walker, const char *base);

static void process_object_response(void *callback_data);

static void start_object_request(struct walker *walker,
				 struct object_request *obj_req)
{
	char *hex = sha1_to_hex(obj_req->sha1);
	char prevfile[PATH_MAX];
	char *url;
	char *posn;
	int prevlocal;
	unsigned char prev_buf[PREV_BUF_SIZE];
	ssize_t prev_read = 0;
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;
	struct active_request_slot *slot;
	struct walker_data *data = walker->data;

	snprintf(prevfile, sizeof(prevfile), "%s.prev", obj_req->filename);
	unlink(prevfile);
	rename(obj_req->tmpfile, prevfile);
	unlink(obj_req->tmpfile);

	if (obj_req->local != -1)
		error("fd leakage in start: %d", obj_req->local);
	obj_req->local = open(obj_req->tmpfile,
			      O_WRONLY | O_CREAT | O_EXCL, 0666);
	/* This could have failed due to the "lazy directory creation";
	 * try to mkdir the last path component.
	 */
	if (obj_req->local < 0 && errno == ENOENT) {
		char *dir = strrchr(obj_req->tmpfile, '/');
		if (dir) {
			*dir = 0;
			mkdir(obj_req->tmpfile, 0777);
			*dir = '/';
		}
		obj_req->local = open(obj_req->tmpfile,
				      O_WRONLY | O_CREAT | O_EXCL, 0666);
	}

	if (obj_req->local < 0) {
		obj_req->state = ABORTED;
		error("Couldn't create temporary file %s for %s: %s",
		      obj_req->tmpfile, obj_req->filename, strerror(errno));
		return;
	}

	memset(&obj_req->stream, 0, sizeof(obj_req->stream));

	inflateInit(&obj_req->stream);

	SHA1_Init(&obj_req->c);

	url = xmalloc(strlen(obj_req->repo->base) + 51);
	obj_req->url = xmalloc(strlen(obj_req->repo->base) + 51);
	strcpy(url, obj_req->repo->base);
	posn = url + strlen(obj_req->repo->base);
	strcpy(posn, "/objects/");
	posn += 9;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);
	strcpy(obj_req->url, url);

	/* If a previous temp file is present, process what was already
	   fetched. */
	prevlocal = open(prevfile, O_RDONLY);
	if (prevlocal != -1) {
		do {
			prev_read = xread(prevlocal, prev_buf, PREV_BUF_SIZE);
			if (prev_read>0) {
				if (fwrite_sha1_file(prev_buf,
						     1,
						     prev_read,
						     obj_req) == prev_read) {
					prev_posn += prev_read;
				} else {
					prev_read = -1;
				}
			}
		} while (prev_read > 0);
		close(prevlocal);
	}
	unlink(prevfile);

	/* Reset inflate/SHA1 if there was an error reading the previous temp
	   file; also rewind to the beginning of the local file. */
	if (prev_read == -1) {
		memset(&obj_req->stream, 0, sizeof(obj_req->stream));
		inflateInit(&obj_req->stream);
		SHA1_Init(&obj_req->c);
		if (prev_posn>0) {
			prev_posn = 0;
			lseek(obj_req->local, 0, SEEK_SET);
			ftruncate(obj_req->local, 0);
		}
	}

	slot = get_active_slot();
	slot->callback_func = process_object_response;
	slot->callback_data = obj_req;
	obj_req->slot = slot;

	curl_easy_setopt(slot->curl, CURLOPT_FILE, obj_req);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, obj_req->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, data->no_pragma_header);

	/* If we have successfully processed data from a previous fetch
	   attempt, only fetch the data we don't already have. */
	if (prev_posn>0) {
		if (walker->get_verbosely)
			fprintf(stderr,
				"Resuming fetch of object %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl,
				 CURLOPT_HTTPHEADER, range_header);
	}

	/* Try to get the request started, abort the request on error */
	obj_req->state = ACTIVE;
	if (!start_active_slot(slot)) {
		obj_req->state = ABORTED;
		obj_req->slot = NULL;
		close(obj_req->local); obj_req->local = -1;
		free(obj_req->url);
		return;
	}
}

static void finish_object_request(struct object_request *obj_req)
{
	struct stat st;

	fchmod(obj_req->local, 0444);
	close(obj_req->local); obj_req->local = -1;

	if (obj_req->http_code == 416) {
		fprintf(stderr, "Warning: requested range invalid; we may already have all the data.\n");
	} else if (obj_req->curl_result != CURLE_OK) {
		if (stat(obj_req->tmpfile, &st) == 0)
			if (st.st_size == 0)
				unlink(obj_req->tmpfile);
		return;
	}

	inflateEnd(&obj_req->stream);
	SHA1_Final(obj_req->real_sha1, &obj_req->c);
	if (obj_req->zret != Z_STREAM_END) {
		unlink(obj_req->tmpfile);
		return;
	}
	if (hashcmp(obj_req->sha1, obj_req->real_sha1)) {
		unlink(obj_req->tmpfile);
		return;
	}
	obj_req->rename =
		move_temp_to_file(obj_req->tmpfile, obj_req->filename);

	if (obj_req->rename == 0)
		walker_say(obj_req->walker, "got %s\n", sha1_to_hex(obj_req->sha1));
}

static void process_object_response(void *callback_data)
{
	struct object_request *obj_req =
		(struct object_request *)callback_data;
	struct walker *walker = obj_req->walker;
	struct walker_data *data = walker->data;
	struct alt_base *alt = data->alt;

	obj_req->curl_result = obj_req->slot->curl_result;
	obj_req->http_code = obj_req->slot->http_code;
	obj_req->slot = NULL;
	obj_req->state = COMPLETE;

	/* Use alternates if necessary */
	if (missing_target(obj_req)) {
		fetch_alternates(walker, alt->base);
		if (obj_req->repo->next != NULL) {
			obj_req->repo =
				obj_req->repo->next;
			close(obj_req->local);
			obj_req->local = -1;
			start_object_request(walker, obj_req);
			return;
		}
	}

	finish_object_request(obj_req);
}

static void release_object_request(struct object_request *obj_req)
{
	struct object_request *entry = object_queue_head;

	if (obj_req->local != -1)
		error("fd leakage in release: %d", obj_req->local);
	if (obj_req == object_queue_head) {
		object_queue_head = obj_req->next;
	} else {
		while (entry->next != NULL && entry->next != obj_req)
			entry = entry->next;
		if (entry->next == obj_req)
			entry->next = entry->next->next;
	}

	free(obj_req->url);
	free(obj_req);
}

#ifdef USE_CURL_MULTI
static int fill_active_slot(struct walker *walker)
{
	struct object_request *obj_req;

	for (obj_req = object_queue_head; obj_req; obj_req = obj_req->next) {
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
	struct object_request *tail;
	struct walker_data *data = walker->data;
	char *filename = sha1_file_name(sha1);

	newreq = xmalloc(sizeof(*newreq));
	newreq->walker = walker;
	hashcpy(newreq->sha1, sha1);
	newreq->repo = data->alt;
	newreq->url = NULL;
	newreq->local = -1;
	newreq->state = WAITING;
	snprintf(newreq->filename, sizeof(newreq->filename), "%s", filename);
	snprintf(newreq->tmpfile, sizeof(newreq->tmpfile),
		 "%s.temp", filename);
	newreq->slot = NULL;
	newreq->next = NULL;

	if (object_queue_head == NULL) {
		object_queue_head = newreq;
	} else {
		tail = object_queue_head;
		while (tail->next != NULL) {
			tail = tail->next;
		}
		tail->next = newreq;
	}

#ifdef USE_CURL_MULTI
	fill_active_slots();
	step_active_slots();
#endif
}

static int fetch_index(struct walker *walker, struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename;
	char *url;
	char tmpfile[PATH_MAX];
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;
	struct walker_data *data = walker->data;

	FILE *indexfile;
	struct active_request_slot *slot;
	struct slot_results results;

	if (has_pack_index(sha1))
		return 0;

	if (walker->get_verbosely)
		fprintf(stderr, "Getting index for pack %s\n", hex);

	url = xmalloc(strlen(repo->base) + 64);
	sprintf(url, "%s/objects/pack/pack-%s.idx", repo->base, hex);

	filename = sha1_pack_index_name(sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	indexfile = fopen(tmpfile, "a");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     tmpfile);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, data->no_pragma_header);
	slot->local = indexfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(indexfile);
	if (prev_posn>0) {
		if (walker->get_verbosely)
			fprintf(stderr,
				"Resuming fetch of index for pack %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			fclose(indexfile);
			return error("Unable to get pack index %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		fclose(indexfile);
		return error("Unable to start request");
	}

	fclose(indexfile);

	return move_temp_to_file(tmpfile, filename);
}

static int setup_index(struct walker *walker, struct alt_base *repo, unsigned char *sha1)
{
	struct packed_git *new_pack;
	if (has_pack_file(sha1))
		return 0; /* don't list this as something we can get */

	if (fetch_index(walker, repo, sha1))
		return -1;

	new_pack = parse_pack_index(sha1);
	if (!new_pack)
		return -1; /* parse_pack_index() already issued error message */
	new_pack->next = repo->packs;
	repo->packs = new_pack;
	return 0;
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
	static const char null_byte = '\0';
	char *data;
	int i = 0;

	if (alt_req->http_specific) {
		if (slot->curl_result != CURLE_OK ||
		    !alt_req->buffer->len) {

			/* Try reusing the slot to get non-http alternates */
			alt_req->http_specific = 0;
			sprintf(alt_req->url, "%s/objects/info/alternates",
				base);
			curl_easy_setopt(slot->curl, CURLOPT_URL,
					 alt_req->url);
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

	fwrite_buffer(&null_byte, 1, 1, alt_req->buffer);
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
			char *target = NULL;
			if (data[i] == '/') {
				/* This counts
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
				/* Relative URL; chop the corresponding
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
				target = xmalloc(serverlen + posn - i - 6);
				memcpy(target, base, serverlen);
				memcpy(target + serverlen, data + i,
				       posn - i - 7);
				target[serverlen + posn - i - 7] = 0;
				if (walker->get_verbosely)
					fprintf(stderr,
						"Also look at %s\n", target);
				newalt = xmalloc(sizeof(*newalt));
				newalt->next = NULL;
				newalt->base = target;
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
	char *url;
	struct active_request_slot *slot;
	struct alternates_request alt_req;
	struct walker_data *cdata = walker->data;

	/* If another request has already started fetching alternates,
	   wait for them to arrive and return to processing this request's
	   curl message */
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

	url = xmalloc(strlen(base) + 31);
	sprintf(url, "%s/objects/info/http-alternates", base);

	/* Use a callback to process the result, since another request
	   may fail and need to have alternates loaded before continuing */
	slot = get_active_slot();
	slot->callback_func = process_alternates_response;
	alt_req.walker = walker;
	slot->callback_data = &alt_req;

	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);

	alt_req.base = base;
	alt_req.url = url;
	alt_req.buffer = &buffer;
	alt_req.http_specific = 1;
	alt_req.slot = slot;

	if (start_active_slot(slot))
		run_active_slot(slot);
	else
		cdata->got_alternates = -1;

	strbuf_release(&buffer);
	free(url);
}

static int fetch_indices(struct walker *walker, struct alt_base *repo)
{
	unsigned char sha1[20];
	char *url;
	struct strbuf buffer = STRBUF_INIT;
	char *data;
	int i = 0;
	int ret = 0;

	struct active_request_slot *slot;
	struct slot_results results;

	if (repo->got_indices)
		return 0;

	if (walker->get_verbosely)
		fprintf(stderr, "Getting pack list for %s\n", repo->base);

	url = xmalloc(strlen(repo->base) + 21);
	sprintf(url, "%s/objects/info/packs", repo->base);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			if (missing_target(&results)) {
				repo->got_indices = 1;
				goto cleanup;
			} else {
				repo->got_indices = 0;
				ret = error("%s", curl_errorstr);
				goto cleanup;
			}
		}
	} else {
		repo->got_indices = 0;
		ret = error("Unable to start request");
		goto cleanup;
	}

	data = buffer.buf;
	while (i < buffer.len) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 <= buffer.len &&
			    !prefixcmp(data + i, " pack-") &&
			    !prefixcmp(data + i + 46, ".pack\n")) {
				get_sha1_hex(data + i + 6, sha1);
				setup_index(walker, repo, sha1);
				i += 51;
				break;
			}
		default:
			while (i < buffer.len && data[i] != '\n')
				i++;
		}
		i++;
	}

	repo->got_indices = 1;
cleanup:
	strbuf_release(&buffer);
	free(url);
	return ret;
}

static int fetch_pack(struct walker *walker, struct alt_base *repo, unsigned char *sha1)
{
	char *url;
	struct packed_git *target;
	struct packed_git **lst;
	FILE *packfile;
	char *filename;
	char tmpfile[PATH_MAX];
	int ret;
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;
	struct walker_data *data = walker->data;

	struct active_request_slot *slot;
	struct slot_results results;

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

	url = xmalloc(strlen(repo->base) + 65);
	sprintf(url, "%s/objects/pack/pack-%s.pack",
		repo->base, sha1_to_hex(target->sha1));

	filename = sha1_pack_name(target->sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	packfile = fopen(tmpfile, "a");
	if (!packfile)
		return error("Unable to open local file %s for pack",
			     tmpfile);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, packfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, data->no_pragma_header);
	slot->local = packfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(packfile);
	if (prev_posn>0) {
		if (walker->get_verbosely)
			fprintf(stderr,
				"Resuming fetch of pack %s at byte %ld\n",
				sha1_to_hex(target->sha1), prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			fclose(packfile);
			return error("Unable to get pack file %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		fclose(packfile);
		return error("Unable to start request");
	}

	target->pack_size = ftell(packfile);
	fclose(packfile);

	ret = move_temp_to_file(tmpfile, filename);
	if (ret)
		return ret;

	lst = &repo->packs;
	while (*lst != target)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	if (verify_pack(target))
		return -1;
	install_packed_git(target);

	return 0;
}

static void abort_object_request(struct object_request *obj_req)
{
	if (obj_req->local >= 0) {
		close(obj_req->local);
		obj_req->local = -1;
	}
	unlink(obj_req->tmpfile);
	if (obj_req->slot) {
		release_active_slot(obj_req->slot);
		obj_req->slot = NULL;
	}
	release_object_request(obj_req);
}

static int fetch_object(struct walker *walker, struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	int ret = 0;
	struct object_request *obj_req = object_queue_head;

	while (obj_req != NULL && hashcmp(obj_req->sha1, sha1))
		obj_req = obj_req->next;
	if (obj_req == NULL)
		return error("Couldn't find request for %s in the queue", hex);

	if (has_sha1_file(obj_req->sha1)) {
		abort_object_request(obj_req);
		return 0;
	}

#ifdef USE_CURL_MULTI
	while (obj_req->state == WAITING) {
		step_active_slots();
	}
#else
	start_object_request(walker, obj_req);
#endif

	while (obj_req->state == ACTIVE) {
		run_active_slot(obj_req->slot);
	}
	if (obj_req->local != -1) {
		close(obj_req->local); obj_req->local = -1;
	}

	if (obj_req->state == ABORTED) {
		ret = error("Request for %s aborted", hex);
	} else if (obj_req->curl_result != CURLE_OK &&
		   obj_req->http_code != 416) {
		if (missing_target(obj_req))
			ret = -1; /* Be silent, it is probably in a pack. */
		else
			ret = error("%s (curl_result = %d, http_code = %ld, sha1 = %s)",
				    obj_req->errorstr, obj_req->curl_result,
				    obj_req->http_code, hex);
	} else if (obj_req->zret != Z_STREAM_END) {
		walker->corrupt_object_found++;
		ret = error("File %s (%s) corrupt", hex, obj_req->url);
	} else if (hashcmp(obj_req->sha1, obj_req->real_sha1)) {
		ret = error("File %s has bad hash", hex);
	} else if (obj_req->rename < 0) {
		ret = error("unable to write sha1 filename %s",
			    obj_req->filename);
	}

	release_object_request(obj_req);
	return ret;
}

static int fetch(struct walker *walker, unsigned char *sha1)
{
	struct walker_data *data = walker->data;
	struct alt_base *altbase = data->alt;

	if (!fetch_object(walker, altbase, sha1))
		return 0;
	while (altbase) {
		if (!fetch_pack(walker, altbase, sha1))
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
	http_cleanup();

	curl_slist_free_all(data->no_pragma_header);
}

struct walker *get_http_walker(const char *url, struct remote *remote)
{
	char *s;
	struct walker_data *data = xmalloc(sizeof(struct walker_data));
	struct walker *walker = xmalloc(sizeof(struct walker));

	http_init(remote);

	data->no_pragma_header = curl_slist_append(NULL, "Pragma:");

	data->alt = xmalloc(sizeof(*data->alt));
	data->alt->base = xmalloc(strlen(url) + 1);
	strcpy(data->alt->base, url);
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
