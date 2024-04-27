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
#include "urlmatch.h"

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

int cmd_url_parse(int argc, const char **argv, const char *prefix)
{
	return 0;
}
