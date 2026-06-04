#!/bin/sh

if test "$#" -lt 2
then
	exit 1
fi

SOURCE_DIR="$1"
BUILD_DIR="$2"
BUILD_TYPE=debug

shift 2

for arg
do
	case "$arg" in
	--release)
		BUILD_TYPE=release;;
	esac
done

case "$(cargo -vV | sed -n 's/^host: \(.*\)$/\1/p')" in
	*-windows-msvc)
		LIBNAME=gitcore.lib
		PATH="$(echo "$PATH" | tr ':' '\n' | grep -Ev "^(/mingw64/bin|/usr/bin)$" | paste -sd: -):/mingw64/bin:/usr/bin"
		export PATH
		;;
	*-windows-*)
		LIBNAME=gitcore.lib;;
	*)
		LIBNAME=libgitcore.a;;
esac

cargo build --lib --quiet --manifest-path="$SOURCE_DIR/Cargo.toml" --target-dir="$BUILD_DIR" "$@"
RET=$?
if test $RET -ne 0
then
	exit $RET
fi

if ! cmp "$BUILD_DIR/$BUILD_TYPE/$LIBNAME" "$BUILD_DIR/libgitcore.a" >/dev/null 2>&1
then
	cp "$BUILD_DIR/$BUILD_TYPE/$LIBNAME" "$BUILD_DIR/libgitcore.a"
fi
