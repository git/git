#!/bin/sh

if [ "$(id -u)" -eq 0 ]; then
  echo >&2 "::warning:: installing rust as root"
fi

if [ "$CARGO_HOME" = "" ]; then
  echo >&2 "::warning:: CARGO_HOME is not set"
  export CARGO_HOME=$HOME/.cargo
fi

export RUSTUP_HOME=$CARGO_HOME

if [ "$RUST_VERSION" = "" ]; then
  echo >&2 "::error:: RUST_VERSION is not set"
  exit 2
fi

## install rustup
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --default-toolchain none -y
if [ ! -f $CARGO_HOME/env ]; then
  echo "PATH=$CARGO_HOME/bin:\$PATH" > $CARGO_HOME/env
fi
## install a specific version of rust
if [ "$BITNESS" = "32" ]; then
  $CARGO_HOME/bin/rustup set default-host i686-unknown-linux-gnu || exit $?
  $CARGO_HOME/bin/rustup install $RUST_VERSION || exit $?
  $CARGO_HOME/bin/rustup default --force-non-host $RUST_VERSION || exit $?
else
  $CARGO_HOME/bin/rustup default $RUST_VERSION || exit $?
fi

. $CARGO_HOME/env
