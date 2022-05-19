#!/bin/sh

test_description='but blame corner cases'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

pick_fc='s/^[0-9a-f^]* *\([^ ]*\) *(\([^ ]*\) .*/\1-\2/'

test_expect_success setup '
	echo A A A A A >one &&
	echo B B B B B >two &&
	echo C C C C C >tres &&
	echo ABC >mouse &&
	test_write_lines 1 2 3 4 5 6 7 8 9 >nine_lines &&
	test_write_lines 1 2 3 4 5 6 7 8 9 a >ten_lines &&
	but add one two tres mouse nine_lines ten_lines &&
	test_tick &&
	GIT_AUTHOR_NAME=Initial but cummit -m Initial &&

	cat one >uno &&
	mv two dos &&
	cat one >>tres &&
	echo DEF >>mouse &&
	but add uno dos tres mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Second but cummit -a -m Second &&

	echo GHIJK >>mouse &&
	but add mouse &&
	test_tick &&
	GIT_AUTHOR_NAME=Third but cummit -m Third &&

	cat mouse >cow &&
	but add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fourth but cummit -m Fourth &&

	cat >cow <<-\EOF &&
	ABC
	DEF
	XXXX
	GHIJK
	EOF
	but add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fifth but cummit -m Fifth
'

test_expect_success 'straight copy without -C' '

	but blame uno | grep Second

'

test_expect_success 'straight move without -C' '

	but blame dos | grep Initial

'

test_expect_success 'straight copy with -C' '

	but blame -C1 uno | grep Second

'

test_expect_success 'straight move with -C' '

	but blame -C1 dos | grep Initial

'

test_expect_success 'straight copy with -C -C' '

	but blame -C -C1 uno | grep Initial

'

test_expect_success 'straight move with -C -C' '

	but blame -C -C1 dos | grep Initial

'

test_expect_success 'append without -C' '

	but blame -L2 tres | grep Second

'

test_expect_success 'append with -C' '

	but blame -L2 -C1 tres | grep Second

'

test_expect_success 'append with -C -C' '

	but blame -L2 -C -C1 tres | grep Second

'

test_expect_success 'append with -C -C -C' '

	but blame -L2 -C -C -C1 tres | grep Initial

'

test_expect_success 'blame wholesale copy' '

	but blame -f -C -C1 HEAD^ -- cow | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	mouse-Third
	EOF
	test_cmp expected current

'

test_expect_success 'blame wholesale copy and more' '

	but blame -f -C -C1 HEAD -- cow | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	cow-Fifth
	mouse-Third
	EOF
	test_cmp expected current

'

test_expect_success 'blame wholesale copy and more in the index' '

	cat >horse <<-\EOF &&
	ABC
	DEF
	XXXX
	YYYY
	GHIJK
	EOF
	but add horse &&
	test_when_finished "but rm -f horse" &&
	but blame -f -C -C1 -- horse | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	cow-Fifth
	horse-Not
	mouse-Third
	EOF
	test_cmp expected current

'

test_expect_success 'blame during cherry-pick with file rename conflict' '

	test_when_finished "but reset --hard && but checkout main" &&
	but checkout HEAD~3 &&
	echo MOUSE >> mouse &&
	but mv mouse rodent &&
	but add rodent &&
	GIT_AUTHOR_NAME=Rodent but cummit -m "rodent" &&
	but checkout --detach main &&
	(but cherry-pick HEAD@{1} || test $? -eq 1) &&
	but show HEAD@{1}:rodent > rodent &&
	but add rodent &&
	but blame -f -C -C1 rodent | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	rodent-Not
	EOF
	test_cmp expected current
'

test_expect_success 'blame path that used to be a directory' '
	mkdir path &&
	echo A A A A A >path/file &&
	echo B B B B B >path/elif &&
	but add path &&
	test_tick &&
	but cummit -m "path was a directory" &&
	rm -fr path &&
	echo A A A A A >path &&
	but add path &&
	test_tick &&
	but cummit -m "path is a regular file" &&
	but blame HEAD^.. -- path
'

test_expect_success 'blame to a cummit with no author name' '
  TREE=$(but rev-parse HEAD:) &&
  cat >badcummit <<EOF &&
tree $TREE
author <noname> 1234567890 +0000
cummitter David Reiss <dreiss@facebook.com> 1234567890 +0000

some message
EOF
  cummit=$(but hash-object -t cummit -w badcummit) &&
  but --no-pager blame $cummit -- uno >/dev/null
'

test_expect_success 'blame -L with invalid start' '
	test_must_fail but blame -L5 tres 2>errors &&
	test_i18ngrep "has only 2 lines" errors
'

test_expect_success 'blame -L with invalid end' '
	but blame -L1,5 tres >out &&
	test_line_count = 2 out
'

test_expect_success 'blame parses <end> part of -L' '
	but blame -L1,1 tres >out &&
	test_line_count = 1 out
'

test_expect_success 'blame -Ln,-(n+1)' '
	but blame -L3,-4 nine_lines >out &&
	test_line_count = 3 out
'

test_expect_success 'indent of line numbers, nine lines' '
	but blame nine_lines >actual &&
	test $(grep -c "  " actual) = 0
'

test_expect_success 'indent of line numbers, ten lines' '
	but blame ten_lines >actual &&
	test $(grep -c "  " actual) = 9
'

test_expect_success 'setup file with CRLF newlines' '
	but config core.autocrlf false &&
	printf "testcase\n" >crlffile &&
	but add crlffile &&
	but cummit -m testcase &&
	printf "testcase\r\n" >crlffile
'

test_expect_success 'blame file with CRLF core.autocrlf true' '
	but config core.autocrlf true &&
	but blame crlffile >actual &&
	grep "A U Thor" actual
'

test_expect_success 'blame file with CRLF attributes text' '
	but config core.autocrlf false &&
	echo "crlffile text" >.butattributes &&
	but blame crlffile >actual &&
	grep "A U Thor" actual
'

test_expect_success 'blame file with CRLF core.autocrlf=true' '
	but config core.autocrlf false &&
	printf "testcase\r\n" >crlfinrepo &&
	>.butattributes &&
	but add crlfinrepo &&
	but cummit -m "add crlfinrepo" &&
	but config core.autocrlf true &&
	mv crlfinrepo tmp &&
	but checkout crlfinrepo &&
	rm tmp &&
	but blame crlfinrepo >actual &&
	grep "A U Thor" actual
'

test_expect_success 'setup coalesce tests' '
	cat >giraffe <<-\EOF &&
	ABC
	DEF
	EOF
	but add giraffe &&
	but cummit -m "original file" &&
	orig=$(but rev-parse HEAD) &&

	cat >giraffe <<-\EOF &&
	ABC
	SPLIT
	DEF
	EOF
	but add giraffe &&
	but cummit -m "interior SPLIT line" &&
	split=$(but rev-parse HEAD) &&

	cat >giraffe <<-\EOF &&
	ABC
	DEF
	EOF
	but add giraffe &&
	but cummit -m "same contents as original" &&
	final=$(but rev-parse HEAD)
'

test_expect_success 'blame coalesce' '
	cat >expect <<-EOF &&
	$orig 1 1 2
	$orig 2 2
	EOF
	but blame --porcelain $final giraffe >actual.raw &&
	grep "^$orig" actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'blame does not coalesce non-adjacent result lines' '
	cat >expect <<-EOF &&
	$orig 1) ABC
	$orig 3) DEF
	EOF
	but blame --no-abbrev -s -L1,1 -L3,3 $split giraffe >actual &&
	test_cmp expect actual
'

test_done
