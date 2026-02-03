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

if test_have_prereq WITH_BREAKING_CHANGES
then
	TURN_ON_SANITIZING=already.turned=on
else
	TURN_ON_SANITIZING=sideband.allowControlCharacters=color
fi

test_expect_success 'disallow (color) control sequences in sideband' '
	write_script .git/color-me-surprised <<-\EOF &&
	printf "error: Have you \\033[31mread\\033[m this?\\a\\n" >&2
	exec "$@"
	EOF
	test_config_global uploadPack.packObjectsHook ./color-me-surprised &&
	test_commit need-at-least-one-commit &&

	git -c $TURN_ON_SANITIZING clone --no-local . throw-away 2>stderr &&
	test_decode_color <stderr >decoded &&
	test_grep RED decoded &&
	test_grep "\\^G" stderr &&
	tr -dc "\\007" <stderr >actual &&
	test_must_be_empty actual &&

	rm -rf throw-away &&
	git -c sideband.allowControlCharacters=false \
		clone --no-local . throw-away 2>stderr &&
	test_decode_color <stderr >decoded &&
	test_grep ! RED decoded &&
	test_grep "\\^G" stderr &&

	rm -rf throw-away &&
	git -c sideband.allowControlCharacters clone --no-local . throw-away 2>stderr &&
	test_decode_color <stderr >decoded &&
	test_grep RED decoded &&
	tr -dc "\\007" <stderr >actual &&
	test_file_not_empty actual
'

test_decode_csi() {
	awk '{
		while (match($0, /\033/) != 0) {
			printf "%sCSI ", substr($0, 1, RSTART-1);
			$0 = substr($0, RSTART + RLENGTH, length($0) - RSTART - RLENGTH + 1);
		}
		print
	}'
}

test_expect_success 'control sequences in sideband allowed by default (in Git v3.8)' '
	write_script .git/color-me-surprised <<-\EOF &&
	printf "error: \\033[31mcolor\\033[m\\033[Goverwrite\\033[Gerase\\033[K\\033?25l\\n" >&2
	exec "$@"
	EOF
	test_config_global uploadPack.packObjectsHook ./color-me-surprised &&
	test_commit need-at-least-one-commit-at-least &&

	rm -rf throw-away &&
	git -c $TURN_ON_SANITIZING clone --no-local . throw-away 2>stderr &&
	test_decode_color <stderr >color-decoded &&
	test_decode_csi <color-decoded >decoded &&
	test_grep ! "CSI \\[K" decoded &&
	test_grep ! "CSI \\[G" decoded &&
	test_grep "\\^\\[?25l" decoded &&

	rm -rf throw-away &&
	git -c sideband.allowControlCharacters=erase,cursor,color \
		clone --no-local . throw-away 2>stderr &&
	test_decode_color <stderr >color-decoded &&
	test_decode_csi <color-decoded >decoded &&
	test_grep "RED" decoded &&
	test_grep "CSI \\[K" decoded &&
	test_grep "CSI \\[G" decoded &&
	test_grep ! "\\^\\[\\[K" decoded &&
	test_grep ! "\\^\\[\\[G" decoded
'

test_expect_success 'allow all control sequences for a specific URL' '
	write_script .git/eraser <<-\EOF &&
	printf "error: Ohai!\\r\\033[K" >&2
	exec "$@"
	EOF
	test_config_global uploadPack.packObjectsHook ./eraser &&
	test_commit one-more-please &&

	rm -rf throw-away &&
	git -c $TURN_ON_SANITIZING clone --no-local . throw-away 2>stderr &&
	test_decode_color <stderr >color-decoded &&
	test_decode_csi <color-decoded >decoded &&
	test_grep ! "CSI \\[K" decoded &&
	test_grep "\\^\\[\\[K" decoded &&

	rm -rf throw-away &&
	git -c sideband.allowControlCharacters=false \
		-c "sideband.file://.allowControlCharacters=true" \
		clone --no-local "file://$PWD" throw-away 2>stderr &&
	test_decode_color <stderr >color-decoded &&
	test_decode_csi <color-decoded >decoded &&
	test_grep "CSI \\[K" decoded &&
	test_grep ! "\\^\\[\\[K" decoded
'

test_done
