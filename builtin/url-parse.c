/* SPDX-License-Identifier: GPL-2.0-only
 *
 * url-parse - parses git URLs and extracts their components
 *
 * Copyright © 2024 Matheus Afonso Martins Moreira
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 */

#include "builtin.h"
#include "gettext.h"
#include "parse-options.h"
#include "urlmatch.h"

static const char * const builtin_url_parse_usage[] = {
	N_("git url-parse [<options>] [--] <url>..."),
	NULL
};

static char *component_arg = NULL;

static struct option builtin_url_parse_options[] = {
	OPT_STRING('c', "component", &component_arg, "<component>", \
		N_("which URL component to extract")),
	OPT_END(),
};

enum url_component {
	URL_NONE = 0,
	URL_PROTOCOL,
	URL_USER,
	URL_PASSWORD,
	URL_HOST,
	URL_PORT,
	URL_PATH,
};

static void parse_or_die(const char *url, struct url_info *info)
{
	if (url_parse(url, info)) {
		return;
	} else {
		die("invalid git URL '%s', %s", url, info->err);
	}
}

static enum url_component get_component_or_die(const char *arg)
{
	if (!strcmp("path", arg))
		return URL_PATH;
	if (!strcmp("host", arg))
		return URL_HOST;
	if (!strcmp("protocol", arg))
		return URL_PROTOCOL;
	if (!strcmp("user", arg))
		return URL_USER;
	if (!strcmp("password", arg))
		return URL_PASSWORD;
	if (!strcmp("port", arg))
		return URL_PORT;
	die("invalid git URL component '%s'", arg);
}

static char *extract(enum url_component component, struct url_info *info)
{
	size_t offset, length;

	switch (component) {
	case URL_PROTOCOL:
		offset = 0;
		length = info->scheme_len;
		break;
	case URL_USER:
		offset = info->user_off;
		length = info->user_len;
		break;
	case URL_PASSWORD:
		offset = info->passwd_off;
		length = info->passwd_len;
		break;
	case URL_HOST:
		offset = info->host_off;
		length = info->host_len;
		break;
	case URL_PORT:
		offset = info->port_off;
		length = info->port_len;
		break;
	case URL_PATH:
		offset = info->path_off;
		length = info->path_len;
		break;
	case URL_NONE:
		return NULL;
	}

	return xstrndup(info->url + offset, length);
}

int cmd_url_parse(int argc, const char **argv, const char *prefix)
{
	struct url_info info;
	enum url_component selected = URL_NONE;
	char *extracted;
	int i;

	argc = parse_options(argc, argv, prefix,
		builtin_url_parse_options,
		builtin_url_parse_usage,
		0);

	if (component_arg)
		selected = get_component_or_die(component_arg);

	for (i = 0; i < argc; ++i) {
		parse_or_die(argv[i], &info);

		if (selected != URL_NONE) {
			extracted = extract(selected, &info);
			if (extracted) {
				puts(extracted);
				free(extracted);
			}
		}

		free(info.url);
	}

	return 0;
}
