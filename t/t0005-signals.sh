#!/bin/sh

test_description='signals work as we expect'
. ./test-lib.sh

cat >expect <<EOF
three
two
one
EOF

test_expect_success 'sigchain works' '
	{ test-sigchain >actual; ret=$?; } &&
	case "$ret" in
	143) true ;; # POSIX w/ SIGTERM=15
	271) true ;; # ksh w/ SIGTERM=15
	  3) true ;; # Windows
	  *) false ;;
	esac &&
	test_cmp expect actual
'

test_expect_success !MINGW 'signals are propagated using shell convention' '
	# we use exec here to avoid any sub-shell interpretation
	# of the exit code
	git config alias.sigterm "!exec test-sigchain" &&
	test_expect_code 143 git sigterm
'

large_git () {
	for i in $(test_seq 1 100)
	do
		git diff --cached --binary || return
	done
}

test_expect_success 'create blob' '
	test-genrandom foo 16384 >file &&
	git add file
'

test_expect_success !MINGW 'a constipated git dies with SIGPIPE' '
	OUT=$( ((large_git; echo $? 1>&3) | :) 3>&1 ) &&
	test "$OUT" -eq 141
'

test_expect_success !MINGW 'a constipated git dies with SIGPIPE even if parent ignores it' '
	OUT=$( ((trap "" PIPE; large_git; echo $? 1>&3) | :) 3>&1 ) &&
	test "$OUT" -eq 141
'

test_done
