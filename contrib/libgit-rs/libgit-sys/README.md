# cgit-info

A small hacky proof-of-concept showing how to provide a Rust FFI for the Git
library.

## Building

`cargo build` automatically builds and picks up on changes made to both
the Rust wrapper and git.git code so there is no need to run `make`
beforehand.

## Running

Assuming you don't make any changes to the Git source, you can just work from
`contrib/cgit-rs` and use `cargo build` or `cargo run` as usual.
