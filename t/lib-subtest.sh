write_sub_test_lib_test () {
	name="$1" # stdin is the body of the test code
	mkdir "$name" &&
	write_script "$name/$name.sh" "$TEST_SHELL_PATH" <<-EOF &&
	test_description='A test of test-lib.sh itself'

	# Point to the t/test-lib.sh, which isn't in ../ as usual
	. "\$TEST_DIRECTORY"/test-lib.sh
	EOF
	cat >>"$name/$name.sh"
}

_run_sub_test_lib_test_common () {
	cmp_op="$1" want_code="$2" name="$3" # stdin is the body of the test code
	shift 3

	# intercept pseudo-options at the front of the argument list that we
	# will not pass to child script
	skip=
	while test $# -gt 0
	do
		case "$1" in
		--skip=*)
			skip=${1#--*=}
			shift
			;;
		*)
			break
			;;
		esac
	done

	(
		cd "$name" &&

		# Pretend we're not running under a test harness, whether we
		# are or not. The test-lib output depends on the setting of
		# this variable, so we need a stable setting under which to run
		# the sub-test.
		sane_unset HARNESS_ACTIVE &&

		export TEST_DIRECTORY &&
		# The child test re-sources GIT-BUILD-OPTIONS and may thus
		# override the test output directory. We thus pass it as an
		# explicit override to the child.
		TEST_OUTPUT_DIRECTORY_OVERRIDE=$(pwd) &&
		export TEST_OUTPUT_DIRECTORY_OVERRIDE &&
		GIT_SKIP_TESTS=$skip &&
		export GIT_SKIP_TESTS &&
		sane_unset GIT_TEST_FAIL_PREREQS &&
		./"$name.sh" "$@" >out 2>err;
		ret=$? &&
		test "$ret" "$cmp_op" "$want_code"
	)
}

write_and_run_sub_test_lib_test () {
	name="$1" descr="$2" # stdin is the body of the test code
	write_sub_test_lib_test "$@" || return 1
	_run_sub_test_lib_test_common -eq 0 "$@"
}

write_and_run_sub_test_lib_test_err () {
	name="$1" descr="$2" # stdin is the body of the test code
	write_sub_test_lib_test "$@" || return 1
	_run_sub_test_lib_test_common -eq 1 "$@"
}

run_sub_test_lib_test () {
	_run_sub_test_lib_test_common -eq 0 "$@"
}

run_sub_test_lib_test_err () {
	_run_sub_test_lib_test_common -eq 1 "$@"
}

_check_sub_test_lib_test_common () {
	name="$1" &&
	sed -e 's/^> //' -e 's/Z$//' >"$name"/expect.out &&
	test_cmp "$name"/expect.out "$name"/out
}

check_sub_test_lib_test () {
	name="$1" # stdin is the expected output from the test
	_check_sub_test_lib_test_common "$name" &&
	test_must_be_empty "$name"/err
}

check_sub_test_lib_test_err () {
	name="$1" # stdin is the expected output from the test
	_check_sub_test_lib_test_common "$name" &&
	# expected error output is in descriptor 3
	sed -e 's/^> //' -e 's/Z$//' <&3 >"$name"/expect.err &&
	test_cmp "$name"/expect.err "$name"/err
}
