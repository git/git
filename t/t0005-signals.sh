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

test_done
