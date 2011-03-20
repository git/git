/*
 * Copyright (c) 2010 Ævar Arnfjörð Bjarmason
 */

#include "git-compat-util.h"
#include "gettext.h"

int use_gettext_poison(void)
{
	static int poison_requested = -1;
	if (poison_requested == -1)
		poison_requested = getenv("GIT_GETTEXT_POISON") ? 1 : 0;
	return poison_requested;
}
