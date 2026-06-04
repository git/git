# Library of functions to format test scripts' output in JUnit XML
# format, to support Git's test suite result to be presented in an
# easily digestible way on Azure Pipelines.
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
# The idea is for `test-lib.sh` to source this file when the user asks
# for JUnit XML; these functions will then override (empty) functions
# that are are called at the appropriate times during the test runs.

start_test_output () {
	junit_xml_dir="$TEST_OUTPUT_DIRECTORY/out"
	mkdir -p "$junit_xml_dir"
	junit_xml_base=${1##*/}
	junit_xml_path="$junit_xml_dir/TEST-${junit_xml_base%.sh}.xml"
	junit_attrs="name=\"${junit_xml_base%.sh}\""
	junit_attrs="$junit_attrs timestamp=\"$(TZ=UTC \
		date +%Y-%m-%dT%H:%M:%S)\""
	write_junit_xml --truncate "<testsuites>" "  <testsuite $junit_attrs>"
	junit_suite_start=$(test-tool date getnanos)
	if test -n "$GIT_TEST_TEE_OUTPUT_FILE"
	then
		GIT_TEST_TEE_OFFSET=0
	fi
}

start_test_case_output () {
	junit_start=$(test-tool date getnanos)
}

finalize_test_case_output () {
	test_case_result=$1
	shift
	case "$test_case_result" in
	ok)
		set -- "$*"
		;;
	failure)
		junit_insert="<failure message=\"not ok $test_count -"
		junit_insert="$junit_insert $(xml_attr_encode --no-lf "$1")\">"
		junit_insert="$junit_insert $(xml_attr_encode \
			"$(if test -n "$GIT_TEST_TEE_OUTPUT_FILE"
			   then
				test-tool path-utils skip-n-bytes \
					"$GIT_TEST_TEE_OUTPUT_FILE" $GIT_TEST_TEE_OFFSET
			   else
				printf '%s\n' "$@" | sed 1d
			   fi)")"
		junit_insert="$junit_insert</failure>"
		if test -n "$GIT_TEST_TEE_OUTPUT_FILE"
		then
			junit_insert="$junit_insert<system-err>$(xml_attr_encode \
				"$(cat "$GIT_TEST_TEE_OUTPUT_FILE")")</system-err>"
		fi
		set -- "$1" "      $junit_insert"
		;;
	fixed)
		set -- "$* (breakage fixed)"
		;;
	broken)
		set -- "$* (known breakage)"
		;;
	skip)
		message="$(xml_attr_encode --no-lf "$skipped_reason")"
		set -- "$1" "      <skipped message=\"$message\" />"
		;;
	esac

	junit_attrs="name=\"$(xml_attr_encode --no-lf "$this_test.$test_count $1")\""
	shift
	junit_attrs="$junit_attrs classname=\"$this_test\""
	junit_attrs="$junit_attrs time=\"$(test-tool \
		date getnanos $junit_start)\""
	write_junit_xml "$(printf '%s\n' \
		"    <testcase $junit_attrs>" "$@" "    </testcase>")"
	junit_have_testcase=t
}

finalize_test_output () {
	if test -n "$junit_xml_path"
	then
		test -n "$junit_have_testcase" || {
			junit_start=$(test-tool date getnanos)
			write_junit_xml_testcase "all tests skipped"
		}

		# adjust the overall time
		junit_time=$(test-tool date getnanos $junit_suite_start)
		sed -e "s/\(<testsuite.*\) time=\"[^\"]*\"/\1/" \
			-e "s/<testsuite [^>]*/& time=\"$junit_time\"/" \
			-e '/^ *<\/testsuite/d' \
			<"$junit_xml_path" >"$junit_xml_path.new"
		mv "$junit_xml_path.new" "$junit_xml_path"

		write_junit_xml "  </testsuite>" "</testsuites>"
		write_junit_xml=
	fi
}

write_junit_xml () {
	case "$1" in
	--truncate)
		>"$junit_xml_path"
		junit_have_testcase=
		shift
		;;
	esac
	printf '%s\n' "$@" >>"$junit_xml_path"
}

xml_attr_encode () {
	if test "x$1" = "x--no-lf"
	then
		shift
		printf '%s' "$*" | test-tool xml-encode
	else
		printf '%s\n' "$@" | test-tool xml-encode
	fi
}
