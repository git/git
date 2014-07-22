#!/bin/sh
#
# Copyright (c) 2008 Christian Couder
#
test_description='Tests replace refs functionality'

exec </dev/null

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

add_and_commit_file ()
{
    _file="$1"
    _msg="$2"

    git add $_file || return $?
    test_tick || return $?
    git commit --quiet -m "$_file: $_msg"
}

commit_buffer_contains_parents ()
{
    git cat-file commit "$1" >payload &&
    sed -n -e '/^$/q' -e '/^parent /p' <payload >actual &&
    shift &&
    for _parent
    do
	echo "parent $_parent"
    done >expected &&
    test_cmp expected actual
}

commit_peeling_shows_parents ()
{
    _parent_number=1
    _commit="$1"
    shift &&
    for _parent
    do
	_found=$(git rev-parse --verify $_commit^$_parent_number) || return 1
	test "$_found" = "$_parent" || return 1
	_parent_number=$(( $_parent_number + 1 ))
    done &&
    test_must_fail git rev-parse --verify $_commit^$_parent_number
}

commit_has_parents ()
{
    commit_buffer_contains_parents "$@" &&
    commit_peeling_shows_parents "$@"
}

HASH1=
HASH2=
HASH3=
HASH4=
HASH5=
HASH6=
HASH7=

test_expect_success 'set up buggy branch' '
     echo "line 1" >>hello &&
     echo "line 2" >>hello &&
     echo "line 3" >>hello &&
     echo "line 4" >>hello &&
     add_and_commit_file hello "4 lines" &&
     HASH1=$(git rev-parse --verify HEAD) &&
     echo "line BUG" >>hello &&
     echo "line 6" >>hello &&
     echo "line 7" >>hello &&
     echo "line 8" >>hello &&
     add_and_commit_file hello "4 more lines with a BUG" &&
     HASH2=$(git rev-parse --verify HEAD) &&
     echo "line 9" >>hello &&
     echo "line 10" >>hello &&
     add_and_commit_file hello "2 more lines" &&
     HASH3=$(git rev-parse --verify HEAD) &&
     echo "line 11" >>hello &&
     add_and_commit_file hello "1 more line" &&
     HASH4=$(git rev-parse --verify HEAD) &&
     sed -e "s/BUG/5/" hello >hello.new &&
     mv hello.new hello &&
     add_and_commit_file hello "BUG fixed" &&
     HASH5=$(git rev-parse --verify HEAD) &&
     echo "line 12" >>hello &&
     echo "line 13" >>hello &&
     add_and_commit_file hello "2 more lines" &&
     HASH6=$(git rev-parse --verify HEAD) &&
     echo "line 14" >>hello &&
     echo "line 15" >>hello &&
     echo "line 16" >>hello &&
     add_and_commit_file hello "again 3 more lines" &&
     HASH7=$(git rev-parse --verify HEAD)
'

test_expect_success 'replace the author' '
     git cat-file commit $HASH2 | grep "author A U Thor" &&
     R=$(git cat-file commit $HASH2 | sed -e "s/A U/O/" | git hash-object -t commit --stdin -w) &&
     git cat-file commit $R | grep "author O Thor" &&
     git update-ref refs/replace/$HASH2 $R &&
     git show HEAD~5 | grep "O Thor" &&
     git show $HASH2 | grep "O Thor"
'

test_expect_success 'test --no-replace-objects option' '
     git cat-file commit $HASH2 | grep "author O Thor" &&
     git --no-replace-objects cat-file commit $HASH2 | grep "author A U Thor" &&
     git show $HASH2 | grep "O Thor" &&
     git --no-replace-objects show $HASH2 | grep "A U Thor"
'

test_expect_success 'test GIT_NO_REPLACE_OBJECTS env variable' '
     GIT_NO_REPLACE_OBJECTS=1 git cat-file commit $HASH2 | grep "author A U Thor" &&
     GIT_NO_REPLACE_OBJECTS=1 git show $HASH2 | grep "A U Thor"
'

cat >tag.sig <<EOF
object $HASH2
type commit
tag mytag
tagger T A Gger <> 0 +0000

EOF

test_expect_success 'tag replaced commit' '
     git mktag <tag.sig >.git/refs/tags/mytag 2>message
'

test_expect_success '"git fsck" works' '
     git fsck master >fsck_master.out &&
     grep "dangling commit $R" fsck_master.out &&
     grep "dangling tag $(cat .git/refs/tags/mytag)" fsck_master.out &&
     test -z "$(git fsck)"
'

test_expect_success 'repack, clone and fetch work' '
     git repack -a -d &&
     git clone --no-hardlinks . clone_dir &&
     (
	  cd clone_dir &&
	  git show HEAD~5 | grep "A U Thor" &&
	  git show $HASH2 | grep "A U Thor" &&
	  git cat-file commit $R &&
	  git repack -a -d &&
	  test_must_fail git cat-file commit $R &&
	  git fetch ../ "refs/replace/*:refs/replace/*" &&
	  git show HEAD~5 | grep "O Thor" &&
	  git show $HASH2 | grep "O Thor" &&
	  git cat-file commit $R
     )
'

test_expect_success '"git replace" listing and deleting' '
     test "$HASH2" = "$(git replace -l)" &&
     test "$HASH2" = "$(git replace)" &&
     aa=${HASH2%??????????????????????????????????????} &&
     test "$HASH2" = "$(git replace --list "$aa*")" &&
     test_must_fail git replace -d $R &&
     test_must_fail git replace --delete &&
     test_must_fail git replace -l -d $HASH2 &&
     git replace -d $HASH2 &&
     git show $HASH2 | grep "A U Thor" &&
     test -z "$(git replace -l)"
'

test_expect_success '"git replace" replacing' '
     git replace $HASH2 $R &&
     git show $HASH2 | grep "O Thor" &&
     test_must_fail git replace $HASH2 $R &&
     git replace -f $HASH2 $R &&
     test_must_fail git replace -f &&
     test "$HASH2" = "$(git replace)"
'

test_expect_success '"git replace" resolves sha1' '
     SHORTHASH2=$(git rev-parse --short=8 $HASH2) &&
     git replace -d $SHORTHASH2 &&
     git replace $SHORTHASH2 $R &&
     git show $HASH2 | grep "O Thor" &&
     test_must_fail git replace $HASH2 $R &&
     git replace -f $HASH2 $R &&
     test_must_fail git replace --force &&
     test "$HASH2" = "$(git replace)"
'

# This creates a side branch where the bug in H2
# does not appear because P2 is created by applying
# H2 and squashing H5 into it.
# P3, P4 and P6 are created by cherry-picking H3, H4
# and H6 respectively.
#
# At this point, we should have the following:
#
#    P2--P3--P4--P6
#   /
# H1-H2-H3-H4-H5-H6-H7
#
# Then we replace H6 with P6.
#
test_expect_success 'create parallel branch without the bug' '
     git replace -d $HASH2 &&
     git show $HASH2 | grep "A U Thor" &&
     git checkout $HASH1 &&
     git cherry-pick $HASH2 &&
     git show $HASH5 | git apply &&
     git commit --amend -m "hello: 4 more lines WITHOUT the bug" hello &&
     PARA2=$(git rev-parse --verify HEAD) &&
     git cherry-pick $HASH3 &&
     PARA3=$(git rev-parse --verify HEAD) &&
     git cherry-pick $HASH4 &&
     PARA4=$(git rev-parse --verify HEAD) &&
     git cherry-pick $HASH6 &&
     PARA6=$(git rev-parse --verify HEAD) &&
     git replace $HASH6 $PARA6 &&
     git checkout master &&
     cur=$(git rev-parse --verify HEAD) &&
     test "$cur" = "$HASH7" &&
     git log --pretty=oneline | grep $PARA2 &&
     git remote add cloned ./clone_dir
'

test_expect_success 'push to cloned repo' '
     git push cloned $HASH6^:refs/heads/parallel &&
     (
	  cd clone_dir &&
	  git checkout parallel &&
	  git log --pretty=oneline | grep $PARA2
     )
'

test_expect_success 'push branch with replacement' '
     git cat-file commit $PARA3 | grep "author A U Thor" &&
     S=$(git cat-file commit $PARA3 | sed -e "s/A U/O/" | git hash-object -t commit --stdin -w) &&
     git cat-file commit $S | grep "author O Thor" &&
     git replace $PARA3 $S &&
     git show $HASH6~2 | grep "O Thor" &&
     git show $PARA3 | grep "O Thor" &&
     git push cloned $HASH6^:refs/heads/parallel2 &&
     (
	  cd clone_dir &&
	  git checkout parallel2 &&
	  git log --pretty=oneline | grep $PARA3 &&
	  git show $PARA3 | grep "A U Thor"
     )
'

test_expect_success 'fetch branch with replacement' '
     git branch tofetch $HASH6 &&
     (
	  cd clone_dir &&
	  git fetch origin refs/heads/tofetch:refs/heads/parallel3 &&
	  git log --pretty=oneline parallel3 >output.txt &&
	  ! grep $PARA3 output.txt &&
	  git show $PARA3 >para3.txt &&
	  grep "A U Thor" para3.txt &&
	  git fetch origin "refs/replace/*:refs/replace/*" &&
	  git log --pretty=oneline parallel3 >output.txt &&
	  grep $PARA3 output.txt &&
	  git show $PARA3 >para3.txt &&
	  grep "O Thor" para3.txt
     )
'

test_expect_success 'bisect and replacements' '
     git bisect start $HASH7 $HASH1 &&
     test "$PARA3" = "$(git rev-parse --verify HEAD)" &&
     git bisect reset &&
     GIT_NO_REPLACE_OBJECTS=1 git bisect start $HASH7 $HASH1 &&
     test "$HASH4" = "$(git rev-parse --verify HEAD)" &&
     git bisect reset &&
     git --no-replace-objects bisect start $HASH7 $HASH1 &&
     test "$HASH4" = "$(git rev-parse --verify HEAD)" &&
     git bisect reset
'

test_expect_success 'index-pack and replacements' '
	git --no-replace-objects rev-list --objects HEAD |
	git --no-replace-objects pack-objects test- &&
	git index-pack test-*.pack
'

test_expect_success 'not just commits' '
	echo replaced >file &&
	git add file &&
	REPLACED=$(git rev-parse :file) &&
	mv file file.replaced &&

	echo original >file &&
	git add file &&
	ORIGINAL=$(git rev-parse :file) &&
	git update-ref refs/replace/$ORIGINAL $REPLACED &&
	mv file file.original &&

	git checkout file &&
	test_cmp file.replaced file
'

test_expect_success 'replaced and replacement objects must be of the same type' '
	test_must_fail git replace mytag $HASH1 &&
	test_must_fail git replace HEAD^{tree} HEAD~1 &&
	BLOB=$(git rev-parse :file) &&
	test_must_fail git replace HEAD^ $BLOB
'

test_expect_success '-f option bypasses the type check' '
	git replace -f mytag $HASH1 &&
	git replace --force HEAD^{tree} HEAD~1 &&
	git replace -f HEAD^ $BLOB
'

test_expect_success 'git cat-file --batch works on replace objects' '
	git replace | grep $PARA3 &&
	echo $PARA3 | git cat-file --batch
'

test_expect_success 'test --format bogus' '
	test_must_fail git replace --format bogus >/dev/null 2>&1
'

test_expect_success 'test --format short' '
	git replace --format=short >actual &&
	git replace >expected &&
	test_cmp expected actual
'

test_expect_success 'test --format medium' '
	H1=$(git --no-replace-objects rev-parse HEAD~1) &&
	HT=$(git --no-replace-objects rev-parse HEAD^{tree}) &&
	MYTAG=$(git --no-replace-objects rev-parse mytag) &&
	{
		echo "$H1 -> $BLOB" &&
		echo "$BLOB -> $REPLACED" &&
		echo "$HT -> $H1" &&
		echo "$PARA3 -> $S" &&
		echo "$MYTAG -> $HASH1"
	} | sort >expected &&
	git replace -l --format medium | sort >actual &&
	test_cmp expected actual
'

test_expect_success 'test --format long' '
	{
		echo "$H1 (commit) -> $BLOB (blob)" &&
		echo "$BLOB (blob) -> $REPLACED (blob)" &&
		echo "$HT (tree) -> $H1 (commit)" &&
		echo "$PARA3 (commit) -> $S (commit)" &&
		echo "$MYTAG (tag) -> $HASH1 (commit)"
	} | sort >expected &&
	git replace --format=long | sort >actual &&
	test_cmp expected actual
'

test_expect_success 'setup a fake editor' '
	write_script fakeeditor <<-\EOF
		sed -e "s/A U Thor/A fake Thor/" "$1" >"$1.new"
		mv "$1.new" "$1"
	EOF
'

test_expect_success '--edit with and without already replaced object' '
	test_must_fail env GIT_EDITOR=./fakeeditor git replace --edit "$PARA3" &&
	GIT_EDITOR=./fakeeditor git replace --force --edit "$PARA3" &&
	git replace -l | grep "$PARA3" &&
	git cat-file commit "$PARA3" | grep "A fake Thor" &&
	git replace -d "$PARA3" &&
	GIT_EDITOR=./fakeeditor git replace --edit "$PARA3" &&
	git replace -l | grep "$PARA3" &&
	git cat-file commit "$PARA3" | grep "A fake Thor"
'

test_expect_success '--edit and change nothing or command failed' '
	git replace -d "$PARA3" &&
	test_must_fail env GIT_EDITOR=true git replace --edit "$PARA3" &&
	test_must_fail env GIT_EDITOR="./fakeeditor;false" git replace --edit "$PARA3" &&
	GIT_EDITOR=./fakeeditor git replace --edit "$PARA3" &&
	git replace -l | grep "$PARA3" &&
	git cat-file commit "$PARA3" | grep "A fake Thor"
'

test_expect_success 'replace ref cleanup' '
	test -n "$(git replace)" &&
	git replace -d $(git replace) &&
	test -z "$(git replace)"
'

test_expect_success '--graft with and without already replaced object' '
	test $(git log --oneline | wc -l) = 7 &&
	git replace --graft $HASH5 &&
	test $(git log --oneline | wc -l) = 3 &&
	commit_has_parents $HASH5 &&
	test_must_fail git replace --graft $HASH5 $HASH4 $HASH3 &&
	git replace --force -g $HASH5 $HASH4 $HASH3 &&
	commit_has_parents $HASH5 $HASH4 $HASH3 &&
	git replace -d $HASH5
'

test_expect_success GPG 'set up a signed commit' '
	echo "line 17" >>hello &&
	echo "line 18" >>hello &&
	git add hello &&
	test_tick &&
	git commit --quiet -S -m "hello: 2 more lines in a signed commit" &&
	HASH8=$(git rev-parse --verify HEAD) &&
	git verify-commit $HASH8
'

test_expect_success GPG '--graft with a signed commit' '
	git cat-file commit $HASH8 >orig &&
	git replace --graft $HASH8 &&
	git cat-file commit $HASH8 >repl &&
	commit_has_parents $HASH8 &&
	test_must_fail git verify-commit $HASH8 &&
	sed -n -e "/^tree /p" -e "/^author /p" -e "/^committer /p" orig >expected &&
	echo >>expected &&
	sed -e "/^$/q" repl >actual &&
	test_cmp expected actual &&
	git replace -d $HASH8
'

test_expect_success GPG 'set up a merge commit with a mergetag' '
	git reset --hard HEAD &&
	git checkout -b test_branch HEAD~2 &&
	echo "line 1 from test branch" >>hello &&
	echo "line 2 from test branch" >>hello &&
	git add hello &&
	test_tick &&
	git commit -m "hello: 2 more lines from a test branch" &&
	HASH9=$(git rev-parse --verify HEAD) &&
	git tag -s -m "tag for testing with a mergetag" test_tag HEAD &&
	git checkout master &&
	git merge -s ours test_tag &&
	HASH10=$(git rev-parse --verify HEAD) &&
	git cat-file commit $HASH10 | grep "^mergetag object"
'

test_expect_success GPG '--graft on a commit with a mergetag' '
	test_must_fail git replace --graft $HASH10 $HASH8^1 &&
	git replace --graft $HASH10 $HASH8^1 $HASH9 &&
	git replace -d $HASH10
'

test_done
