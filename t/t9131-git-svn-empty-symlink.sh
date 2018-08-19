#!/bin/sh

test_description='test that git handles an svn repository with empty symlinks'

. ./lib-git-svn.sh
test_expect_success 'load svn dumpfile' '
	svnadmin load "$rawsvnrepo" <<EOF
SVN-fs-dump-format-version: 2

UUID: 60780f9a-7df5-43b4-83ab-60e2c0673ef7

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2008-11-26T07:17:27.590577Z
PROPS-END

Revision-number: 1
Prop-content-length: 111
Content-length: 111

K 7
svn:log
V 4
test
K 10
svn:author
V 12
normalperson
K 8
svn:date
V 27
2008-11-26T07:18:03.511836Z
PROPS-END

Node-path: bar
Node-kind: file
Node-action: add
Prop-content-length: 33
Text-content-length: 0
Text-content-md5: d41d8cd98f00b204e9800998ecf8427e
Content-length: 33

K 11
svn:special
V 1
*
PROPS-END

Revision-number: 2
Prop-content-length: 121
Content-length: 121

K 7
svn:log
V 13
bar => doink

K 10
svn:author
V 12
normalperson
K 8
svn:date
V 27
2008-11-27T03:55:31.601672Z
PROPS-END

Node-path: bar
Node-kind: file
Node-action: change
Text-content-length: 10
Text-content-md5: 92ca4fe7a9721f877f765c252dcd66c9
Content-length: 10

link doink

EOF
'

test_expect_success 'clone using git svn' 'git svn clone -r1 "$svnrepo" x'
test_expect_success 'enable broken symlink workaround' \
  '(cd x && git config svn.brokenSymlinkWorkaround true)'
test_expect_success '"bar" is an empty file' 'test_must_be_empty x/bar'
test_expect_success 'get "bar" => symlink fix from svn' \
		'(cd x && git svn rebase)'
test_expect_success SYMLINKS '"bar" becomes a symlink' 'test -h x/bar'


test_expect_success 'clone using git svn' 'git svn clone -r1 "$svnrepo" y'
test_expect_success 'disable broken symlink workaround' \
  '(cd y && git config svn.brokenSymlinkWorkaround false)'
test_expect_success '"bar" is an empty file' 'test_must_be_empty y/bar'
test_expect_success 'get "bar" => symlink fix from svn' \
		'(cd y && git svn rebase)'
test_expect_success '"bar" does not become a symlink' '! test -L y/bar'

# svn.brokenSymlinkWorkaround is unset
test_expect_success 'clone using git svn' 'git svn clone -r1 "$svnrepo" z'
test_expect_success '"bar" is an empty file' 'test_must_be_empty z/bar'
test_expect_success 'get "bar" => symlink fix from svn' \
		'(cd z && git svn rebase)'
test_expect_success '"bar" does not become a symlink' '! test -L z/bar'


test_done
