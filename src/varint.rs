/// Decode the variable-length integer stored in `bufp` and return the decoded value.
///
/// Returns 0 in case the decoded integer would overflow u64::MAX.
///
/// # Safety
///
/// The buffer must be NUL-terminated to ensure safety.
#[no_mangle]
pub unsafe extern "C" fn decode_varint(bufp: *mut *const u8) -> u64 {
    let mut buf = *bufp;
    let mut c = *buf;
    let mut val = u64::from(c & 127);

    buf = buf.add(1);

    while (c & 128) != 0 {
        val = val.wrapping_add(1);
        if val == 0 || val.leading_zeros() < 7 {
            return 0; // overflow
        }

        c = *buf;
        buf = buf.add(1);

        val = (val << 7) + u64::from(c & 127);
    }

    *bufp = buf;
    val
}

/// Encode `value` into `buf` as a variable-length integer unless `buf` is null.
///
/// Returns the number of bytes written, or, if `buf` is null, the number of bytes that would be
/// written to encode the integer.
///
/// # Safety
///
/// `buf` must either be null or point to at least 16 bytes of memory.
#[no_mangle]
pub unsafe extern "C" fn encode_varint(value: u64, buf: *mut u8) -> u8 {
    let mut varint: [u8; 16] = [0; 16];
    let mut pos = varint.len() - 1;

    varint[pos] = (value & 127) as u8;

    let mut value = value >> 7;
    while value != 0 {
        pos -= 1;
        value -= 1;
        varint[pos] = 128 | (value & 127) as u8;
        value >>= 7;
    }

    if !buf.is_null() {
        std::ptr::copy_nonoverlapping(varint.as_ptr().add(pos), buf, varint.len() - pos);
    }

    (varint.len() - pos) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_decode_varint() {
        unsafe {
            assert_eq!(decode_varint(&mut [0x00].as_slice().as_ptr()), 0);
            assert_eq!(decode_varint(&mut [0x01].as_slice().as_ptr()), 1);
            assert_eq!(decode_varint(&mut [0x7f].as_slice().as_ptr()), 127);
            assert_eq!(decode_varint(&mut [0x80, 0x00].as_slice().as_ptr()), 128);
            assert_eq!(decode_varint(&mut [0x80, 0x01].as_slice().as_ptr()), 129);
            assert_eq!(decode_varint(&mut [0x80, 0x7f].as_slice().as_ptr()), 255);

            // Overflows are expected to return 0.
            assert_eq!(decode_varint(&mut [0x88; 16].as_slice().as_ptr()), 0);
        }
    }

    #[test]
    fn test_encode_varint() {
        unsafe {
            let mut varint: [u8; 16] = [0; 16];

            assert_eq!(encode_varint(0, std::ptr::null_mut()), 1);

            assert_eq!(encode_varint(0, varint.as_mut_slice().as_mut_ptr()), 1);
            assert_eq!(varint, [0; 16]);

            assert_eq!(encode_varint(10, varint.as_mut_slice().as_mut_ptr()), 1);
            assert_eq!(varint, [10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

            assert_eq!(encode_varint(127, varint.as_mut_slice().as_mut_ptr()), 1);
            assert_eq!(varint, [127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

            assert_eq!(encode_varint(128, varint.as_mut_slice().as_mut_ptr()), 2);
            assert_eq!(varint, [128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

            assert_eq!(encode_varint(129, varint.as_mut_slice().as_mut_ptr()), 2);
            assert_eq!(varint, [128, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);

            assert_eq!(encode_varint(255, varint.as_mut_slice().as_mut_ptr()), 2);
            assert_eq!(varint, [128, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        }
    }
}
