#include "ivec.h"

#ifdef __cplusplus
extern "C" {
#endif

static void ivec_set_capacity(void *self_, usize new_capacity)
{
	struct rawivec *self = self_;

	if (new_capacity == 0)
		FREE_AND_NULL(self->ptr);
	else
		self->ptr = xrealloc(self->ptr, new_capacity * self->element_size);
	self->capacity = new_capacity;
}

void ivec_init(void *self_, usize element_size)
{
	struct rawivec *self = self_;

	self->ptr = NULL;
	self->length = 0;
	self->capacity = 0;
	self->element_size = element_size;
}

/*
 * MUST CALL IVEC_INIT() FIRST!!!
 * This function will free the ivec, set self.capacity and self.length
 * to the specified capacity, and then calloc self.capacity number of
 * elements.
 */
void ivec_zero(void *self_, usize capacity)
{
	struct rawivec *self = self_;

	if (self->ptr)
		FREE_AND_NULL(self->ptr);
	self->capacity = self->length = capacity;
	self->ptr = xcalloc(self->capacity, self->element_size);
}

void ivec_clear(void *self_)
{
	struct rawivec *self = self_;

	self->length = 0;
}

void ivec_reserve_exact(void *self_, usize additional)
{
	struct rawivec *self = self_;
	usize new_capacity = self->capacity + additional;

	ivec_set_capacity(self, new_capacity);
}

void ivec_reserve(void *self_, usize additional)
{
	struct rawivec *self = self_;
	usize growby = 128;

	if (self->capacity > growby) {
		growby = self->capacity;
	}
	if (additional > growby) {
		growby = additional;
	}
	ivec_reserve_exact(self, growby);
}

void ivec_shrink_to_fit(void *self_)
{
	struct rawivec *self = self_;

	ivec_set_capacity(self_, self->length);
}

void ivec_resize(void *self_, usize new_length, void *default_value)
{
	struct rawivec *self = self_;
	isize additional = (isize) (new_length - self->capacity);

	if (additional > 0) {
		ivec_reserve(self_, additional);
	}

	for (usize i = self->length; i < new_length; i++) {
		void *dst = (u8 *)self->ptr + (self->length + i) * self->element_size;
		memcpy(dst, default_value, self->element_size);
	}
	self->length = new_length;
}

void ivec_push(void *self_, void *value)
{
	struct rawivec *self = self_;
	u8 *dst;

	if (self->length == self->capacity) {
		ivec_reserve(self_, 1);
	}
	dst = (u8 *)self->ptr + self->length * self->element_size;
	memcpy(dst, value, self->element_size);
	self->length++;
}

void ivec_extend_from_slice(void *self_, void const *ptr, usize size)
{
	struct rawivec *self = self_;
	u8 *dst;

	if (size == 0)
		return;

	if (self->length + size > self->capacity) {
		ivec_reserve(self_, self->capacity - self->length + size);
	}
	dst = (u8 *)self->ptr + self->length * self->element_size;
	memcpy(dst, ptr, size * self->element_size);
	self->length += size;
}

bool ivec_equal(void *self_, void *other)
{
	struct rawivec *lhs = self_;
	struct rawivec *rhs = other;

	if (lhs->element_size != rhs->element_size) {
		return false;
	}

	if (lhs->length != rhs->length) {
		return false;
	}

	for (usize i = 0; i < lhs->length; i++) {
		void *left = (u8 *)lhs->ptr + i * lhs->element_size;
		void *right = (u8 *)rhs->ptr + i * rhs->element_size;
		if (memcmp(left, right, lhs->element_size) != 0) {
			return false;
		}
	}

	return true;
}


void ivec_free(void *self_)
{
	struct rawivec *self = self_;

	FREE_AND_NULL(self->ptr);
	self->length = 0;
	self->capacity = 0;
	/* don't modify self->element_size */
}

void ivec_move(void *source, void *destination)
{
	struct rawivec *src = source;
	struct rawivec *dst = destination;

	if (src->element_size != dst->element_size)
		BUG("mismatched element_size");

	ivec_free(destination);
	dst->ptr = src->ptr;
	dst->length = src->length;
	dst->capacity = src->capacity;

	src->ptr = NULL;
	src->length = 0;
	src->capacity = 0;
}

#ifdef __cplusplus
}
#endif
