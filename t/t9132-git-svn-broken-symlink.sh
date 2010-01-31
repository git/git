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
Text-content-length: 4
Text-content-md5: 912ec803b2ce49e4a541068d495ab570
Content-length: 37

K 11
svn:special
V 1
*
PROPS-END
asdf

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

test_expect_success SYMLINKS '"bar" is a symlink that points to "asdf"' '
	test -L x/bar &&
	(cd x && test xasdf = x"`git cat-file blob HEAD:bar`")
'

test_expect_success 'get "bar" => symlink fix from svn' '
	(cd x && git svn rebase)
'

test_expect_success SYMLINKS '"bar" remains a proper symlink' '
	test -L x/bar &&
	(cd x && test xdoink = x"`git cat-file blob HEAD:bar`")
'

test_done
