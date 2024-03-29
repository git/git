/*
 * Generic implementation of callbacks with await checking.
 */
#include "promise.h"

void promise_assert_finished(struct promise_t *p) {
	if (p->state == PROMISE_UNRESOLVED) {
		BUG("expected promise to have been resolved/rejected");
	}
}

void promise_assert_failure(struct promise_t *p) {
	if (p->state != PROMISE_FAILURE) {
		BUG("expected promise to have been rejected");
	}
}

void promise_resolve(struct promise_t *p, int status) {
	if (p->state != PROMISE_UNRESOLVED) {
		BUG("promise was already resolved/rejected");
		return;
	}
	p->result.success_result = status;
	p->state = PROMISE_SUCCESS;
}

void promise_reject(struct promise_t *p, int status, const char* fmt, ...) {
	va_list args;
	if (p->state != PROMISE_UNRESOLVED) {
		BUG("promise was already resolved/rejected");
		return;
	}
	p->result.failure_result.status = status;

	strbuf_init(&p->result.failure_result.message, 0);

	va_start(args, fmt);
	strbuf_vaddf(&p->result.failure_result.message, fmt, args);
	va_end(args);

	p->state = PROMISE_FAILURE;
}

struct promise_t *promise_init(void) {
	// Promises are allocated on the heap, because they represent potentially long-running tasks,
	// and a stack-allocated value might not live long enough.
	struct promise_t *new_promise = xmalloc(sizeof(struct promise_t));
	struct failure_result_t failure_result;

	new_promise->state = PROMISE_UNRESOLVED;
	failure_result.status = 0;
	new_promise->result.failure_result = failure_result;

	return new_promise;
}

/**
 * Outputs an error message and size from a failed promise. The error message must be
 * free()'ed by the caller. Calling this function is not allowed if the promise is not
 * failed.
 *
 * Argument `size` may be omitted by passing in NULL.
 *
 * Note that although *error_message is null-terminated, its size may be larger
 * than the terminated string, and its actual size is indicated by *size.
 */
void promise_copy_error(struct promise_t *p, char **error_message, size_t *size) {
	size_t local_size;
	promise_assert_failure(p);

	*error_message = strbuf_detach(&p->result.failure_result.message, &local_size);
	if (size) {
		*size = local_size;
	}

	// We are only doing a copy, not a consume, so we need to put the error message back
	// the way we found it.
	strbuf_add(&p->result.failure_result.message, *error_message, strlen(*error_message));
}

/**
 * Fully deallocates the promise as well as the error message, if any.
 */
void promise_release(struct promise_t *p) {
	if (p->state == PROMISE_FAILURE) {
		strbuf_release(&p->result.failure_result.message);
	}
	free(p);
}
