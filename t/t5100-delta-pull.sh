#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test pulling deltified objects

'
. ./test-lib.sh

locate_obj='s|\(..\)|.git/objects/\1/|'

test_expect_success \
    setup \
    'cat ../README >a &&
    git-update-cache --add a &&
    a0=`git-ls-files --stage |
        sed -e '\''s/^[0-7]* \([0-9a-f]*\) .*/\1/'\''` &&

    sed -e 's/test/TEST/g' ../README >a &&
    git-update-cache a &&
    a1=`git-ls-files --stage |
        sed -e '\''s/^[0-7]* \([0-9a-f]*\) .*/\1/'\''` &&
    tree=`git-write-tree` &&
    commit=`git-commit-tree $tree </dev/null` &&
    a0f=`echo "$a0" | sed -e "$locate_obj"` &&
    a1f=`echo "$a1" | sed -e "$locate_obj"` &&
    echo commit $commit &&
    echo a0 $a0 &&
    echo a1 $a1 &&
    ls -l $a0f $a1f &&
    echo $commit >.git/HEAD &&
    git-mkdelta -v $a0 $a1 &&
    ls -l $a0f $a1f'

# Now commit has a tree that records delitified "a" whose SHA1 is a1.
# Create a new repo and pull this commit into it.

test_expect_success \
    'setup and cd into new repo' \
    'mkdir dest && cd dest && rm -fr .git && git-init-db'
     
test_expect_success \
    'pull from deltified repo into a new repo without -d' \
    'rm -fr .git a && git-init-db &&
     git-local-pull -v -a $commit ../.git/ &&
     git-cat-file blob $a1 >a &&
     diff -u a ../a'

test_expect_failure \
    'pull from deltified repo into a new repo with -d' \
    'rm -fr .git a && git-init-db &&
     git-local-pull -v -a -d $commit ../.git/ &&
     git-cat-file blob $a1 >a &&
     diff -u a ../a'

test_expect_failure \
    'pull from deltified repo after delta failure without --recover' \
    'rm -f a &&
     git-local-pull -v -a $commit ../.git/ &&
     git-cat-file blob $a1 >a &&
     diff -u a ../a'

test_expect_success \
    'pull from deltified repo after delta failure with --recover' \
    'rm -f a &&
     git-local-pull -v -a --recover $commit ../.git/ &&
     git-cat-file blob $a1 >a &&
     diff -u a ../a'

test_expect_success \
    'missing-tree or missing-blob should be re-fetched without --recover' \
    'rm -f a $a0f $a1f &&
     git-local-pull -v -a $commit ../.git/ &&
     git-cat-file blob $a1 >a &&
     diff -u a ../a'

test_done

