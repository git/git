#!/bin/sh
#
# Probe the compiler for vintage, version, etc. This is used for setting
# optional make knobs under the DEVELOPER knob.

CC="$*"

# we get something like (this is at least true for gcc and clang)
#
# FreeBSD clang version 3.4.1 (tags/RELEASE...)
get_version_line() {
	LANG=C LC_ALL=C $CC -v 2>&1 | grep ' version '
}

get_family() {
	get_version_line | sed 's/^\(.*\) version [0-9].*/\1/'
}

get_version() {
	# A string that begins with a digit up to the next SP
	ver=$(get_version_line | sed 's/^.* version \([0-9][^ ]*\).*/\1/')

	# There are known -variant suffixes that do not affect the
	# meaning of the main version number.  Strip them.
	ver=${ver%-win32}
	ver=${ver%-posix}

	echo "$ver"
}

print_flags() {
	family=$1
	version=$(get_version | cut -f 1 -d .)

	# Print a feature flag not only for the current version, but also
	# for any prior versions we encompass. This avoids needing to do
	# numeric comparisons in make, which are awkward.
	while test "$version" -gt 0
	do
		echo $family$version
		version=$((version - 1))
	done
}

case "$(get_family)" in
gcc)
	print_flags gcc
	;;
clang | *" clang")
	print_flags clang
	;;
"Apple LLVM")
	print_flags clang
	;;
*)
	: unknown compiler family
	;;
esac
