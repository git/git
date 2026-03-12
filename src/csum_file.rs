// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation: version 2 of the License, dated June 1991.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see <https://www.gnu.org/licenses/>.

use crate::hash::{HashAlgorithm, GIT_MAX_RAWSZ};
use std::ffi::CStr;
use std::io::{self, Write};
use std::os::raw::c_void;

/// A writer that can write files identified by their hash or containing a trailing hash.
pub struct HashFile {
    ptr: *mut c_void,
    algo: HashAlgorithm,
}

impl HashFile {
    /// Create a new HashFile.
    ///
    /// The hash used will be `algo`, its name should be in `name`, and an open file descriptor
    /// pointing to that file should be in `fd`.
    pub fn new(algo: HashAlgorithm, fd: i32, name: &CStr) -> HashFile {
        HashFile {
            ptr: unsafe { c::hashfd(algo.hash_algo_ptr(), fd, name.as_ptr()) },
            algo,
        }
    }

    /// Finalize this HashFile instance.
    ///
    /// Returns the hash computed over the data.
    pub fn finalize(self, component: u32, flags: u32) -> Vec<u8> {
        let mut result = vec![0u8; GIT_MAX_RAWSZ];
        unsafe { c::finalize_hashfile(self.ptr, result.as_mut_ptr(), component, flags) };
        result.truncate(self.algo.raw_len());
        result
    }
}

impl Write for HashFile {
    fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        for chunk in data.chunks(u32::MAX as usize) {
            unsafe {
                c::hashwrite(
                    self.ptr,
                    chunk.as_ptr() as *const c_void,
                    chunk.len() as u32,
                )
            };
        }
        Ok(data.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        unsafe { c::hashflush(self.ptr) };
        Ok(())
    }
}

pub mod c {
    use std::os::raw::{c_char, c_int, c_void};

    extern "C" {
        pub fn hashfd(algop: *const c_void, fd: i32, name: *const c_char) -> *mut c_void;
        pub fn hashwrite(f: *mut c_void, data: *const c_void, len: u32);
        pub fn hashflush(f: *mut c_void);
        pub fn finalize_hashfile(
            f: *mut c_void,
            data: *mut u8,
            component: u32,
            flags: u32,
        ) -> c_int;
    }
}
