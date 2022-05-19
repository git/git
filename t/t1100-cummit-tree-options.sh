#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='git cummit-tree options test

This test checks that git cummit-tree can create a specific cummit
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
    'git write-tree >treeid'

test_expect_success \
    'construct cummit' \
    'echo comment text |
     GIT_AUTHOR_NAME="Author Name" \
     GIT_AUTHOR_EMAIL="author@email" \
     GIT_AUTHOR_DATE="2005-05-26 23:00" \
     GIT_CUMMITTER_NAME="cummitter Name" \
     GIT_CUMMITTER_EMAIL="cummitter@email" \
     GIT_CUMMITTER_DATE="2005-05-26 23:30" \
     TZ=GMT git cummit-tree $(cat treeid) >cummitid 2>/dev/null'

test_expect_success \
    'read cummit' \
    'git cat-file cummit $(cat cummitid) >cummit'

test_expect_success \
    'compare cummit' \
    'test_cmp expected cummit'


test_expect_success 'flags and then non flags' '
	test_tick &&
	echo comment text |
	git cummit-tree $(cat treeid) >cummitid &&
	echo comment text |
	git cummit-tree $(cat treeid) -p $(cat cummitid) >childid-1 &&
	echo comment text |
	git cummit-tree -p $(cat cummitid) $(cat treeid) >childid-2 &&
	test_cmp childid-1 childid-2 &&
	git cummit-tree $(cat treeid) -m foo >childid-3 &&
	git cummit-tree -m foo $(cat treeid) >childid-4 &&
	test_cmp childid-3 childid-4
'

test_done
