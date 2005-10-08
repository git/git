#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "../cache.h"

typedef struct fakemmapwritable {
	void *start;
	size_t length;
	int fd;
	off_t offset;
	struct fakemmapwritable *next;
} fakemmapwritable;

static fakemmapwritable *writablelist = NULL;

void *gitfakemmap(void *start, size_t length, int prot , int flags, int fd, off_t offset)
{
	int n = 0;

	if(start != NULL)
		die("Invalid usage of gitfakemmap.");

	if(lseek(fd, offset, SEEK_SET)<0) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	start = xmalloc(length);
	if(start == NULL) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	while(n < length) {
		int count = read(fd, start+n, length-n);

		if(count == 0) {
			memset(start+n, 0, length-n);
			break;
		}

		if(count < 0) {
			free(start);
			errno = EACCES;
			return MAP_FAILED;
		}

		n += count;
	}

	if(prot & PROT_WRITE) {
		fakemmapwritable *next = xmalloc(sizeof(fakemmapwritable));
		next->start = start;
		next->length = length;
		next->fd = dup(fd);
		next->offset = offset;
		next->next = writablelist;
		writablelist = next;
	}

	return start;
}

int gitfakemunmap(void *start, size_t length)
{
	fakemmapwritable *writable = writablelist, *before = NULL;

	while(writable && (writable->start > start + length
			|| writable->start + writable->length < start)) {
		before = writable;
		writable = writable->next;
	}

	if(writable) {
		/* need to write back the contents */
		int n = 0;

		if(writable->start != start || writable->length != length)
			die("fakemmap does not support partial write back.");

		if(lseek(writable->fd, writable->offset, SEEK_SET) < 0) {
			free(start);
			errno = EBADF;
			return -1;
		}

		while(n < length) {
			int count = write(writable->fd, start + n, length - n);

			if(count < 0) {
				errno = EINVAL;
				return -1;
			}

			n += count;
		}

		close(writable->fd);

		if(before)
			before->next = writable->next;
		else
			writablelist = writable->next;

		free(writable);
	}

	free(start);

	return 0;
}

