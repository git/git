/*
 * Totally braindamaged mbox splitter program.
 *
 * It just splits a mbox into a list of files: "0001" "0002" ..
 * so you can process them further from there.
 */
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

static int usage(void)
{
	fprintf(stderr, "mailsplit <mbox> <directory>\n");
	exit(1);
}

static int linelen(const char *map, unsigned long size)
{
	int len = 0, c;

	do {
		c = *map;
		map++;
		size--;
		len++;
	} while (size && c != '\n');
	return len;
}

static int is_from_line(const char *line, int len)
{
	const char *colon;

	if (len < 20 || memcmp("From ", line, 5))
		return 0;

	colon = line + len - 2;
	line += 5;
	for (;;) {
		if (colon < line)
			return 0;
		if (*--colon == ':')
			break;
	}

	if (!isdigit(colon[-4]) ||
	    !isdigit(colon[-2]) ||
	    !isdigit(colon[-1]) ||
	    !isdigit(colon[ 1]) ||
	    !isdigit(colon[ 2]))
		return 0;

	/* year */
	if (strtol(colon+3, NULL, 10) <= 90)
		return 0;

	/* Ok, close enough */
	return 1;
}

static int parse_email(const void *map, unsigned long size)
{
	unsigned long offset;

	if (size < 6 || memcmp("From ", map, 5))
		goto corrupt;

	/* Make sure we don't trigger on this first line */
	map++; size--; offset=1;

	/*
	 * Search for a line beginning with "From ", and 
	 * having smething that looks like a date format.
	 */
	do {
		int len = linelen(map, size);
		if (is_from_line(map, len))
			return offset;
		map += len;
		size -= len;
		offset += len;
	} while (size);
	return offset;

corrupt:
	fprintf(stderr, "corrupt mailbox\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int fd, nr;
	struct stat st;
	unsigned long size;
	void *map;

	if (argc != 3)
		usage();
	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		exit(1);
	}
	if (chdir(argv[2]) < 0)
		usage();
	if (fstat(fd, &st) < 0) {
		perror("stat");
		exit(1);
	}
	size = st.st_size;
	map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}
	close(fd);
	nr = 0;
	do {
		char name[10];
		unsigned long len = parse_email(map, size);
		assert(len <= size);
		sprintf(name, "%04d", ++nr);
		fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd < 0) {
			perror(name);
			exit(1);
		}
		if (write(fd, map, len) != len) {
			perror("write");
			exit(1);
		}
		close(fd);
		map += len;
		size -= len;
	} while (size > 0);
	return 0;
}
