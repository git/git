#!/bin/sh
#
# Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>
#

# FIXME: Test the various index usages, -i and -o, test reflog,
# signoff

test_description='git commit'
. ./test-lib.sh
. "$TEST_DIRECTORY/diff-lib.sh"

author='The Real Author <someguy@his.email.org>'

test_tick

test_expect_success 'initial status' '
	echo bongo bongo >file &&
	git add file &&
	git status >actual &&
	test_i18ngrep "Initial commit" actual
'

test_expect_success 'fail initial amend' '
	test_must_fail git commit --amend
'

test_expect_success 'setup: initial commit' '
	git commit -m initial
'

test_expect_success '-m and -F do not mix' '
	git checkout HEAD file && echo >>file && git add file &&
	test_must_fail git commit -m foo -m bar -F file
'

test_expect_success '-m and -C do not mix' '
	git checkout HEAD file && echo >>file && git add file &&
	test_must_fail git commit -C HEAD -m illegal
'

test_expect_success 'paths and -a do not mix' '
	echo King of the bongo >file &&
	test_must_fail git commit -m foo -a file
'

test_expect_success PERL 'can use paths with --interactive' '
	echo bong-o-bong >file &&
	# 2: update, 1:st path, that is all, 7: quit
	( echo 2; echo 1; echo; echo 7 ) |
	git commit -m foo --interactive file &&
	git reset --hard HEAD^
'

test_expect_success 'using invalid commit with -C' '
	test_must_fail git commit --allow-empty -C bogus
'

test_expect_success 'nothing to commit' '
	git reset --hard &&
	test_must_fail git commit -m initial
'

test_expect_success '--dry-run fails with nothing to commit' '
	test_must_fail git commit -m initial --dry-run
'

test_expect_success '--short fails with nothing to commit' '
	test_must_fail git commit -m initial --short
'

test_expect_success '--porcelain fails with nothing to commit' '
	test_must_fail git commit -m initial --porcelain
'

test_expect_success '--long fails with nothing to commit' '
	test_must_fail git commit -m initial --long
'

test_expect_success 'setup: non-initial commit' '
	echo bongo bongo bongo >file &&
	git commit -m next -a
'

test_expect_success '--dry-run with stuff to commit returns ok' '
	echo bongo bongo bongo >>file &&
	git commit -m next -a --dry-run
'

test_expect_failure '--short with stuff to commit returns ok' '
	echo bongo bongo bongo >>file &&
	git commit -m next -a --short
'

test_expect_failure '--porcelain with stuff to commit returns ok' '
	echo bongo bongo bongo >>file &&
	git commit -m next -a --porcelain
'

test_expect_success '--long with stuff to commit returns ok' '
	echo bongo bongo bongo >>file &&
	git commit -m next -a --long
'

test_expect_success 'commit message from non-existing file' '
	echo more bongo: bongo bongo bongo bongo >file &&
	test_must_fail git commit -F gah -a
'

test_expect_success 'empty commit message' '
	# Empty except stray tabs and spaces on a few lines.
	sed -e "s/@//g" >msg <<-\EOF &&
		@		@
		@@
		@  @
		@Signed-off-by: hula@
	EOF
	test_must_fail git commit -F msg -a
'

test_expect_success 'template "emptyness" check does not kick in with -F' '
	git checkout HEAD file && echo >>file && git add file &&
	git commit -t file -F file
'

test_expect_success 'template "emptyness" check' '
	git checkout HEAD file && echo >>file && git add file &&
	test_must_fail git commit -t file 2>err &&
	test_i18ngrep "did not edit" err
'

test_expect_success 'setup: commit message from file' '
	git checkout HEAD file && echo >>file && git add file &&
	echo this is the commit message, coming from a file >msg &&
	git commit -F msg -a
'

test_expect_success 'amend commit' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e "s/a file/an amend commit/g" < "$1" > "$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor &&
	EDITOR=./editor git commit --amend
'

test_expect_success 'amend --only ignores staged contents' '
	cp file file.expect &&
	echo changed >file &&
	git add file &&
	git commit --no-edit --amend --only &&
	git cat-file blob HEAD:file >file.actual &&
	test_cmp file.expect file.actual &&
	git diff --exit-code
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
	git commit --allow-empty -m "unamended" &&
	echo needs more bongo >file &&
	git add file &&
	EDITOR=./editor git commit --no-edit --amend &&
	git diff --exit-code HEAD -- file &&
	git diff-tree -s --format=%s HEAD >msg &&
	test_cmp expect msg
'

test_expect_success '--amend --edit' '
	echo amended >expect &&
	git commit --allow-empty -m "unamended" &&
	echo bongo again >file &&
	git add file &&
	EDITOR=./editor git commit --edit --amend &&
	git diff-tree -s --format=%s HEAD >msg &&
	test_cmp expect msg
'

test_expect_success '--amend --edit of empty message' '
	cat >replace <<-\EOF &&
	#!/bin/sh
	echo "amended" >"$1"
	EOF
	chmod 755 replace &&
	git commit --allow-empty --allow-empty-message -m "" &&
	echo more bongo >file &&
	git add file &&
	EDITOR=./replace git commit --edit --amend &&
	git diff-tree -s --format=%s HEAD >msg &&
	./replace expect &&
	test_cmp expect msg
'

test_expect_success '-m --edit' '
	echo amended >expect &&
	git commit --allow-empty -m buffer &&
	echo bongo bongo >file &&
	git add file &&
	EDITOR=./editor git commit -m unamended --edit &&
	git diff-tree -s  --format=%s HEAD >msg &&
	test_cmp expect msg
'

test_expect_success '-m and -F do not mix' '
	echo enough with the bongos >file &&
	test_must_fail git commit -F msg -m amending .
'

test_expect_success 'using message from other commit' '
	git commit -C HEAD^ .
'

test_expect_success 'editing message from other commit' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e "s/amend/older/g"  < "$1" > "$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor &&
	echo hula hula >file &&
	EDITOR=./editor git commit -c HEAD^ -a
'

test_expect_success 'message from stdin' '
	echo silly new contents >file &&
	echo commit message from stdin |
	git commit -F - -a
'

test_expect_success 'overriding author from command line' '
	echo gak >file &&
	git commit -m author \
		--author "Rubber Duck <rduck@convoy.org>" -a >output 2>&1 &&
	grep Rubber.Duck output
'

test_expect_success PERL 'interactive add' '
	echo 7 |
	git commit --interactive |
	grep "What now"
'

test_expect_success PERL "commit --interactive doesn't change index if editor aborts" '
	echo zoo >file &&
	test_must_fail git diff --exit-code >diff1 &&
	(echo u ; echo "*" ; echo q) |
	(
		EDITOR=: &&
		export EDITOR &&
		test_must_fail git commit --interactive
	) &&
	git diff >diff2 &&
	compare_diff_patch diff1 diff2
'

test_expect_success 'editor not invoked if -F is given' '
	cat >editor <<-\EOF &&
	#!/bin/sh
	sed -e s/good/bad/g <"$1" >"$1-"
	mv "$1-" "$1"
	EOF
	chmod 755 editor &&

	echo A good commit message. >msg &&
	echo moo >file &&

	EDITOR=./editor git commit -a -F msg &&
	git show -s --pretty=format:%s >subject &&
	grep -q good subject &&

	echo quack >file &&
	echo Another good message. |
	EDITOR=./editor git commit -a -F - &&
	git show -s --pretty=format:%s >subject &&
	grep -q good subject
'

test_expect_success 'partial commit that involves removal (1)' '

	git rm --cached file &&
	mv file elif &&
	git add elif &&
	git commit -m "Partial: add elif" elif &&
	git diff-tree --name-status HEAD^ HEAD >current &&
	echo "A	elif" >expected &&
	test_cmp expected current

'

test_expect_success 'partial commit that involves removal (2)' '

	git commit -m "Partial: remove file" file &&
	git diff-tree --name-status HEAD^ HEAD >current &&
	echo "D	file" >expected &&
	test_cmp expected current

'

test_expect_success 'partial commit that involves removal (3)' '

	git rm --cached elif &&
	echo elif >elif &&
	git commit -m "Partial: modify elif" elif &&
	git diff-tree --name-status HEAD^ HEAD >current &&
	echo "M	elif" >expected &&
	test_cmp expected current

'

test_expect_success 'amend commit to fix author' '

	oldtick=$GIT_AUTHOR_DATE &&
	test_tick &&
	git reset --hard &&
	git cat-file -p HEAD |
	sed -e "s/author.*/author $author $oldtick/" \
		-e "s/^\(committer.*> \).*$/\1$GIT_COMMITTER_DATE/" > \
		expected &&
	git commit --amend --author="$author" &&
	git cat-file -p HEAD > current &&
	test_cmp expected current

'

test_expect_success 'amend commit to fix date' '

	test_tick &&
	newtick=$GIT_AUTHOR_DATE &&
	git reset --hard &&
	git cat-file -p HEAD |
	sed -e "s/author.*/author $author $newtick/" \
		-e "s/^\(committer.*> \).*$/\1$GIT_COMMITTER_DATE/" > \
		expected &&
	git commit --amend --date="$newtick" &&
	git cat-file -p HEAD > current &&
	test_cmp expected current

'

test_expect_success 'commit mentions forced date in output' '
	git commit --amend --date=2010-01-02T03:04:05 >output &&
	grep "Date: *Sat Jan 2 03:04:05 2010" output
'

test_expect_success 'commit complains about completely bogus dates' '
	test_must_fail git commit --amend --date=seventeen
'

test_expect_success 'commit --date allows approxidate' '
	git commit --amend \
		--date="midnight the 12th of october, anno domini 1979" &&
	echo "Fri Oct 12 00:00:00 1979 +0000" >expect &&
	git log -1 --format=%ad >actual &&
	test_cmp expect actual
'

test_expect_success 'sign off (1)' '

	echo 1 >positive &&
	git add positive &&
	git commit -s -m "thank you" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	(
		echo thank you
		echo
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'sign off (2)' '

	echo 2 >positive &&
	git add positive &&
	existing="Signed-off-by: Watch This <watchthis@example.com>" &&
	git commit -s -m "thank you

$existing" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	(
		echo thank you
		echo
		echo $existing
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'signoff gap' '

	echo 3 >positive &&
	git add positive &&
	alt="Alt-RFC-822-Header: Value" &&
	git commit -s -m "welcome

$alt" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	(
		echo welcome
		echo
		echo $alt
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'signoff gap 2' '

	echo 4 >positive &&
	git add positive &&
	alt="fixed: 34" &&
	git commit -s -m "welcome

We have now
$alt" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	(
		echo welcome
		echo
		echo We have now
		echo $alt
		echo
		git var GIT_COMMITTER_IDENT |
		sed -e "s/>.*/>/" -e "s/^/Signed-off-by: /"
	) >expected &&
	test_cmp expected actual
'

test_expect_success 'multiple -m' '

	>negative &&
	git add negative &&
	git commit -m "one" -m "two" -m "three" &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	(
		echo one
		echo
		echo two
		echo
		echo three
	) >expected &&
	test_cmp expected actual

'

test_expect_success 'amend commit to fix author' '

	oldtick=$GIT_AUTHOR_DATE &&
	test_tick &&
	git reset --hard &&
	git cat-file -p HEAD |
	sed -e "s/author.*/author $author $oldtick/" \
		-e "s/^\(committer.*> \).*$/\1$GIT_COMMITTER_DATE/" > \
		expected &&
	git commit --amend --author="$author" &&
	git cat-file -p HEAD > current &&
	test_cmp expected current

'

test_expect_success 'git commit <file> with dirty index' '
	echo tacocat > elif &&
	echo tehlulz > chz &&
	git add chz &&
	git commit elif -m "tacocat is a palindrome" &&
	git show --stat | grep elif &&
	git diff --cached | grep chz
'

test_expect_success 'same tree (single parent)' '

	git reset --hard &&
	test_must_fail git commit -m empty

'

test_expect_success 'same tree (single parent) --allow-empty' '

	git commit --allow-empty -m "forced empty" &&
	git cat-file commit HEAD | grep forced

'

test_expect_success 'same tree (merge and amend merge)' '

	git checkout -b side HEAD^ &&
	echo zero >zero &&
	git add zero &&
	git commit -m "add zero" &&
	git checkout master &&

	git merge -s ours side -m "empty ok" &&
	git diff HEAD^ HEAD >actual &&
	: >expected &&
	test_cmp expected actual &&

	git commit --amend -m "empty really ok" &&
	git diff HEAD^ HEAD >actual &&
	: >expected &&
	test_cmp expected actual

'

test_expect_success 'amend using the message from another commit' '

	git reset --hard &&
	test_tick &&
	git commit --allow-empty -m "old commit" &&
	old=$(git rev-parse --verify HEAD) &&
	test_tick &&
	git commit --allow-empty -m "new commit" &&
	new=$(git rev-parse --verify HEAD) &&
	test_tick &&
	git commit --allow-empty --amend -C "$old" &&
	git show --pretty="format:%ad %s" "$old" >expected &&
	git show --pretty="format:%ad %s" HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'amend using the message from a commit named with tag' '

	git reset --hard &&
	test_tick &&
	git commit --allow-empty -m "old commit" &&
	old=$(git rev-parse --verify HEAD) &&
	git tag -a -m "tag on old" tagged-old HEAD &&
	test_tick &&
	git commit --allow-empty -m "new commit" &&
	new=$(git rev-parse --verify HEAD) &&
	test_tick &&
	git commit --allow-empty --amend -C tagged-old &&
	git show --pretty="format:%ad %s" "$old" >expected &&
	git show --pretty="format:%ad %s" HEAD >actual &&
	test_cmp expected actual

'

test_expect_success 'amend can copy notes' '

	git config notes.rewrite.amend true &&
	git config notes.rewriteRef "refs/notes/*" &&
	test_commit foo &&
	git notes add -m"a note" &&
	test_tick &&
	git commit --amend -m"new foo" &&
	test "$(git notes show)" = "a note"

'

test_expect_success 'commit a file whose name is a dash' '
	git reset --hard &&
	for i in 1 2 3 4 5
	do
		echo $i
	done >./- &&
	git add ./- &&
	test_tick &&
	git commit -m "add dash" >output </dev/null &&
	test_i18ngrep " changed, 5 insertions" output
'

test_expect_success '--only works on to-be-born branch' '
	# This test relies on having something in the index, as it
	# would not otherwise actually prove much.  So check this.
	test -n "$(git ls-files)" &&
	git checkout --orphan orphan &&
	echo foo >newfile &&
	git add newfile &&
	git commit --only newfile -m"--only on unborn branch" &&
	echo newfile >expected &&
	git ls-tree -r --name-only HEAD >actual &&
	test_cmp expected actual
'

test_done
