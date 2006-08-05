#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"
#include "tag.h"
#include "blob.h"
#include "http.h"
#include "refs.h"
#include "diff.h"
#include "revision.h"
#include "exec_cmd.h"

#include <expat.h>

static const char http_push_usage[] =
"git-http-push [--all] [--force] [--verbose] <remote> [<head>...]\n";

#ifndef XML_STATUS_OK
enum XML_Status {
  XML_STATUS_OK = 1,
  XML_STATUS_ERROR = 0
};
#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#endif

#define PREV_BUF_SIZE 4096
#define RANGE_HEADER_SIZE 30

/* DAV methods */
#define DAV_LOCK "LOCK"
#define DAV_MKCOL "MKCOL"
#define DAV_MOVE "MOVE"
#define DAV_PROPFIND "PROPFIND"
#define DAV_PUT "PUT"
#define DAV_UNLOCK "UNLOCK"
#define DAV_DELETE "DELETE"

/* DAV lock flags */
#define DAV_PROP_LOCKWR (1u << 0)
#define DAV_PROP_LOCKEX (1u << 1)
#define DAV_LOCK_OK (1u << 2)

/* DAV XML properties */
#define DAV_CTX_LOCKENTRY ".multistatus.response.propstat.prop.supportedlock.lockentry"
#define DAV_CTX_LOCKTYPE_WRITE ".multistatus.response.propstat.prop.supportedlock.lockentry.locktype.write"
#define DAV_CTX_LOCKTYPE_EXCLUSIVE ".multistatus.response.propstat.prop.supportedlock.lockentry.lockscope.exclusive"
#define DAV_ACTIVELOCK_OWNER ".prop.lockdiscovery.activelock.owner.href"
#define DAV_ACTIVELOCK_TIMEOUT ".prop.lockdiscovery.activelock.timeout"
#define DAV_ACTIVELOCK_TOKEN ".prop.lockdiscovery.activelock.locktoken.href"
#define DAV_PROPFIND_RESP ".multistatus.response"
#define DAV_PROPFIND_NAME ".multistatus.response.href"
#define DAV_PROPFIND_COLLECTION ".multistatus.response.propstat.prop.resourcetype.collection"

/* DAV request body templates */
#define PROPFIND_SUPPORTEDLOCK_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:prop xmlns:R=\"%s\">\n<D:supportedlock/>\n</D:prop>\n</D:propfind>"
#define PROPFIND_ALL_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:allprop/>\n</D:propfind>"
#define LOCK_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:lockinfo xmlns:D=\"DAV:\">\n<D:lockscope><D:exclusive/></D:lockscope>\n<D:locktype><D:write/></D:locktype>\n<D:owner>\n<D:href>mailto:%s</D:href>\n</D:owner>\n</D:lockinfo>"

#define LOCK_TIME 600
#define LOCK_REFRESH 30

/* bits #0-15 in revision.h */

#define LOCAL    (1u<<16)
#define REMOTE   (1u<<17)
#define FETCHING (1u<<18)
#define PUSHING  (1u<<19)

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5

static int pushing = 0;
static int aborted = 0;
static signed char remote_dir_exists[256];

static struct curl_slist *no_pragma_header;
static struct curl_slist *default_headers;

static int push_verbosely = 0;
static int push_all = 0;
static int force_all = 0;

static struct object_list *objects = NULL;

struct repo
{
	char *url;
	int path_len;
	int has_info_refs;
	int can_update_info_refs;
	int has_info_packs;
	struct packed_git *packs;
	struct remote_lock *locks;
};

static struct repo *remote = NULL;

enum transfer_state {
	NEED_FETCH,
	RUN_FETCH_LOOSE,
	RUN_FETCH_PACKED,
	NEED_PUSH,
	RUN_MKCOL,
	RUN_PUT,
	RUN_MOVE,
	ABORTED,
	COMPLETE,
};

struct transfer_request
{
	struct object *obj;
	char *url;
	char *dest;
	struct remote_lock *lock;
	struct curl_slist *headers;
	struct buffer buffer;
	char filename[PATH_MAX];
	char tmpfile[PATH_MAX];
	int local_fileno;
	FILE *local_stream;
	enum transfer_state state;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	unsigned char real_sha1[20];
	SHA_CTX c;
	z_stream stream;
	int zret;
	int rename;
	void *userData;
	struct active_request_slot *slot;
	struct transfer_request *next;
};

static struct transfer_request *request_queue_head = NULL;

struct xml_ctx
{
	char *name;
	int len;
	char *cdata;
	void (*userFunc)(struct xml_ctx *ctx, int tag_closed);
	void *userData;
};

struct remote_lock
{
	char *url;
	char *owner;
	char *token;
	time_t start_time;
	long timeout;
	int refreshing;
	struct remote_lock *next;
};

/* Flags that control remote_ls processing */
#define PROCESS_FILES (1u << 0)
#define PROCESS_DIRS  (1u << 1)
#define RECURSIVE     (1u << 2)

/* Flags that remote_ls passes to callback functions */
#define IS_DIR (1u << 0)

struct remote_ls_ctx
{
	char *path;
	void (*userFunc)(struct remote_ls_ctx *ls);
	void *userData;
	int flags;
	char *dentry_name;
	int dentry_flags;
	struct remote_ls_ctx *parent;
};

static void finish_request(struct transfer_request *request);
static void release_request(struct transfer_request *request);

static void process_response(void *callback_data)
{
	struct transfer_request *request =
		(struct transfer_request *)callback_data;

	finish_request(request);
}

#ifdef USE_CURL_MULTI
static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	struct transfer_request *request = (struct transfer_request *)data;
	do {
		ssize_t retval = write(request->local_fileno,
				       (char *) ptr + posn, size - posn);
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

static void start_fetch_loose(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->obj->sha1);
	char *filename;
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

	filename = sha1_file_name(request->obj->sha1);
	snprintf(request->filename, sizeof(request->filename), "%s", filename);
	snprintf(request->tmpfile, sizeof(request->tmpfile),
		 "%s.temp", filename);

	snprintf(prevfile, sizeof(prevfile), "%s.prev", request->filename);
	unlink(prevfile);
	rename(request->tmpfile, prevfile);
	unlink(request->tmpfile);

	if (request->local_fileno != -1)
		error("fd leakage in start: %d", request->local_fileno);
	request->local_fileno = open(request->tmpfile,
				     O_WRONLY | O_CREAT | O_EXCL, 0666);
	/* This could have failed due to the "lazy directory creation";
	 * try to mkdir the last path component.
	 */
	if (request->local_fileno < 0 && errno == ENOENT) {
		char *dir = strrchr(request->tmpfile, '/');
		if (dir) {
			*dir = 0;
			mkdir(request->tmpfile, 0777);
			*dir = '/';
		}
		request->local_fileno = open(request->tmpfile,
					     O_WRONLY | O_CREAT | O_EXCL, 0666);
	}

	if (request->local_fileno < 0) {
		request->state = ABORTED;
		error("Couldn't create temporary file %s for %s: %s",
		      request->tmpfile, request->filename, strerror(errno));
		return;
	}

	memset(&request->stream, 0, sizeof(request->stream));

	inflateInit(&request->stream);

	SHA1_Init(&request->c);

	url = xmalloc(strlen(remote->url) + 50);
	request->url = xmalloc(strlen(remote->url) + 50);
	strcpy(url, remote->url);
	posn = url + strlen(remote->url);
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
			lseek(request->local_fileno, SEEK_SET, 0);
			ftruncate(request->local_fileno, 0);
		}
	}

	slot = get_active_slot();
	slot->callback_func = process_response;
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
		if (push_verbosely)
			fprintf(stderr,
				"Resuming fetch of object %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl,
				 CURLOPT_HTTPHEADER, range_header);
	}

	/* Try to get the request started, abort the request on error */
	request->state = RUN_FETCH_LOOSE;
	if (!start_active_slot(slot)) {
		fprintf(stderr, "Unable to start GET request\n");
		remote->can_update_info_refs = 0;
		release_request(request);
	}
}

static void start_mkcol(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->obj->sha1);
	struct active_request_slot *slot;
	char *posn;

	request->url = xmalloc(strlen(remote->url) + 13);
	strcpy(request->url, remote->url);
	posn = request->url + strlen(remote->url);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	strcpy(posn, "/");

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1); /* undo PUT setup */
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_MKCOL);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_MKCOL;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}
#endif

static void start_fetch_packed(struct transfer_request *request)
{
	char *url;
	struct packed_git *target;
	FILE *packfile;
	char *filename;
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;

	struct transfer_request *check_request = request_queue_head;
	struct active_request_slot *slot;

	target = find_sha1_pack(request->obj->sha1, remote->packs);
	if (!target) {
		fprintf(stderr, "Unable to fetch %s, will not be able to update server info refs\n", sha1_to_hex(request->obj->sha1));
		remote->can_update_info_refs = 0;
		release_request(request);
		return;
	}

	fprintf(stderr,	"Fetching pack %s\n", sha1_to_hex(target->sha1));
	fprintf(stderr, " which contains %s\n", sha1_to_hex(request->obj->sha1));

	filename = sha1_pack_name(target->sha1);
	snprintf(request->filename, sizeof(request->filename), "%s", filename);
	snprintf(request->tmpfile, sizeof(request->tmpfile),
		 "%s.temp", filename);

	url = xmalloc(strlen(remote->url) + 64);
	sprintf(url, "%sobjects/pack/pack-%s.pack",
		remote->url, sha1_to_hex(target->sha1));

	/* Make sure there isn't another open request for this pack */
	while (check_request) {
		if (check_request->state == RUN_FETCH_PACKED &&
		    !strcmp(check_request->url, url)) {
			free(url);
			release_request(request);
			return;
		}
		check_request = check_request->next;
	}

	packfile = fopen(request->tmpfile, "a");
	if (!packfile) {
		fprintf(stderr, "Unable to open local file %s for pack",
			filename);
		remote->can_update_info_refs = 0;
		free(url);
		return;
	}

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	request->slot = slot;
	request->local_stream = packfile;
	request->userData = target;

	request->url = url;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, packfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = packfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(packfile);
	if (prev_posn>0) {
		if (push_verbosely)
			fprintf(stderr,
				"Resuming fetch of pack %s at byte %ld\n",
				sha1_to_hex(target->sha1), prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	/* Try to get the request started, abort the request on error */
	request->state = RUN_FETCH_PACKED;
	if (!start_active_slot(slot)) {
		fprintf(stderr, "Unable to start GET request\n");
		remote->can_update_info_refs = 0;
		release_request(request);
	}
}

static void start_put(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->obj->sha1);
	struct active_request_slot *slot;
	char *posn;
	char type[20];
	char hdr[50];
	void *unpacked;
	unsigned long len;
	int hdrlen;
	ssize_t size;
	z_stream stream;

	unpacked = read_sha1_file(request->obj->sha1, type, &len);
	hdrlen = sprintf(hdr, "%s %lu", type, len) + 1;

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, zlib_compression_level);
	size = deflateBound(&stream, len + hdrlen);
	request->buffer.buffer = xmalloc(size);

	/* Compress it */
	stream.next_out = request->buffer.buffer;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = (void *)hdr;
	stream.avail_in = hdrlen;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */;

	/* Then the data itself.. */
	stream.next_in = unpacked;
	stream.avail_in = len;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	free(unpacked);

	request->buffer.size = stream.total_out;
	request->buffer.posn = 0;

	request->url = xmalloc(strlen(remote->url) + 
			       strlen(request->lock->token) + 51);
	strcpy(request->url, remote->url);
	posn = request->url + strlen(remote->url);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);
	request->dest = xmalloc(strlen(request->url) + 14);
	sprintf(request->dest, "Destination: %s", request->url);
	posn += 38;
	*(posn++) = '.';
	strcpy(posn, request->lock->token);

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &request->buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, request->buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PUT);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_PUT, 1);
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_PUT;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}

static void start_move(struct transfer_request *request)
{
	struct active_request_slot *slot;
	struct curl_slist *dav_headers = NULL;

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1); /* undo PUT setup */
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_MOVE);
	dav_headers = curl_slist_append(dav_headers, request->dest);
	dav_headers = curl_slist_append(dav_headers, "Overwrite: T");
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_MOVE;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}

static int refresh_lock(struct remote_lock *lock)
{
	struct active_request_slot *slot;
	struct slot_results results;
	char *if_header;
	char timeout_header[25];
	struct curl_slist *dav_headers = NULL;
	int rc = 0;

	lock->refreshing = 1;

	if_header = xmalloc(strlen(lock->token) + 25);
	sprintf(if_header, "If: (<opaquelocktoken:%s>)", lock->token);
	sprintf(timeout_header, "Timeout: Second-%ld", lock->timeout);
	dav_headers = curl_slist_append(dav_headers, if_header);
	dav_headers = curl_slist_append(dav_headers, timeout_header);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_LOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			fprintf(stderr, "LOCK HTTP error %ld\n",
				results.http_code);
		} else {
			lock->start_time = time(NULL);
			rc = 1;
		}
	}

	lock->refreshing = 0;
	curl_slist_free_all(dav_headers);
	free(if_header);

	return rc;
}

static void check_locks(void)
{
	struct remote_lock *lock = remote->locks;
	time_t current_time = time(NULL);
	int time_remaining;

	while (lock) {
		time_remaining = lock->start_time + lock->timeout -
			current_time;
		if (!lock->refreshing && time_remaining < LOCK_REFRESH) {
			if (!refresh_lock(lock)) {
				fprintf(stderr,
					"Unable to refresh lock for %s\n",
					lock->url);
				aborted = 1;
				return;
			}
		}
		lock = lock->next;
	}
}

static void release_request(struct transfer_request *request)
{
	struct transfer_request *entry = request_queue_head;

	if (request == request_queue_head) {
		request_queue_head = request->next;
	} else {
		while (entry->next != NULL && entry->next != request)
			entry = entry->next;
		if (entry->next == request)
			entry->next = entry->next->next;
	}

	if (request->local_fileno != -1)
		close(request->local_fileno);
	if (request->local_stream)
		fclose(request->local_stream);
	if (request->url != NULL)
		free(request->url);
	free(request);
}

static void finish_request(struct transfer_request *request)
{
	struct stat st;
	struct packed_git *target;
	struct packed_git **lst;

	request->curl_result = request->slot->curl_result;
	request->http_code = request->slot->http_code;
	request->slot = NULL;

	/* Keep locks active */
	check_locks();

	if (request->headers != NULL)
		curl_slist_free_all(request->headers);

	/* URL is reused for MOVE after PUT */
	if (request->state != RUN_PUT) {
		free(request->url);
		request->url = NULL;
	}

	if (request->state == RUN_MKCOL) {
		if (request->curl_result == CURLE_OK ||
		    request->http_code == 405) {
			remote_dir_exists[request->obj->sha1[0]] = 1;
			start_put(request);
		} else {
			fprintf(stderr, "MKCOL %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->obj->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_PUT) {
		if (request->curl_result == CURLE_OK) {
			start_move(request);
		} else {
			fprintf(stderr,	"PUT %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->obj->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_MOVE) {
		if (request->curl_result == CURLE_OK) {
			if (push_verbosely)
				fprintf(stderr, "    sent %s\n",
					sha1_to_hex(request->obj->sha1));
			request->obj->flags |= REMOTE;
			release_request(request);
		} else {
			fprintf(stderr, "MOVE %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->obj->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_FETCH_LOOSE) {
		fchmod(request->local_fileno, 0444);
		close(request->local_fileno); request->local_fileno = -1;

		if (request->curl_result != CURLE_OK &&
		    request->http_code != 416) {
			if (stat(request->tmpfile, &st) == 0) {
				if (st.st_size == 0)
					unlink(request->tmpfile);
			}
		} else {
			if (request->http_code == 416)
				fprintf(stderr, "Warning: requested range invalid; we may already have all the data.\n");

			inflateEnd(&request->stream);
			SHA1_Final(request->real_sha1, &request->c);
			if (request->zret != Z_STREAM_END) {
				unlink(request->tmpfile);
			} else if (memcmp(request->obj->sha1, request->real_sha1, 20)) {
				unlink(request->tmpfile);
			} else {
				request->rename =
					move_temp_to_file(
						request->tmpfile,
						request->filename);
				if (request->rename == 0) {
					request->obj->flags |= (LOCAL | REMOTE);
				}
			}
		}

		/* Try fetching packed if necessary */
		if (request->obj->flags & LOCAL)
			release_request(request);
		else
			start_fetch_packed(request);

	} else if (request->state == RUN_FETCH_PACKED) {
		if (request->curl_result != CURLE_OK) {
			fprintf(stderr, "Unable to get pack file %s\n%s",
				request->url, curl_errorstr);
			remote->can_update_info_refs = 0;
		} else {
			fclose(request->local_stream);
			request->local_stream = NULL;
			if (!move_temp_to_file(request->tmpfile,
					       request->filename)) {
				target = (struct packed_git *)request->userData;
				lst = &remote->packs;
				while (*lst != target)
					lst = &((*lst)->next);
				*lst = (*lst)->next;

				if (!verify_pack(target, 0))
					install_packed_git(target);
				else
					remote->can_update_info_refs = 0;
			}
		}
		release_request(request);
	}
}

#ifdef USE_CURL_MULTI
void fill_active_slots(void)
{
	struct transfer_request *request = request_queue_head;
	struct transfer_request *next;
	struct active_request_slot *slot = active_queue_head;
	int num_transfers;

	if (aborted)
		return;

	while (active_requests < max_requests && request != NULL) {
		next = request->next;
		if (request->state == NEED_FETCH) {
			start_fetch_loose(request);
		} else if (pushing && request->state == NEED_PUSH) {
			if (remote_dir_exists[request->obj->sha1[0]] == 1) {
				start_put(request);
			} else {
				start_mkcol(request);
			}
			curl_multi_perform(curlm, &num_transfers);
		}
		request = next;
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

static void get_remote_object_list(unsigned char parent);

static void add_fetch_request(struct object *obj)
{
	struct transfer_request *request;

	check_locks();

	/*
	 * Don't fetch the object if it's known to exist locally
	 * or is already in the request queue
	 */
	if (remote_dir_exists[obj->sha1[0]] == -1)
		get_remote_object_list(obj->sha1[0]);
	if (obj->flags & (LOCAL | FETCHING))
		return;

	obj->flags |= FETCHING;
	request = xmalloc(sizeof(*request));
	request->obj = obj;
	request->url = NULL;
	request->lock = NULL;
	request->headers = NULL;
	request->local_fileno = -1;
	request->local_stream = NULL;
	request->state = NEED_FETCH;
	request->next = request_queue_head;
	request_queue_head = request;

#ifdef USE_CURL_MULTI
	fill_active_slots();
	step_active_slots();
#endif
}

static int add_send_request(struct object *obj, struct remote_lock *lock)
{
	struct transfer_request *request = request_queue_head;
	struct packed_git *target;

	/* Keep locks active */
	check_locks();

	/*
	 * Don't push the object if it's known to exist on the remote
	 * or is already in the request queue
	 */
	if (remote_dir_exists[obj->sha1[0]] == -1)
		get_remote_object_list(obj->sha1[0]);
	if (obj->flags & (REMOTE | PUSHING))
		return 0;
	target = find_sha1_pack(obj->sha1, remote->packs);
	if (target) {
		obj->flags |= REMOTE;
		return 0;
	}

	obj->flags |= PUSHING;
	request = xmalloc(sizeof(*request));
	request->obj = obj;
	request->url = NULL;
	request->lock = lock;
	request->headers = NULL;
	request->local_fileno = -1;
	request->local_stream = NULL;
	request->state = NEED_PUSH;
	request->next = request_queue_head;
	request_queue_head = request;

#ifdef USE_CURL_MULTI
	fill_active_slots();
	step_active_slots();
#endif

	return 1;
}

static int fetch_index(unsigned char *sha1)
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

	/* Don't use the index if the pack isn't there */
	url = xmalloc(strlen(remote->url) + 64);
	sprintf(url, "%sobjects/pack/pack-%s.pack", remote->url, hex);
	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			free(url);
			return error("Unable to verify pack %s is available",
				     hex);
		}
	} else {
		return error("Unable to start request");
	}

	if (has_pack_index(sha1))
		return 0;

	if (push_verbosely)
		fprintf(stderr, "Getting index for pack %s\n", hex);

	sprintf(url, "%sobjects/pack/pack-%s.idx", remote->url, hex);

	filename = sha1_pack_index_name(sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	indexfile = fopen(tmpfile, "a");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     filename);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = indexfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(indexfile);
	if (prev_posn>0) {
		if (push_verbosely)
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
			free(url);
			fclose(indexfile);
			return error("Unable to get pack index %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		free(url);
		fclose(indexfile);
		return error("Unable to start request");
	}

	free(url);
	fclose(indexfile);

	return move_temp_to_file(tmpfile, filename);
}

static int setup_index(unsigned char *sha1)
{
	struct packed_git *new_pack;

	if (fetch_index(sha1))
		return -1;

	new_pack = parse_pack_index(sha1);
	new_pack->next = remote->packs;
	remote->packs = new_pack;
	return 0;
}

static int fetch_indices(void)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	struct active_request_slot *slot;
	struct slot_results results;

	data = xcalloc(1, 4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (push_verbosely)
		fprintf(stderr, "Getting pack list\n");

	url = xmalloc(strlen(remote->url) + 20);
	sprintf(url, "%sobjects/info/packs", remote->url);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			free(buffer.buffer);
			free(url);
			if (results.http_code == 404)
				return 0;
			else
				return error("%s", curl_errorstr);
		}
	} else {
		free(buffer.buffer);
		free(url);
		return error("Unable to start request");
	}
	free(url);

	data = buffer.buffer;
	while (i < buffer.posn) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 < buffer.posn &&
			    !strncmp(data + i, " pack-", 6) &&
			    !strncmp(data + i + 46, ".pack\n", 6)) {
				get_sha1_hex(data + i + 6, sha1);
				setup_index(sha1);
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
	return 0;
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
	len = baselen + 1;
	for (cp = ref; (ch = *cp) != 0; cp++, len++)
		if (needs_quote(ch))
			len += 2; /* extra two hex plus replacement % */
	qref = xmalloc(len);
	memcpy(qref, base, baselen);
	for (cp = ref, dp = qref + baselen; (ch = *cp) != 0; cp++) {
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
	char *base = remote->url;
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

static void one_remote_object(const char *hex)
{
	unsigned char sha1[20];
	struct object *obj;

	if (get_sha1_hex(hex, sha1) != 0)
		return;

	obj = lookup_object(sha1);
	if (!obj)
		obj = parse_object(sha1);

	/* Ignore remote objects that don't exist locally */
	if (!obj)
		return;

	obj->flags |= REMOTE;
	if (!object_list_contains(objects, obj))
		object_list_insert(obj, &objects);
}

static void handle_lockprop_ctx(struct xml_ctx *ctx, int tag_closed)
{
	int *lock_flags = (int *)ctx->userData;

	if (tag_closed) {
		if (!strcmp(ctx->name, DAV_CTX_LOCKENTRY)) {
			if ((*lock_flags & DAV_PROP_LOCKEX) &&
			    (*lock_flags & DAV_PROP_LOCKWR)) {
				*lock_flags |= DAV_LOCK_OK;
			}
			*lock_flags &= DAV_LOCK_OK;
		} else if (!strcmp(ctx->name, DAV_CTX_LOCKTYPE_WRITE)) {
			*lock_flags |= DAV_PROP_LOCKWR;
		} else if (!strcmp(ctx->name, DAV_CTX_LOCKTYPE_EXCLUSIVE)) {
			*lock_flags |= DAV_PROP_LOCKEX;
		}
	}
}

static void handle_new_lock_ctx(struct xml_ctx *ctx, int tag_closed)
{
	struct remote_lock *lock = (struct remote_lock *)ctx->userData;

	if (tag_closed && ctx->cdata) {
		if (!strcmp(ctx->name, DAV_ACTIVELOCK_OWNER)) {
			lock->owner = xmalloc(strlen(ctx->cdata) + 1);
			strcpy(lock->owner, ctx->cdata);
		} else if (!strcmp(ctx->name, DAV_ACTIVELOCK_TIMEOUT)) {
			if (!strncmp(ctx->cdata, "Second-", 7))
				lock->timeout =
					strtol(ctx->cdata + 7, NULL, 10);
		} else if (!strcmp(ctx->name, DAV_ACTIVELOCK_TOKEN)) {
			if (!strncmp(ctx->cdata, "opaquelocktoken:", 16)) {
				lock->token = xmalloc(strlen(ctx->cdata) - 15);
				strcpy(lock->token, ctx->cdata + 16);
			}
		}
	}
}

static void one_remote_ref(char *refname);

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
	strlcpy(ctx->cdata, s, len + 1);
}

static struct remote_lock *lock_remote(const char *path, long timeout)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct buffer out_buffer;
	struct buffer in_buffer;
	char *out_data;
	char *in_data;
	char *url;
	char *ep;
	char timeout_header[25];
	struct remote_lock *lock = NULL;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;

	url = xmalloc(strlen(remote->url) + strlen(path) + 1);
	sprintf(url, "%s%s", remote->url, path);

	/* Make sure leading directories exist for the remote ref */
	ep = strchr(url + strlen(remote->url) + 11, '/');
	while (ep) {
		*ep = 0;
		slot = get_active_slot();
		slot->results = &results;
		curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(slot->curl, CURLOPT_URL, url);
		curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_MKCOL);
		curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
		if (start_active_slot(slot)) {
			run_active_slot(slot);
			if (results.curl_result != CURLE_OK &&
			    results.http_code != 405) {
				fprintf(stderr,
					"Unable to create branch path %s\n",
					url);
				free(url);
				return NULL;
			}
		} else {
			fprintf(stderr, "Unable to start MKCOL request\n");
			free(url);
			return NULL;
		}
		*ep = '/';
		ep = strchr(ep + 1, '/');
	}

	out_buffer.size = strlen(LOCK_REQUEST) + strlen(git_default_email) - 2;
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1, LOCK_REQUEST, git_default_email);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	sprintf(timeout_header, "Timeout: Second-%ld", timeout);
	dav_headers = curl_slist_append(dav_headers, timeout_header);
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
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_LOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	lock = xcalloc(1, sizeof(*lock));
	lock->timeout = -1;

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_new_lock_ctx;
			ctx.userData = lock;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);
			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
				lock->timeout = -1;
			}
		}
	} else {
		fprintf(stderr, "Unable to start LOCK request\n");
	}

	curl_slist_free_all(dav_headers);
	free(out_data);
	free(in_data);

	if (lock->token == NULL || lock->timeout <= 0) {
		if (lock->token != NULL)
			free(lock->token);
		if (lock->owner != NULL)
			free(lock->owner);
		free(url);
		free(lock);
		lock = NULL;
	} else {
		lock->url = url;
		lock->start_time = time(NULL);
		lock->next = remote->locks;
		remote->locks = lock;
	}

	return lock;
}

static int unlock_remote(struct remote_lock *lock)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct remote_lock *prev = remote->locks;
	char *lock_token_header;
	struct curl_slist *dav_headers = NULL;
	int rc = 0;

	lock_token_header = xmalloc(strlen(lock->token) + 31);
	sprintf(lock_token_header, "Lock-Token: <opaquelocktoken:%s>",
		lock->token);
	dav_headers = curl_slist_append(dav_headers, lock_token_header);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_UNLOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK)
			rc = 1;
		else
			fprintf(stderr, "UNLOCK HTTP error %ld\n",
				results.http_code);
	} else {
		fprintf(stderr, "Unable to start UNLOCK request\n");
	}

	curl_slist_free_all(dav_headers);
	free(lock_token_header);

	if (remote->locks == lock) {
		remote->locks = lock->next;
	} else {
		while (prev && prev->next != lock)
			prev = prev->next;
		if (prev)
			prev->next = prev->next->next;
	}

	if (lock->owner != NULL)
		free(lock->owner);
	free(lock->url);
	free(lock->token);
	free(lock);

	return rc;
}

static void remote_ls(const char *path, int flags,
		      void (*userFunc)(struct remote_ls_ctx *ls),
		      void *userData);

static void process_ls_object(struct remote_ls_ctx *ls)
{
	unsigned int *parent = (unsigned int *)ls->userData;
	char *path = ls->dentry_name;
	char *obj_hex;

	if (!strcmp(ls->path, ls->dentry_name) && (ls->flags & IS_DIR)) {
		remote_dir_exists[*parent] = 1;
		return;
	}

	if (strlen(path) != 49)
		return;
	path += 8;
	obj_hex = xmalloc(strlen(path));
	strlcpy(obj_hex, path, 3);
	strcpy(obj_hex + 2, path + 3);
	one_remote_object(obj_hex);
	free(obj_hex);
}

static void process_ls_ref(struct remote_ls_ctx *ls)
{
	if (!strcmp(ls->path, ls->dentry_name) && (ls->dentry_flags & IS_DIR)) {
		fprintf(stderr, "  %s\n", ls->dentry_name);
		return;
	}

	if (!(ls->dentry_flags & IS_DIR))
		one_remote_ref(ls->dentry_name);
}

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
					remote_ls(ls->dentry_name,
						  ls->flags,
						  ls->userFunc,
						  ls->userData);
				}
			} else if (ls->flags & PROCESS_FILES) {
				ls->userFunc(ls);
			}
		} else if (!strcmp(ctx->name, DAV_PROPFIND_NAME) && ctx->cdata) {
			ls->dentry_name = xmalloc(strlen(ctx->cdata) -
						  remote->path_len + 1);
			strcpy(ls->dentry_name, ctx->cdata + remote->path_len);
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

static void remote_ls(const char *path, int flags,
		      void (*userFunc)(struct remote_ls_ctx *ls),
		      void *userData)
{
	char *url = xmalloc(strlen(remote->url) + strlen(path) + 1);
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
	ls.path = strdup(path);
	ls.dentry_name = NULL;
	ls.dentry_flags = 0;
	ls.userData = userData;
	ls.userFunc = userFunc;

	sprintf(url, "%s%s", remote->url, path);

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
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
			}
		}
	} else {
		fprintf(stderr, "Unable to start PROPFIND request\n");
	}

	free(ls.path);
	free(url);
	free(out_data);
	free(in_buffer.buffer);
	curl_slist_free_all(dav_headers);
}

static void get_remote_object_list(unsigned char parent)
{
	char path[] = "objects/XX/";
	static const char hex[] = "0123456789abcdef";
	unsigned int val = parent;

	path[8] = hex[val >> 4];
	path[9] = hex[val & 0xf];
	remote_dir_exists[val] = 0;
	remote_ls(path, (PROCESS_FILES | PROCESS_DIRS),
		  process_ls_object, &val);
}

static int locking_available(void)
{
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
	int lock_flags = 0;

	out_buffer.size =
		strlen(PROPFIND_SUPPORTEDLOCK_REQUEST) +
		strlen(remote->url) - 2;
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1,
		 PROPFIND_SUPPORTEDLOCK_REQUEST, remote->url);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	dav_headers = curl_slist_append(dav_headers, "Depth: 0");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, remote->url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PROPFIND);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_lockprop_ctx;
			ctx.userData = &lock_flags;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);

			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
				lock_flags = 0;
			}
		}
	} else {
		fprintf(stderr, "Unable to start PROPFIND request\n");
	}

	free(out_data);
	free(in_buffer.buffer);
	curl_slist_free_all(dav_headers);

	return lock_flags;
}

struct object_list **add_one_object(struct object *obj, struct object_list **p)
{
	struct object_list *entry = xmalloc(sizeof(struct object_list));
	entry->item = obj;
	entry->next = *p;
	*p = entry;
	return &entry->next;
}

static struct object_list **process_blob(struct blob *blob,
					 struct object_list **p,
					 struct name_path *path,
					 const char *name)
{
	struct object *obj = &blob->object;

	obj->flags |= LOCAL;

	if (obj->flags & (UNINTERESTING | SEEN))
		return p;

	obj->flags |= SEEN;
	return add_one_object(obj, p);
}

static struct object_list **process_tree(struct tree *tree,
					 struct object_list **p,
					 struct name_path *path,
					 const char *name)
{
	struct object *obj = &tree->object;
	struct tree_desc desc;
	struct name_entry entry;
	struct name_path me;

	obj->flags |= LOCAL;

	if (obj->flags & (UNINTERESTING | SEEN))
		return p;
	if (parse_tree(tree) < 0)
		die("bad tree object %s", sha1_to_hex(obj->sha1));

	obj->flags |= SEEN;
	name = strdup(name);
	p = add_one_object(obj, p);
	me.up = path;
	me.elem = name;
	me.elem_len = strlen(name);

	desc.buf = tree->buffer;
	desc.size = tree->size;

	while (tree_entry(&desc, &entry)) {
		if (S_ISDIR(entry.mode))
			p = process_tree(lookup_tree(entry.sha1), p, &me, name);
		else
			p = process_blob(lookup_blob(entry.sha1), p, &me, name);
	}
	free(tree->buffer);
	tree->buffer = NULL;
	return p;
}

static int get_delta(struct rev_info *revs, struct remote_lock *lock)
{
	int i;
	struct commit *commit;
	struct object_list **p = &objects;
	int count = 0;

	while ((commit = get_revision(revs)) != NULL) {
		p = process_tree(commit->tree, p, NULL, "");
		commit->object.flags |= LOCAL;
		if (!(commit->object.flags & UNINTERESTING))
			count += add_send_request(&commit->object, lock);
	}

	for (i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *entry = revs->pending.objects + i;
		struct object *obj = entry->item;
		const char *name = entry->name;

		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == OBJ_TAG) {
			obj->flags |= SEEN;
			p = add_one_object(obj, p);
			continue;
		}
		if (obj->type == OBJ_TREE) {
			p = process_tree((struct tree *)obj, p, NULL, name);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			p = process_blob((struct blob *)obj, p, NULL, name);
			continue;
		}
		die("unknown pending object %s (%s)", sha1_to_hex(obj->sha1), name);
	}

	while (objects) {
		if (!(objects->item->flags & UNINTERESTING))
			count += add_send_request(objects->item, lock);
		objects = objects->next;
	}

	return count;
}

static int update_remote(unsigned char *sha1, struct remote_lock *lock)
{
	struct active_request_slot *slot;
	struct slot_results results;
	char *out_data;
	char *if_header;
	struct buffer out_buffer;
	struct curl_slist *dav_headers = NULL;
	int i;

	if_header = xmalloc(strlen(lock->token) + 25);
	sprintf(if_header, "If: (<opaquelocktoken:%s>)", lock->token);
	dav_headers = curl_slist_append(dav_headers, if_header);

	out_buffer.size = 41;
	out_data = xmalloc(out_buffer.size + 1);
	i = snprintf(out_data, out_buffer.size + 1, "%s\n", sha1_to_hex(sha1));
	if (i != out_buffer.size) {
		fprintf(stderr, "Unable to initialize PUT request body\n");
		return 0;
	}
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PUT);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_PUT, 1);
	curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		free(out_data);
		free(if_header);
		if (results.curl_result != CURLE_OK) {
			fprintf(stderr,
				"PUT error: curl result=%d, HTTP code=%ld\n",
				results.curl_result, results.http_code);
			/* We should attempt recovery? */
			return 0;
		}
	} else {
		free(out_data);
		free(if_header);
		fprintf(stderr, "Unable to start PUT request\n");
		return 0;
	}

	return 1;
}

static struct ref *local_refs, **local_tail;
static struct ref *remote_refs, **remote_tail;

static int one_local_ref(const char *refname, const unsigned char *sha1)
{
	struct ref *ref;
	int len = strlen(refname) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	memcpy(ref->new_sha1, sha1, 20);
	memcpy(ref->name, refname, len);
	*local_tail = ref;
	local_tail = &ref->next;
	return 0;
}

static void one_remote_ref(char *refname)
{
	struct ref *ref;
	unsigned char remote_sha1[20];
	struct object *obj;
	int len = strlen(refname) + 1;

	if (fetch_ref(refname, remote_sha1) != 0) {
		fprintf(stderr,
			"Unable to fetch ref %s from %s\n",
			refname, remote->url);
		return;
	}

	/*
	 * Fetch a copy of the object if it doesn't exist locally - it
	 * may be required for updating server info later.
	 */
	if (remote->can_update_info_refs && !has_sha1_file(remote_sha1)) {
		obj = lookup_unknown_object(remote_sha1);
		if (obj) {
			fprintf(stderr,	"  fetch %s for %s\n",
				sha1_to_hex(remote_sha1), refname);
			add_fetch_request(obj);
		}
	}

	ref = xcalloc(1, sizeof(*ref) + len);
	memcpy(ref->old_sha1, remote_sha1, 20);
	memcpy(ref->name, refname, len);
	*remote_tail = ref;
	remote_tail = &ref->next;
}

static void get_local_heads(void)
{
	local_tail = &local_refs;
	for_each_ref(one_local_ref);
}

static void get_dav_remote_heads(void)
{
	remote_tail = &remote_refs;
	remote_ls("refs/", (PROCESS_FILES | PROCESS_DIRS | RECURSIVE), process_ls_ref, NULL);
}

static int is_zero_sha1(const unsigned char *sha1)
{
	int i;

	for (i = 0; i < 20; i++) {
		if (*sha1++)
			return 0;
	}
	return 1;
}

static void unmark_and_free(struct commit_list *list, unsigned int mark)
{
	while (list) {
		struct commit_list *temp = list;
		temp->item->object.flags &= ~mark;
		list = temp->next;
		free(temp);
	}
}

static int ref_newer(const unsigned char *new_sha1,
		     const unsigned char *old_sha1)
{
	struct object *o;
	struct commit *old, *new;
	struct commit_list *list, *used;
	int found = 0;

	/* Both new and old must be commit-ish and new is descendant of
	 * old.  Otherwise we require --force.
	 */
	o = deref_tag(parse_object(old_sha1), NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	old = (struct commit *) o;

	o = deref_tag(parse_object(new_sha1), NULL, 0);
	if (!o || o->type != OBJ_COMMIT)
		return 0;
	new = (struct commit *) o;

	if (parse_commit(new) < 0)
		return 0;

	used = list = NULL;
	commit_list_insert(new, &list);
	while (list) {
		new = pop_most_recent_commit(&list, TMP_MARK);
		commit_list_insert(new, &used);
		if (new == old) {
			found = 1;
			break;
		}
	}
	unmark_and_free(list, TMP_MARK);
	unmark_and_free(used, TMP_MARK);
	return found;
}

static void mark_edge_parents_uninteresting(struct commit *commit)
{
	struct commit_list *parents;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		if (!(parent->object.flags & UNINTERESTING))
			continue;
		mark_tree_uninteresting(parent->tree);
	}
}

static void mark_edges_uninteresting(struct commit_list *list)
{
	for ( ; list; list = list->next) {
		struct commit *commit = list->item;

		if (commit->object.flags & UNINTERESTING) {
			mark_tree_uninteresting(commit->tree);
			continue;
		}
		mark_edge_parents_uninteresting(commit);
	}
}

static void add_remote_info_ref(struct remote_ls_ctx *ls)
{
	struct buffer *buf = (struct buffer *)ls->userData;
	unsigned char remote_sha1[20];
	struct object *o;
	int len;
	char *ref_info;

	if (fetch_ref(ls->dentry_name, remote_sha1) != 0) {
		fprintf(stderr,
			"Unable to fetch ref %s from %s\n",
			ls->dentry_name, remote->url);
		aborted = 1;
		return;
	}

	o = parse_object(remote_sha1);
	if (!o) {
		fprintf(stderr,
			"Unable to parse object %s for remote ref %s\n",
			sha1_to_hex(remote_sha1), ls->dentry_name);
		aborted = 1;
		return;
	}

	len = strlen(ls->dentry_name) + 42;
	ref_info = xcalloc(len + 1, 1);
	sprintf(ref_info, "%s	%s\n",
		sha1_to_hex(remote_sha1), ls->dentry_name);
	fwrite_buffer(ref_info, 1, len, buf);
	free(ref_info);

	if (o->type == OBJ_TAG) {
		o = deref_tag(o, ls->dentry_name, 0);
		if (o) {
			len = strlen(ls->dentry_name) + 45;
			ref_info = xcalloc(len + 1, 1);
			sprintf(ref_info, "%s	%s^{}\n",
				sha1_to_hex(o->sha1), ls->dentry_name);
			fwrite_buffer(ref_info, 1, len, buf);
			free(ref_info);
		}
	}
}

static void update_remote_info_refs(struct remote_lock *lock)
{
	struct buffer buffer;
	struct active_request_slot *slot;
	struct slot_results results;
	char *if_header;
	struct curl_slist *dav_headers = NULL;

	buffer.buffer = xcalloc(1, 4096);
	buffer.size = 4096;
	buffer.posn = 0;
	remote_ls("refs/", (PROCESS_FILES | RECURSIVE),
		  add_remote_info_ref, &buffer);
	if (!aborted) {
		if_header = xmalloc(strlen(lock->token) + 25);
		sprintf(if_header, "If: (<opaquelocktoken:%s>)", lock->token);
		dav_headers = curl_slist_append(dav_headers, if_header);

		slot = get_active_slot();
		slot->results = &results;
		curl_easy_setopt(slot->curl, CURLOPT_INFILE, &buffer);
		curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, buffer.posn);
		curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
		curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
		curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PUT);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
		curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
		curl_easy_setopt(slot->curl, CURLOPT_PUT, 1);
		curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);

		buffer.posn = 0;

		if (start_active_slot(slot)) {
			run_active_slot(slot);
			if (results.curl_result != CURLE_OK) {
				fprintf(stderr,
					"PUT error: curl result=%d, HTTP code=%ld\n",
					results.curl_result, results.http_code);
			}
		}
		free(if_header);
	}
	free(buffer.buffer);
}

static int remote_exists(const char *path)
{
	char *url = xmalloc(strlen(remote->url) + strlen(path) + 1);
	struct active_request_slot *slot;
	struct slot_results results;

	sprintf(url, "%s%s", remote->url, path);

        slot = get_active_slot();
	slot->results = &results;
        curl_easy_setopt(slot->curl, CURLOPT_URL, url);
        curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);

        if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.http_code == 404)
			return 0;
		else if (results.curl_result == CURLE_OK)
			return 1;
		else
			fprintf(stderr, "HEAD HTTP error %ld\n", results.http_code);
	} else {
		fprintf(stderr, "Unable to start HEAD request\n");
	}

	return -1;
}

static void fetch_symref(const char *path, char **symref, unsigned char *sha1)
{
	char *url;
	struct buffer buffer;
	struct active_request_slot *slot;
	struct slot_results results;

	url = xmalloc(strlen(remote->url) + strlen(path) + 1);
	sprintf(url, "%s%s", remote->url, path);

	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = xmalloc(buffer.size);

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			die("Couldn't get %s for remote symref\n%s",
			    url, curl_errorstr);
		}
	} else {
		die("Unable to start remote symref request");
	}
	free(url);

	if (*symref != NULL)
		free(*symref);
	*symref = NULL;
	memset(sha1, 0, 20);

	if (buffer.posn == 0)
		return;

	/* If it's a symref, set the refname; otherwise try for a sha1 */
	if (!strncmp((char *)buffer.buffer, "ref: ", 5)) {
		*symref = xmalloc(buffer.posn - 5);
		strlcpy(*symref, (char *)buffer.buffer + 5, buffer.posn - 5);
	} else {
		get_sha1_hex(buffer.buffer, sha1);
	}

	free(buffer.buffer);
}

static int verify_merge_base(unsigned char *head_sha1, unsigned char *branch_sha1)
{
	struct commit *head = lookup_commit(head_sha1);
	struct commit *branch = lookup_commit(branch_sha1);
	struct commit_list *merge_bases = get_merge_bases(head, branch, 1);

	if (merge_bases && !merge_bases->next && merge_bases->item == branch)
		return 1;

	return 0;
}

static int delete_remote_branch(char *pattern, int force)
{
	struct ref *refs = remote_refs;
	struct ref *remote_ref = NULL;
	unsigned char head_sha1[20];
	char *symref = NULL;
	int match;
	int patlen = strlen(pattern);
	int i;
	struct active_request_slot *slot;
	struct slot_results results;
	char *url;

	/* Find the remote branch(es) matching the specified branch name */
	for (match = 0; refs; refs = refs->next) {
		char *name = refs->name;
		int namelen = strlen(name);
		if (namelen < patlen ||
		    memcmp(name + namelen - patlen, pattern, patlen))
			continue;
		if (namelen != patlen && name[namelen - patlen - 1] != '/')
			continue;
		match++;
		remote_ref = refs;
	}
	if (match == 0)
		return error("No remote branch matches %s", pattern);
	if (match != 1)
		return error("More than one remote branch matches %s",
			     pattern);

	/*
	 * Remote HEAD must be a symref (not exactly foolproof; a remote
	 * symlink to a symref will look like a symref)
	 */
	fetch_symref("HEAD", &symref, head_sha1);
	if (!symref)
		return error("Remote HEAD is not a symref");

	/* Remote branch must not be the remote HEAD */
	for (i=0; symref && i<MAXDEPTH; i++) {
		if (!strcmp(remote_ref->name, symref))
			return error("Remote branch %s is the current HEAD",
				     remote_ref->name);
		fetch_symref(symref, &symref, head_sha1);
	}

	/* Run extra sanity checks if delete is not forced */
	if (!force) {
		/* Remote HEAD must resolve to a known object */
		if (symref)
			return error("Remote HEAD symrefs too deep");
		if (is_zero_sha1(head_sha1))
			return error("Unable to resolve remote HEAD");
		if (!has_sha1_file(head_sha1))
			return error("Remote HEAD resolves to object %s\nwhich does not exist locally, perhaps you need to fetch?", sha1_to_hex(head_sha1));

		/* Remote branch must resolve to a known object */
		if (is_zero_sha1(remote_ref->old_sha1))
			return error("Unable to resolve remote branch %s",
				     remote_ref->name);
		if (!has_sha1_file(remote_ref->old_sha1))
			return error("Remote branch %s resolves to object %s\nwhich does not exist locally, perhaps you need to fetch?", remote_ref->name, sha1_to_hex(remote_ref->old_sha1));

		/* Remote branch must be an ancestor of remote HEAD */
		if (!verify_merge_base(head_sha1, remote_ref->old_sha1)) {
			return error("The branch '%s' is not a strict subset of your current HEAD.\nIf you are sure you want to delete it, run:\n\t'git http-push -D %s %s'", remote_ref->name, remote->url, pattern);
		}
	}

	/* Send delete request */
	fprintf(stderr, "Removing remote branch '%s'\n", remote_ref->name);
	url = xmalloc(strlen(remote->url) + strlen(remote_ref->name) + 1);
	sprintf(url, "%s%s", remote->url, remote_ref->name);
	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_DELETE);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		free(url);
		if (results.curl_result != CURLE_OK)
			return error("DELETE request failed (%d/%ld)\n",
				     results.curl_result, results.http_code);
	} else {
		free(url);
		return error("Unable to start DELETE request");
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct transfer_request *request;
	struct transfer_request *next_request;
	int nr_refspec = 0;
	char **refspec = NULL;
	struct remote_lock *ref_lock = NULL;
	struct remote_lock *info_ref_lock = NULL;
	struct rev_info revs;
	int delete_branch = 0;
	int force_delete = 0;
	int objects_to_send;
	int rc = 0;
	int i;
	int new_refs;
	struct ref *ref;

	setup_git_directory();
	setup_ident();

	remote = xcalloc(sizeof(*remote), 1);

	argv++;
	for (i = 1; i < argc; i++, argv++) {
		char *arg = *argv;

		if (*arg == '-') {
			if (!strcmp(arg, "--all")) {
				push_all = 1;
				continue;
			}
			if (!strcmp(arg, "--force")) {
				force_all = 1;
				continue;
			}
			if (!strcmp(arg, "--verbose")) {
				push_verbosely = 1;
				continue;
			}
			if (!strcmp(arg, "-d")) {
				delete_branch = 1;
				continue;
			}
			if (!strcmp(arg, "-D")) {
				delete_branch = 1;
				force_delete = 1;
				continue;
			}
		}
		if (!remote->url) {
			char *path = strstr(arg, "//");
			remote->url = arg;
			if (path) {
				path = strchr(path+2, '/');
				if (path)
					remote->path_len = strlen(path);
			}
			continue;
		}
		refspec = argv;
		nr_refspec = argc - i;
		break;
	}

	if (!remote->url)
		usage(http_push_usage);

	if (delete_branch && nr_refspec != 1)
		die("You must specify only one branch name when deleting a remote branch");

	memset(remote_dir_exists, -1, 256);

	http_init();

	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");
	default_headers = curl_slist_append(default_headers, "Range:");
	default_headers = curl_slist_append(default_headers, "Destination:");
	default_headers = curl_slist_append(default_headers, "If:");
	default_headers = curl_slist_append(default_headers,
					    "Pragma: no-cache");

	/* Verify DAV compliance/lock support */
	if (!locking_available()) {
		fprintf(stderr, "Error: no DAV locking support on remote repo %s\n", remote->url);
		rc = 1;
		goto cleanup;
	}

	/* Check whether the remote has server info files */
	remote->can_update_info_refs = 0;
	remote->has_info_refs = remote_exists("info/refs");
	remote->has_info_packs = remote_exists("objects/info/packs");
	if (remote->has_info_refs) {
		info_ref_lock = lock_remote("info/refs", LOCK_TIME);
		if (info_ref_lock)
			remote->can_update_info_refs = 1;
	}
	if (remote->has_info_packs)
		fetch_indices();

	/* Get a list of all local and remote heads to validate refspecs */
	get_local_heads();
	fprintf(stderr, "Fetching remote heads...\n");
	get_dav_remote_heads();

	/* Remove a remote branch if -d or -D was specified */
	if (delete_branch) {
		if (delete_remote_branch(refspec[0], force_delete) == -1)
			fprintf(stderr, "Unable to delete remote branch %s\n",
				refspec[0]);
		goto cleanup;
	}

	/* match them up */
	if (!remote_tail)
		remote_tail = &remote_refs;
	if (match_refs(local_refs, remote_refs, &remote_tail,
		       nr_refspec, refspec, push_all))
		return -1;
	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n");
		return 0;
	}

	new_refs = 0;
	for (ref = remote_refs; ref; ref = ref->next) {
		char old_hex[60], *new_hex;
		const char *commit_argv[4];
		int commit_argc;
		char *new_sha1_hex, *old_sha1_hex;

		if (!ref->peer_ref)
			continue;
		if (!memcmp(ref->old_sha1, ref->peer_ref->new_sha1, 20)) {
			if (push_verbosely || 1)
				fprintf(stderr, "'%s': up-to-date\n", ref->name);
			continue;
		}

		if (!force_all &&
		    !is_zero_sha1(ref->old_sha1) &&
		    !ref->force) {
			if (!has_sha1_file(ref->old_sha1) ||
			    !ref_newer(ref->peer_ref->new_sha1,
				       ref->old_sha1)) {
				/* We do not have the remote ref, or
				 * we know that the remote ref is not
				 * an ancestor of what we are trying to
				 * push.  Either way this can be losing
				 * commits at the remote end and likely
				 * we were not up to date to begin with.
				 */
				error("remote '%s' is not a strict "
				      "subset of local ref '%s'. "
				      "maybe you are not up-to-date and "
				      "need to pull first?",
				      ref->name,
				      ref->peer_ref->name);
				rc = -2;
				continue;
			}
		}
		memcpy(ref->new_sha1, ref->peer_ref->new_sha1, 20);
		if (is_zero_sha1(ref->new_sha1)) {
			error("cannot happen anymore");
			rc = -3;
			continue;
		}
		new_refs++;
		strcpy(old_hex, sha1_to_hex(ref->old_sha1));
		new_hex = sha1_to_hex(ref->new_sha1);

		fprintf(stderr, "updating '%s'", ref->name);
		if (strcmp(ref->name, ref->peer_ref->name))
			fprintf(stderr, " using '%s'", ref->peer_ref->name);
		fprintf(stderr, "\n  from %s\n  to   %s\n", old_hex, new_hex);


		/* Lock remote branch ref */
		ref_lock = lock_remote(ref->name, LOCK_TIME);
		if (ref_lock == NULL) {
			fprintf(stderr, "Unable to lock remote branch %s\n",
				ref->name);
			rc = 1;
			continue;
		}

		/* Set up revision info for this refspec */
		commit_argc = 3;
		new_sha1_hex = strdup(sha1_to_hex(ref->new_sha1));
		old_sha1_hex = NULL;
		commit_argv[1] = "--objects";
		commit_argv[2] = new_sha1_hex;
		if (!push_all && !is_zero_sha1(ref->old_sha1)) {
			old_sha1_hex = xmalloc(42);
			sprintf(old_sha1_hex, "^%s",
				sha1_to_hex(ref->old_sha1));
			commit_argv[3] = old_sha1_hex;
			commit_argc++;
		}
		init_revisions(&revs, setup_git_directory());
		setup_revisions(commit_argc, commit_argv, &revs, NULL);
		free(new_sha1_hex);
		if (old_sha1_hex) {
			free(old_sha1_hex);
			commit_argv[1] = NULL;
		}

		/* Generate a list of objects that need to be pushed */
		pushing = 0;
		prepare_revision_walk(&revs);
		mark_edges_uninteresting(revs.commits);
		objects_to_send = get_delta(&revs, ref_lock);
		finish_all_active_slots();

		/* Push missing objects to remote, this would be a
		   convenient time to pack them first if appropriate. */
		pushing = 1;
		if (objects_to_send)
			fprintf(stderr, "    sending %d objects\n",
				objects_to_send);
#ifdef USE_CURL_MULTI
		fill_active_slots();
#endif
		finish_all_active_slots();

		/* Update the remote branch if all went well */
		if (aborted || !update_remote(ref->new_sha1, ref_lock)) {
			rc = 1;
			goto unlock;
		}

	unlock:
		if (!rc)
			fprintf(stderr, "    done\n");
		unlock_remote(ref_lock);
		check_locks();
	}

	/* Update remote server info if appropriate */
	if (remote->has_info_refs && new_refs) {
		if (info_ref_lock && remote->can_update_info_refs) {
			fprintf(stderr, "Updating remote server info\n");
			update_remote_info_refs(info_ref_lock);
		} else {
			fprintf(stderr, "Unable to update server info\n");
		}
	}
	if (info_ref_lock)
		unlock_remote(info_ref_lock);

 cleanup:
	free(remote);

	curl_slist_free_all(no_pragma_header);
	curl_slist_free_all(default_headers);

	http_cleanup();

	request = request_queue_head;
	while (request != NULL) {
		next_request = request->next;
		release_request(request);
		request = next_request;
	}

	return rc;
}
