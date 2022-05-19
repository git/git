#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='mmap sliding window tests'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'rm -f .but/index* &&
     for i in a b c
     do
         echo $i >$i &&
	 test-tool genrandom "$i" 32768 >>$i &&
         but update-index --add $i || return 1
     done &&
     echo d >d && cat c >>d && but update-index --add d &&
     tree=$(but write-tree) &&
     cummit1=$(but cummit-tree $tree </dev/null) &&
     but update-ref HEAD $cummit1 &&
     but repack -a -d &&
     test "$(but count-objects)" = "0 objects, 0 kilobytes" &&
     pack1=$(ls .but/objects/pack/*.pack) &&
     test -f "$pack1"'

test_expect_success \
    'verify-pack -v, defaults' \
    'but verify-pack -v "$pack1"'

test_expect_success \
    'verify-pack -v, packedGitWindowSize == 1 page' \
    'but config core.packedGitWindowSize 512 &&
     but verify-pack -v "$pack1"'

test_expect_success \
    'verify-pack -v, packedGit{WindowSize,Limit} == 1 page' \
    'but config core.packedGitWindowSize 512 &&
     but config core.packedGitLimit 512 &&
     but verify-pack -v "$pack1"'

test_expect_success \
    'repack -a -d, packedGit{WindowSize,Limit} == 1 page' \
    'but config core.packedGitWindowSize 512 &&
     but config core.packedGitLimit 512 &&
     cummit2=$(but cummit-tree $tree -p $cummit1 </dev/null) &&
     but update-ref HEAD $cummit2 &&
     but repack -a -d &&
     test "$(but count-objects)" = "0 objects, 0 kilobytes" &&
     pack2=$(ls .but/objects/pack/*.pack) &&
     test -f "$pack2" &&
     test "$pack1" \!= "$pack2"'

test_expect_success \
    'verify-pack -v, defaults' \
    'but config --unset core.packedGitWindowSize &&
     but config --unset core.packedGitLimit &&
     but verify-pack -v "$pack2"'

test_done
