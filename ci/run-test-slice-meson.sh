#!/bin/sh

# We must load the build options so we know where to find
# things like TEST_OUTPUT_DIRECTORY. This has to come before
# loading lib.sh, though, because it may clobber some CI lib
# variables like our custom GIT_TEST_OPTS.
. "$1"/GIT-BUILD-OPTIONS
. ${0%/*}/lib.sh

group "Run tests" \
	meson test -C "$1" --no-rebuild --print-errorlogs \
		--test-args="$GIT_TEST_OPTS" --slice "$(($2))/$3" ||
handle_failed_tests
