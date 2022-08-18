#!/bin/sh

test_description='git sleuth corner cases'
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

	cat >cow <<-\EOF &&
	ABC
	DEF
	XXXX
	GHIJK
	EOF
	git add cow &&
	test_tick &&
	GIT_AUTHOR_NAME=Fifth git commit -m Fifth
'

test_expect_success 'straight copy without -C' '

	git sleuth uno | grep Second

'

test_expect_success 'straight move without -C' '

	git sleuth dos | grep Initial

'

test_expect_success 'straight copy with -C' '

	git sleuth -C1 uno | grep Second

'

test_expect_success 'straight move with -C' '

	git sleuth -C1 dos | grep Initial

'

test_expect_success 'straight copy with -C -C' '

	git sleuth -C -C1 uno | grep Initial

'

test_expect_success 'straight move with -C -C' '

	git sleuth -C -C1 dos | grep Initial

'

test_expect_success 'append without -C' '

	git sleuth -L2 tres | grep Second

'

test_expect_success 'append with -C' '

	git sleuth -L2 -C1 tres | grep Second

'

test_expect_success 'append with -C -C' '

	git sleuth -L2 -C -C1 tres | grep Second

'

test_expect_success 'append with -C -C -C' '

	git sleuth -L2 -C -C -C1 tres | grep Initial

'

test_expect_success 'sleuth wholesale copy' '

	git sleuth -f -C -C1 HEAD^ -- cow | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	mouse-Third
	EOF
	test_cmp expected current

'

test_expect_success 'sleuth wholesale copy and more' '

	git sleuth -f -C -C1 HEAD -- cow | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	cow-Fifth
	mouse-Third
	EOF
	test_cmp expected current

'

test_expect_success 'sleuth wholesale copy and more in the index' '

	cat >horse <<-\EOF &&
	ABC
	DEF
	XXXX
	YYYY
	GHIJK
	EOF
	git add horse &&
	test_when_finished "git rm -f horse" &&
	git sleuth -f -C -C1 -- horse | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	cow-Fifth
	horse-Not
	mouse-Third
	EOF
	test_cmp expected current

'

test_expect_success 'sleuth during cherry-pick with file rename conflict' '

	test_when_finished "git reset --hard && git checkout main" &&
	git checkout HEAD~3 &&
	echo MOUSE >> mouse &&
	git mv mouse rodent &&
	git add rodent &&
	GIT_AUTHOR_NAME=Rodent git commit -m "rodent" &&
	git checkout --detach main &&
	(git cherry-pick HEAD@{1} || test $? -eq 1) &&
	git show HEAD@{1}:rodent > rodent &&
	git add rodent &&
	git sleuth -f -C -C1 rodent | sed -e "$pick_fc" >current &&
	cat >expected <<-\EOF &&
	mouse-Initial
	mouse-Second
	rodent-Not
	EOF
	test_cmp expected current
'

test_expect_success 'sleuth path that used to be a directory' '
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
	git sleuth HEAD^.. -- path
'

test_expect_success 'sleuth to a commit with no author name' '
  TREE=$(git rev-parse HEAD:) &&
  cat >badcommit <<EOF &&
tree $TREE
author <noname> 1234567890 +0000
committer David Reiss <dreiss@facebook.com> 1234567890 +0000

some message
EOF
  COMMIT=$(git hash-object -t commit -w badcommit) &&
  git --no-pager sleuth $COMMIT -- uno >/dev/null
'

test_expect_success 'sleuth -L with invalid start' '
	test_must_fail git sleuth -L5 tres 2>errors &&
	test_i18ngrep "has only 2 lines" errors
'

test_expect_success 'sleuth -L with invalid end' '
	git sleuth -L1,5 tres >out &&
	test_line_count = 2 out
'

test_expect_success 'sleuth parses <end> part of -L' '
	git sleuth -L1,1 tres >out &&
	test_line_count = 1 out
'

test_expect_success 'sleuth -Ln,-(n+1)' '
	git sleuth -L3,-4 nine_lines >out &&
	test_line_count = 3 out
'

test_expect_success 'indent of line numbers, nine lines' '
	git sleuth nine_lines >actual &&
	test $(grep -c "  " actual) = 0
'

test_expect_success 'indent of line numbers, ten lines' '
	git sleuth ten_lines >actual &&
	test $(grep -c "  " actual) = 9
'

test_expect_success 'setup file with CRLF newlines' '
	git config core.autocrlf false &&
	printf "testcase\n" >crlffile &&
	git add crlffile &&
	git commit -m testcase &&
	printf "testcase\r\n" >crlffile
'

test_expect_success 'sleuth file with CRLF core.autocrlf true' '
	git config core.autocrlf true &&
	git sleuth crlffile >actual &&
	grep "A U Thor" actual
'

test_expect_success 'sleuth file with CRLF attributes text' '
	git config core.autocrlf false &&
	echo "crlffile text" >.gitattributes &&
	git sleuth crlffile >actual &&
	grep "A U Thor" actual
'

test_expect_success 'sleuth file with CRLF core.autocrlf=true' '
	git config core.autocrlf false &&
	printf "testcase\r\n" >crlfinrepo &&
	>.gitattributes &&
	git add crlfinrepo &&
	git commit -m "add crlfinrepo" &&
	git config core.autocrlf true &&
	mv crlfinrepo tmp &&
	git checkout crlfinrepo &&
	rm tmp &&
	git sleuth crlfinrepo >actual &&
	grep "A U Thor" actual
'

test_expect_success 'setup coalesce tests' '
	cat >giraffe <<-\EOF &&
	ABC
	DEF
	EOF
	git add giraffe &&
	git commit -m "original file" &&
	orig=$(git rev-parse HEAD) &&

	cat >giraffe <<-\EOF &&
	ABC
	SPLIT
	DEF
	EOF
	git add giraffe &&
	git commit -m "interior SPLIT line" &&
	split=$(git rev-parse HEAD) &&

	cat >giraffe <<-\EOF &&
	ABC
	DEF
	EOF
	git add giraffe &&
	git commit -m "same contents as original" &&
	final=$(git rev-parse HEAD)
'

test_expect_success 'sleuth coalesce' '
	cat >expect <<-EOF &&
	$orig 1 1 2
	$orig 2 2
	EOF
	git sleuth --porcelain $final giraffe >actual.raw &&
	grep "^$orig" actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'sleuth does not coalesce non-adjacent result lines' '
	cat >expect <<-EOF &&
	$orig 1) ABC
	$orig 3) DEF
	EOF
	git sleuth --no-abbrev -s -L1,1 -L3,3 $split giraffe >actual &&
	test_cmp expect actual
'

test_done
