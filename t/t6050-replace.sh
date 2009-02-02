#!/bin/sh
#
# Copyright (c) 2008 Christian Couder
#
test_description='Tests replace refs functionality'

exec </dev/null

. ./test-lib.sh

add_and_commit_file()
{
    _file="$1"
    _msg="$2"

    git add $_file || return $?
    test_tick || return $?
    git commit --quiet -m "$_file: $_msg"
}

HASH1=
HASH2=
HASH3=
HASH4=
HASH5=
HASH6=
HASH7=

test_expect_success 'set up buggy branch' '
     echo "line 1" >> hello &&
     echo "line 2" >> hello &&
     echo "line 3" >> hello &&
     echo "line 4" >> hello &&
     add_and_commit_file hello "4 lines" &&
     HASH1=$(git rev-parse --verify HEAD) &&
     echo "line BUG" >> hello &&
     echo "line 6" >> hello &&
     echo "line 7" >> hello &&
     echo "line 8" >> hello &&
     add_and_commit_file hello "4 more lines with a BUG" &&
     HASH2=$(git rev-parse --verify HEAD) &&
     echo "line 9" >> hello &&
     echo "line 10" >> hello &&
     add_and_commit_file hello "2 more lines" &&
     HASH3=$(git rev-parse --verify HEAD) &&
     echo "line 11" >> hello &&
     add_and_commit_file hello "1 more line" &&
     HASH4=$(git rev-parse --verify HEAD) &&
     sed -e "s/BUG/5/" hello > hello.new &&
     mv hello.new hello &&
     add_and_commit_file hello "BUG fixed" &&
     HASH5=$(git rev-parse --verify HEAD) &&
     echo "line 12" >> hello &&
     echo "line 13" >> hello &&
     add_and_commit_file hello "2 more lines" &&
     HASH6=$(git rev-parse --verify HEAD)
     echo "line 14" >> hello &&
     echo "line 15" >> hello &&
     echo "line 16" >> hello &&
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
     git fsck master > fsck_master.out &&
     grep "dangling commit $R" fsck_master.out &&
     grep "dangling tag $(cat .git/refs/tags/mytag)" fsck_master.out &&
     test -z "$(git fsck)"
'

test_expect_success 'repack, clone and fetch work' '
     git repack -a -d &&
     git clone --no-hardlinks . clone_dir &&
     cd clone_dir &&
     git show HEAD~5 | grep "A U Thor" &&
     git show $HASH2 | grep "A U Thor" &&
     git cat-file commit $R &&
     git repack -a -d &&
     test_must_fail git cat-file commit $R &&
     git fetch ../ "refs/replace/*:refs/replace/*" &&
     git show HEAD~5 | grep "O Thor" &&
     git show $HASH2 | grep "O Thor" &&
     git cat-file commit $R &&
     cd ..
'

test_expect_success '"git replace" listing and deleting' '
     test "$HASH2" = "$(git replace -l)" &&
     test "$HASH2" = "$(git replace)" &&
     aa=${HASH2%??????????????????????????????????????} &&
     test "$HASH2" = "$(git replace -l "$aa*")" &&
     test_must_fail git replace -d $R &&
     test_must_fail git replace -d &&
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

#
#
test_done
