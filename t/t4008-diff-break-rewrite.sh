#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Break and then rename

We have two very different files, file0 and file1, registered in a tree.

We update file1 so drastically that it is more similar to file0, and
then remove file0.  With -B, changes to file1 should be broken into
separate delete and create, resulting in removal of file0, removal of
original file1 and creation of completely rewritten file1.  The latter
two are then merged back into a single "complete rewrite".

Further, with -B and -M together, these three modifications should
turn into rename-edit of file0 into file1.

Starting from the same two files in the tree, we swap file0 and file1.
With -B, this should be detected as two complete rewrites.

Further, with -B and -M together, these should turn into two renames.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh ;# test-lib chdir's into trash

test_expect_success setup '
	echo some dissimilar content >file0 &&
	COPYING_test_data >file1 &&
	blob0_id=$(but hash-object file0) &&
	blob1_id=$(but hash-object file1) &&
	but update-index --add file0 file1 &&
	but tag reference $(but write-tree)
'

test_expect_success 'change file1 with copy-edit of file0 and remove file0' '
	sed -e "s/but/BUT/" file0 >file1 &&
	blob2_id=$(but hash-object file1) &&
	rm -f file0 &&
	but update-index --remove file0 file1
'

test_expect_success 'run diff with -B (#1)' '
	but diff-index -B --cached reference >current &&
	cat >expect <<-EOF &&
	:100644 000000 $blob0_id $ZERO_OID D	file0
	:100644 100644 $blob1_id $blob2_id M100	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'run diff with -B and -M (#2)' '
	but diff-index -B -M reference >current &&
	cat >expect <<-EOF &&
	:100644 100644 $blob0_id $blob2_id R100	file0	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'swap file0 and file1' '
	rm -f file0 file1 &&
	but read-tree -m reference &&
	but checkout-index -f -u -a &&
	mv file0 tmp &&
	mv file1 file0 &&
	mv tmp file1 &&
	but update-index file0 file1
'

test_expect_success 'run diff with -B (#3)' '
	but diff-index -B reference >current &&
	cat >expect <<-EOF &&
	:100644 100644 $blob0_id $blob1_id M100	file0
	:100644 100644 $blob1_id $blob0_id M100	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'run diff with -B and -M (#4)' '
	but diff-index -B -M reference >current &&
	cat >expect <<-EOF &&
	:100644 100644 $blob1_id $blob1_id R100	file1	file0
	:100644 100644 $blob0_id $blob0_id R100	file0	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'make file0 into something completely different' '
	rm -f file0 &&
	test_ln_s_add frotz file0 &&
	slink_id=$(printf frotz | but hash-object --stdin) &&
	but update-index file1
'

test_expect_success 'run diff with -B (#5)' '
	but diff-index -B reference >current &&
	cat >expect <<-EOF &&
	:100644 120000 $blob0_id $slink_id T	file0
	:100644 100644 $blob1_id $blob0_id M100	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'run diff with -B -M (#6)' '
	but diff-index -B -M reference >current &&

	# file0 changed from regular to symlink.  file1 is the same as the preimage
	# of file0.  Because the change does not make file0 disappear, file1 is
	# denoted as a copy of file0
	cat >expect <<-EOF &&
	:100644 120000 $blob0_id $slink_id T	file0
	:100644 100644 $blob0_id $blob0_id C	file0	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'run diff with -M (#7)' '
	but diff-index -M reference >current &&

	# This should not mistake file0 as the copy source of new file1
	# due to type differences.
	cat >expect <<-EOF &&
	:100644 120000 $blob0_id $slink_id T	file0
	:100644 100644 $blob1_id $blob0_id M	file1
	EOF
	compare_diff_raw expect current
'

test_expect_success 'file1 edited to look like file0 and file0 rename-edited to file2' '
	rm -f file0 file1 &&
	but read-tree -m reference &&
	but checkout-index -f -u -a &&
	sed -e "s/but/BUT/" file0 >file1 &&
	sed -e "s/but/GET/" file0 >file2 &&
	blob3_id=$(but hash-object file2) &&
	rm -f file0 &&
	but update-index --add --remove file0 file1 file2
'

test_expect_success 'run diff with -B (#8)' '
	but diff-index -B reference >current &&
	cat >expect <<-EOF &&
	:100644 000000 $blob0_id $ZERO_OID D	file0
	:100644 100644 $blob1_id $blob2_id M100	file1
	:000000 100644 $ZERO_OID $blob3_id A	file2
	EOF
	compare_diff_raw expect current
'

test_expect_success 'run diff with -B -C (#9)' '
	but diff-index -B -C reference >current &&
	cat >expect <<-EOF &&
	:100644 100644 $blob0_id $blob2_id C095	file0	file1
	:100644 100644 $blob0_id $blob3_id R095	file0	file2
	EOF
	compare_diff_raw expect current
'

test_done
