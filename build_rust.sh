#!/bin/sh

if [ -z "$CARGO_HOME" ]; then
  export CARGO_HOME=$HOME/.cargo
  echo >&2 "::warning:: CARGO_HOME is not set"
fi
echo "CARGO_HOME=$CARGO_HOME"

rustc -vV
cargo --version

dir_git_root=${0%/*}
dir_build=$1
rust_target=$2
crate=$3

dir_rust=$dir_git_root/rust

if [ "$dir_git_root" = "" ]; then
  echo "did not specify the directory for the root of git"
  exit 1
fi

if [ "$dir_build" = "" ]; then
  echo "did not specify the build directory"
  exit 1
fi

if [ "$rust_target" = "" ]; then
  echo "did not specify the rust_target"
  exit 1
fi

if [ "$rust_target" = "release" ]; then
  rust_args="--release"
  export RUSTFLAGS='-Aunused_imports -Adead_code'
elif [ "$rust_target" = "debug" ]; then
  rust_args=""
  export RUSTFLAGS='-Aunused_imports -Adead_code -C debuginfo=2 -C opt-level=1 -C force-frame-pointers=yes'
else
  echo "illegal rust_target value $rust_target"
  exit 1
fi

cd $dir_rust && cargo clean && pwd && cargo build -p $crate $rust_args; cd ..

libfile="lib${crate}.a"
dst=$dir_build/$libfile

if [ "$dir_git_root" != "$dir_build" ]; then
  src=$dir_rust/target/$rust_target/$libfile
  if [ ! -f $src ]; then
    echo >&2 "::error:: cannot find path of static library"
    exit 5
  fi

  rm $dst 2>/dev/null
  mv $src $dst
fi
