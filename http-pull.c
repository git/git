#include "cache.h"
#include "commit.h"

#include "pull.h"

#include <curl/curl.h>
#include <curl/easy.h>

#if LIBCURL_VERSION_NUM < 0x070704
#define curl_global_cleanup() do { /* nothing */ } while(0)
#endif
#if LIBCURL_VERSION_NUM < 0x070800
#define curl_global_init(a) do { /* nothing */ } while(0)
#endif

static CURL *curl;

static char *base;

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

static int got_indices = 0;

static struct packed_git *packs = NULL;

static int fetch_index(unsigned char *sha1)
{
	char *filename;
	char *url;

	FILE *indexfile;

	if (has_pack_index(sha1))
		return 0;

	if (get_verbosely)
		fprintf(stderr, "Getting index for pack %s\n",
			sha1_to_hex(sha1));
	
	url = xmalloc(strlen(base) + 64);
	sprintf(url, "%s/objects/pack/pack-%s.idx",
		base, sha1_to_hex(sha1));
	
	filename = sha1_pack_index_name(sha1);
	indexfile = fopen(filename, "w");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     filename);

	curl_easy_setopt(curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	
	if (curl_easy_perform(curl)) {
		fclose(indexfile);
		return error("Unable to get pack index %s", url);
	}

	fclose(indexfile);
	return 0;
}

static int setup_index(unsigned char *sha1)
{
	struct packed_git *new_pack;
	if (has_pack_file(sha1))
		return 0; // don't list this as something we can get

	if (fetch_index(sha1))
		return -1;

	new_pack = parse_pack_index(sha1);
	new_pack->next = packs;
	packs = new_pack;
	return 0;
}

static int fetch_indices(void)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	if (got_indices)
		return 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting pack list\n");
	
	url = xmalloc(strlen(base) + 21);
	sprintf(url, "%s/objects/info/packs", base);

	curl_easy_setopt(curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	
	if (curl_easy_perform(curl)) {
		return error("Unable to get pack index %s", url);
	}

	do {
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
	} while (i < buffer.posn);

	got_indices = 1;
	return 0;
}

static int fetch_pack(unsigned char *sha1)
{
	char *url;
	struct packed_git *target;
	struct packed_git **lst;
	FILE *packfile;
	char *filename;

	if (fetch_indices())
		return -1;
	target = find_sha1_pack(sha1, packs);
	if (!target)
		return error("Couldn't get %s: not separate or in any pack",
			     sha1_to_hex(sha1));

	if (get_verbosely) {
		fprintf(stderr, "Getting pack %s\n",
			sha1_to_hex(target->sha1));
		fprintf(stderr, " which contains %s\n",
			sha1_to_hex(sha1));
	}

	url = xmalloc(strlen(base) + 65);
	sprintf(url, "%s/objects/pack/pack-%s.pack",
		base, sha1_to_hex(target->sha1));

	filename = sha1_pack_name(target->sha1);
	packfile = fopen(filename, "w");
	if (!packfile)
		return error("Unable to open local file %s for pack",
			     filename);

	curl_easy_setopt(curl, CURLOPT_FILE, packfile);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	
	if (curl_easy_perform(curl)) {
		fclose(packfile);
		return error("Unable to get pack file %s", url);
	}

	fclose(packfile);

	lst = &packs;
	while (*lst != target)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	install_packed_git(target);

	return 0;
}

int fetch(unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename = sha1_file_name(sha1);
	unsigned char real_sha1[20];
	char *url;
	char *posn;

	local = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0666);

	if (local < 0)
		return error("Couldn't open local object %s\n", filename);

	memset(&stream, 0, sizeof(stream));

	inflateInit(&stream);

	SHA1_Init(&c);

	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(curl, CURLOPT_FILE, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);

	url = xmalloc(strlen(base) + 50);
	strcpy(url, base);
	posn = url + strlen(base);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);

	curl_easy_setopt(curl, CURLOPT_URL, url);

	if (curl_easy_perform(curl)) {
		unlink(filename);
		if (fetch_pack(sha1))
			return error("Tried %s", url);
		return 0;
	}

	close(local);
	inflateEnd(&stream);
	SHA1_Final(real_sha1, &c);
	if (zret != Z_STREAM_END) {
		unlink(filename);
		return error("File %s (%s) corrupt\n", hex, url);
	}
	if (memcmp(sha1, real_sha1, 20)) {
		unlink(filename);
		return error("File %s has bad hash\n", hex);
	}
	
	pull_say("got %s\n", hex);
	return 0;
}

int fetch_ref(char *ref, unsigned char *sha1)
{
        char *url, *posn;
        char hex[42];
        struct buffer buffer;
        buffer.size = 41;
        buffer.posn = 0;
        buffer.buffer = hex;
        hex[41] = '\0';
        
        curl_easy_setopt(curl, CURLOPT_FILE, &buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);

        url = xmalloc(strlen(base) + 6 + strlen(ref));
        strcpy(url, base);
        posn = url + strlen(base);
        strcpy(posn, "refs/");
        posn += 5;
        strcpy(posn, ref);

        curl_easy_setopt(curl, CURLOPT_URL, url);

        if (curl_easy_perform(curl))
                return error("Couldn't get %s for %s\n", url, ref);

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
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("git-http-pull [-c] [-t] [-a] [-d] [-v] [--recover] [-w ref] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();

	curl_ssl_verify = gitenv("GIT_SSL_NO_VERIFY") ? 0 : 1;
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, curl_ssl_verify);
#if LIBCURL_VERSION_NUM >= 0x070907
	curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
#endif

	base = url;

	if (pull(commit_id))
		return 1;

	curl_global_cleanup();
	return 0;
}
