#ifndef TEST_TOOL_UTILS_H
#define TEST_TOOL_UTILS_H

struct test_cmd {
	const char *name;
	int (*fn)(int argc, const char **argv);
};

#endif
