#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='mmap sliding window tests'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'rm -f .git/index*
     for i in a b c
     do
         echo $i >$i &&
         test-genrandom "$i" 32768 >>$i &&
         git update-index --add $i || return 1
     done &&
     echo d >d && cat c >>d && git update-index --add d &&
     tree=`git write-tree` &&
     commit1=`git commit-tree $tree </dev/null` &&
     git update-ref HEAD $commit1 &&
     git repack -a -d &&
     test "`git count-objects`" = "0 objects, 0 kilobytes" &&
     pack1=`ls .git/objects/pack/*.pack` &&
     test -f "$pack1"'

test_expect_success \
    'verify-pack -v, defaults' \
    'git verify-pack -v "$pack1"'

test_expect_success \
    'verify-pack -v, packedGitWindowSize == 1 page' \
    'git config core.packedGitWindowSize 512 &&
     git verify-pack -v "$pack1"'

test_expect_success \
    'verify-pack -v, packedGit{WindowSize,Limit} == 1 page' \
    'git config core.packedGitWindowSize 512 &&
     git config core.packedGitLimit 512 &&
     git verify-pack -v "$pack1"'

test_expect_success \
    'repack -a -d, packedGit{WindowSize,Limit} == 1 page' \
    'git config core.packedGitWindowSize 512 &&
     git config core.packedGitLimit 512 &&
     commit2=`git commit-tree $tree -p $commit1 </dev/null` &&
     git update-ref HEAD $commit2 &&
     git repack -a -d &&
     test "`git count-objects`" = "0 objects, 0 kilobytes" &&
     pack2=`ls .git/objects/pack/*.pack` &&
     test -f "$pack2"
     test "$pack1" \!= "$pack2"'

test_expect_success \
    'verify-pack -v, defaults' \
    'git config --unset core.packedGitWindowSize &&
     git config --unset core.packedGitLimit &&
     git verify-pack -v "$pack2"'

test_done
