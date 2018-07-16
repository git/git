#include "git-compat-util.h"
#include "fetch-negotiator.h"
#include "negotiator/default.h"
#include "negotiator/skipping.h"

void fetch_negotiator_init(struct fetch_negotiator *negotiator,
			   const char *algorithm)
{
	if (algorithm && !strcmp(algorithm, "skipping")) {
		skipping_negotiator_init(negotiator);
		return;
	}
	default_negotiator_init(negotiator);
}
