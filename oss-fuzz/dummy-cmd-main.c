#include "git-compat-util.h"

/*
 * When linking the fuzzers, we link against common-main.o to pick up some
 * symbols. However, even though we ignore common-main:main(), we still need to
 * provide all the symbols it references. In the fuzzers' case, we need to
 * provide a dummy cmd_main() for the linker to be happy. It will never be
 * executed.
 */

int cmd_main(int argc UNUSED, const char **argv UNUSED) {
	BUG("We should not execute cmd_main() from a fuzz target");
	return 1;
}
