#ifndef __clang__
#include "fsm-darwin-gcc.h"
#else
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#ifndef AVAILABLE_MAC_OS_X_VERSION_10_13_AND_LATER
/*
 * This enum value was added in 10.13 to:
 *
 * /Applications/Xcode.app/Contents/Developer/Platforms/ \
 *    MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/ \
 *    Library/Frameworks/CoreServices.framework/Frameworks/ \
 *    FSEvents.framework/Versions/Current/Headers/FSEvents.h
 *
 * If we're compiling against an older SDK, this symbol won't be
 * present.  Silently define it here so that we don't have to ifdef
 * the logging or masking below.  This should be harmless since older
 * versions of macOS won't ever emit this FS event anyway.
 */
#define kFSEventStreamEventFlagItemCloned         0x00400000
#endif
#endif

#include "cache.h"
#include "fsmonitor.h"
#include "fsm-listen.h"

int fsm_listen__ctor(struct fsmonitor_daemon_state *state)
{
	return -1;
}

void fsm_listen__dtor(struct fsmonitor_daemon_state *state)
{
}

void fsm_listen__stop_async(struct fsmonitor_daemon_state *state)
{
}

void fsm_listen__loop(struct fsmonitor_daemon_state *state)
{
}
