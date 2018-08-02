#include "git-compat-util.h"
#include "fetch-negotiator.h"
#include "negotiator/default.h"

void fetch_negotiator_init(struct fetch_negotiator *negotiator)
{
	default_negotiator_init(negotiator);
}
