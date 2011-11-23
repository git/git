#!/bin/sh

test_description='git status with certain file name lengths'

. ./test-lib.sh

files="0 1 2 3 4 5 6 7 8 9 a b c d e f g h i j k l m n o p q r s t u v w x y z"

check() {
	len=$1
	prefix=$2

	for i in $files
	do
		: >$prefix$i
	done

	test_expect_success "status, filename length $len" "
		git add $prefix* &&
		git status
	"
	rm $prefix* .git/index
}

check  1
check  2 p
check  3 px
check  4 pre
check  5 pref
check  6 prefi
check  7 prefix
check  8 prefix-
check  9 prefix-p
check 10 prefix-pr
check 11 prefix-pre
check 12 prefix-pref
check 13 prefix-prefi
check 14 prefix-prefix
check 15 prefix-prefix-
check 16 prefix-prefix-p
check 17 prefix-prefix-pr
check 18 prefix-prefix-pre
check 19 prefix-prefix-pref
check 20 prefix-prefix-prefi
check 21 prefix-prefix-prefix
check 22 prefix-prefix-prefix-
check 23 prefix-prefix-prefix-p
check 24 prefix-prefix-prefix-pr

test_done
