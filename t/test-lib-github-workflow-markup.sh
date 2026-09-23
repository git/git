# Library of functions to mark up test scripts' output suitable for
# pretty-printing it in GitHub workflows.
#
# Copyright (c) 2022 Johannes Schindelin
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see https://www.gnu.org/licenses/ .
#
# The idea is for `test-lib.sh` to source this file when run in GitHub
# workflows; these functions will then override (empty) functions
# that are called at the appropriate times during the test runs.

test_skip_test_preamble=t

start_test_output () {
	test -n "$GIT_TEST_TEE_OUTPUT_FILE" ||
	die "--github-workflow-markup requires --verbose-log"
	github_markup_output="${GIT_TEST_TEE_OUTPUT_FILE%.out}.markup"
	>$github_markup_output
	GIT_TEST_TEE_OFFSET=0
}

# No need to override start_test_case_output

# Escapes a chunk of text (read from stdin) into a single line suitable
# for use as a GitHub Actions workflow command's message, i.e. the part
# after the last "::" in "::error file=a,line=1::<here>".
github_escape_message_ () {
	sed -e ':a' -e 'N' -e '$!ba' -e 's/%/%25/g' -e 's/\r/%0D/g' -e 's/\n/%0A/g'
}

finalize_test_case_output () {
	test_case_result=$1
	shift

	case "$test_case_result" in
	ok|broken)
		# Exit without printing the "ok" or "broken" tests
		return
		;;
	esac

	test_script_name=${0##*/}
	test_case_line=$(grep -n -F -- "$1" "$TEST_DIRECTORY/$test_script_name" |
		head -n 1 | cut -d: -f1)
	test_case_output=$(test-tool path-utils skip-n-bytes \
		"$GIT_TEST_TEE_OUTPUT_FILE" $GIT_TEST_TEE_OFFSET)

	case "$test_case_result" in
	failure)
		test_case_summary=$(printf '%s\n' "$test_case_output" |
			tail -n 20 | github_escape_message_)
		echo >>$github_markup_output "::error file=t/$test_script_name,line=${test_case_line:-1}::failed: $this_test.$test_count $1%0A%0A$test_case_summary"
		;;
	fixed)
		echo >>$github_markup_output "::notice file=t/$test_script_name,line=${test_case_line:-1}::fixed: $this_test.$test_count $1"
		;;
	esac

	echo >>$github_markup_output "::group::$test_case_result: $this_test.$test_count $*"
	printf '%s\n' "$test_case_output" >>$github_markup_output
	echo >>$github_markup_output "::endgroup::"
}

finalize_test_leak_output () {
	test_script_name=${0##*/}
	test_leak_summary=$(head -n 40 "$TEST_RESULTS_SAN_FILE".* 2>/dev/null |
		github_escape_message_)
	echo >>$github_markup_output "::error file=t/$test_script_name,line=1::memory leak logged around $this_test.$test_count%0A%0A$test_leak_summary"
}

# No need to override finalize_test_output
