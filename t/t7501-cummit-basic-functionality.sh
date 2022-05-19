#!/bin/sh
#
# Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>
#

# FIXME: Test the various index usages, -i and -o, test reflog,
# signoff

test_description='but cummit'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-diff.sh"

author='The Real Author <someguy@his.email.org>'

test_tick

test_expect_success 'initial status' '
	echo bongo bongo >file &&
	but add file &&
	but status >actual &&
	test_i18ngrep "No cummits yet" actual
'

test_expect_success 'fail initial amend' '
	test_must_fail but cummit --amend
'

test_expect_success 'setup: initial cummit' '
	but cummit -m initial
'

test_expect_success '-m and -F do not mix' '
	but checkout HEAD file && echo >>file && but add file &&
	test_must_fail but cummit -m foo -m bar -F file
'

test_expect_success '-m and -C do not mix' '
	but checkout HEAD file && echo >>file && but add file &&
	test_must_fail but cummit -C HEAD -m illegal
'

test_expect_success 'paths and -a do not mix' '
	echo King of the bongo >file &&
	test_must_fail but cummit -m foo -a file
'

test_expect_success PERL 'can use paths with --interactive' '
	echo bong-o-bong >file &&
	# 2: update, 1:st path, that is all, 7: quit
	test_write_lines 2 1 "" 7 |
	but cummit -m foo --interactive file &&
	but reset --hard HEAD^
'

test_expect_success 'removed files and relative paths' '
	test_when_finished "rm -rf foo" &&
	but init foo &&
	>foo/foo.txt &&
	but -C foo add foo.txt &&
	but -C foo cummit -m first &&
	but -C foo rm foo.txt &&

	mkdir -p foo/bar &&
	but -C foo/bar cummit -m second ../foo.txt
'

test_expect_success 'using invalid cummit with -C' '
	test_must_fail but cummit --allow-empty -C bogus
'

test_expect_success 'nothing to cummit' '
	but reset --hard &&
	test_must_fail but cummit -m initial
'

test_expect_success '--dry-run fails with nothing to cummit' '
	test_must_fail but cummit -m initial --dry-run
'

test_expect_success '--short fails with nothing to cummit' '
	test_must_fail but cummit -m initial --short
'

test_expect_success '--porcelain fails with nothing to cummit' '
	test_must_fail but cummit -m initial --porcelain
'

test_expect_success '--long fails with nothing to cummit' '
	test_must_fail but cummit -m initial --long
'

test_expect_success 'setup: non-initial cummit' '
	echo bongo bongo bongo >file &&
	but cummit -m next -a
'

test_expect_success '--dry-run with stuff to cummit returns ok' '
	echo bongo bongo bongo >>file &&
	but cummit -m next -a --dry-run
'

test_expect_success '--short with stuff to cummit returns ok' '
	echo bongo bongo bongo >>file &&
	but cummit -m next -a --short
'

test_expect_success '--porcelain with stuff to cummit returns ok' '
	echo bongo bongo bongo >>file &&
	but cummit -m next -a --porcelain
'

test_expect_success '--long with stuff to cummit returns ok' '
	echo bongo bongo bongo >>file &&
	but cummit -m next -a --long
'

test_expect_success 'cummit message from non-existing file' '
	echo more bongo: bongo bongo bongo bongo >file &&
	test_must_fail but cummit -F gah -a
'

test_expect_success 'empty cummit message' '
	# Empty except stray tabs and spaces on a few lines.
	sed -e "s/@//g" >msg <<-\EOF &&
		@		@
		@@
		@  @
		@Signed-off-by: hula@
	EOF
	test_must_fail but cummit -F msg -a
'

test_expect_success 'template "emptyness" check does not kick in with -F' '
	but checkout HEAD file && echo >>file && but add file &&
	but cummit -t file -F file
'

test_expect_success 'template "emptyness" check' '
	but checkout HEAD file && echo >>file && but add file &&
	test_must_fail but cummit -t file 2>err &&
	test_i18ngrep "did not edit" err
'

test_expect_success 'setup: cummit message from file' '
	but checkout HEAD file && echo >>file && but add file &&
	echo this is the cummit message, coming from a file >msg &&
	but cummit -F msg -a
'

test_expect_success 'amend cummit' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e "s/a file/an amend cummit/g" <"$1" >"$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor &&
	EDITOR=./editor but cummit --amend
'

test_expect_success 'amend --only ignores staged contents' '
	cp file file.expect &&
	echo changed >file &&
	but add file &&
	but cummit --no-edit --amend --only &&
	but cat-file blob HEAD:file >file.actual &&
	test_cmp file.expect file.actual &&
	but diff --exit-code
'

test_expect_success 'allow-empty --only ignores staged contents' '
	echo changed-again >file &&
	but add file &&
	but cummit --allow-empty --only -m "empty" &&
	but cat-file blob HEAD:file >file.actual &&
	test_cmp file.expect file.actual &&
	but diff --exit-code
'

test_expect_success 'set up editor' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e "s/unamended/amended/g" <"$1" >"$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor
'

test_expect_success 'amend without launching editor' '
	echo unamended >expect &&
	but cummit --allow-empty -m "unamended" &&
	echo needs more bongo >file &&
	but add file &&
	EDITOR=./editor but cummit --no-edit --amend &&
	but diff --exit-code HEAD -- file &&
	but diff-tree -s --format=%s HEAD >msg &&
	test_cmp expect msg
'

test_expect_success '--amend --edit' '
	echo amended >expect &&
	but cummit --allow-empty -m "unamended" &&
	echo bongo again >file &&
	but add file &&
	EDITOR=./editor but cummit --edit --amend &&
	but diff-tree -s --format=%s HEAD >msg &&
	test_cmp expect msg
'

test_expect_success '--amend --edit of empty message' '
	cat >replace <<-\EOF &&
	#!/bin/sh
	echo "amended" >"$1"
	EOF
	chmod 755 replace &&
	but cummit --allow-empty --allow-empty-message -m "" &&
	echo more bongo >file &&
	but add file &&
	EDITOR=./replace but cummit --edit --amend &&
	but diff-tree -s --format=%s HEAD >msg &&
	./replace expect &&
	test_cmp expect msg
'

test_expect_success '--amend to set message to empty' '
	echo bata >file &&
	but add file &&
	but cummit -m "unamended" &&
	but cummit --amend --allow-empty-message -m "" &&
	but diff-tree -s --format=%s HEAD >msg &&
	echo "" >expect &&
	test_cmp expect msg
'

test_expect_success '--amend to set empty message needs --allow-empty-message' '
	echo conga >file &&
	but add file &&
	but cummit -m "unamended" &&
	test_must_fail but cummit --amend -m "" &&
	but diff-tree -s --format=%s HEAD >msg &&
	echo "unamended" >expect &&
	test_cmp expect msg
'

test_expect_success '-m --edit' '
	echo amended >expect &&
	but cummit --allow-empty -m buffer &&
	echo bongo bongo >file &&
	but add file &&
	EDITOR=./editor but cummit -m unamended --edit &&
	but diff-tree -s  --format=%s HEAD >msg &&
	test_cmp expect msg
'

test_expect_success '-m and -F do not mix' '
	echo enough with the bongos >file &&
	test_must_fail but cummit -F msg -m amending .
'

test_expect_success 'using message from other cummit' '
	but cummit -C HEAD^ .
'

test_expect_success 'editing message from other cummit' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e "s/amend/older/g"  <"$1" >"$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor &&
	echo hula hula >file &&
	EDITOR=./editor but cummit -c HEAD^ -a
'

test_expect_success 'message from stdin' '
	echo silly new contents >file &&
	echo cummit message from stdin |
	but cummit -F - -a
'

test_expect_success 'overriding author from command line' '
	echo gak >file &&
	but cummit -m author \
		--author "Rubber Duck <rduck@convoy.org>" -a >output 2>&1 &&
	grep Rubber.Duck output
'

test_expect_success PERL 'interactive add' '
	echo 7 | test_must_fail but cummit --interactive >out &&
	grep "What now" out
'

test_expect_success PERL "cummit --interactive doesn't change index if editor aborts" '
	echo zoo >file &&
	test_must_fail but diff --exit-code >diff1 &&
	test_write_lines u "*" q |
	(
		EDITOR=: &&
		export EDITOR &&
		test_must_fail but cummit --interactive
	) &&
	but diff >diff2 &&
	compare_diff_patch diff1 diff2
'

test_expect_success 'editor not invoked if -F is given' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e s/good/bad/g <"$1" >"$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor &&

	echo A good cummit message. >msg &&
	echo moo >file &&

	EDITOR=./editor but cummit -a -F msg &&
	but show -s --pretty=format:%s >subject &&
	grep -q good subject &&

	echo quack >file &&
	echo Another good message. |
	EDITOR=./editor but cummit -a -F - &&
	but show -s --pretty=format:%s >subject &&
	grep -q good subject
'

test_expect_success 'partial cummit that involves removal (1)' '

	but rm --cached file &&
	mv file elif &&
	but add elif &&
	but cummit -m "Partial: add elif" elif &&
	but diff-tree --name-status HEAD^ HEAD >current &&
	echo "A	elif" >expected &&
	test_cmp expected current

'

test_expect_success 'partial cummit that involves removal (2)' '

	but cummit -m "Partial: remove file" file &&
	but diff-tree --name-status HEAD^ HEAD >current &&
	echo "D	file" >expected &&
	test_cmp expected current

'

test_expect_success 'partial cummit that involves removal (3)' '

	but rm --cached elif &&
	echo elif >elif &&
	but cummit -m "Partial: modify elif" elif &&
	but diff-tree --name-status HEAD^ HEAD >current &&
	echo "M	elif" >expected &&
	test_cmp expected current

'

test_expect_success 'amend cummit to fix author' '

	oldtick=$GIT_AUTHOR_DATE &&
	test_tick &&
	but reset --hard &&
	but cat-file -p HEAD >cummit &&
	sed -e "s/author.*/author $author $oldtick/" \
		-e "s/^\(cummitter.*> \).*$/\1$GIT_CUMMITTER_DATE/" \
		cummit >expected &&
	but cummit --amend --author="$author" &&
	but cat-file -p HEAD >current &&
	test_cmp expected current

'

test_expect_success 'amend cummit to fix date' '

	test_tick &&
	newtick=$GIT_AUTHOR_DATE &&
	but reset --hard &&
	but cat-file -p HEAD >cummit &&
	sed -e "s/author.*/author $author $newtick/" \
		-e "s/^\(cummitter.*> \).*$/\1$GIT_CUMMITTER_DATE/" \
		cummit >expected &&
	but cummit --amend --date="$newtick" &&
	but cat-file -p HEAD >current &&
	test_cmp expected current

'

test_expect_success 'cummit mentions forced date in output' '
	but cummit --amend --date=2010-01-02T03:04:05 >output &&
	grep "Date: *Sat Jan 2 03:04:05 2010" output
'

test_expect_success 'cummit complains about completely bogus dates' '
	test_must_fail but cummit --amend --date=seventeen
'

test_expect_success 'cummit --date allows approxidate' '
	but cummit --amend \
		--date="midnight the 12th of october, anno domini 1979" &&
	echo "Fri Oct 12 00:00:00 1979 +0000" >expect &&
	but log -1 --format=%ad >actual &&
	test_cmp expect actual
'

test_expect_success 'sign off (1)' '

	echo 1 >positive &&
	but add positive &&
	but cummit -s -m "thank you" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo thank you &&
		echo &&
		but var GIT_CUMMITTER_IDENT >ident &&
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /" ident
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'sign off (2)' '

	echo 2 >positive &&
	but add positive &&
	existing="Signed-off-by: Watch This <watchthis@example.com>" &&
	but cummit -s -m "thank you

$existing" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo thank you &&
		echo &&
		echo $existing &&
		but var GIT_CUMMITTER_IDENT >ident &&
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /" ident
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'signoff gap' '

	echo 3 >positive &&
	but add positive &&
	alt="Alt-RFC-822-Header: Value" &&
	but cummit -s -m "welcome

$alt" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo welcome &&
		echo &&
		echo $alt &&
		but var GIT_CUMMITTER_IDENT >ident &&
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /" ident
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'signoff gap 2' '

	echo 4 >positive &&
	but add positive &&
	alt="fixed: 34" &&
	but cummit -s -m "welcome

We have now
$alt" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo welcome &&
		echo &&
		echo We have now &&
		echo $alt &&
		echo &&
		but var GIT_CUMMITTER_IDENT >ident &&
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /" ident
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'signoff respects trailer config' '

	echo 5 >positive &&
	but add positive &&
	but cummit -s -m "subject

non-trailer line
Myfooter: x" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo subject &&
		echo &&
		echo non-trailer line &&
		echo Myfooter: x &&
		echo &&
		echo "Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>"
	) >expected &&
	test_cmp expected actual &&

	echo 6 >positive &&
	but add positive &&
	but -c "trailer.Myfooter.ifexists=add" cummit -s -m "subject

non-trailer line
Myfooter: x" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo subject &&
		echo &&
		echo non-trailer line &&
		echo Myfooter: x &&
		echo "Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'signoff not confused by ---' '
	cat >expected <<-EOF &&
		subject

		body
		---
		these dashes confuse the parser!

		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	# should be a noop, since we already signed
	but cummit --allow-empty --signoff -F expected &&
	but log -1 --pretty=format:%B >actual &&
	test_cmp expected actual
'

test_expect_success 'multiple -m' '

	>negative &&
	but add negative &&
	but cummit -m "one" -m "two" -m "three" &&
	but cat-file commit HEAD >cummit &&
	sed -e "1,/^\$/d" cummit >actual &&
	(
		echo one &&
		echo &&
		echo two &&
		echo &&
		echo three
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'amend cummit to fix author' '

	oldtick=$GIT_AUTHOR_DATE &&
	test_tick &&
	but reset --hard &&
	but cat-file -p HEAD >cummit &&
	sed -e "s/author.*/author $author $oldtick/" \
		-e "s/^\(cummitter.*> \).*$/\1$GIT_CUMMITTER_DATE/" \
		cummit >expected &&
	but cummit --amend --author="$author" &&
	but cat-file -p HEAD >current &&
	test_cmp expected current

'

test_expect_success 'but cummit <file> with dirty index' '
	echo tacocat >elif &&
	echo tehlulz >chz &&
	but add chz &&
	but cummit elif -m "tacocat is a palindrome" &&
	but show --stat >stat &&
	grep elif stat &&
	but diff --cached >diff &&
	grep chz diff
'

test_expect_success 'same tree (single parent)' '

	but reset --hard &&
	test_must_fail but cummit -m empty

'

test_expect_success 'same tree (single parent) --allow-empty' '

	but cummit --allow-empty -m "forced empty" &&
	but cat-file commit HEAD >cummit &&
	grep forced cummit

'

test_expect_success 'same tree (merge and amend merge)' '

	but checkout -b side HEAD^ &&
	echo zero >zero &&
	but add zero &&
	but cummit -m "add zero" &&
	but checkout main &&

	but merge -s ours side -m "empty ok" &&
	but diff HEAD^ HEAD >actual &&
	test_must_be_empty actual &&

	but cummit --amend -m "empty really ok" &&
	but diff HEAD^ HEAD >actual &&
	test_must_be_empty actual

'

test_expect_success 'amend using the message from another cummit' '

	but reset --hard &&
	test_tick &&
	but cummit --allow-empty -m "old cummit" &&
	old=$(but rev-parse --verify HEAD) &&
	test_tick &&
	but cummit --allow-empty -m "new cummit" &&
	new=$(but rev-parse --verify HEAD) &&
	test_tick &&
	but cummit --allow-empty --amend -C "$old" &&
	but show --pretty="format:%ad %s" "$old" >expected &&
	but show --pretty="format:%ad %s" HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'amend using the message from a cummit named with tag' '

	but reset --hard &&
	test_tick &&
	but cummit --allow-empty -m "old cummit" &&
	old=$(but rev-parse --verify HEAD) &&
	but tag -a -m "tag on old" tagged-old HEAD &&
	test_tick &&
	but cummit --allow-empty -m "new cummit" &&
	new=$(but rev-parse --verify HEAD) &&
	test_tick &&
	but cummit --allow-empty --amend -C tagged-old &&
	but show --pretty="format:%ad %s" "$old" >expected &&
	but show --pretty="format:%ad %s" HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'amend can copy notes' '

	but config notes.rewrite.amend true &&
	but config notes.rewriteRef "refs/notes/*" &&
	test_cummit foo &&
	but notes add -m"a note" &&
	test_tick &&
	but cummit --amend -m"new foo" &&
	test "$(but notes show)" = "a note"

'

test_expect_success 'cummit a file whose name is a dash' '
	but reset --hard &&
	test_write_lines 1 2 3 4 5 >./- &&
	but add ./- &&
	test_tick &&
	but cummit -m "add dash" >output </dev/null &&
	test_i18ngrep " changed, 5 insertions" output
'

test_expect_success '--only works on to-be-born branch' '
	# This test relies on having something in the index, as it
	# would not otherwise actually prove much.  So check this.
	test -n "$(but ls-files)" &&
	but checkout --orphan orphan &&
	echo foo >newfile &&
	but add newfile &&
	but cummit --only newfile -m"--only on unborn branch" &&
	echo newfile >expected &&
	but ls-tree -r --name-only HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--dry-run with conflicts fixed from a merge' '
	# setup two branches with conflicting information
	# in the same file, resolve the conflict,
	# call cummit with --dry-run
	echo "Initial contents, unimportant" >test-file &&
	but add test-file &&
	but cummit -m "Initial cummit" &&
	echo "cummit-1-state" >test-file &&
	but cummit -m "cummit 1" -i test-file &&
	but tag cummit-1 &&
	but checkout -b branch-2 HEAD^1 &&
	echo "cummit-2-state" >test-file &&
	but cummit -m "cummit 2" -i test-file &&
	test_must_fail but merge --no-cummit cummit-1 &&
	echo "cummit-2-state" >test-file &&
	but add test-file &&
	but cummit --dry-run &&
	but cummit -m "conflicts fixed from merge."
'

test_expect_success '--dry-run --short' '
	>test-file &&
	but add test-file &&
	but cummit --dry-run --short
'

test_done
