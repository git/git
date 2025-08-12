#include "ivec.h"

static void ivec_set_capacity(void* self, usize new_capacity) {
	struct rawivec *this = self;
	if (new_capacity == 0)
		FREE_AND_NULL(this->ptr);
	else
		this->ptr = xrealloc(this->ptr, new_capacity * this->element_size);
	this->capacity = new_capacity;
}

void ivec_init(void* self, usize element_size) {
	struct rawivec *this = self;
	this->ptr = NULL;
	this->length = 0;
	this->capacity = 0;
	this->element_size = element_size;
}

/*
 * MUST CALL IVEC_INIT() FIRST!!!
 * This function will free the ivec, set self.capacity and self.length
 * to the specified capacity, and then calloc self.capacity number of
 * elements.
 */
void ivec_zero(void* self, usize capacity) {
	struct rawivec *this = self;
	if (this->ptr)
		FREE_AND_NULL(this->ptr);
	this->capacity = this->length = capacity;
	this->ptr = xcalloc(this->capacity, this->element_size);
}

void ivec_clear(void* self) {
	struct rawivec *this = self;
	this->length = 0;
}

void ivec_reserve_exact(void* self, usize additional) {
	struct rawivec *this = self;
	usize new_capacity = this->capacity + additional;
	ivec_set_capacity(self, new_capacity);
}

void ivec_reserve(void* self, usize additional) {
	struct rawivec *this = self;
	usize growby = 128;
	if (this->capacity > growby) {
		growby = this->capacity;
	}
	if (additional > growby) {
		growby = additional;
	}
	ivec_reserve_exact(self, growby);
}

void ivec_shrink_to_fit(void* self) {
	struct rawivec *this = self;
	ivec_set_capacity(self, this->length);
}

void ivec_resize(void* self, usize new_length, void* default_value) {
	struct rawivec *this = self;
	isize additional = (isize) (new_length - this->capacity);
	if (additional > 0) {
		ivec_reserve(self, additional);
	}

	for (usize i = this->length; i < new_length; i++) {
		void* dst = (u8*) this->ptr + (this->length + i) * this->element_size;
		memcpy(dst, default_value, this->element_size);
	}
	this->length = new_length;
}

void ivec_push(void* self, void* value) {
	struct rawivec *this = self;
	u8* dst;

	if (this->length == this->capacity) {
		ivec_reserve(self, 1);
	}
	dst = (u8*) this->ptr + this->length * this->element_size;
	memcpy(dst, value, this->element_size);
	this->length++;
}

void ivec_extend_from_slice(void* self, void const* ptr, usize size) {
	struct rawivec *this = self;
	u8* dst;

	if (size == 0)
		return;

	if (this->length + size > this->capacity) {
		ivec_reserve(self, this->capacity - this->length + size);
	}
	dst = (u8*) this->ptr + this->length * this->element_size;
	memcpy(dst, ptr, size * this->element_size);
	this->length += size;
}

bool ivec_equal(void* self, void* other) {
	struct rawivec *lhs = self;
	struct rawivec *rhs = other;

	if (lhs->element_size != rhs->element_size) {
		return false;
	}

	if (lhs->length != rhs->length) {
		return false;
	}


	for (usize i = 0; i < lhs->length; i++) {
		void* left = (u8 *) lhs->ptr + i * lhs->element_size;
		void* right = (u8 *) rhs->ptr + i * rhs->element_size;
		if (memcmp(left, right, lhs->element_size) != 0) {
			return false;
		}
	}

	return true;
}


void ivec_free(void* self) {
	struct rawivec *this = self;
	FREE_AND_NULL(this->ptr);
	this->length = 0;
	this->capacity = 0;
	/* don't modify self->element_size */
}

void ivec_move(void* source, void* destination) {
	struct rawivec *this = source;
	struct rawivec *that = destination;

	if (this->element_size != that->element_size)
		BUG("mismatched element_size");

	ivec_free(destination);
	that->ptr = this->ptr;
	that->length = this->length;
	that->capacity = this->capacity;

	this->ptr = NULL;
	this->length = 0;
	this->capacity = 0;
}
