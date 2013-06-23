#!/bin/sh

base=$(basename "$0")

TOOL_OPTIONS='--leak-check=no'

test -z "$GIT_VALGRIND_ENABLED" &&
exec "$GIT_VALGRIND"/../../"$base" "$@"

case "$GIT_VALGRIND_MODE" in
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
	TOOL_OPTIONS="--tool=$GIT_VALGRIND_MODE"
esac

exec valgrind -q --error-exitcode=126 \
	--gen-suppressions=all \
	--suppressions="$GIT_VALGRIND/default.supp" \
	$TOOL_OPTIONS \
	--log-fd=4 \
	--input-fd=4 \
	$GIT_VALGRIND_OPTIONS \
	"$GIT_VALGRIND"/../../"$base" "$@"
