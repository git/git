pub fn main() {
    let ac = autocfg::new();
    ac.emit_has_path("std::ffi::c_char");
}
