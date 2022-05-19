#!/bin/sh

test_description='but bugreport'

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
	test_when_finished rm but-bugreport-check-headers.txt &&
	but bugreport -s check-headers &&
	check_all_headers_populated <but-bugreport-check-headers.txt
'

test_expect_success 'dies if file with same name as report already exists' '
	test_when_finished rm but-bugreport-duplicate.txt &&
	>>but-bugreport-duplicate.txt &&
	test_must_fail but bugreport --suffix duplicate
'

test_expect_success '--output-directory puts the report in the provided dir' '
	test_when_finished rm -fr foo/ &&
	but bugreport -o foo/ &&
	test_path_is_file foo/but-bugreport-*
'

test_expect_success 'incorrect arguments abort with usage' '
	test_must_fail but bugreport --false 2>output &&
	test_i18ngrep usage output &&
	test_path_is_missing but-bugreport-*
'

test_expect_success 'runs outside of a but dir' '
	test_when_finished rm non-repo/but-bugreport-* &&
	nonbut but bugreport
'

test_expect_success 'can create leading directories outside of a but dir' '
	test_when_finished rm -fr foo/bar/baz &&
	nonbut but bugreport -o foo/bar/baz
'

test_expect_success 'indicates populated hooks' '
	test_when_finished rm but-bugreport-hooks.txt &&

	test_hook applypatch-msg <<-\EOF &&
	true
	EOF
	test_hook unknown-hook <<-\EOF &&
	true
	EOF
	but bugreport -s hooks &&

	sort >expect <<-\EOF &&
	[Enabled Hooks]
	applypatch-msg
	EOF

	sed -ne "/^\[Enabled Hooks\]$/,/^$/p" <but-bugreport-hooks.txt >actual &&
	test_cmp expect actual
'

test_done
