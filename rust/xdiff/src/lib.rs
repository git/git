pub mod xprepare;
pub mod xtypes;

use crate::xprepare::trim_ends;
use crate::xtypes::xdfile;

#[no_mangle]
unsafe extern "C" fn xdl_trim_ends(xdf1: *mut xdfile, xdf2: *mut xdfile) -> i32 {
    let xdf1 = xdf1.as_mut().expect("null pointer");
    let xdf2 = xdf2.as_mut().expect("null pointer");

    trim_ends(xdf1, xdf2);

    0
}
