#!/bin/sh

test_description='signals work as we expect'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

cat >expect <<EOF
three
two
one
EOF

test_expect_success 'sigchain works' '
	{ test-tool sigchain >actual; ret=$?; } &&
	{
		# Signal death by raise() on Windows acts like exit(3),
		# regardless of the signal number. So we must allow that
		# as well as the normal signal check.
		test_match_signal 15 "$ret" ||
		test "$ret" = 3
	} &&
	test_cmp expect actual
'

test_expect_success !MINGW 'signals are propagated using shell convention' '
	# we use exec here to avoid any sub-shell interpretation
	# of the exit code
	but config alias.sigterm "!exec test-tool sigchain" &&
	test_expect_code 143 but sigterm
'

large_but () {
	for i in $(test_seq 1 100)
	do
		but diff --cached --binary || return
	done
}

test_expect_success 'create blob' '
	test-tool genrandom foo 16384 >file &&
	but add file
'

test_expect_success !MINGW 'a constipated but dies with SIGPIPE' '
	OUT=$( ((large_but; echo $? 1>&3) | :) 3>&1 ) &&
	test_match_signal 13 "$OUT"
'

test_expect_success !MINGW 'a constipated but dies with SIGPIPE even if parent ignores it' '
	OUT=$( ((trap "" PIPE && large_but; echo $? 1>&3) | :) 3>&1 ) &&
	test_match_signal 13 "$OUT"
'

test_done
