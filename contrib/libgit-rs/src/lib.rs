use std::ffi::{c_void, CStr, CString};
use std::path::Path;

#[cfg(has_std__ffi__c_char)]
use std::ffi::{c_char, c_int};

#[cfg(not(has_std__ffi__c_char))]
#[allow(non_camel_case_types)]
pub type c_char = i8;

#[cfg(not(has_std__ffi__c_char))]
#[allow(non_camel_case_types)]
pub type c_int = i32;

use libgit_sys::*;

pub struct ConfigSet(*mut libgit_config_set);
impl ConfigSet {
    pub fn new() -> Self {
        unsafe { ConfigSet(libgit_configset_alloc()) }
    }

    pub fn add_files(&mut self, files: &[&Path]) {
        for file in files {
            let pstr = file.to_str().expect("Invalid UTF-8");
            let rs = CString::new(pstr).expect("Couldn't convert to CString");
            unsafe {
                libgit_configset_add_file(self.0, rs.as_ptr());
            }
        }
    }

    pub fn get_int(&mut self, key: &str) -> Option<i32> {
        let key = CString::new(key).expect("Couldn't convert to CString");
        let mut val: c_int = 0;
        unsafe {
            if libgit_configset_get_int(self.0, key.as_ptr(), &mut val as *mut c_int) != 0 {
                return None;
            }
        }

        Some(val.into())
    }

    pub fn get_string(&mut self, key: &str) -> Option<String> {
        let key = CString::new(key).expect("Couldn't convert key to CString");
        let mut val: *mut c_char = std::ptr::null_mut();
        unsafe {
            if libgit_configset_get_string(self.0, key.as_ptr(), &mut val as *mut *mut c_char) != 0
            {
                return None;
            }
            let borrowed_str = CStr::from_ptr(val);
            let owned_str =
                String::from(borrowed_str.to_str().expect("Couldn't convert val to str"));
            free(val as *mut c_void); // Free the xstrdup()ed pointer from the C side
            Some(owned_str)
        }
    }
}

impl Default for ConfigSet {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for ConfigSet {
    fn drop(&mut self) {
        unsafe {
            libgit_configset_free(self.0);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn load_configs_via_configset() {
        let mut cs = ConfigSet::new();
        cs.add_files(&[
            Path::new("testdata/config1"),
            Path::new("testdata/config2"),
            Path::new("testdata/config3"),
        ]);
        // ConfigSet retrieves correct value
        assert_eq!(cs.get_int("trace2.eventTarget"), Some(1));
        // ConfigSet respects last config value set
        assert_eq!(cs.get_int("trace2.eventNesting"), Some(3));
        // ConfigSet returns None for missing key
        assert_eq!(cs.get_string("foo.bar"), None);
    }
}
