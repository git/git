#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "cache.h"
#include "commit.h"
#include <errno.h>
#include <stdio.h>

#include <curl/curl.h>
#include <curl/easy.h>

static CURL *curl;

static char *base;

static int tree = 0;
static int commits = 0;
static int all = 0;

static SHA_CTX c;
static z_stream stream;

static int local;
static int zret;

static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb, 
			       void *data) {
	char expn[4096];
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

static int fetch(unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename = sha1_file_name(sha1);
	char real_sha1[20];
	char *url;
	char *posn;

	if (has_sha1_file(sha1)) {
		return 0;
	}

	local = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0666);

	if (local < 0)
		return error("Couldn't open %s\n", filename);

	memset(&stream, 0, sizeof(stream));

	inflateInit(&stream);

	SHA1_Init(&c);

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

	/*printf("Getting %s\n", hex);*/

	if (curl_easy_perform(curl))
		return error("Couldn't get %s for %s\n", url, hex);

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
	
	return 0;
}

static int process_tree(unsigned char *sha1)
{
	struct tree *tree = lookup_tree(sha1);
	struct tree_entry_list *entries;

	if (parse_tree(tree))
		return -1;

	for (entries = tree->entries; entries; entries = entries->next) {
		if (fetch(entries->item.tree->object.sha1))
			return -1;
		if (entries->directory) {
			if (process_tree(entries->item.tree->object.sha1))
				return -1;
		}
	}
	return 0;
}

static int process_commit(unsigned char *sha1)
{
	struct commit *obj = lookup_commit(sha1);

	if (fetch(sha1))
		return -1;

	if (parse_commit(obj))
		return -1;

	if (tree) {
		if (fetch(obj->tree->object.sha1))
			return -1;
		if (process_tree(obj->tree->object.sha1))
			return -1;
		if (!all)
			tree = 0;
	}
	if (commits) {
		struct commit_list *parents = obj->parents;
		for (; parents; parents = parents->next) {
			if (has_sha1_file(parents->item->object.sha1))
				continue;
			if (fetch(parents->item->object.sha1)) {
				/* The server might not have it, and
				 * we don't mind. 
				 */
				continue;
			}
			if (process_commit(parents->item->object.sha1))
				return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;
	unsigned char sha1[20];

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			tree = 1;
		} else if (argv[arg][1] == 'c') {
			commits = 1;
		} else if (argv[arg][1] == 'a') {
			all = 1;
			tree = 1;
			commits = 1;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("http-pull [-c] [-t] [-a] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	get_sha1_hex(commit_id, sha1);

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();

	base = url;

	if (fetch(sha1))
		return 1;
	if (process_commit(sha1))
		return 1;

	curl_global_cleanup();
	return 0;
}
