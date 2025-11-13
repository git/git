use std::collections::HashMap;
use std::path::PathBuf;


fn parse_bool_from_str(value: &str) -> bool {
    match value {
        "1" | "true"  | "yes" | "on"  => true,
        "0" | "false" | "no"  | "off" => false,
        _ => false
    }
}

fn parse_bool_from_option(value: Option<&String>, default: bool) -> bool {
    match value {
        Some(v) => {
            parse_bool_from_str(v.as_str())
        },
        None => default,
    }
}

/// To build without linking against C libraries run `USE_LINKING=false cargo build`
/// To run tests set GIT_BUILD_DIR and run `USE_LINKING=true cargo test`
pub struct BuildHelper {
    crate_env: HashMap<String, String>,
}


impl BuildHelper {
    pub fn new(crate_env: HashMap<String, String>) -> Self {
        Self {
            crate_env,
        }
    }

    pub fn crate_name(&self) -> String {
        self.crate_env["CARGO_PKG_NAME"].clone()
    }

    pub fn dir_crate(&self) -> PathBuf {
        PathBuf::from(self.crate_env["CARGO_MANIFEST_DIR"].clone())
    }

    pub fn build(self) {
        let use_linking = parse_bool_from_option(self.crate_env.get("USE_LINKING"), self.crate_env.get("CARGO_TARGET_DIR").is_none());
        let dir_crate = self.dir_crate();
        let dir_git = dir_crate.parent().unwrap().parent().unwrap();

        println!("cargo:rerun-if-changed={}", dir_git.display());

        if use_linking {
            if let Some(git_build_dir) = self.crate_env.get("GIT_BUILD_DIR") {
                let path_git_build_dir = PathBuf::from(git_build_dir);
                let is_abs = path_git_build_dir.is_absolute();
                if !is_abs {
                    panic!("GIT_BUILD_DIR must be an absolute path: {}'", path_git_build_dir.display());
                }
                if !path_git_build_dir.is_dir() {
                    panic!("'GIT_BUILD_DIR' is not a directory: {}", path_git_build_dir.display());
                }
                println!("cargo:rustc-link-search=native={}", git_build_dir);
            } else {
                panic!("environment variable 'GIT_BUILD_DIR' is not set");
            }

            println!("cargo:rustc-link-lib=static=git");
            println!("cargo:rustc-link-lib=pcre2-8");
            if self.crate_env.get("ZLIB_NG").is_some() {
                println!("cargo:rustc-link-lib=z-ng");
            } else {
                println!("cargo:rustc-link-lib=z");
            }
        } else {
            println!("cargo:warning={} is not linking against C objects, `USE_LINKING=true cargo test`", self.crate_env["CARGO_PKG_NAME"]);
        }
    }
}
