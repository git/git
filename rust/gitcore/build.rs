use link_with_c::BuildHelper;

fn main() {
    BuildHelper::new(std::env::vars().collect())
        // .generate_header(|_|{})
        .build();
}
