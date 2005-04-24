#include "cache.h"
#include "rsh.h"
#include <sys/socket.h>
#include <errno.h>

void service(int fd_in, int fd_out) {
	ssize_t size;
	int posn;
	char sha1[20];
	unsigned long objsize;
	void *buf;
	do {
		posn = 0;
		do {
			size = read(fd_in, sha1 + posn, 20 - posn);
			if (size < 0) {
				perror("rpush: read ");
				return;
			}
			if (!size)
				return;
			posn += size;
		} while (posn < 20);

		/* fprintf(stderr, "Serving %s\n", sha1_to_hex(sha1)); */

		buf = map_sha1_file(sha1, &objsize);
		if (!buf) {
			fprintf(stderr, "rpush: could not find %s\n", 
				sha1_to_hex(sha1));
			return;
		}
		posn = 0;
		do {
			size = write(fd_out, buf + posn, objsize - posn);
			if (size <= 0) {
				if (!size) {
					fprintf(stderr, "rpush: write closed");
				} else {
					perror("rpush: write ");
				}
				return;
			}
			posn += size;
		} while (posn < objsize);
	} while (1);
}

int main(int argc, char **argv)
{
	int arg = 1;
        char *commit_id;
        char *url;
	int fd_in, fd_out;
	while (arg < argc && argv[arg][0] == '-') {
                arg++;
        }
        if (argc < arg + 2) {
                usage("rpush [-c] [-t] [-a] commit-id url");
                return 1;
        }
	commit_id = argv[arg];
	url = argv[arg + 1];
	if (setup_connection(&fd_in, &fd_out, "rpull", url, arg, argv + 1))
		return 1;

	service(fd_in, fd_out);
	return 0;
}
