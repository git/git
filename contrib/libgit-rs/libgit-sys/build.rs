use std::env;
use std::path::PathBuf;

pub fn main() -> std::io::Result<()> {
    let ac = autocfg::new();
    ac.emit_has_path("std::ffi::c_char");

    let crate_root = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let git_root = crate_root.join("../../..");
    let dst = PathBuf::from(env::var_os("OUT_DIR").unwrap());

    let make_output = make_cmd::gnu_make()
        .env("DEVELOPER", "1")
        .env_remove("PROFILE")
        .current_dir(git_root.clone())
        .args([
            "INCLUDE_LIBGIT_RS=YesPlease",
            "contrib/libgit-rs/libgit-sys/libgitpub.a",
        ])
        .output()
        .expect("Make failed to run");
    if !make_output.status.success() {
        panic!(
            "Make failed:\n  stdout = {}\n  stderr = {}\n",
            String::from_utf8(make_output.stdout).unwrap(),
            String::from_utf8(make_output.stderr).unwrap()
        );
    }
    std::fs::copy(crate_root.join("libgitpub.a"), dst.join("libgitpub.a"))?;
    println!("cargo:rustc-link-search=native={}", dst.display());
    println!("cargo:rustc-link-lib=gitpub");
    println!("cargo:rerun-if-changed={}", git_root.display());

    Ok(())
}
