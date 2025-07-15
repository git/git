#!/bin/sh

if [ "$CARGO_HOME" = "" ]; then
  echo >&2 "::error:: CARGO_HOME is not set"
  exit 2
fi
export PATH="$CARGO_HOME/bin:$PATH"
rustup -vV || exit $?

## Enforce the correct Rust toolchain
rustup override unset || true

## install a specific version of rust
if [ "$RUST_TARGET" != "" ]; then
  rustup default --force-non-host "$RUST_VERSION-$RUST_TARGET" || exit $?
else
  rustup default "$RUST_VERSION" || exit $?
fi

rustc -vV || exit $?

RE_RUST_TARGET="$RUST_TARGET"
if [ "$RUST_TARGET" = "" ]; then
  RE_RUST_TARGET="[^ ]+"
fi

if ! rustup show active-toolchain | grep -E "^$RUST_VERSION-$RE_RUST_TARGET \(default\)$"; then
  echo >&2 "::error:: wrong Rust toolchain, active-toolchain: $(rustup show active-toolchain)"
  exit 3
fi
