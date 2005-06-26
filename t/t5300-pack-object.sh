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
	     git-update-cache --add $i || exit
     done &&
     cat c >d && echo foo >>d && git-update-cache --add d &&
     tree=`git-write-tree` && {
	 echo $tree &&
	 git-ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
     } >obj-list'

test_expect_success \
    'pack without delta' \
    'git-pack-objects --window=0 test-1 <obj-list'

rm -fr .git2
mkdir .git2

test_expect_success \
    'unpack without delta' \
    'GIT_OBJECT_DIRECTORY=.git2/objects &&
     export GIT_OBJECT_DIRECTORY &&
     git-init-db &&
     git-unpack-objects test-1'

unset GIT_OBJECT_DIRECTORY
cd $TRASH/.git2

test_expect_success \
    'check unpack without delta' \
    '(cd ../.git && find objects -type f -print) |
     while read path
     do
         cmp $path ../.git/$path || {
	     echo $path differs.
	     exit 1
	 }
     done'
cd $TRASH

test_expect_success \
    'pack with delta' \
    'pwd &&
     git-pack-objects test-2 <obj-list'

rm -fr .git2
mkdir .git2

test_expect_success \
    'unpack with delta' \
    'GIT_OBJECT_DIRECTORY=.git2/objects &&
     export GIT_OBJECT_DIRECTORY &&
     git-init-db &&
     git-unpack-objects test-2'

unset GIT_OBJECT_DIRECTORY
cd $TRASH/.git2
test_expect_success \
    'check unpack with delta' \
    '(cd ../.git && find objects -type f -print) |
     while read path
     do
         cmp $path ../.git/$path || {
	     echo $path differs.
	     exit 1
	 }
     done'
cd $TRASH

test_done
