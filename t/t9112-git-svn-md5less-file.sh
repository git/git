#!/bin/sh

test_description='test that git handles an svn repository with missing md5sums'

. ./lib-git-svn.sh

# Loading a node from a svn dumpfile without a Text-Content-Length
# field causes svn to neglect to store or report an md5sum.  (it will
# calculate one if you had put Text-Content-Length: 0).  This showed
# up in a repository creted with cvs2svn.

cat > dumpfile.svn <<EOF
SVN-fs-dump-format-version: 1

Revision-number: 1
Prop-content-length: 98
Content-length: 98

K 7
svn:log
V 0

K 10
svn:author
V 4
test
K 8
svn:date
V 27
2007-05-06T12:37:01.153339Z
PROPS-END

Node-path: md5less-file
Node-kind: file
Node-action: add
Prop-content-length: 10
Content-length: 10

PROPS-END

EOF

test_expect_success 'load svn dumpfile' "svnadmin load $rawsvnrepo < dumpfile.svn"

test_expect_success 'initialize git-svn' "git-svn init $svnrepo"
test_expect_success 'fetch revisions from svn' 'git-svn fetch'
test_done
