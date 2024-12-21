use std::ffi::c_void;

#[cfg(has_std__ffi__c_char)]
use std::ffi::{c_char, c_int};

#[cfg(not(has_std__ffi__c_char))]
#[allow(non_camel_case_types)]
pub type c_char = i8;

#[cfg(not(has_std__ffi__c_char))]
#[allow(non_camel_case_types)]
pub type c_int = i32;

extern crate libz_sys;

#[allow(non_camel_case_types)]
#[repr(C)]
pub struct libgit_config_set {
    _data: [u8; 0],
    _marker: core::marker::PhantomData<(*mut u8, core::marker::PhantomPinned)>,
}

extern "C" {
    pub fn free(ptr: *mut c_void);

    pub fn libgit_user_agent() -> *const c_char;
    pub fn libgit_user_agent_sanitized() -> *const c_char;

    pub fn libgit_configset_alloc() -> *mut libgit_config_set;
    pub fn libgit_configset_free(cs: *mut libgit_config_set);

    pub fn libgit_configset_add_file(cs: *mut libgit_config_set, filename: *const c_char) -> c_int;

    pub fn libgit_configset_get_int(
        cs: *mut libgit_config_set,
        key: *const c_char,
        int: *mut c_int,
    ) -> c_int;

    pub fn libgit_configset_get_string(
        cs: *mut libgit_config_set,
        key: *const c_char,
        dest: *mut *mut c_char,
    ) -> c_int;

}

#[cfg(test)]
mod tests {
    use std::ffi::CStr;

    use super::*;

    #[test]
    fn user_agent_starts_with_git() {
        let c_str = unsafe { CStr::from_ptr(libgit_user_agent()) };
        let agent = c_str
            .to_str()
            .expect("User agent contains invalid UTF-8 data");
        assert!(
            agent.starts_with("git/"),
            r#"Expected user agent to start with "git/", got: {}"#,
            agent
        );
    }

    #[test]
    fn sanitized_user_agent_starts_with_git() {
        let c_str = unsafe { CStr::from_ptr(libgit_user_agent_sanitized()) };
        let agent = c_str
            .to_str()
            .expect("Sanitized user agent contains invalid UTF-8 data");
        assert!(
            agent.starts_with("git/"),
            r#"Expected user agent to start with "git/", got: {}"#,
            agent
        );
    }
}
