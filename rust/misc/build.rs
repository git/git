use build_helper::BuildHelper;

fn main() {
    BuildHelper::new(std::env::vars().collect())
        .build();
}
