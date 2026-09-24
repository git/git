use sha1dc::Hasher;
use std::ffi::CString;
use std::os::raw::c_char;
use std::{ptr, slice};

/// Initialize a collision-detecting SHA-1 context.
///
/// # Safety
/// `ctx` must point to an uninitialized SHA-1 context.
#[no_mangle]
pub unsafe extern "C" fn sha1dc_rs_init(ctx: *mut *mut Hasher) {
    *ctx = Box::into_raw(Box::new(Hasher::new()));
}

/// Replace a SHA-1 context with a clone of another.
///
/// # Safety
/// Both contexts must be initialized.
#[no_mangle]
pub unsafe extern "C" fn sha1dc_rs_clone(dst: *mut *mut Hasher, src: *const *mut Hasher) {
    let hasher = Box::new((**src).clone());
    drop(Box::from_raw(*dst));
    *dst = Box::into_raw(hasher);
}

/// Update the SHA-1 hasher with the given bytes.
///
/// # Safety
/// `ctx` must be initialized and `data` must point to `len` bytes unless
/// `len` is zero.
#[no_mangle]
pub unsafe extern "C" fn sha1dc_rs_update(ctx: *mut *mut Hasher, data: *const c_char, len: usize) {
    if len != 0 {
        (**ctx).update(slice::from_raw_parts(data.cast::<u8>(), len));
    }
}

/// Finalize SHA-1, reporting detected collisions through `die`.
///
/// # Safety
/// `ctx` must be initialized, `hash` must point to at least 20 bytes, and
/// `die` must be a non-returning C variadic function.
#[no_mangle]
pub unsafe extern "C" fn sha1dc_rs_final(
    hash: *mut u8,
    ctx: *mut *mut Hasher,
    die: unsafe extern "C" fn(*const c_char, ...) -> !,
) {
    let hasher = *Box::from_raw(*ctx);
    *ctx = ptr::null_mut();
    match hasher.finalize() {
        Ok(digest) => ptr::copy_nonoverlapping(digest.as_bytes().as_ptr(), hash, 20),
        Err(collision) => {
            let message = CString::new(format!(
                "SHA-1 appears to be part of a collision attack: {}",
                collision.digest()
            ))
            .expect("collision message contains no NUL");
            die(message.as_ptr());
        }
    }
}

/// Discard a SHA-1 context without producing a digest.
///
/// # Safety
/// `ctx` must be initialized.
#[no_mangle]
pub unsafe extern "C" fn sha1dc_rs_discard(ctx: *mut *mut Hasher) {
    drop(Box::from_raw(*ctx));
    *ctx = ptr::null_mut();
}
