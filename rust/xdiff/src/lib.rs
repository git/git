

#[no_mangle]
unsafe extern "C" fn xxh3_64(ptr: *const u8, size: usize) -> u64 {
    let slice = std::slice::from_raw_parts(ptr, size);
    xxhash_rust::xxh3::xxh3_64(slice)
}
