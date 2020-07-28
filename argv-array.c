#include "cache.h"
#include "argv-array.h"
#include "strbuf.h"

const char *empty_strvec[] = { NULL };

void strvec_init(struct strvec *array)
{
	array->argv = empty_strvec;
	array->argc = 0;
	array->alloc = 0;
}

static void strvec_push_nodup(struct strvec *array, const char *value)
{
	if (array->argv == empty_strvec)
		array->argv = NULL;

	ALLOC_GROW(array->argv, array->argc + 2, array->alloc);
	array->argv[array->argc++] = value;
	array->argv[array->argc] = NULL;
}

const char *strvec_push(struct strvec *array, const char *value)
{
	strvec_push_nodup(array, xstrdup(value));
	return array->argv[array->argc - 1];
}

const char *strvec_pushf(struct strvec *array, const char *fmt, ...)
{
	va_list ap;
	struct strbuf v = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_vaddf(&v, fmt, ap);
	va_end(ap);

	strvec_push_nodup(array, strbuf_detach(&v, NULL));
	return array->argv[array->argc - 1];
}

void strvec_pushl(struct strvec *array, ...)
{
	va_list ap;
	const char *arg;

	va_start(ap, array);
	while ((arg = va_arg(ap, const char *)))
		strvec_push(array, arg);
	va_end(ap);
}

void strvec_pushv(struct strvec *array, const char **argv)
{
	for (; *argv; argv++)
		strvec_push(array, *argv);
}

void strvec_pop(struct strvec *array)
{
	if (!array->argc)
		return;
	free((char *)array->argv[array->argc - 1]);
	array->argv[array->argc - 1] = NULL;
	array->argc--;
}

void strvec_split(struct strvec *array, const char *to_split)
{
	while (isspace(*to_split))
		to_split++;
	for (;;) {
		const char *p = to_split;

		if (!*p)
			break;

		while (*p && !isspace(*p))
			p++;
		strvec_push_nodup(array, xstrndup(to_split, p - to_split));

		while (isspace(*p))
			p++;
		to_split = p;
	}
}

void strvec_clear(struct strvec *array)
{
	if (array->argv != empty_strvec) {
		int i;
		for (i = 0; i < array->argc; i++)
			free((char *)array->argv[i]);
		free(array->argv);
	}
	strvec_init(array);
}

const char **strvec_detach(struct strvec *array)
{
	if (array->argv == empty_strvec)
		return xcalloc(1, sizeof(const char *));
	else {
		const char **ret = array->argv;
		strvec_init(array);
		return ret;
	}
}
