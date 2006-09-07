/*
 * Copyright (c) 2006 Franck Bui-Huu
 */
#include <time.h>
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "pkt-line.h"

static const char upload_archive_usage[] =
	"git-upload-archive <repo>";


int cmd_upload_archive(int argc, const char **argv, const char *prefix)
{
	struct archiver ar;
	const char *sent_argv[MAX_ARGS];
	const char *arg_cmd = "argument ";
	char *p, buf[4096];
	int treeish_idx;
	int sent_argc;
	int len;

	if (argc != 2)
		usage(upload_archive_usage);

	if (strlen(argv[1]) > sizeof(buf))
		die("insanely long repository name");

	strcpy(buf, argv[1]); /* enter-repo smudges its argument */

	if (!enter_repo(buf, 0))
		die("not a git archive");

	/* put received options in sent_argv[] */
	sent_argc = 1;
	sent_argv[0] = "git-upload-archive";
	for (p = buf;;) {
		/* This will die if not enough free space in buf */
		len = packet_read_line(0, p, (buf + sizeof buf) - p);
		if (len == 0)
			break;	/* got a flush */
		if (sent_argc > MAX_ARGS - 2)
			die("Too many options (>29)");

		if (p[len-1] == '\n') {
			p[--len] = 0;
		}
		if (len < strlen(arg_cmd) ||
		    strncmp(arg_cmd, p, strlen(arg_cmd)))
			die("'argument' token or flush expected");

		len -= strlen(arg_cmd);
		memmove(p, p + strlen(arg_cmd), len);
		sent_argv[sent_argc++] = p;
		p += len;
		*p++ = 0;
	}
	sent_argv[sent_argc] = NULL;

	/* parse all options sent by the client */
	treeish_idx = parse_archive_args(sent_argc, sent_argv, &ar);

	parse_treeish_arg(sent_argv + treeish_idx, &ar.args, prefix);
	parse_pathspec_arg(sent_argv + treeish_idx + 1, &ar.args);

	packet_write(1, "ACK\n");
	packet_flush(1);

	return ar.write_archive(&ar.args);
}

