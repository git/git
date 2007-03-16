#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-pack-object

'
. ./test-lib.sh

TRASH=`pwd`

test_expect_success \
    'setup' \
    'rm -f .git/index*
     for i in a b c
     do
	     dd if=/dev/zero bs=4k count=1 | tr "\\0" $i >$i &&
	     git-update-index --add $i || return 1
     done &&
     cat c >d && echo foo >>d && git-update-index --add d &&
     tree=`git-write-tree` &&
     commit=`git-commit-tree $tree </dev/null` && {
	 echo $tree &&
	 echo $commit &&
	 git-ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
     } >obj-list && {
	 git-diff-tree --root -p $commit &&
	 while read object
	 do
	    t=`git-cat-file -t $object` &&
	    git-cat-file $t $object || return 1
	 done <obj-list
     } >expect'

test_expect_success \
    'pack without delta' \
    'packname_1=$(git-pack-objects --window=0 test-1 <obj-list)'

rm -fr .git2
mkdir .git2

test_expect_success \
    'unpack without delta' \
    "GIT_OBJECT_DIRECTORY=.git2/objects &&
     export GIT_OBJECT_DIRECTORY &&
     git-init &&
     git-unpack-objects -n <test-1-${packname_1}.pack &&
     git-unpack-objects <test-1-${packname_1}.pack"

unset GIT_OBJECT_DIRECTORY
cd "$TRASH/.git2"

test_expect_success \
    'check unpack without delta' \
    '(cd ../.git && find objects -type f -print) |
     while read path
     do
         cmp $path ../.git/$path || {
	     echo $path differs.
	     return 1
	 }
     done'
cd "$TRASH"

test_expect_success \
    'pack with delta' \
    'pwd &&
     packname_2=$(git-pack-objects test-2 <obj-list)'

rm -fr .git2
mkdir .git2

test_expect_success \
    'unpack with delta' \
    'GIT_OBJECT_DIRECTORY=.git2/objects &&
     export GIT_OBJECT_DIRECTORY &&
     git-init &&
     git-unpack-objects -n <test-2-${packname_2}.pack &&
     git-unpack-objects <test-2-${packname_2}.pack'

unset GIT_OBJECT_DIRECTORY
cd "$TRASH/.git2"
test_expect_success \
    'check unpack with delta' \
    '(cd ../.git && find objects -type f -print) |
     while read path
     do
         cmp $path ../.git/$path || {
	     echo $path differs.
	     return 1
	 }
     done'
cd "$TRASH"

rm -fr .git2
mkdir .git2

test_expect_success \
    'use packed objects' \
    'GIT_OBJECT_DIRECTORY=.git2/objects &&
     export GIT_OBJECT_DIRECTORY &&
     git-init &&
     cp test-1-${packname_1}.pack test-1-${packname_1}.idx .git2/objects/pack && {
	 git-diff-tree --root -p $commit &&
	 while read object
	 do
	    t=`git-cat-file -t $object` &&
	    git-cat-file $t $object || return 1
	 done <obj-list
    } >current &&
    diff expect current'


test_expect_success \
    'use packed deltified objects' \
    'GIT_OBJECT_DIRECTORY=.git2/objects &&
     export GIT_OBJECT_DIRECTORY &&
     rm .git2/objects/pack/test-* &&
     cp test-2-${packname_2}.pack test-2-${packname_2}.idx .git2/objects/pack && {
	 git-diff-tree --root -p $commit &&
	 while read object
	 do
	    t=`git-cat-file -t $object` &&
	    git-cat-file $t $object || return 1
	 done <obj-list
    } >current &&
    diff expect current'

unset GIT_OBJECT_DIRECTORY

test_expect_success \
    'verify pack' \
    'git-verify-pack test-1-${packname_1}.idx test-2-${packname_2}.idx'

test_expect_success \
    'corrupt a pack and see if verify catches' \
    'cp test-1-${packname_1}.idx test-3.idx &&
     cp test-2-${packname_2}.pack test-3.pack &&
     if git-verify-pack test-3.idx
     then false
     else :;
     fi &&

     : PACK_SIGNATURE &&
     cp test-1-${packname_1}.pack test-3.pack &&
     dd if=/dev/zero of=test-3.pack count=1 bs=1 conv=notrunc seek=2 &&
     if git-verify-pack test-3.idx
     then false
     else :;
     fi &&

     : PACK_VERSION &&
     cp test-1-${packname_1}.pack test-3.pack &&
     dd if=/dev/zero of=test-3.pack count=1 bs=1 conv=notrunc seek=7 &&
     if git-verify-pack test-3.idx
     then false
     else :;
     fi &&

     : TYPE/SIZE byte of the first packed object data &&
     cp test-1-${packname_1}.pack test-3.pack &&
     dd if=/dev/zero of=test-3.pack count=1 bs=1 conv=notrunc seek=12 &&
     if git-verify-pack test-3.idx
     then false
     else :;
     fi &&

     : sum of the index file itself &&
     l=`wc -c <test-3.idx` &&
     l=`expr $l - 20` &&
     cp test-1-${packname_1}.pack test-3.pack &&
     dd if=/dev/zero of=test-3.idx count=20 bs=1 conv=notrunc seek=$l &&
     if git-verify-pack test-3.pack
     then false
     else :;
     fi &&

     :'

test_expect_success \
    'build pack index for an existing pack' \
    'cp test-1-${packname_1}.pack test-3.pack &&
     git-index-pack -o tmp.idx test-3.pack &&
     cmp tmp.idx test-1-${packname_1}.idx &&

     git-index-pack test-3.pack &&
     cmp test-3.idx test-1-${packname_1}.idx &&

     cp test-2-${packname_2}.pack test-3.pack &&
     git-index-pack -o tmp.idx test-2-${packname_2}.pack &&
     cmp tmp.idx test-2-${packname_2}.idx &&

     git-index-pack test-3.pack &&
     cmp test-3.idx test-2-${packname_2}.idx &&

     :'

test_done
