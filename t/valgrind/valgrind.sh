#!/bin/sh

base=$(basename "$0")

exec valgrind -q --error-exitcode=126 \
	--leak-check=no \
	--suppressions="$GIT_VALGRIND/default.supp" \
	--gen-suppressions=all \
	--track-origins=yes \
	--log-fd=4 \
	--input-fd=4 \
	$GIT_VALGRIND_OPTIONS \
	"$GIT_VALGRIND"/../../"$base" "$@"
