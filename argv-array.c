#include "cache.h"
#include "argv-array.h"
#include "strbuf.h"

const char *empty_argv[] = { NULL };

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

const char *argv_array_push(struct argv_array *array, const char *value)
{
	argv_array_push_nodup(array, xstrdup(value));
	return array->argv[array->argc - 1];
}

const char *argv_array_pushf(struct argv_array *array, const char *fmt, ...)
{
	va_list ap;
	struct strbuf v = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_vaddf(&v, fmt, ap);
	va_end(ap);

	argv_array_push_nodup(array, strbuf_detach(&v, NULL));
	return array->argv[array->argc - 1];
}

void argv_array_pushl(struct argv_array *array, ...)
{
	va_list ap;
	const char *arg;

	va_start(ap, array);
	while((arg = va_arg(ap, const char *)))
		argv_array_push(array, arg);
	va_end(ap);
}

void argv_array_pushv(struct argv_array *array, const char **argv)
{
	for (; *argv; argv++)
		argv_array_push(array, *argv);
}

void argv_array_pop(struct argv_array *array)
{
	if (!array->argc)
		return;
	free((char *)array->argv[array->argc - 1]);
	array->argv[array->argc - 1] = NULL;
	array->argc--;
}

void argv_array_split(struct argv_array *array, const char *to_split)
{
	while (isspace(*to_split))
		to_split++;
	for (;;) {
		const char *p = to_split;

		if (!*p)
			break;

		while (*p && !isspace(*p))
			p++;
		argv_array_push_nodup(array, xstrndup(to_split, p - to_split));

		while (isspace(*p))
			p++;
		to_split = p;
	}
}

void argv_array_clear(struct argv_array *array)
{
	if (array->argv != empty_argv) {
		int i;
		for (i = 0; i < array->argc; i++)
			free((char *)array->argv[i]);
		free(array->argv);
	}
	argv_array_init(array);
}

const char **argv_array_detach(struct argv_array *array)
{
	if (array->argv == empty_argv)
		return xcalloc(1, sizeof(const char *));
	else {
		const char **ret = array->argv;
		argv_array_init(array);
		return ret;
	}
}
