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

if [ "$(git config --get core.filemode)" = false ]
then
	say 'filemode disabled on the filesystem, using update-index --chmod=+x'
	test_expect_success \
	    'git update-index --chmod=+x' \
	    'git update-index rezrov &&
	     git update-index --chmod=+x rezrov &&
	     git diff-index $tree >current'
else
	test_expect_success \
	    'chmod' \
	    'chmod +x rezrov &&
	     git update-index rezrov &&
	     git diff-index $tree >current'
fi

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
sed -e 's/\(:100644 100755\) \('"$_x40"'\) \2 /\1 X X /' <current >check
echo ":100644 100755 X X M	rezrov" >expected

test_expect_success \
    'verify' \
    'git diff expected check'

test_done
