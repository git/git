#include "cache.h"
#include "argv-array.h"
#include "strbuf.h"

static const char *empty_argv_storage = NULL;
const char **empty_argv = &empty_argv_storage;

void argv_array_init(struct argv_array *array)
{
	array->argv = empty_argv;
	array->argc = 0;
	array->alloc = 0;
}

static void argv_array_push_nodup(struct argv_array *array, const char *value)
{
	if (array->argv == empty_argv)
		array->argv = NULL;

	ALLOC_GROW(array->argv, array->argc + 2, array->alloc);
	array->argv[array->argc++] = value;
	array->argv[array->argc] = NULL;
}

void argv_array_push(struct argv_array *array, const char *value)
{
	argv_array_push_nodup(array, xstrdup(value));
}

void argv_array_pushf(struct argv_array *array, const char *fmt, ...)
{
	va_list ap;
	struct strbuf v = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_vaddf(&v, fmt, ap);
	va_end(ap);

	argv_array_push_nodup(array, strbuf_detach(&v, NULL));
}

void argv_array_clear(struct argv_array *array)
{
	if (array->argv != empty_argv) {
		int i;
		for (i = 0; i < array->argc; i++)
			free((char **)array->argv[i]);
		free(array->argv);
	}
	argv_array_init(array);
}
