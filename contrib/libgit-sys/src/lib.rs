#[cfg(has_std__ffi__c_char)]
use std::ffi::c_char;

#[cfg(not(has_std__ffi__c_char))]
#[allow(non_camel_case_types)]
pub type c_char = i8;

extern crate libz_sys;

extern "C" {
    pub fn libgit_user_agent() -> *const c_char;
    pub fn libgit_user_agent_sanitized() -> *const c_char;
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
