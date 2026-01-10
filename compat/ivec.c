#include "ivec.h"

struct IVec_c_void {
	void *ptr;
	size_t length;
	size_t capacity;
	size_t element_size;
};

static void _set_capacity(void *self_, size_t new_capacity)
{
	struct IVec_c_void *self = self_;

	if (new_capacity == self->capacity) {
		return;
	}
	if (new_capacity == 0) {
		FREE_AND_NULL(self->ptr);
	} else {
		self->ptr = realloc(self->ptr, new_capacity * self->element_size);
	}
	self->capacity = new_capacity;
}


void ivec_init(void *self_, size_t element_size)
{
	struct IVec_c_void *self = self_;

	self->ptr = NULL;
	self->length = 0;
	self->capacity = 0;
	self->element_size = element_size;
}

void ivec_zero(void *self_, size_t capacity)
{
	struct IVec_c_void *self = self_;

	self->ptr = calloc(capacity, self->element_size);
	self->length = capacity;
	self->capacity = capacity;
	// DO NOT MODIFY element_size!!!
}

void ivec_reserve_exact(void *self_, size_t additional)
{
	struct IVec_c_void *self = self_;

	_set_capacity(self, self->capacity + additional);
}

void ivec_reserve(void *self_, size_t additional)
{
	struct IVec_c_void *self = self_;

	size_t growby = 128;
	if (self->capacity > growby)
		growby = self->capacity;
	if (additional > growby)
		growby = additional;

	_set_capacity(self, self->capacity + growby);
}

void ivec_shrink_to_fit(void *self_)
{
	struct IVec_c_void *self = self_;

	_set_capacity(self, self->length);
}

void ivec_push(void *self_, const void *value)
{
	struct IVec_c_void *self = self_;
	void *dst = NULL;

	if (self->length == self->capacity)
		ivec_reserve(self, 1);

	dst = (uint8_t*)self->ptr + self->length * self->element_size;
	memcpy(dst, value, self->element_size);
	self->length++;
}

void ivec_free(void *self_)
{
	struct IVec_c_void *self = self_;

	FREE_AND_NULL(self->ptr);
	self->length = 0;
	self->capacity = 0;
	// DO NOT MODIFY element_size!!!
}

void ivec_move(void *src_, void *dst_)
{
	struct IVec_c_void *src = src_;
	struct IVec_c_void *dst = dst_;

	ivec_free(dst);
	dst->ptr = src->ptr;
	dst->length = src->length;
	dst->capacity = src->capacity;
	// DO NOT MODIFY element_size!!!

	src->ptr = NULL;
	src->length = 0;
	src->capacity = 0;
	// DO NOT MODIFY element_size!!!
}
