#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='but cummit-tree options test

This test checks that but cummit-tree can create a specific cummit
object by defining all environment variables that it understands.

Also make sure that command line parser understands the normal
"flags first and then non flag arguments" command line.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

cat >expected <<EOF
tree $EMPTY_TREE
author Author Name <author@email> 1117148400 +0000
cummitter cummitter Name <cummitter@email> 1117150200 +0000

comment text
EOF

test_expect_success \
    'test preparation: write empty tree' \
    'but write-tree >treeid'

test_expect_success \
    'construct cummit' \
    'echo comment text |
     BUT_AUTHOR_NAME="Author Name" \
     BUT_AUTHOR_EMAIL="author@email" \
     BUT_AUTHOR_DATE="2005-05-26 23:00" \
     BUT_CUMMITTER_NAME="cummitter Name" \
     BUT_CUMMITTER_EMAIL="cummitter@email" \
     BUT_CUMMITTER_DATE="2005-05-26 23:30" \
     TZ=GMT but cummit-tree $(cat treeid) >cummitid 2>/dev/null'

test_expect_success \
    'read cummit' \
    'but cat-file cummit $(cat cummitid) >cummit'

test_expect_success \
    'compare cummit' \
    'test_cmp expected cummit'


test_expect_success 'flags and then non flags' '
	test_tick &&
	echo comment text |
	but cummit-tree $(cat treeid) >cummitid &&
	echo comment text |
	but cummit-tree $(cat treeid) -p $(cat cummitid) >childid-1 &&
	echo comment text |
	but cummit-tree -p $(cat cummitid) $(cat treeid) >childid-2 &&
	test_cmp childid-1 childid-2 &&
	but cummit-tree $(cat treeid) -m foo >childid-3 &&
	but cummit-tree -m foo $(cat treeid) >childid-4 &&
	test_cmp childid-3 childid-4
'

test_done
