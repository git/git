#include "cache.h"
#include "commit.h"

#include "fetch.h"

#include <curl/curl.h>
#include <curl/easy.h>

#if LIBCURL_VERSION_NUM < 0x070704
#define curl_global_cleanup() do { /* nothing */ } while(0)
#endif
#if LIBCURL_VERSION_NUM < 0x070800
#define curl_global_init(a) do { /* nothing */ } while(0)
#endif

static CURL *curl;
static struct curl_slist *no_pragma_header;
static char curl_errorstr[CURL_ERROR_SIZE];

static char *initial_base;

struct alt_base
{
	char *base;
	int got_indices;
	struct packed_git *packs;
	struct alt_base *next;
};

struct alt_base *alt = NULL;

static SHA_CTX c;
static z_stream stream;

static int local;
static int zret;

static int curl_ssl_verify;

struct buffer
{
        size_t posn;
        size_t size;
        void *buffer;
};

static size_t fwrite_buffer(void *ptr, size_t eltsize, size_t nmemb,
                            struct buffer *buffer)
{
        size_t size = eltsize * nmemb;
        if (size > buffer->size - buffer->posn)
                size = buffer->size - buffer->posn;
        memcpy(buffer->buffer + buffer->posn, ptr, size);
        buffer->posn += size;
        return size;
}

static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	do {
		ssize_t retval = write(local, ptr + posn, size - posn);
		if (retval < 0)
			return posn;
		posn += retval;
	} while (posn < size);

	stream.avail_in = size;
	stream.next_in = ptr;
	do {
		stream.next_out = expn;
		stream.avail_out = sizeof(expn);
		zret = inflate(&stream, Z_SYNC_FLUSH);
		SHA1_Update(&c, expn, sizeof(expn) - stream.avail_out);
	} while (stream.avail_in && zret == Z_OK);
	return size;
}

void prefetch(unsigned char *sha1)
{
}

static int got_alternates = 0;

static int fetch_index(struct alt_base *repo, unsigned char *sha1)
{
	char *filename;
	char *url;

	FILE *indexfile;

	if (has_pack_index(sha1))
		return 0;

	if (get_verbosely)
		fprintf(stderr, "Getting index for pack %s\n",
			sha1_to_hex(sha1));
	
	url = xmalloc(strlen(repo->base) + 64);
	sprintf(url, "%s/objects/pack/pack-%s.idx",
		repo->base, sha1_to_hex(sha1));
	
	filename = sha1_pack_index_name(sha1);
	indexfile = fopen(filename, "w");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     filename);

	curl_easy_setopt(curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, no_pragma_header);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errorstr);
	
	if (curl_easy_perform(curl)) {
		fclose(indexfile);
		return error("Unable to get pack index %s\n%s", url,
			     curl_errorstr);
	}

	fclose(indexfile);
	return 0;
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

static int fetch_alternates(char *base)
{
	int ret = 0;
	struct buffer buffer;
	char *url;
	char *data;
	int i = 0;
	int http_specific = 1;
	if (got_alternates)
		return 0;
	data = xmalloc(4096);
	buffer.size = 4095;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting alternates list\n");
	
	url = xmalloc(strlen(base) + 31);
	sprintf(url, "%s/objects/info/http-alternates", base);

	curl_easy_setopt(curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	if (curl_easy_perform(curl) || !buffer.posn) {
		http_specific = 0;

		sprintf(url, "%s/objects/info/alternates", base);
		
		curl_easy_setopt(curl, CURLOPT_FILE, &buffer);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		
		if (curl_easy_perform(curl)) {
			return 0;
		}
	}

	data[buffer.posn] = '\0';

	while (i < buffer.posn) {
		int posn = i;
		while (posn < buffer.posn && data[posn] != '\n')
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
			} else if (http_specific) {
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
				newalt->next = alt;
				newalt->base = target;
				newalt->got_indices = 0;
				newalt->packs = NULL;
				alt = newalt;
				ret++;
			}
		}
		i = posn + 1;
	}
	got_alternates = 1;
	
	return ret;
}

static int fetch_indices(struct alt_base *repo)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	if (repo->got_indices)
		return 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting pack list\n");
	
	url = xmalloc(strlen(repo->base) + 21);
	sprintf(url, "%s/objects/info/packs", repo->base);

	curl_easy_setopt(curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errorstr);
	
	if (curl_easy_perform(curl))
		return error("%s", curl_errorstr);

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
	packfile = fopen(filename, "w");
	if (!packfile)
		return error("Unable to open local file %s for pack",
			     filename);

	curl_easy_setopt(curl, CURLOPT_FILE, packfile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, no_pragma_header);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errorstr);

	if (curl_easy_perform(curl)) {
		fclose(packfile);
		return error("Unable to get pack file %s\n%s", url,
			     curl_errorstr);
	}

	fclose(packfile);

	lst = &repo->packs;
	while (*lst != target)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	install_packed_git(target);

	return 0;
}

int fetch_object(struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename = sha1_file_name(sha1);
	unsigned char real_sha1[20];
	char tmpfile[PATH_MAX];
	int ret;
	char *url;
	char *posn;

	snprintf(tmpfile, sizeof(tmpfile), "%s/obj_XXXXXX",
		 get_object_directory());

	local = mkstemp(tmpfile);
	if (local < 0)
		return error("Couldn't create temporary file %s for %s: %s\n",
			     tmpfile, filename, strerror(errno));

	memset(&stream, 0, sizeof(stream));

	inflateInit(&stream);

	SHA1_Init(&c);

	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(curl, CURLOPT_FILE, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, no_pragma_header);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errorstr);

	url = xmalloc(strlen(repo->base) + 50);
	strcpy(url, repo->base);
	posn = url + strlen(repo->base);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);

	curl_easy_setopt(curl, CURLOPT_URL, url);

	if (curl_easy_perform(curl)) {
		unlink(filename);
		return error("%s", curl_errorstr);
	}

	fchmod(local, 0444);
	close(local);
	inflateEnd(&stream);
	SHA1_Final(real_sha1, &c);
	if (zret != Z_STREAM_END) {
		unlink(tmpfile);
		return error("File %s (%s) corrupt\n", hex, url);
	}
	if (memcmp(sha1, real_sha1, 20)) {
		unlink(tmpfile);
		return error("File %s has bad hash\n", hex);
	}
	ret = link(tmpfile, filename);
	if (ret < 0) {
		/* Same Coda hack as in write_sha1_file(sha1_file.c) */
		ret = errno;
		if (ret == EXDEV && !rename(tmpfile, filename))
			goto out;
	}
	unlink(tmpfile);
	if (ret) {
		if (ret != EEXIST)
			return error("unable to write sha1 filename %s: %s",
				     filename, strerror(ret));
	}
 out:
	pull_say("got %s\n", hex);
	return 0;
}

int fetch(unsigned char *sha1)
{
	struct alt_base *altbase = alt;
	while (altbase) {
		if (!fetch_object(altbase, sha1))
			return 0;
		if (!fetch_pack(altbase, sha1))
			return 0;
		if (fetch_alternates(altbase->base) > 0) {
			altbase = alt;
			continue;
		}
		altbase = altbase->next;
	}
	return error("Unable to find %s under %s\n", sha1_to_hex(sha1), 
		     initial_base);
}

int fetch_ref(char *ref, unsigned char *sha1)
{
        char *url, *posn;
        char hex[42];
        struct buffer buffer;
	char *base = initial_base;
        buffer.size = 41;
        buffer.posn = 0;
        buffer.buffer = hex;
        hex[41] = '\0';
        
        curl_easy_setopt(curl, CURLOPT_FILE, &buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errorstr);

        url = xmalloc(strlen(base) + 6 + strlen(ref));
        strcpy(url, base);
        posn = url + strlen(base);
        strcpy(posn, "refs/");
        posn += 5;
        strcpy(posn, ref);

        curl_easy_setopt(curl, CURLOPT_URL, url);

        if (curl_easy_perform(curl))
                return error("Couldn't get %s for %s\n%s",
			     url, ref, curl_errorstr);

        hex[40] = '\0';
        get_sha1_hex(hex, sha1);
        return 0;
}

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;

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

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();
	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");

	curl_ssl_verify = getenv("GIT_SSL_NO_VERIFY") ? 0 : 1;
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, curl_ssl_verify);
#if LIBCURL_VERSION_NUM >= 0x070907
	curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
#endif

	alt = xmalloc(sizeof(*alt));
	alt->base = url;
	alt->got_indices = 0;
	alt->packs = NULL;
	alt->next = NULL;
	initial_base = url;

	if (pull(commit_id))
		return 1;

	curl_slist_free_all(no_pragma_header);
	curl_global_cleanup();
	return 0;
}
