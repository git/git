#include "sigchain.h"
#include "cache.h"

#define X(f) \
static void f(int sig) { \
	puts(#f); \
	fflush(stdout); \
	sigchain_pop(sig); \
	raise(sig); \
}
X(one)
X(two)
X(three)
#undef X

int main(int argc, char **argv) {
	sigchain_push(SIGINT, one);
	sigchain_push(SIGINT, two);
	sigchain_push(SIGINT, three);
	raise(SIGINT);
	return 0;
}
