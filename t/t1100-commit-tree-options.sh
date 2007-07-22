#!/bin/sh
#
# Copyright (C) 2005 Rene Scharfe
#

test_description='git commit-tree options test

This test checks that git commit-tree can create a specific commit
object by defining all environment variables that it understands.
'

. ./test-lib.sh

cat >expected <<EOF
tree 4b825dc642cb6eb9a060e54bf8d69288fbee4904
author Author Name <author@email> 1117148400 +0000
committer Committer Name <committer@email> 1117150200 +0000

comment text
EOF

test_expect_success \
    'test preparation: write empty tree' \
    'git write-tree >treeid'

test_expect_success \
    'construct commit' \
    'echo comment text |
     GIT_AUTHOR_NAME="Author Name" \
     GIT_AUTHOR_EMAIL="author@email" \
     GIT_AUTHOR_DATE="2005-05-26 23:00" \
     GIT_COMMITTER_NAME="Committer Name" \
     GIT_COMMITTER_EMAIL="committer@email" \
     GIT_COMMITTER_DATE="2005-05-26 23:30" \
     TZ=GMT git commit-tree `cat treeid` >commitid 2>/dev/null'

test_expect_success \
    'read commit' \
    'git cat-file commit `cat commitid` >commit'

test_expect_success \
    'compare commit' \
    'diff expected commit'

test_done
