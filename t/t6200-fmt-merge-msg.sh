#!/bin/sh
#
# Copyright (c) 2006, Junio C Hamano
#

test_description='fmt-merge-msg test'

. ./test-lib.sh

datestamp=1151939923
setdate () {
	GIT_COMMITTER_DATE="$datestamp +0200"
	GIT_AUTHOR_DATE="$datestamp +0200"
	datestamp=`expr "$datestamp" + 1`
	export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
}

test_expect_success setup '
	echo one >one &&
	git add one &&
	setdate &&
	git commit -m "Initial" &&

	echo uno >one &&
	echo dos >two &&
	git add two &&
	setdate &&
	git commit -a -m "Second" &&

	git checkout -b left &&

	echo $datestamp >one &&
	setdate &&
	git commit -a -m "Common #1" &&

	echo $datestamp >one &&
	setdate &&
	git commit -a -m "Common #2" &&

	git branch right &&

	echo $datestamp >two &&
	setdate &&
	git commit -a -m "Left #3" &&

	echo $datestamp >two &&
	setdate &&
	git commit -a -m "Left #4" &&

	echo $datestamp >two &&
	setdate &&
	git commit -a -m "Left #5" &&

	git checkout right &&

	echo $datestamp >three &&
	git add three &&
	setdate &&
	git commit -a -m "Right #3" &&

	echo $datestamp >three &&
	setdate &&
	git commit -a -m "Right #4" &&

	echo $datestamp >three &&
	setdate &&
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
	git diff actual expected
'

cat >expected <<\EOF
Merge branch 'left' of ../trash
EOF

test_expect_success 'merge-msg test #2' '

	git checkout master &&
	git fetch ../trash left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	git diff actual expected
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

test_expect_success 'merge-msg test #3' '

	git config merge.summary true &&

	git checkout master &&
	setdate &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	git diff actual expected
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

test_expect_success 'merge-msg test #4' '

	git config merge.summary true &&

	git checkout master &&
	setdate &&
	git fetch . left right &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	git diff actual expected
'

test_expect_success 'merge-msg test #5' '

	git config merge.summary yes &&

	git checkout master &&
	setdate &&
	git fetch . left right &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	git diff actual expected
'

test_done
