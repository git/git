use std::hash::Hasher;
use xxhash_rust::xxh3::Xxh3Default;
use crate::xutils::*;

pub mod xutils;

pub const XDF_IGNORE_WHITESPACE: u64 = 1 << 1;
pub const XDF_IGNORE_WHITESPACE_CHANGE: u64 = 1 << 2;
pub const XDF_IGNORE_WHITESPACE_AT_EOL: u64 = 1 << 3;
pub const XDF_IGNORE_CR_AT_EOL: u64 = 1 << 4;
pub const XDF_WHITESPACE_FLAGS: u64 = XDF_IGNORE_WHITESPACE |
    XDF_IGNORE_WHITESPACE_CHANGE |
    XDF_IGNORE_WHITESPACE_AT_EOL |
    XDF_IGNORE_CR_AT_EOL;


#[no_mangle]
unsafe extern "C" fn xdl_line_hash(ptr: *const u8, size: usize, flags: u64) -> u64 {
    let line = std::slice::from_raw_parts(ptr, size);

    line_hash(line, flags)
}

#[no_mangle]
unsafe extern "C" fn xdl_line_equal(lhs: *const u8, lhs_len: usize, rhs: *const u8, rhs_len: usize, flags: u64) -> bool {
    let lhs_line = std::slice::from_raw_parts(lhs, lhs_len);
    let rhs_line = std::slice::from_raw_parts(rhs, rhs_len);

    line_equal(lhs_line, rhs_line, flags)
}
