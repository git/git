# Helpers for tests invoking parallel-checkout

set_checkout_config () {
	if test $# -ne 2
	then
		BUG "usage: set_checkout_config <workers> <threshold>"
	fi &&

	test_config_global checkout.workers $1 &&
	test_config_global checkout.thresholdForParallelism $2
}

# Run "${@:2}" and check that $1 checkout workers were used
test_checkout_workers () {
	if test $# -lt 2
	then
		BUG "too few arguments to test_checkout_workers"
	fi &&

	local expected_workers=$1 &&
	shift &&

	local trace_file=trace-test-checkout-workers &&
	rm -f "$trace_file" &&
	GIT_TRACE2="$(pwd)/$trace_file" "$@" &&

	local workers=$(grep "child_start\[..*\] git checkout--worker" "$trace_file" | wc -l) &&
	test $workers -eq $expected_workers &&
	rm "$trace_file"
}

# Verify that both the working tree and the index were created correctly
verify_checkout () {
	if test $# -ne 1
	then
		BUG "usage: verify_checkout <repository path>"
	fi &&

	git -C "$1" diff-index --ignore-submodules=none --exit-code HEAD -- &&
	git -C "$1" status --porcelain >"$1".status &&
	test_must_be_empty "$1".status
}
