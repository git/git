#!/bin/sh

test_description='remote messages are colorized on the client'

. ./test-lib.sh

test_expect_success 'setup' '
	test_hook --setup update <<-\EOF &&
	echo error: error
	echo ERROR: also highlighted
	echo hint: hint
	echo hinting: not highlighted
	echo success: success
	echo warning: warning
	echo prefixerror: error
	echo " " "error: leading space"
	echo "    "
	echo Err
	echo SUCCESS
	exit 0
	EOF
	echo 1 >file &&
	git add file &&
	git commit -m 1 &&
	git clone . child &&
	(
		cd child &&
		test_commit message2 file content2
	)
'

test_expect_success 'keywords' '
	git --git-dir child/.git -c color.remote=always push -f origin HEAD:refs/heads/keywords 2>output &&
	test_decode_color <output >decoded &&
	grep "<BOLD;RED>error<RESET>: error" decoded &&
	grep "<YELLOW>hint<RESET>:" decoded &&
	grep "<BOLD;GREEN>success<RESET>:" decoded &&
	grep "<BOLD;GREEN>SUCCESS<RESET>" decoded &&
	grep "<BOLD;YELLOW>warning<RESET>:" decoded
'

test_expect_success 'whole words at line start' '
	git --git-dir child/.git -c color.remote=always push -f origin HEAD:refs/heads/whole-words 2>output &&
	test_decode_color <output >decoded &&
	grep "<YELLOW>hint<RESET>:" decoded &&
	grep "hinting: not highlighted" decoded &&
	grep "prefixerror: error" decoded
'

test_expect_success 'short line' '
	git -C child -c color.remote=always push -f origin HEAD:short-line 2>output &&
	test_decode_color <output >decoded &&
	grep "remote: Err" decoded
'

test_expect_success 'case-insensitive' '
	git --git-dir child/.git -c color.remote=always push -f origin HEAD:refs/heads/case-insensitive 2>output &&
	test_decode_color <output >decoded &&
	grep "<BOLD;RED>error<RESET>: error" decoded &&
	grep "<BOLD;RED>ERROR<RESET>: also highlighted" decoded
'

test_expect_success 'leading space' '
	git --git-dir child/.git -c color.remote=always push -f origin HEAD:refs/heads/leading-space 2>output &&
	test_decode_color <output >decoded &&
	grep "  <BOLD;RED>error<RESET>: leading space" decoded
'

test_expect_success 'spaces only' '
	git -C child -c color.remote=always push -f origin HEAD:only-space 2>output &&
	test_decode_color <output >decoded &&
	grep "remote:     " decoded
'

test_expect_success 'no coloring for redirected output' '
	git --git-dir child/.git push -f origin HEAD:refs/heads/redirected-output 2>output &&
	test_decode_color <output >decoded &&
	grep "error: error" decoded
'

test_expect_success 'push with customized color' '
	git --git-dir child/.git -c color.remote=always -c color.remote.error=blue push -f origin HEAD:refs/heads/customized-color 2>output &&
	test_decode_color <output >decoded &&
	grep "<BLUE>error<RESET>:" decoded &&
	grep "<BOLD;GREEN>success<RESET>:" decoded
'


test_expect_success 'error in customized color' '
	git --git-dir child/.git -c color.remote=always -c color.remote.error=i-am-not-a-color push -f origin HEAD:refs/heads/error-customized-color 2>output &&
	test_decode_color <output >decoded &&
	grep "<BOLD;GREEN>success<RESET>:" decoded
'

test_expect_success 'fallback to color.ui' '
	git --git-dir child/.git -c color.ui=always push -f origin HEAD:refs/heads/fallback-color-ui 2>output &&
	test_decode_color <output >decoded &&
	grep "<BOLD;RED>error<RESET>: error" decoded
'

test_done
