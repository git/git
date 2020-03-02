#!/bin/sh

test_description='git bugreport'

. ./test-lib.sh

# Headers "[System Info]" will be followed by a non-empty line if we put some
# information there; we can make sure all our headers were followed by some
# information to check if the command was successful.
HEADER_PATTERN="^\[.*\]$"

check_all_headers_populated () {
	while read -r line
	do
		if test "$(grep "$HEADER_PATTERN" "$line")"
		then
			echo "$line"
			read -r nextline
			if test -z "$nextline"; then
				return 1;
			fi
		fi
	done
}

test_expect_success 'creates a report with content in the right places' '
	git bugreport -s check-headers &&
	check_all_headers_populated <git-bugreport-check-headers.txt &&
	test_when_finished rm git-bugreport-check-headers.txt
'

test_expect_success 'dies if file with same name as report already exists' '
	>>git-bugreport-duplicate.txt &&
	test_must_fail git bugreport --suffix duplicate &&
	test_when_finished rm git-bugreport-duplicate.txt
'

test_expect_success '--output-directory puts the report in the provided dir' '
	git bugreport -o foo/ &&
	test_path_is_file foo/git-bugreport-* &&
	test_when_finished rm -fr foo/
'

test_expect_success 'incorrect arguments abort with usage' '
	test_must_fail git bugreport --false 2>output &&
	test_i18ngrep usage output &&
	test_path_is_missing git-bugreport-*
'

test_expect_success 'runs outside of a git dir' '
	nongit git bugreport &&
	test_when_finished rm non-repo/git-bugreport-*
'

test_expect_success 'can create leading directories outside of a git dir' '
	nongit git bugreport -o foo/bar/baz &&
	test_when_finished rm -fr foo/bar/baz
'


test_done
