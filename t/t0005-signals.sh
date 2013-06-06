#!/bin/sh

test_description='signals work as we expect'
. ./test-lib.sh

cat >expect <<EOF
three
two
one
EOF

test_expect_success 'sigchain works' '
	test-sigchain >actual
	case "$?" in
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

test_done
