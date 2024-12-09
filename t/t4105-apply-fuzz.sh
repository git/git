#!/bin/sh

test_description='apply with fuzz and offset'


. ./test-lib.sh

dotest () {
	name="$1" && shift &&
	test_expect_success "$name" "
		git checkout-index -f -q -u file &&
		git apply $* &&
		test_cmp expect file
	"
}

test_expect_success setup '

	test_write_lines 1 2 3 4 5 6 7 8 9 10 11 12 >file &&
	git update-index --add file &&
	test_write_lines 1 2 3 4 5 6 7 a b c d e 8 9 10 11 12 >file &&
	cat file >expect &&
	git diff >O0.diff &&

	sed -e "s/@@ -5,6 +5,11 @@/@@ -2,6 +2,11 @@/" >O1.diff O0.diff &&
	sed -e "s/@@ -5,6 +5,11 @@/@@ -7,6 +7,11 @@/" >O2.diff O0.diff &&
	sed -e "s/@@ -5,6 +5,11 @@/@@ -19,6 +19,11 @@/" >O3.diff O0.diff &&

	sed -e "s/^ 5/ S/" >F0.diff O0.diff &&
	sed -e "s/^ 5/ S/" >F1.diff O1.diff &&
	sed -e "s/^ 5/ S/" >F2.diff O2.diff &&
	sed -e "s/^ 5/ S/" >F3.diff O3.diff

'

dotest 'unmodified patch' O0.diff

dotest 'minus offset' O1.diff

dotest 'plus offset' O2.diff

dotest 'big offset' O3.diff

dotest 'fuzz with no offset' -C2 F0.diff

dotest 'fuzz with minus offset' -C2 F1.diff

dotest 'fuzz with plus offset' -C2 F2.diff

dotest 'fuzz with big offset' -C2 F3.diff

test_done
