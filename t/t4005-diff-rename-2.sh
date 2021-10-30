#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Same rename detection as t4003 but testing diff-raw.'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh ;# test-lib chdir's into trash

test_expect_success 'setup reference tree' '
	COPYING_test_data >COPYING &&
	echo frotz >rezrov &&
	git update-index --add COPYING rezrov &&
	tree=$(git write-tree) &&
	echo $tree &&
	sed -e "s/HOWEVER/However/" <COPYING >COPYING.1 &&
	sed -e "s/GPL/G.P.L/g" <COPYING >COPYING.2 &&
	origoid=$(git hash-object COPYING) &&
	oid1=$(git hash-object COPYING.1) &&
	oid2=$(git hash-object COPYING.2)
'

################################################################
# tree has COPYING and rezrov.  work tree has COPYING.1 and COPYING.2,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# and COPYING.2 are based on COPYING, and do not say anything about
# rezrov.

test_expect_success 'validate output from rename/copy detection (#1)' '
	rm -f COPYING &&
	git update-index --add --remove COPYING COPYING.? &&

	cat <<-EOF >expected &&
	:100644 100644 $origoid $oid1 C1234	COPYING	COPYING.1
	:100644 100644 $origoid $oid2 R1234	COPYING	COPYING.2
	EOF
	git diff-index -C $tree >current &&
	compare_diff_raw expected current
'

################################################################
# tree has COPYING and rezrov.  work tree has COPYING and COPYING.1,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# is based on COPYING and COPYING is still there, and do not say anything
# about rezrov.

test_expect_success 'validate output from rename/copy detection (#2)' '
	mv COPYING.2 COPYING &&
	git update-index --add --remove COPYING COPYING.1 COPYING.2 &&

	cat <<-EOF >expected &&
	:100644 100644 $origoid $oid2 M	COPYING
	:100644 100644 $origoid $oid1 C1234	COPYING	COPYING.1
	EOF
	git diff-index -C $tree >current &&
	compare_diff_raw current expected
'

################################################################
# tree has COPYING and rezrov.  work tree has the same COPYING and
# copy-edited COPYING.1, and unchanged rezrov.  We should not say
# anything about rezrov or COPYING, since the revised again diff-raw
# nows how to say Copy.

test_expect_success 'validate output from rename/copy detection (#3)' '
	COPYING_test_data >COPYING &&
	git update-index --add --remove COPYING COPYING.1 &&

	cat <<-EOF >expected &&
	:100644 100644 $origoid $oid1 C1234	COPYING	COPYING.1
	EOF
	git diff-index -C --find-copies-harder $tree >current &&
	compare_diff_raw current expected
'

test_done
