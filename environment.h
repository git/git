#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "strvec.h"

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
extern char comment_line_char;
extern int auto_comment_line_char;

/*
 * Wrapper of getenv() that returns a strdup value. This value is kept
 * in argv to be freed later.
 */
const char *getenv_safe(struct strvec *argv, const char *name);

#endif
