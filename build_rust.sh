#!/bin/sh

rustc -vV || exit $?
cargo --version || exit $?

dir_git_root=${0%/*}
dir_build=$1
rust_build_profile=$2
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

if [ "$rust_build_profile" = "" ]; then
  echo "did not specify the rust_build_profile"
  exit 1
fi

if [ "$rust_build_profile" = "release" ]; then
  rust_args="--release"
  export RUSTFLAGS=''
elif [ "$rust_build_profile" = "debug" ]; then
  rust_args=""
  export RUSTFLAGS='-C debuginfo=2 -C opt-level=1 -C force-frame-pointers=yes'
else
  echo "illegal rust_build_profile value $rust_build_profile"
  exit 1
fi

libfile="lib${crate}.a"
if rustup show active-toolchain | grep windows-msvc; then
  libfile="${crate}.lib"
  PATH="$(echo $PATH | tr ':' '\n' | grep -Ev "^(/mingw64/bin|/usr/bin)$" | paste -sd: -):/mingw64/bin:/usr/bin"
fi

CARGO_TARGET_DIR=$dir_git_root/.build/rust/$crate
export CARGO_TARGET_DIR

cd $dir_rust && cargo clean && pwd && USE_LINKING="false" cargo build -p $crate $rust_args; cd $dir_git_root

src=$CARGO_TARGET_DIR/$rust_build_profile/$libfile
dst=$dir_build/$libfile

if [ ! -f $src ]; then
  echo >&2 "::error:: cannot find path of static library $src is not a file or does not exist"
  exit 5
fi

rm $dst 2>/dev/null
echo mv $src $dst
mv $src $dst
