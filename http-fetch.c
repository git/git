#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"
#include "http.h"

#ifndef NO_EXPAT
#include <expat.h>

/* Definitions for DAV requests */
#define DAV_PROPFIND "PROPFIND"
#define DAV_PROPFIND_RESP ".multistatus.response"
#define DAV_PROPFIND_NAME ".multistatus.response.href"
#define DAV_PROPFIND_COLLECTION ".multistatus.response.propstat.prop.resourcetype.collection"
#define PROPFIND_ALL_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:allprop/>\n</D:propfind>"

/* Definitions for processing XML DAV responses */
#ifndef XML_STATUS_OK
enum XML_Status {
  XML_STATUS_OK = 1,
  XML_STATUS_ERROR = 0
};
#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#endif

/* Flags that control remote_ls processing */
#define PROCESS_FILES (1u << 0)
#define PROCESS_DIRS  (1u << 1)
#define RECURSIVE     (1u << 2)

/* Flags that remote_ls passes to callback functions */
#define IS_DIR (1u << 0)
#endif

#define PREV_BUF_SIZE 4096
#define RANGE_HEADER_SIZE 30

static int got_alternates = -1;
static int corrupt_object_found = 0;

static struct curl_slist *no_pragma_header;

struct alt_base
{
	char *base;
	int path_len;
	int got_indices;
	struct packed_git *packs;
	struct alt_base *next;
};

static struct alt_base *alt = NULL;

enum object_request_state {
	WAITING,
	ABORTED,
	ACTIVE,
	COMPLETE,
};

struct object_request
{
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
	char *base;
	char *url;
	struct buffer *buffer;
	struct active_request_slot *slot;
	int http_specific;
};

#ifndef NO_EXPAT
struct xml_ctx
{
	char *name;
	int len;
	char *cdata;
	void (*userFunc)(struct xml_ctx *ctx, int tag_closed);
	void *userData;
};

struct remote_ls_ctx
{
	struct alt_base *repo;
	char *path;
	void (*userFunc)(struct remote_ls_ctx *ls);
	void *userData;
	int flags;
	char *dentry_name;
	int dentry_flags;
	int rc;
	struct remote_ls_ctx *parent;
};
#endif

static struct object_request *object_queue_head = NULL;

static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	struct object_request *obj_req = (struct object_request *)data;
	do {
		ssize_t retval = write(obj_req->local,
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

static void fetch_alternates(char *base);

static void process_object_response(void *callback_data);

static void start_object_request(struct object_request *obj_req)
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

	url = xmalloc(strlen(obj_req->repo->base) + 50);
	obj_req->url = xmalloc(strlen(obj_req->repo->base) + 50);
	strcpy(url, obj_req->repo->base);
	posn = url + strlen(obj_req->repo->base);
	strcpy(posn, "objects/");
	posn += 8;
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
			prev_read = read(prevlocal, prev_buf, PREV_BUF_SIZE);
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
			lseek(obj_req->local, SEEK_SET, 0);
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
	if (memcmp(obj_req->sha1, obj_req->real_sha1, 20)) {
		unlink(obj_req->tmpfile);
		return;
	}
	obj_req->rename =
		move_temp_to_file(obj_req->tmpfile, obj_req->filename);

	if (obj_req->rename == 0)
		pull_say("got %s\n", sha1_to_hex(obj_req->sha1));
}

static void process_object_response(void *callback_data)
{
	struct object_request *obj_req =
		(struct object_request *)callback_data;

	obj_req->curl_result = obj_req->slot->curl_result;
	obj_req->http_code = obj_req->slot->http_code;
	obj_req->slot = NULL;
	obj_req->state = COMPLETE;

	/* Use alternates if necessary */
	if (obj_req->http_code == 404 ||
	    obj_req->curl_result == CURLE_FILE_COULDNT_READ_FILE) {
		fetch_alternates(alt->base);
		if (obj_req->repo->next != NULL) {
			obj_req->repo =
				obj_req->repo->next;
			close(obj_req->local);
			obj_req->local = -1;
			start_object_request(obj_req);
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
void fill_active_slots(void)
{
	struct object_request *obj_req = object_queue_head;
	struct active_request_slot *slot = active_queue_head;
	int num_transfers;

	while (active_requests < max_requests && obj_req != NULL) {
		if (obj_req->state == WAITING) {
			if (has_sha1_file(obj_req->sha1))
				obj_req->state = COMPLETE;
			else
				start_object_request(obj_req);
			curl_multi_perform(curlm, &num_transfers);
		}
		obj_req = obj_req->next;
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
	struct object_request *newreq;
	struct object_request *tail;
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
	struct slot_results results;

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
	slot->results = &results;
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

static void process_alternates_response(void *callback_data)
{
	struct alternates_request *alt_req =
		(struct alternates_request *)callback_data;
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
			if (slot->finished != NULL)
				(*slot->finished) = 0;
			if (!start_active_slot(slot)) {
				got_alternates = -1;
				slot->in_use = 0;
				if (slot->finished != NULL)
					(*slot->finished) = 1;
			}
			return;
		}
	} else if (slot->curl_result != CURLE_OK) {
		if (slot->http_code != 404 &&
		    slot->curl_result != CURLE_FILE_COULDNT_READ_FILE) {
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
			char *path;
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
				safe_strncpy(target, base, serverlen);
				safe_strncpy(target + serverlen, data + i, posn - i - 6);
				if (get_verbosely)
					fprintf(stderr,
						"Also look at %s\n", target);
				newalt = xmalloc(sizeof(*newalt));
				newalt->next = NULL;
				newalt->base = target;
				newalt->got_indices = 0;
				newalt->packs = NULL;
				path = strstr(target, "//");
				if (path) {
					path = strchr(path+2, '/');
					if (path)
						newalt->path_len = strlen(path);
				}

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
	struct alternates_request alt_req;

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
	slot->callback_func = process_alternates_response;
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

#ifndef NO_EXPAT
static void
xml_start_tag(void *userData, const char *name, const char **atts)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	const char *c = strchr(name, ':');
	int new_len;

	if (c == NULL)
		c = name;
	else
		c++;

	new_len = strlen(ctx->name) + strlen(c) + 2;

	if (new_len > ctx->len) {
		ctx->name = xrealloc(ctx->name, new_len);
		ctx->len = new_len;
	}
	strcat(ctx->name, ".");
	strcat(ctx->name, c);

	if (ctx->cdata) {
		free(ctx->cdata);
		ctx->cdata = NULL;
	}

	ctx->userFunc(ctx, 0);
}

static void
xml_end_tag(void *userData, const char *name)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	const char *c = strchr(name, ':');
	char *ep;

	ctx->userFunc(ctx, 1);

	if (c == NULL)
		c = name;
	else
		c++;

	ep = ctx->name + strlen(ctx->name) - strlen(c) - 1;
	*ep = 0;
}

static void
xml_cdata(void *userData, const XML_Char *s, int len)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	if (ctx->cdata)
		free(ctx->cdata);
	ctx->cdata = xmalloc(len + 1);
	safe_strncpy(ctx->cdata, s, len + 1);
}

static int remote_ls(struct alt_base *repo, const char *path, int flags,
		     void (*userFunc)(struct remote_ls_ctx *ls),
		     void *userData);

static void handle_remote_ls_ctx(struct xml_ctx *ctx, int tag_closed)
{
	struct remote_ls_ctx *ls = (struct remote_ls_ctx *)ctx->userData;

	if (tag_closed) {
		if (!strcmp(ctx->name, DAV_PROPFIND_RESP) && ls->dentry_name) {
			if (ls->dentry_flags & IS_DIR) {
				if (ls->flags & PROCESS_DIRS) {
					ls->userFunc(ls);
				}
				if (strcmp(ls->dentry_name, ls->path) &&
				    ls->flags & RECURSIVE) {
					ls->rc = remote_ls(ls->repo,
							   ls->dentry_name,
							   ls->flags,
							   ls->userFunc,
							   ls->userData);
				}
			} else if (ls->flags & PROCESS_FILES) {
				ls->userFunc(ls);
			}
		} else if (!strcmp(ctx->name, DAV_PROPFIND_NAME) && ctx->cdata) {
			ls->dentry_name = xmalloc(strlen(ctx->cdata) -
						  ls->repo->path_len + 1);
			strcpy(ls->dentry_name, ctx->cdata + ls->repo->path_len);
		} else if (!strcmp(ctx->name, DAV_PROPFIND_COLLECTION)) {
			ls->dentry_flags |= IS_DIR;
		}
	} else if (!strcmp(ctx->name, DAV_PROPFIND_RESP)) {
		if (ls->dentry_name) {
			free(ls->dentry_name);
		}
		ls->dentry_name = NULL;
		ls->dentry_flags = 0;
	}
}

static int remote_ls(struct alt_base *repo, const char *path, int flags,
		     void (*userFunc)(struct remote_ls_ctx *ls),
		     void *userData)
{
	char *url = xmalloc(strlen(repo->base) + strlen(path) + 1);
	struct active_request_slot *slot;
	struct slot_results results;
	struct buffer in_buffer;
	struct buffer out_buffer;
	char *in_data;
	char *out_data;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;
	struct remote_ls_ctx ls;

	ls.flags = flags;
	ls.repo = repo;
	ls.path = strdup(path);
	ls.dentry_name = NULL;
	ls.dentry_flags = 0;
	ls.userData = userData;
	ls.userFunc = userFunc;
	ls.rc = 0;

	sprintf(url, "%s%s", repo->base, path);

	out_buffer.size = strlen(PROPFIND_ALL_REQUEST);
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1, PROPFIND_ALL_REQUEST);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	dav_headers = curl_slist_append(dav_headers, "Depth: 1");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PROPFIND);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_remote_ls_ctx;
			ctx.userData = &ls;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);

			if (result != XML_STATUS_OK) {
				ls.rc = error("XML error: %s",
					      XML_ErrorString(
						      XML_GetErrorCode(parser)));
			}
		} else {
			ls.rc = -1;
		}
	} else {
		ls.rc = error("Unable to start PROPFIND request");
	}

	free(ls.path);
	free(url);
	free(out_data);
	free(in_buffer.buffer);
	curl_slist_free_all(dav_headers);

	return ls.rc;
}

static void process_ls_pack(struct remote_ls_ctx *ls)
{
	unsigned char sha1[20];

	if (strlen(ls->dentry_name) == 63 &&
	    !strncmp(ls->dentry_name, "objects/pack/pack-", 18) &&
	    !strncmp(ls->dentry_name+58, ".pack", 5)) {
		get_sha1_hex(ls->dentry_name + 18, sha1);
		setup_index(ls->repo, sha1);
	}
}
#endif

static int fetch_indices(struct alt_base *repo)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	struct active_request_slot *slot;
	struct slot_results results;

	if (repo->got_indices)
		return 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting pack list for %s\n", repo->base);

#ifndef NO_EXPAT
	if (remote_ls(repo, "objects/pack/", PROCESS_FILES,
		      process_ls_pack, NULL) == 0)
		return 0;
#endif

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
			if (results.http_code == 404 ||
			    results.curl_result == CURLE_FILE_COULDNT_READ_FILE) {
				repo->got_indices = 1;
				free(buffer.buffer);
				return 0;
			} else {
				repo->got_indices = 0;
				free(buffer.buffer);
				return error("%s", curl_errorstr);
			}
		}
	} else {
		repo->got_indices = 0;
		free(buffer.buffer);
		return error("Unable to start request");
	}

	data = buffer.buffer;
	while (i < buffer.posn) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 <= buffer.posn &&
			    !strncmp(data + i, " pack-", 6) &&
			    !strncmp(data + i + 46, ".pack\n", 6)) {
				get_sha1_hex(data + i + 6, sha1);
				setup_index(repo, sha1);
				i += 51;
				break;
			}
		default:
			while (i < buffer.posn && data[i] != '\n')
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
	struct slot_results results;

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
	slot->results = &results;
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
		if (results.curl_result != CURLE_OK) {
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

static int fetch_object(struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	int ret = 0;
	struct object_request *obj_req = object_queue_head;

	while (obj_req != NULL && memcmp(obj_req->sha1, sha1, 20))
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
	start_object_request(obj_req);
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
		if (obj_req->http_code == 404 ||
		    obj_req->curl_result == CURLE_FILE_COULDNT_READ_FILE)
			ret = -1; /* Be silent, it is probably in a pack. */
		else
			ret = error("%s (curl_result = %d, http_code = %ld, sha1 = %s)",
				    obj_req->errorstr, obj_req->curl_result,
				    obj_req->http_code, hex);
	} else if (obj_req->zret != Z_STREAM_END) {
		corrupt_object_found++;
		ret = error("File %s (%s) corrupt", hex, obj_req->url);
	} else if (memcmp(obj_req->sha1, obj_req->real_sha1, 20)) {
		ret = error("File %s has bad hash", hex);
	} else if (obj_req->rename < 0) {
		ret = error("unable to write sha1 filename %s",
			    obj_req->filename);
	}

	release_object_request(obj_req);
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
	return error("Unable to find %s under %s", sha1_to_hex(sha1),
		     alt->base);
}

static inline int needs_quote(int ch)
{
	if (((ch >= 'A') && (ch <= 'Z'))
			|| ((ch >= 'a') && (ch <= 'z'))
			|| ((ch >= '0') && (ch <= '9'))
			|| (ch == '/')
			|| (ch == '-')
			|| (ch == '.'))
		return 0;
	return 1;
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
	struct slot_results results;
        buffer.size = 41;
        buffer.posn = 0;
        buffer.buffer = hex;
        hex[41] = '\0';

	url = quote_ref_url(base, ref);
	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK)
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
	char *path;
	int arg = 1;
	int rc = 0;

	setup_git_directory();
	git_config(git_default_config);

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
	write_ref_log_details = url;

	http_init();

	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");

	alt = xmalloc(sizeof(*alt));
	alt->base = url;
	alt->got_indices = 0;
	alt->packs = NULL;
	alt->next = NULL;
	path = strstr(url, "//");
	if (path) {
		path = strchr(path+2, '/');
		if (path)
			alt->path_len = strlen(path);
	}

	if (pull(commit_id))
		rc = 1;

	http_cleanup();

	curl_slist_free_all(no_pragma_header);

	if (corrupt_object_found) {
		fprintf(stderr,
"Some loose object were found to be corrupt, but they might be just\n"
"a false '404 Not Found' error message sent with incorrect HTTP\n"
"status code.  Suggest running git fsck-objects.\n");
	}
	return rc;
}
