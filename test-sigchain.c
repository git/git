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
	sigchain_push(SIGTERM, one);
	sigchain_push(SIGTERM, two);
	sigchain_push(SIGTERM, three);
	raise(SIGTERM);
	return 0;
}
