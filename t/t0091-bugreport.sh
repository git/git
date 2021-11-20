#!/bin/sh

test_description='git bugreport'

TEST_PASSES_SANITIZE_LEAK=true
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
	test_when_finished rm git-bugreport-check-headers.txt &&
	git bugreport -s check-headers &&
	check_all_headers_populated <git-bugreport-check-headers.txt
'

test_expect_success 'dies if file with same name as report already exists' '
	test_when_finished rm git-bugreport-duplicate.txt &&
	>>git-bugreport-duplicate.txt &&
	test_must_fail git bugreport --suffix duplicate
'

test_expect_success '--output-directory puts the report in the provided dir' '
	test_when_finished rm -fr foo/ &&
	git bugreport -o foo/ &&
	test_path_is_file foo/git-bugreport-*
'

test_expect_success 'incorrect arguments abort with usage' '
	test_must_fail git bugreport --false 2>output &&
	test_i18ngrep usage output &&
	test_path_is_missing git-bugreport-*
'

test_expect_success 'runs outside of a git dir' '
	test_when_finished rm non-repo/git-bugreport-* &&
	nongit git bugreport
'

test_expect_success 'can create leading directories outside of a git dir' '
	test_when_finished rm -fr foo/bar/baz &&
	nongit git bugreport -o foo/bar/baz
'

test_expect_success 'indicates populated hooks' '
	test_when_finished rm git-bugreport-hooks.txt &&
	test_when_finished rm -fr .git/hooks &&
	rm -fr .git/hooks &&
	mkdir .git/hooks &&
	for hook in applypatch-msg prepare-commit-msg.sample
	do
		write_script ".git/hooks/$hook" <<-EOF || return 1
		echo "hook $hook exists"
		EOF
	done &&
	git bugreport -s hooks &&
	grep applypatch-msg git-bugreport-hooks.txt &&
	! grep prepare-commit-msg git-bugreport-hooks.txt
'

test_done
