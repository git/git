#!/bin/sh

## github workflows actions-rs/toolchain@v1 doesn't work for docker
## targets. This script should only be used if the ci pipeline
## doesn't support installing rust on a particular target.

if [ "$(id -u)" -eq 0 ]; then
  echo >&2 "::warning:: installing rust as root"
fi

if [ "$CARGO_HOME" = "" ]; then
  echo >&2 "::error:: CARGO_HOME is not set"
  exit 2
fi

export RUSTUP_HOME=$CARGO_HOME

## install rustup
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --default-toolchain none -y
if [ ! -f $CARGO_HOME/env ]; then
  echo "PATH=$CARGO_HOME/bin:\$PATH" > $CARGO_HOME/env
fi
. $CARGO_HOME/env

rustup -vV
