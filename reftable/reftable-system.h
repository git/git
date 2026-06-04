#ifndef REFTABLE_SYSTEM_H
#define REFTABLE_SYSTEM_H

/*
 * This header defines the platform-specific bits required to compile the
 * reftable library. It should provide an environment that bridges over the
 * gaps between POSIX and your system, as well as the zlib interfaces. This
 * header is expected to be changed by the individual project.
 */

#define MINGW_DONT_HANDLE_IN_USE_ERROR
#include "compat/posix.h"
#include "compat/zlib-compat.h"

int reftable_fsync(int fd);
#define fsync(fd) reftable_fsync(fd)

#endif
