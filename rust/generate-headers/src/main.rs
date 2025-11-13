use cbindgen;

use std::io::Write;
use std::path::PathBuf;
use cbindgen::Config;

fn generate_header<F>(crate_name: String, editor: F)
where
    F: Fn(&mut Config)
{
    let dir_workspace = PathBuf::default();
    let dir_rust = dir_workspace.join("rust");
    let dir_crate = dir_rust.join(crate_name.clone());
    let dir_generated = dir_workspace.join("generated");
    if !dir_generated.exists() {
        std::fs::create_dir(dir_generated.clone()).unwrap();
    }

    let file_cbindgen = dir_rust.join("cbindgen-template.toml");
    let file_out = dir_generated.join(format!("{}.h", crate_name.clone()));

    let mut config = Config::from_file(file_cbindgen.display().to_string().as_str()).unwrap();
    config.include_guard = Some(format!("{}_H", crate_name.to_uppercase()));

    editor(&mut config);

    let mut buffer = Vec::<u8>::new();
    cbindgen::Builder::new()
        .with_crate(dir_crate.clone())
        .with_config(config)
        .with_std_types(true)
        .generate()
        .expect("Unable to generate bindings")
        .write(&mut buffer);

    let mut fd = std::fs::File::create(file_out).unwrap();
    fd.write(buffer.as_slice()).unwrap();
}

fn main() {
    // cargo run -p generate-headers

    generate_header(String::from("gitcore"), |_|{});
}
