#ifndef MERGE_FILE_H
#define MERGE_FILE_H

extern void *merge_file(const char *path, struct blob *base, struct blob *our,
			struct blob *their, unsigned long *size);

#endif
