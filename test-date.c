#include <stdio.h>
#include <time.h>

#include "cache.h"

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		char result[100];
		time_t t;

		memcpy(result, "bad", 4);
		parse_date(argv[i], result, sizeof(result));
		t = strtoul(result, NULL, 0);
		printf("%s -> %s -> %s", argv[i], result, ctime(&t));
	}
	return 0;
}
