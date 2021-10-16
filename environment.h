#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "strvec.h"

/*
 * Wrapper of getenv() that returns a strdup value. This value is kept
 * in argv to be freed later.
 */
const char *getenv_safe(struct strvec *argv, const char *name);

#endif
