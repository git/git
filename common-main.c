#include "git-compat-util.h"

int main(int argc, char **av)
{
	/*
	 * This const trickery is explained in
	 * 84d32bf7678259c08406571cd6ce4b7a6724dcba
	 */
	const char **argv = (const char **)av;

	return cmd_main(argc, argv);
}
