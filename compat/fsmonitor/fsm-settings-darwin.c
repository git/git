#include "cache.h"
#include "config.h"
#include "repository.h"
#include "fsmonitor-settings.h"

enum fsmonitor_reason fsm_os__incompatible(struct repository *r)
{
	return FSMONITOR_REASON_ZERO;
}
