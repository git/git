/*
 * test-run-command.c: test run command API.
 *
 * (C) 2009 Ilari Liusvaara <ilari.liusvaara@elisanet.fi>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "git-compat-util.h"
#include "run-command.h"
#include <string.h>
#include <errno.h>

int main(int argc, char **argv)
{
	struct child_process proc = CHILD_PROCESS_INIT;

	if (argc < 3)
		return 1;
	proc.argv = (const char **)argv+2;

	if (!strcmp(argv[1], "start-command-ENOENT")) {
		if (start_command(&proc) < 0 && errno == ENOENT)
			return 0;
		fprintf(stderr, "FAIL %s\n", argv[1]);
		return 1;
	}
	if (!strcmp(argv[1], "run-command"))
		exit(run_command(&proc));

	fprintf(stderr, "check usage\n");
	return 1;
}
