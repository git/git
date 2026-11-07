#!/bin/sh
#
# Build and test Git
#

. ${0%/*}/lib.sh

case "$jobname" in
*-meson)
	group "Configure" meson setup build . \
		--fatal-meson-warnings \
		--warnlevel 2 --werror \
		--wrap-mode nofallback \
		-Dfuzzers=true \
		-Dtest_output_directory="${TEST_OUTPUT_DIRECTORY:-$(pwd)/t}" \
		$MESONFLAGS
	group "Build" meson compile -C build --
	group "Run tests" meson test -C build --print-errorlogs --test-args="$GIT_TEST_OPTS" || (
		./t/aggregate-results.sh "${TEST_OUTPUT_DIRECTORY:-t}/test-results"
		handle_failed_tests
	)
	;;
*)
	group Build make
	group "Run tests" make test ||
	handle_failed_tests
	;;
esac

check_unignored_build_artifacts
save_good_tree
