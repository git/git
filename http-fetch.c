#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"
#include "http.h"

#define PREV_BUF_SIZE 4096
#define RANGE_HEADER_SIZE 30

static int got_alternates = -1;

static struct curl_slist *no_pragma_header;

struct alt_base
{
	char *base;
	int got_indices;
	struct packed_git *packs;
	struct alt_base *next;
};

static struct alt_base *alt = NULL;

enum transfer_state {
	WAITING,
	ABORTED,
	ACTIVE,
	COMPLETE,
};

struct transfer_request
{
	unsigned char sha1[20];
	struct alt_base *repo;
	char *url;
	char filename[PATH_MAX];
	char tmpfile[PATH_MAX];
	int local;
	enum transfer_state state;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	unsigned char real_sha1[20];
	SHA_CTX c;
	z_stream stream;
	int zret;
	int rename;
	struct active_request_slot *slot;
	struct transfer_request *next;
};

struct alt_request {
	char *base;
	char *url;
	struct buffer *buffer;
	struct active_request_slot *slot;
	int http_specific;
};

static struct transfer_request *request_queue_head = NULL;

static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	struct transfer_request *request = (struct transfer_request *)data;
	do {
		ssize_t retval = write(request->local,
				       ptr + posn, size - posn);
		if (retval < 0)
			return posn;
		posn += retval;
	} while (posn < size);

	request->stream.avail_in = size;
	request->stream.next_in = ptr;
	do {
		request->stream.next_out = expn;
		request->stream.avail_out = sizeof(expn);
		request->zret = inflate(&request->stream, Z_SYNC_FLUSH);
		SHA1_Update(&request->c, expn,
			    sizeof(expn) - request->stream.avail_out);
	} while (request->stream.avail_in && request->zret == Z_OK);
	data_received++;
	return size;
}

static void fetch_alternates(char *base);

static void process_object_response(void *callback_data);

static void start_request(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->sha1);
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

	snprintf(prevfile, sizeof(prevfile), "%s.prev", request->filename);
	unlink(prevfile);
	rename(request->tmpfile, prevfile);
	unlink(request->tmpfile);

	if (request->local != -1)
		error("fd leakage in start: %d", request->local);
	request->local = open(request->tmpfile,
			      O_WRONLY | O_CREAT | O_EXCL, 0666);
	/* This could have failed due to the "lazy directory creation";
	 * try to mkdir the last path component.
	 */
	if (request->local < 0 && errno == ENOENT) {
		char *dir = strrchr(request->tmpfile, '/');
		if (dir) {
			*dir = 0;
			mkdir(request->tmpfile, 0777);
			*dir = '/';
		}
		request->local = open(request->tmpfile,
				      O_WRONLY | O_CREAT | O_EXCL, 0666);
	}

	if (request->local < 0) {
		request->state = ABORTED;
		error("Couldn't create temporary file %s for %s: %s\n",
		      request->tmpfile, request->filename, strerror(errno));
		return;
	}

	memset(&request->stream, 0, sizeof(request->stream));

	inflateInit(&request->stream);

	SHA1_Init(&request->c);

	url = xmalloc(strlen(request->repo->base) + 50);
	request->url = xmalloc(strlen(request->repo->base) + 50);
	strcpy(url, request->repo->base);
	posn = url + strlen(request->repo->base);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);
	strcpy(request->url, url);

	/* If a previous temp file is present, process what was already
	   fetched. */
	prevlocal = open(prevfile, O_RDONLY);
	if (prevlocal != -1) {
		do {
			prev_read = read(prevlocal, prev_buf, PREV_BUF_SIZE);
			if (prev_read>0) {
				if (fwrite_sha1_file(prev_buf,
						     1,
						     prev_read,
						     request) == prev_read) {
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
		memset(&request->stream, 0, sizeof(request->stream));
		inflateInit(&request->stream);
		SHA1_Init(&request->c);
		if (prev_posn>0) {
			prev_posn = 0;
			lseek(request->local, SEEK_SET, 0);
			ftruncate(request->local, 0);
		}
	}

	slot = get_active_slot();
	slot->callback_func = process_object_response;
	slot->callback_data = request;
	request->slot = slot;

	curl_easy_setopt(slot->curl, CURLOPT_FILE, request);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);

	/* If we have successfully processed data from a previous fetch
	   attempt, only fetch the data we don't already have. */
	if (prev_posn>0) {
		if (get_verbosely)
			fprintf(stderr,
				"Resuming fetch of object %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl,
				 CURLOPT_HTTPHEADER, range_header);
	}

	/* Try to get the request started, abort the request on error */
	request->state = ACTIVE;
	if (!start_active_slot(slot)) {
		request->state = ABORTED;
		request->slot = NULL;
		close(request->local); request->local = -1;
		free(request->url);
	}
}

static void finish_request(struct transfer_request *request)
{
	struct stat st;

	fchmod(request->local, 0444);
	close(request->local); request->local = -1;

	if (request->http_code == 416) {
		fprintf(stderr, "Warning: requested range invalid; we may already have all the data.\n");
	} else if (request->curl_result != CURLE_OK) {
		if (stat(request->tmpfile, &st) == 0)
			if (st.st_size == 0)
				unlink(request->tmpfile);
		return;
	}

	inflateEnd(&request->stream);
	SHA1_Final(request->real_sha1, &request->c);
	if (request->zret != Z_STREAM_END) {
		unlink(request->tmpfile);
		return;
	}
	if (memcmp(request->sha1, request->real_sha1, 20)) {
		unlink(request->tmpfile);
		return;
	}
	request->rename =
		move_temp_to_file(request->tmpfile, request->filename);

	if (request->rename == 0)
		pull_say("got %s\n", sha1_to_hex(request->sha1));
}

static void process_object_response(void *callback_data)
{
	struct transfer_request *request =
		(struct transfer_request *)callback_data;

	request->curl_result = request->slot->curl_result;
	request->http_code = request->slot->http_code;
	request->slot = NULL;
	request->state = COMPLETE;

	/* Use alternates if necessary */
	if (request->http_code == 404) {
		fetch_alternates(alt->base);
		if (request->repo->next != NULL) {
			request->repo =
				request->repo->next;
			close(request->local);
			request->local = -1;
			start_request(request);
			return;
		}
	}

	finish_request(request);
}

static void release_request(struct transfer_request *request)
{
	struct transfer_request *entry = request_queue_head;

	if (request->local != -1)
		error("fd leakage in release: %d", request->local);
	if (request == request_queue_head) {
		request_queue_head = request->next;
	} else {
		while (entry->next != NULL && entry->next != request)
			entry = entry->next;
		if (entry->next == request)
			entry->next = entry->next->next;
	}

	free(request->url);
	free(request);
}

#ifdef USE_CURL_MULTI
void fill_active_slots(void)
{
	struct transfer_request *request = request_queue_head;
	struct active_request_slot *slot = active_queue_head;
	int num_transfers;

	while (active_requests < max_requests && request != NULL) {
		if (request->state == WAITING) {
			if (has_sha1_file(request->sha1))
				release_request(request);
			else
				start_request(request);
			curl_multi_perform(curlm, &num_transfers);
		}
		request = request->next;
	}

	while (slot != NULL) {
		if (!slot->in_use && slot->curl != NULL) {
			curl_easy_cleanup(slot->curl);
			slot->curl = NULL;
		}
		slot = slot->next;
	}				
}
#endif

void prefetch(unsigned char *sha1)
{
	struct transfer_request *newreq;
	struct transfer_request *tail;
	char *filename = sha1_file_name(sha1);

	newreq = xmalloc(sizeof(*newreq));
	memcpy(newreq->sha1, sha1, 20);
	newreq->repo = alt;
	newreq->url = NULL;
	newreq->local = -1;
	newreq->state = WAITING;
	snprintf(newreq->filename, sizeof(newreq->filename), "%s", filename);
	snprintf(newreq->tmpfile, sizeof(newreq->tmpfile),
		 "%s.temp", filename);
	newreq->next = NULL;

	if (request_queue_head == NULL) {
		request_queue_head = newreq;
	} else {
		tail = request_queue_head;
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

static int fetch_index(struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename;
	char *url;
	char tmpfile[PATH_MAX];
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;

	FILE *indexfile;
	struct active_request_slot *slot;

	if (has_pack_index(sha1))
		return 0;

	if (get_verbosely)
		fprintf(stderr, "Getting index for pack %s\n", hex);
	
	url = xmalloc(strlen(repo->base) + 64);
	sprintf(url, "%s/objects/pack/pack-%s.idx", repo->base, hex);
	
	filename = sha1_pack_index_name(sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	indexfile = fopen(tmpfile, "a");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     filename);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = indexfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(indexfile);
	if (prev_posn>0) {
		if (get_verbosely)
			fprintf(stderr,
				"Resuming fetch of index for pack %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
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

static int setup_index(struct alt_base *repo, unsigned char *sha1)
{
	struct packed_git *new_pack;
	if (has_pack_file(sha1))
		return 0; // don't list this as something we can get

	if (fetch_index(repo, sha1))
		return -1;

	new_pack = parse_pack_index(sha1);
	new_pack->next = repo->packs;
	repo->packs = new_pack;
	return 0;
}

static void process_alternates(void *callback_data)
{
	struct alt_request *alt_req = (struct alt_request *)callback_data;
	struct active_request_slot *slot = alt_req->slot;
	struct alt_base *tail = alt;
	char *base = alt_req->base;
	static const char null_byte = '\0';
	char *data;
	int i = 0;

	if (alt_req->http_specific) {
		if (slot->curl_result != CURLE_OK ||
		    !alt_req->buffer->posn) {

			/* Try reusing the slot to get non-http alternates */
			alt_req->http_specific = 0;
			sprintf(alt_req->url, "%s/objects/info/alternates",
				base);
			curl_easy_setopt(slot->curl, CURLOPT_URL,
					 alt_req->url);
			active_requests++;
			slot->in_use = 1;
			if (start_active_slot(slot)) {
				return;
			} else {
				got_alternates = -1;
				slot->in_use = 0;
				return;
			}
		}
	} else if (slot->curl_result != CURLE_OK) {
		if (slot->http_code != 404) {
			got_alternates = -1;
			return;
		}
	}

	fwrite_buffer(&null_byte, 1, 1, alt_req->buffer);
	alt_req->buffer->posn--;
	data = alt_req->buffer->buffer;

	while (i < alt_req->buffer->posn) {
		int posn = i;
		while (posn < alt_req->buffer->posn && data[posn] != '\n')
			posn++;
		if (data[posn] == '\n') {
			int okay = 0;
			int serverlen = 0;
			struct alt_base *newalt;
			char *target = NULL;
			if (data[i] == '/') {
				serverlen = strchr(base + 8, '/') - base;
				okay = 1;
			} else if (!memcmp(data + i, "../", 3)) {
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
				// If the server got removed, give up.
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
			// skip 'objects' at end
			if (okay) {
				target = xmalloc(serverlen + posn - i - 6);
				strncpy(target, base, serverlen);
				strncpy(target + serverlen, data + i,
					posn - i - 7);
				target[serverlen + posn - i - 7] = '\0';
				if (get_verbosely)
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

	got_alternates = 1;
}

static void fetch_alternates(char *base)
{
	struct buffer buffer;
	char *url;
	char *data;
	struct active_request_slot *slot;
	static struct alt_request alt_req;

	/* If another request has already started fetching alternates,
	   wait for them to arrive and return to processing this request's
	   curl message */
#ifdef USE_CURL_MULTI
	while (got_alternates == 0) {
		step_active_slots();
	}
#endif

	/* Nothing to do if they've already been fetched */
	if (got_alternates == 1)
		return;

	/* Start the fetch */
	got_alternates = 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting alternates list for %s\n", base);
	
	url = xmalloc(strlen(base) + 31);
	sprintf(url, "%s/objects/info/http-alternates", base);

	/* Use a callback to process the result, since another request
	   may fail and need to have alternates loaded before continuing */
	slot = get_active_slot();
	slot->callback_func = process_alternates;
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
		got_alternates = -1;

	free(data);
	free(url);
}

static int fetch_indices(struct alt_base *repo)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	struct active_request_slot *slot;

	if (repo->got_indices)
		return 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting pack list for %s\n", repo->base);
	
	url = xmalloc(strlen(repo->base) + 21);
	sprintf(url, "%s/objects/info/packs", repo->base);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			free(buffer.buffer);
			return error("%s", curl_errorstr);
		}
	} else {
		free(buffer.buffer);
		return error("Unable to start request");
	}

	data = buffer.buffer;
	while (i < buffer.posn) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 < buffer.posn &&
			    !strncmp(data + i, " pack-", 6) &&
			    !strncmp(data + i + 46, ".pack\n", 6)) {
				get_sha1_hex(data + i + 6, sha1);
				setup_index(repo, sha1);
				i += 51;
				break;
			}
		default:
			while (data[i] != '\n')
				i++;
		}
		i++;
	}

	free(buffer.buffer);
	repo->got_indices = 1;
	return 0;
}

static int fetch_pack(struct alt_base *repo, unsigned char *sha1)
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

	struct active_request_slot *slot;

	if (fetch_indices(repo))
		return -1;
	target = find_sha1_pack(sha1, repo->packs);
	if (!target)
		return -1;

	if (get_verbosely) {
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
			     filename);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, packfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = packfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(packfile);
	if (prev_posn>0) {
		if (get_verbosely)
			fprintf(stderr,
				"Resuming fetch of pack %s at byte %ld\n",
				sha1_to_hex(target->sha1), prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			fclose(packfile);
			return error("Unable to get pack file %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		fclose(packfile);
		return error("Unable to start request");
	}

	fclose(packfile);

	ret = move_temp_to_file(tmpfile, filename);
	if (ret)
		return ret;

	lst = &repo->packs;
	while (*lst != target)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	if (verify_pack(target, 0))
		return -1;
	install_packed_git(target);

	return 0;
}

static int fetch_object(struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	int ret = 0;
	struct transfer_request *request = request_queue_head;

	while (request != NULL && memcmp(request->sha1, sha1, 20))
		request = request->next;
	if (request == NULL)
		return error("Couldn't find request for %s in the queue", hex);

	if (has_sha1_file(request->sha1)) {
		release_request(request);
		return 0;
	}

#ifdef USE_CURL_MULTI
	while (request->state == WAITING) {
		step_active_slots();
	}
#else
	start_request(request);
#endif

	while (request->state == ACTIVE) {
		run_active_slot(request->slot);
	}
	if (request->local != -1) {
		close(request->local); request->local = -1;
	}

	if (request->state == ABORTED) {
		ret = error("Request for %s aborted", hex);
	} else if (request->curl_result != CURLE_OK &&
		   request->http_code != 416) {
		if (request->http_code == 404)
			ret = -1; /* Be silent, it is probably in a pack. */
		else
			ret = error("%s (curl_result = %d, http_code = %ld, sha1 = %s)",
				    request->errorstr, request->curl_result,
				    request->http_code, hex);
	} else if (request->zret != Z_STREAM_END) {
		ret = error("File %s (%s) corrupt\n", hex, request->url);
	} else if (memcmp(request->sha1, request->real_sha1, 20)) {
		ret = error("File %s has bad hash\n", hex);
	} else if (request->rename < 0) {
		ret = error("unable to write sha1 filename %s: %s",
			    request->filename,
			    strerror(request->rename));
	}

	release_request(request);
	return ret;
}

int fetch(unsigned char *sha1)
{
	struct alt_base *altbase = alt;

	if (!fetch_object(altbase, sha1))
		return 0;
	while (altbase) {
		if (!fetch_pack(altbase, sha1))
			return 0;
		fetch_alternates(alt->base);
		altbase = altbase->next;
	}
	return error("Unable to find %s under %s\n", sha1_to_hex(sha1), 
		     alt->base);
}

static inline int needs_quote(int ch)
{
	switch (ch) {
	case '/': case '-': case '.':
	case 'A'...'Z':	case 'a'...'z':	case '0'...'9':
		return 0;
	default:
		return 1;
	}
}

static inline int hex(int v)
{
	if (v < 10) return '0' + v;
	else return 'A' + v - 10;
}

static char *quote_ref_url(const char *base, const char *ref)
{
	const char *cp;
	char *dp, *qref;
	int len, baselen, ch;

	baselen = strlen(base);
	len = baselen + 6; /* "refs/" + NUL */
	for (cp = ref; (ch = *cp) != 0; cp++, len++)
		if (needs_quote(ch))
			len += 2; /* extra two hex plus replacement % */
	qref = xmalloc(len);
	memcpy(qref, base, baselen);
	memcpy(qref + baselen, "refs/", 5);
	for (cp = ref, dp = qref + baselen + 5; (ch = *cp) != 0; cp++) {
		if (needs_quote(ch)) {
			*dp++ = '%';
			*dp++ = hex((ch >> 4) & 0xF);
			*dp++ = hex(ch & 0xF);
		}
		else
			*dp++ = ch;
	}
	*dp = 0;

	return qref;
}

int fetch_ref(char *ref, unsigned char *sha1)
{
        char *url;
        char hex[42];
        struct buffer buffer;
	char *base = alt->base;
	struct active_request_slot *slot;
        buffer.size = 41;
        buffer.posn = 0;
        buffer.buffer = hex;
        hex[41] = '\0';
        
	url = quote_ref_url(base, ref);
	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK)
			return error("Couldn't get %s for %s\n%s",
				     url, ref, curl_errorstr);
	} else {
		return error("Unable to start request");
	}

        hex[40] = '\0';
        get_sha1_hex(hex, sha1);
        return 0;
}

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;
	int rc = 0;

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			get_tree = 1;
		} else if (argv[arg][1] == 'c') {
			get_history = 1;
		} else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		} else if (argv[arg][1] == 'v') {
			get_verbosely = 1;
		} else if (argv[arg][1] == 'w') {
			write_ref = argv[arg + 1];
			arg++;
		} else if (!strcmp(argv[arg], "--recover")) {
			get_recover = 1;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("git-http-fetch [-c] [-t] [-a] [-d] [-v] [--recover] [-w ref] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	http_init();

	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");

	alt = xmalloc(sizeof(*alt));
	alt->base = url;
	alt->got_indices = 0;
	alt->packs = NULL;
	alt->next = NULL;

	if (pull(commit_id))
		rc = 1;

	curl_slist_free_all(no_pragma_header);

	http_cleanup();

	return rc;
}
