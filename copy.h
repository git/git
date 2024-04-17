#ifndef COPY_H
#define COPY_H

#define COPY_READ_ERROR (-2)
#define COPY_WRITE_ERROR (-3)
int copy_fd(int ifd, int ofd);
int copy_file(const char *dst, const char *src, int mode);
int copy_file_with_time(const char *dst, const char *src, int mode);

/*
 * Compare the file mode and contents of two given files.
 *
 * If both files are actually symbolic links, the function returns 1 if the link
 * targets are identical or 0 if they are not.
 *
 * If any of the two files cannot be accessed or in case of read failures, this
 * function returns 0.
 *
 * If the file modes and contents are identical, the function returns 1,
 * otherwise it returns 0.
 */
int do_files_match(const char *path1, const char *path2);

#endif /* COPY_H */
