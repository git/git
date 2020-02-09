#!/bin/sh

test_description='git bugreport'

. ./test-lib.sh

# Headers "[System Info]" will be followed by a non-empty line if we put some
# information there; we can make sure all our headers were followed by some
# information to check if the command was successful.
HEADER_PATTERN="^\[.*\]$"
check_all_headers_populated() {
	while read -r line; do
		if test "$(grep "$HEADER_PATTERN" "$line")"; then
			echo "$line"
			read -r nextline
			if test -z "$nextline"; then
				return 1;
			fi
		fi
	done
}

test_expect_success 'creates a report with content in the right places' '
	git bugreport &&
	REPORT="$(ls git-bugreport-*)" &&
	check_all_headers_populated <$REPORT &&
	rm $REPORT
'

test_expect_success 'dies if file with same name as report already exists' '
	touch git-bugreport-duplicate.txt &&
	test_must_fail git bugreport --suffix duplicate &&
	rm git-bugreport-duplicate.txt
'

test_expect_success '--output-directory puts the report in the provided dir' '
	mkdir foo/ &&
	git bugreport -o foo/ &&
	test_path_is_file foo/git-bugreport-* &&
	rm -fr foo/
'

test_expect_success 'incorrect arguments abort with usage' '
	test_must_fail git bugreport --false 2>output &&
	grep usage output &&
	test_path_is_missing git-bugreport-*
'

test_done
