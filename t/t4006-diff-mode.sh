#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test mode change diffs.

'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'echo frotz >rezrov &&
     git update-index --add rezrov &&
     tree=`git write-tree` &&
     echo $tree'

test_expect_success \
    'chmod' \
    'test_chmod +x rezrov &&
     git diff-index $tree >current'

sed -e 's/\(:100644 100755\) \('"$_x40"'\) \2 /\1 X X /' <current >check
echo ":100644 100755 X X M	rezrov" >expected

test_expect_success \
    'verify' \
    'test_cmp expected check'

test_done
