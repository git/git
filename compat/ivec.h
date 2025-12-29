#ifndef IVEC_H
#define IVEC_H

#include <git-compat-util.h>

#define IVEC_INIT(variable) ivec_init(&(variable), sizeof(*(variable).ptr))

#ifndef CBINDGEN
#define DEFINE_IVEC_TYPE(type, suffix) \
struct IVec_##suffix { \
	type* ptr; \
	size_t length; \
	size_t capacity; \
	size_t element_size; \
}

DEFINE_IVEC_TYPE(bool, bool);

DEFINE_IVEC_TYPE(uint8_t, u8);
DEFINE_IVEC_TYPE(uint16_t, u16);
DEFINE_IVEC_TYPE(uint32_t, u32);
DEFINE_IVEC_TYPE(uint64_t, u64);

DEFINE_IVEC_TYPE(int8_t, i8);
DEFINE_IVEC_TYPE(int16_t, i16);
DEFINE_IVEC_TYPE(int32_t, i32);
DEFINE_IVEC_TYPE(int64_t, i64);

DEFINE_IVEC_TYPE(float, f32);
DEFINE_IVEC_TYPE(double, f64);

DEFINE_IVEC_TYPE(size_t, usize);
DEFINE_IVEC_TYPE(ssize_t, isize);
#endif

void ivec_init(void *self_, size_t element_size);

void ivec_zero(void *self_, size_t capacity);

void ivec_reserve_exact(void *self_, size_t additional);

void ivec_reserve(void *self_, size_t additional);

void ivec_shrink_to_fit(void *self_);

void ivec_push(void *self_, const void *value);

void ivec_free(void *self_);

void ivec_move(void *src, void *dst);

#endif /* IVEC_H */
