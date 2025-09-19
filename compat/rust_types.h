#ifndef COMPAT_RUST_TYPES_H
#define COMPAT_RUST_TYPES_H

#include <compat/posix.h>

/*
 * A typedef for bool is not needed because C bool and Rust bool are
 * the same if #include <stdbool.h> is used.
 */

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;

typedef float     f32;
typedef double    f64;

typedef size_t    usize;
typedef ptrdiff_t isize;
typedef uint32_t  rust_char;

#endif /* COMPAT_RUST_TYPES_H */
