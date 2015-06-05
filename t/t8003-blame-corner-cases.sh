#!/bin/sh

test_description='git blame corner cases'
. ./test-lib.sh

pick_fc='s/^[0-9a-f^]* *\([^ ]*\) *(\([^ ]*\) .*/\1-\2/'

test_expect_success setup '

	echo A A A A A >one &&
	echo B B B B B >two &&
	echo C C C C C >tres &&
	echo ABC >mouse &&
	for i in 1 2 3 4 5 6 7 8 9
	do
		echo $i
	done >nine_lines &&
	for i in 1 2 3 4 5 6 7 8 9 a
	do
		echo $i
	done >ten_lines &&
	git add one two tres mouse nine_lines ten_lines &&
	test_tick &&
	GIT_AUTHOR_NAME=Initial git commit -m Initial &&

	cat one >uno &&
	mv two dos &&
	cat one >>tres &&
	echo DEF >>mouse &&
	git add uno dos tres mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Second git commit -a -m Second &&

	echo GHIJK >>mouse &&
	git add mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Third git commit -m Third &&

	cat mouse >cow &&
	git add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fourth git commit -m Fourth &&

	{
		echo ABC
		echo DEF
		echo XXXX
		echo GHIJK
	} >cow &&
	git add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fifth git commit -m Fifth
'

test_expect_success 'straight copy without -C' '

	git blame uno | grep Second

'

test_expect_success 'straight move without -C' '

	git blame dos | grep Initial

'

test_expect_success 'straight copy with -C' '

	git blame -C1 uno | grep Second

'

test_expect_success 'straight move with -C' '

	git blame -C1 dos | grep Initial

'

test_expect_success 'straight copy with -C -C' '

	git blame -C -C1 uno | grep Initial

'

test_expect_success 'straight move with -C -C' '

	git blame -C -C1 dos | grep Initial

'

test_expect_success 'append without -C' '

	git blame -L2 tres | grep Second

'

test_expect_success 'append with -C' '

	git blame -L2 -C1 tres | grep Second

'

test_expect_success 'append with -C -C' '

	git blame -L2 -C -C1 tres | grep Second

'

test_expect_success 'append with -C -C -C' '

	git blame -L2 -C -C -C1 tres | grep Initial

'

test_expect_success 'blame wholesale copy' '

	git blame -f -C -C1 HEAD^ -- cow | sed -e "$pick_fc" >current &&
	{
		echo mouse-Initial
		echo mouse-Second
		echo mouse-Third
	} >expected &&
	test_cmp expected current

'

test_expect_success 'blame wholesale copy and more' '

	git blame -f -C -C1 HEAD -- cow | sed -e "$pick_fc" >current &&
	{
		echo mouse-Initial
		echo mouse-Second
		echo cow-Fifth
		echo mouse-Third
	} >expected &&
	test_cmp expected current

'

test_expect_success 'blame path that used to be a directory' '
	mkdir path &&
	echo A A A A A >path/file &&
	echo B B B B B >path/elif &&
	git add path &&
	test_tick &&
	git commit -m "path was a directory" &&
	rm -fr path &&
	echo A A A A A >path &&
	git add path &&
	test_tick &&
	git commit -m "path is a regular file" &&
	git blame HEAD^.. -- path
'

test_expect_success 'blame to a commit with no author name' '
  TREE=`git rev-parse HEAD:` &&
  cat >badcommit <<EOF &&
tree $TREE
author <noname> 1234567890 +0000
committer David Reiss <dreiss@facebook.com> 1234567890 +0000

some message
EOF
  COMMIT=`git hash-object -t commit -w badcommit` &&
  git --no-pager blame $COMMIT -- uno >/dev/null
'

test_expect_success 'blame -L with invalid start' '
	test_must_fail git blame -L5 tres 2>errors &&
	grep "has only 2 lines" errors
'

test_expect_success 'blame -L with invalid end' '
	test_must_fail git blame -L1,5 tres 2>errors &&
	grep "has only 2 lines" errors
'

test_expect_success 'blame parses <end> part of -L' '
	git blame -L1,1 tres >out &&
	cat out &&
	test $(wc -l < out) -eq 1
'

test_expect_success 'indent of line numbers, nine lines' '
	git blame nine_lines >actual &&
	test $(grep -c "  " actual) = 0
'

test_expect_success 'indent of line numbers, ten lines' '
	git blame ten_lines >actual &&
	test $(grep -c "  " actual) = 9
'

test_expect_success 'setup file with CRLF newlines' '
	git config core.autocrlf false &&
	printf "testcase\n" >crlffile &&
	git add crlffile &&
	git commit -m testcase &&
	printf "testcase\r\n" >crlffile
'

test_expect_success 'blame file with CRLF core.autocrlf true' '
	git config core.autocrlf true &&
	git blame crlffile >actual &&
	grep "A U Thor" actual
'

test_expect_success 'blame file with CRLF attributes text' '
	git config core.autocrlf false &&
	echo "crlffile text" >.gitattributes &&
	git blame crlffile >actual &&
	grep "A U Thor" actual
'

test_done
