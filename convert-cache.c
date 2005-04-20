#include "cache.h"

struct entry {
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	int converted;
};

#define MAXOBJECTS (1000000)

static struct entry *convert[MAXOBJECTS];
static int nr_convert;

static struct entry * convert_entry(unsigned char *sha1);

static struct entry *insert_new(unsigned char *sha1, int pos)
{
	struct entry *new = malloc(sizeof(struct entry));

	memset(new, 0, sizeof(*new));
	memcpy(new->old_sha1, sha1, 20);
	memmove(convert + pos + 1, convert + pos, (nr_convert - pos) * sizeof(struct entry *));
	convert[pos] = new;
	nr_convert++;
	if (nr_convert == MAXOBJECTS)
		die("you're kidding me - hit maximum object limit");
	return new;
}

static struct entry *lookup_entry(unsigned char *sha1)
{
	int low = 0, high = nr_convert;

	while (low < high) {
		int next = (low + high) / 2;
		struct entry *n = convert[next];
		int cmp = memcmp(sha1, n->old_sha1, 20);
		if (!cmp)
			return n;
		if (cmp < 0) {
			high = next;
			continue;
		}
		low = next+1;
	}
	return insert_new(sha1, low);
}

static void convert_blob(void *buffer, unsigned long size)
{
	/* Nothing to do */
}

static void convert_binary_sha1(void *buffer)
{
	struct entry *entry = convert_entry(buffer);
	memcpy(buffer, entry->new_sha1, 20);
}

static void convert_ascii_sha1(void *buffer)
{
	unsigned char sha1[20];
	struct entry *entry;

	if (get_sha1_hex(buffer, sha1))
		die("bad sha1");
	entry = convert_entry(sha1);
	memcpy(buffer, sha1_to_hex(entry->new_sha1), 40);
}

static void convert_tree(void *buffer, unsigned long size)
{
	while (size) {
		int len = 1+strlen(buffer);

		convert_binary_sha1(buffer + len);

		len += 20;
		if (len > size)
			die("corrupt tree object");
		size -= len;
		buffer += len;
	}
}

static void convert_commit(void *buffer, unsigned long size)
{
	convert_ascii_sha1(buffer+5);
	buffer += 46;    /* "tree " + "hex sha1" + "\n" */
	while (!memcmp(buffer, "parent ", 7)) {
		convert_ascii_sha1(buffer+7);
		buffer += 48;
	}
}

static struct entry * convert_entry(unsigned char *sha1)
{
	struct entry *entry = lookup_entry(sha1);
	char type[20];
	void *buffer, *data;
	unsigned long size, offset;

	if (entry->converted)
		return entry;
	data = read_sha1_file(sha1, type, &size);
	if (!data)
		die("unable to read object %s", sha1_to_hex(sha1));

	buffer = malloc(size + 100);
	offset = sprintf(buffer, "%s %lu", type, size)+1;
	memcpy(buffer + offset, data, size);
	
	if (!strcmp(type, "blob"))
		convert_blob(buffer + offset, size);
	else if (!strcmp(type, "tree"))
		convert_tree(buffer + offset, size);
	else if (!strcmp(type, "commit"))
		convert_commit(buffer + offset, size);
	else
		die("unknown object type '%s' in %s", type, sha1_to_hex(sha1));
	write_sha1_file(buffer, size + offset, entry->new_sha1);
	entry->converted = 1;
	free(buffer);
	return entry;
}

int main(int argc, char **argv)
{
	unsigned char sha1[20];
	struct entry *entry;

	if (argc != 2 || get_sha1_hex(argv[1], sha1))
		usage("convert-cache <sha1>");

	entry = convert_entry(sha1);
	printf("new sha1: %s\n", sha1_to_hex(entry->new_sha1));
	return 0;
}
