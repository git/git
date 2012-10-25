#ifndef ARGV_ARRAY_H
#define ARGV_ARRAY_H

extern const char *empty_argv[];

struct argv_array {
	const char **argv;
	int argc;
	int alloc;
};

#define ARGV_ARRAY_INIT { empty_argv, 0, 0 }

void argv_array_init(struct argv_array *);
void argv_array_push(struct argv_array *, const char *);
__attribute__((format (printf,2,3)))
void argv_array_pushf(struct argv_array *, const char *fmt, ...);
void argv_array_pushl(struct argv_array *, ...);
void argv_array_pop(struct argv_array *);
void argv_array_clear(struct argv_array *);
const char **argv_array_detach(struct argv_array *array, int *argc);
void argv_array_free_detached(const char **argv);

#endif /* ARGV_ARRAY_H */
