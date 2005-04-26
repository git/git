#ifndef DIFF_H
#define DIFF_H

extern void prepare_diff_cmd(void);

extern void show_differences(const char *name, /* filename on the filesystem */
			     const char *label, /* diff label to use */
			     void *old_contents, /* contents in core */
			     unsigned long long old_size, /* size in core */
			     int reverse /* 0: diff core file
					    1: diff file core */);

extern void show_diff_empty(const unsigned char *sha1,
			    const char *name,
			    int reverse);

#endif /* DIFF_H */
