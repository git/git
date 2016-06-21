#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define HEADERSIZE	1024

int main(int argc, char **argv)
{
	char buffer[HEADERSIZE];
	ssize_t n;

	n = read(0, buffer, HEADERSIZE);
	if (n < HEADERSIZE) {
		fprintf(stderr, "read error\n");
		return 3;
	}
	if (buffer[156] != 'g')
		return 1;
	if (memcmp(&buffer[512], "52 comment=", 11))
		return 1;
	n = write(1, &buffer[523], 41);
	if (n < 41) {
		fprintf(stderr, "write error\n");
		return 2;
	}
	return 0;
}
