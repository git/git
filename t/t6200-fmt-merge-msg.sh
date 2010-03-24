#!/bin/sh
#
# Copyright (c) 2006, Junio C Hamano
#

test_description='fmt-merge-msg test'

. ./test-lib.sh

test_expect_success setup '
	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m "Initial" &&

	git clone . remote &&

	echo uno >one &&
	echo dos >two &&
	git add two &&
	test_tick &&
	git commit -a -m "Second" &&

	git checkout -b left &&

	echo "c1" >one &&
	test_tick &&
	git commit -a -m "Common #1" &&

	echo "c2" >one &&
	test_tick &&
	git commit -a -m "Common #2" &&

	git branch right &&

	echo "l3" >two &&
	test_tick &&
	git commit -a -m "Left #3" &&

	echo "l4" >two &&
	test_tick &&
	git commit -a -m "Left #4" &&

	echo "l5" >two &&
	test_tick &&
	git commit -a -m "Left #5" &&
	git tag tag-l5 &&

	git checkout right &&

	echo "r3" >three &&
	git add three &&
	test_tick &&
	git commit -a -m "Right #3" &&
	git tag tag-r3 &&

	echo "r4" >three &&
	test_tick &&
	git commit -a -m "Right #4" &&

	echo "r5" >three &&
	test_tick &&
	git commit -a -m "Right #5" &&

	git show-branch
'

cat >expected <<\EOF
Merge branch 'left'
EOF

test_expect_success 'merge-msg test #1' '

	git checkout master &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
Merge branch 'left' of $(pwd)
EOF

test_expect_success 'merge-msg test #2' '

	git checkout master &&
	git fetch "$(pwd)" left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
Merge branch 'left'

* left:
  Left #5
  Left #4
  Left #3
  Common #2
  Common #1
EOF

test_expect_success 'merge-msg test #3-1' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.log true &&

	git checkout master &&
	test_tick &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg test #3-2' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.summary true &&

	git checkout master &&
	test_tick &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
Merge branches 'left' and 'right'

* left:
  Left #5
  Left #4
  Left #3
  Common #2
  Common #1

* right:
  Right #5
  Right #4
  Right #3
  Common #2
  Common #1
EOF

test_expect_success 'merge-msg test #4-1' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.log true &&

	git checkout master &&
	test_tick &&
	git fetch . left right &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg test #4-2' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.summary true &&

	git checkout master &&
	test_tick &&
	git fetch . left right &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg test #5-1' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.log yes &&

	git checkout master &&
	test_tick &&
	git fetch . left right &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg test #5-2' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.summary yes &&

	git checkout master &&
	test_tick &&
	git fetch . left right &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg -F' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.summary yes &&

	git checkout master &&
	test_tick &&
	git fetch . left right &&

	git fmt-merge-msg -F .git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg -F in subdirectory' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.summary yes &&

	git checkout master &&
	test_tick &&
	git fetch . left right &&
	mkdir sub &&
	cp .git/FETCH_HEAD sub/FETCH_HEAD &&
	(
		cd sub &&
		git fmt-merge-msg -F FETCH_HEAD >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'merge-msg with nothing to merge' '

	git config --unset-all merge.log
	git config --unset-all merge.summary
	git config merge.summary yes &&

	(
		cd remote &&
		git checkout -b unrelated &&
		test_tick &&
		git fetch origin &&
		git fmt-merge-msg <.git/FETCH_HEAD >../actual
	) &&

	test_cmp /dev/null actual
'

test_done
