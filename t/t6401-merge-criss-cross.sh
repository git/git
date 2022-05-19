#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

# See https://lore.kernel.org/but/Pine.LNX.4.44.0504271254120.4678-100000@wax.eds.org/ for a
# nice description of what this is about.


test_description='Test criss-cross merge'
. ./test-lib.sh

test_expect_success 'prepare repository' '
	test_write_lines 1 2 3 4 5 6 7 8 9 >file &&
	but add file &&
	but cummit -m "Initial cummit" file &&

	but branch A &&
	but branch B &&
	but checkout A &&

	test_write_lines 1 2 3 4 5 6 7 "8 changed in B8, branch A" 9 >file &&
	but cummit -m "B8" file &&
	but checkout B &&

	test_write_lines 1 2 "3 changed in C3, branch B" 4 5 6 7 8 9 >file &&
	but cummit -m "C3" file &&
	but branch C3 &&

	but merge -m "pre E3 merge" A &&

	test_write_lines 1 2 "3 changed in E3, branch B. New file size" 4 5 6 7 "8 changed in B8, branch A" 9 >file &&
	but cummit -m "E3" file &&

	but checkout A &&
	but merge -m "pre D8 merge" C3 &&
	test_write_lines 1 2 "3 changed in C3, branch B" 4 5 6 7 "8 changed in D8, branch A. New file size 2" 9 >file &&

	but cummit -m D8 file
'

test_expect_success 'Criss-cross merge' '
	but merge -m "final merge" B
'

test_expect_success 'Criss-cross merge result' '
	cat <<-\EOF >file-expect &&
	1
	2
	3 changed in E3, branch B. New file size
	4
	5
	6
	7
	8 changed in D8, branch A. New file size 2
	9
	EOF

	test_cmp file-expect file
'

test_expect_success 'Criss-cross merge fails (-s resolve)' '
	but reset --hard A^ &&
	test_must_fail but merge -s resolve -m "final merge" B
'

test_done
