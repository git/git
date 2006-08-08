/*
 * Copyright (c) 2006 Junio C Hamano
 */
#include "cache.h"
#include "pkt-line.h"
#include "exec_cmd.h"
#include "builtin.h"

static const char upload_tar_usage[] = "git-upload-tar <repo>";

static int nak(const char *reason)
{
	packet_write(1, "NACK %s\n", reason);
	packet_flush(1);
	return 1;
}

int cmd_upload_tar(int argc, const char **argv, const char *prefix)
{
	int len;
	const char *dir = argv[1];
	char buf[8192];
	unsigned char sha1[20];
	char *base = NULL;
	char hex[41];
	int ac;
	const char *av[4];

	if (argc != 2)
		usage(upload_tar_usage);
	if (strlen(dir) < sizeof(buf)-1)
		strcpy(buf, dir); /* enter-repo smudges its argument */
	else
		packet_write(1, "NACK insanely long repository name %s\n", dir);
	if (!enter_repo(buf, 0)) {
		packet_write(1, "NACK not a git archive %s\n", dir);
		packet_flush(1);
		return 1;
	}

	len = packet_read_line(0, buf, sizeof(buf));
	if (len < 5 || strncmp("want ", buf, 5))
		return nak("expected want");
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (get_sha1(buf + 5, sha1))
		return nak("expected sha1");
        strcpy(hex, sha1_to_hex(sha1));

	len = packet_read_line(0, buf, sizeof(buf));
	if (len) {
		if (len < 5 || strncmp("base ", buf, 5))
			return nak("expected (optional) base");
		if (buf[len-1] == '\n')
			buf[--len] = 0;
		base = strdup(buf + 5);
		len = packet_read_line(0, buf, sizeof(buf));
	}
	if (len)
		return nak("expected flush");

	packet_write(1, "ACK\n");
	packet_flush(1);

	ac = 0;
	av[ac++] = "tar-tree";
	av[ac++] = hex;
	if (base)
		av[ac++] = base;
	av[ac++] = NULL;
	execv_git_cmd(av);
	/* should it return that is an error */
	return 1;
}
