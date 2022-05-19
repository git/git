#!/bin/sh

base=$(basename "$0")
case "$base" in
test-*)
	program="$BUT_VALGRIND/../../t/helper/$base"
	;;
*)
	program="$BUT_VALGRIND/../../$base"
	;;
esac

TOOL_OPTIONS='--leak-check=no'

test -z "$BUT_VALGRIND_ENABLED" &&
exec "$program" "$@"

case "$BUT_VALGRIND_MODE" in
memcheck-fast)
	;;
memcheck)
	VALGRIND_VERSION=$(valgrind --version)
	VALGRIND_MAJOR=$(expr "$VALGRIND_VERSION" : '[^0-9]*\([0-9]*\)')
	VALGRIND_MINOR=$(expr "$VALGRIND_VERSION" : '[^0-9]*[0-9]*\.\([0-9]*\)')
	test 3 -gt "$VALGRIND_MAJOR" ||
	test 3 -eq "$VALGRIND_MAJOR" -a 4 -gt "$VALGRIND_MINOR" ||
	TOOL_OPTIONS="$TOOL_OPTIONS --track-origins=yes"
	;;
*)
	TOOL_OPTIONS="--tool=$BUT_VALGRIND_MODE"
esac

exec valgrind -q --error-exitcode=126 \
	--gen-suppressions=all \
	--suppressions="$BUT_VALGRIND/default.supp" \
	$TOOL_OPTIONS \
	--log-fd=4 \
	--input-fd=4 \
	$BUT_VALGRIND_OPTIONS \
	"$program" "$@"
