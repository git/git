#ifndef PROMISE_H
#define PROMISE_H

#include "git-compat-util.h"
#include "strbuf.h"

enum promise_state {
	PROMISE_UNRESOLVED = 0,
	PROMISE_SUCCESS = 1,
	PROMISE_FAILURE = 2,
};

typedef int success_result_t;

struct failure_result_t {
	int status;
	struct strbuf message;
};

struct promise_t {
	enum promise_state state;
	union {
		success_result_t success_result;
		struct failure_result_t failure_result;
	} result;
};

// Function to assert that a promise has been resolved
void promise_assert_finished(struct promise_t *p);

// Function to assert that a promise has been rejected
void promise_assert_failure(struct promise_t *p);

// Function to resolve a promise with a success result
void promise_resolve(struct promise_t *p, int status);

// Function to reject a promise with a failure result and an optional formatted error message
void promise_reject(struct promise_t *p, int status, const char* fmt, ...);

// Function to create a new promise
struct promise_t *promise_init(void);

// Copies the error out of a failed promise
void promise_copy_error(struct promise_t *promise, char **error_message, size_t *size);

// Fully deallocates the promise
void promise_release(struct promise_t *promise);

#define PROMISE_SUCCEED(p, errcode) do { \
	promise_resolve(p, errcode); \
	return; \
} while (0)

#define PROMISE_THROW(p, errcode, ...) do { \
	promise_reject(p, errcode, __VA_ARGS__); \
	return; \
} while (0)

#define PROMISE_BUBBLE_UP(dst, src, ...) do { \
	if (strlen(src->result.failure_result.message.buf) != 0) { \
		strbuf_insertf(&src->result.failure_result.message, 0, "\n\t"); \
		strbuf_insertf(&src->result.failure_result.message, 0, _("caused by:")); \
		strbuf_insertf(&src->result.failure_result.message, 0, "\n"); \
		strbuf_insertf(&src->result.failure_result.message, 0, __VA_ARGS__); \
	} \
	promise_reject(dst, src->result.failure_result.status, "%s", src->result.failure_result.message.buf); \
	promise_release(src); \
	return; \
} while (0)

#endif
