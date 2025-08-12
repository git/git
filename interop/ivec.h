#ifndef IVEC_H
#define IVEC_H

#include "../git-compat-util.h"

struct rawivec {
	void* ptr;
	usize length;
	usize capacity;
	usize element_size;
};

#define DEFINE_IVEC_TYPE(type, suffix) \
struct ivec_##suffix { \
	type* ptr; \
	size_t length; \
	size_t capacity; \
	size_t element_size; \
}

#define IVEC_INIT(variable) ivec_init(&(variable), sizeof(*(variable).ptr))

DEFINE_IVEC_TYPE(u8, u8);
DEFINE_IVEC_TYPE(u16, u16);
DEFINE_IVEC_TYPE(u32, u32);
DEFINE_IVEC_TYPE(u64, u64);

DEFINE_IVEC_TYPE(i8, i8);
DEFINE_IVEC_TYPE(i16, i16);
DEFINE_IVEC_TYPE(i32, i32);
DEFINE_IVEC_TYPE(i64, i64);

DEFINE_IVEC_TYPE(f32, f32);
DEFINE_IVEC_TYPE(f64, f64);

DEFINE_IVEC_TYPE(usize, usize);
DEFINE_IVEC_TYPE(isize, isize);

void ivec_init(void* self, usize element_size);
void ivec_zero(void* self, usize capacity);
void ivec_clear(void* self);
void ivec_reserve_exact(void* self, usize additional);
void ivec_reserve(void* self, usize additional);
void ivec_shrink_to_fit(void* self);
void ivec_resize(void* self, usize new_length, void* default_value);
void ivec_push(void* self, void* value);
void ivec_extend_from_slice(void* self, void const* ptr, usize size);
bool ivec_equal(void* self, void* other);
void ivec_free(void* self);
void ivec_move(void* source, void* destination);

#endif //IVEC_H
