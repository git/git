#!/bin/sh


rustc -vV || exit $?
cargo --version || exit $?

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

if grep x86_64-pc-windows-msvc rust/target/.rustc_info.json
then
  libfile="${crate}.lib"
else
  libfile="lib${crate}.a"
fi
dst=$dir_build/$libfile

if [ "$dir_git_root" != "$dir_build" ]; then
  src=$dir_rust/target/$rust_target/$libfile
  if [ ! -f $src ]; then
    echo >&2 "::error:: cannot find path of static library $src is not a file or does not exist"
    exit 5
  fi

  rm $dst 2>/dev/null
  mv $src $dst
fi
