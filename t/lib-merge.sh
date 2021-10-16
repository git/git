# Helper functions used by merge tests.

test_expect_merge_algorithm () {
	status_for_recursive=$1 status_for_ort=$2
	shift 2

	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_expect_${status_for_ort} "$@"
	else
		test_expect_${status_for_recursive} "$@"
	fi
}
