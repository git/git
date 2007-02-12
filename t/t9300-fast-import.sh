#!/bin/sh
#
# Copyright (c) 2007 Shawn Pearce
#

test_description='test git-fast-import utility'
. ./test-lib.sh
. ../diff-lib.sh ;# test-lib chdir's into trash

file2_data='file2
second line of EOF'

file3_data='EOF
in 3rd file
 END'

file4_data=abcd
file4_len=4

file5_data='an inline file.
  we should see it later.'

file6_data='#!/bin/sh
echo "$@"'

###
### series A
###

test_tick
cat >input <<INPUT_END
blob
mark :2
data <<EOF
$file2_data
EOF

blob
mark :3
data <<END
$file3_data
END

blob
mark :4
data $file4_len
$file4_data
commit refs/heads/master
mark :5
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
initial
COMMIT

M 644 :2 file2
M 644 :3 file3
M 755 :4 file4

INPUT_END
test_expect_success \
    'A: create pack from stdin' \
    'git-fast-import --export-marks=marks.out <input &&
	 git-whatchanged master'
test_expect_success \
	'A: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'

cat >expect <<EOF
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

initial
EOF
test_expect_success \
	'A: verify commit' \
	'git-cat-file commit master | sed 1d >actual &&
	diff -u expect actual'

cat >expect <<EOF
100644 blob file2
100644 blob file3
100755 blob file4
EOF
test_expect_success \
	'A: verify tree' \
	'git-cat-file -p master^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	 diff -u expect actual'

echo "$file2_data" >expect
test_expect_success \
	'A: verify file2' \
	'git-cat-file blob master:file2 >actual && diff -u expect actual'

echo "$file3_data" >expect
test_expect_success \
	'A: verify file3' \
	'git-cat-file blob master:file3 >actual && diff -u expect actual'

printf "$file4_data" >expect
test_expect_success \
	'A: verify file4' \
	'git-cat-file blob master:file4 >actual && diff -u expect actual'

cat >expect <<EOF
:2 `git-rev-parse --verify master:file2`
:3 `git-rev-parse --verify master:file3`
:4 `git-rev-parse --verify master:file4`
:5 `git-rev-parse --verify master^0`
EOF
test_expect_success \
	'A: verify marks output' \
	'diff -u expect marks.out'

###
### series B
###

test_tick
cat >input <<INPUT_END
commit refs/heads/branch
mark :1
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
corrupt
COMMIT

from refs/heads/master
M 755 0000000000000000000000000000000000000001 zero1

INPUT_END
test_expect_failure \
    'B: fail on invalid blob sha1' \
    'git-fast-import <input'
rm -f .git/objects/pack_* .git/objects/index_*

###
### series C
###

newf=`echo hi newf | git-hash-object -w --stdin`
oldf=`git-rev-parse --verify master:file2`
test_tick
cat >input <<INPUT_END
commit refs/heads/branch
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
second
COMMIT

from refs/heads/master
M 644 $oldf file2/oldf
M 755 $newf file2/newf
D file3

INPUT_END
test_expect_success \
    'C: incremental import create pack from stdin' \
    'git-fast-import <input &&
	 git-whatchanged branch'
test_expect_success \
	'C: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'
test_expect_success \
	'C: validate reuse existing blob' \
	'test $newf = `git-rev-parse --verify branch:file2/newf`
	 test $oldf = `git-rev-parse --verify branch:file2/oldf`'

cat >expect <<EOF
parent `git-rev-parse --verify master^0`
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

second
EOF
test_expect_success \
	'C: verify commit' \
	'git-cat-file commit branch | sed 1d >actual &&
	 diff -u expect actual'

cat >expect <<EOF
:000000 100755 0000000000000000000000000000000000000000 f1fb5da718392694d0076d677d6d0e364c79b0bc A	file2/newf
:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 R100	file2	file2/oldf
:100644 000000 0d92e9f3374ae2947c23aa477cbc68ce598135f1 0000000000000000000000000000000000000000 D	file3
EOF
git-diff-tree -M -r master branch >actual
test_expect_success \
	'C: validate rename result' \
	'compare_diff_raw expect actual'

###
### series D
###

test_tick
cat >input <<INPUT_END
commit refs/heads/branch
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
third
COMMIT

from refs/heads/branch^0
M 644 inline newdir/interesting
data <<EOF
$file5_data
EOF

M 755 inline newdir/exec.sh
data <<EOF
$file6_data
EOF

INPUT_END
test_expect_success \
    'D: inline data in commit' \
    'git-fast-import <input &&
	 git-whatchanged branch'
test_expect_success \
	'D: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'

cat >expect <<EOF
:000000 100755 0000000000000000000000000000000000000000 35a59026a33beac1569b1c7f66f3090ce9c09afc A	newdir/exec.sh
:000000 100644 0000000000000000000000000000000000000000 046d0371e9220107917db0d0e030628de8a1de9b A	newdir/interesting
EOF
git-diff-tree -M -r branch^ branch >actual
test_expect_success \
	'D: validate new files added' \
	'compare_diff_raw expect actual'

echo "$file5_data" >expect
test_expect_success \
	'D: verify file5' \
	'git-cat-file blob branch:newdir/interesting >actual &&
	 diff -u expect actual'

echo "$file6_data" >expect
test_expect_success \
	'D: verify file6' \
	'git-cat-file blob branch:newdir/exec.sh >actual &&
	 diff -u expect actual'

###
### series E
###

cat >input <<INPUT_END
commit refs/heads/branch
author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> Tue Feb 6 11:22:18 2007 -0500
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> Tue Feb 6 12:35:02 2007 -0500
data <<COMMIT
RFC 2822 type date
COMMIT

from refs/heads/branch^0

INPUT_END
test_expect_failure \
    'E: rfc2822 date, --date-format=raw' \
    'git-fast-import --date-format=raw <input'
test_expect_success \
    'E: rfc2822 date, --date-format=rfc2822' \
    'git-fast-import --date-format=rfc2822 <input'
test_expect_success \
	'E: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'

cat >expect <<EOF
author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> 1170778938 -0500
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1170783302 -0500

RFC 2822 type date
EOF
test_expect_success \
	'E: verify commit' \
	'git-cat-file commit branch | sed 1,2d >actual &&
	diff -u expect actual'

###
### series F
###

old_branch=`git-rev-parse --verify branch^0`
test_tick
cat >input <<INPUT_END
commit refs/heads/branch
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
losing things already?
COMMIT

from refs/heads/branch~1

reset refs/heads/other
from refs/heads/branch

INPUT_END
test_expect_success \
    'F: non-fast-forward update skips' \
    'if git-fast-import <input
	 then
		echo BAD gfi did not fail
		return 1
	 else
		if test $old_branch = `git-rev-parse --verify branch^0`
		then
			: branch unaffected and failure returned
			return 0
		else
			echo BAD gfi changed branch $old_branch
			return 1
		fi
	 fi
	'
test_expect_success \
	'F: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'

cat >expect <<EOF
tree `git-rev-parse branch~1^{tree}`
parent `git-rev-parse branch~1`
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

losing things already?
EOF
test_expect_success \
	'F: verify other commit' \
	'git-cat-file commit other >actual &&
	diff -u expect actual'

###
### series G
###

old_branch=`git-rev-parse --verify branch^0`
test_tick
cat >input <<INPUT_END
commit refs/heads/branch
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
losing things already?
COMMIT

from refs/heads/branch~1

INPUT_END
test_expect_success \
    'G: non-fast-forward update forced' \
    'git-fast-import --force <input'
test_expect_success \
	'G: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'
test_expect_success \
	'G: branch changed, but logged' \
	'test $old_branch != `git-rev-parse --verify branch^0` &&
	 test $old_branch = `git-rev-parse --verify branch@{1}`'

###
### series H
###

test_tick
cat >input <<INPUT_END
commit refs/heads/H
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
third
COMMIT

from refs/heads/branch^0
M 644 inline i-will-die
data <<EOF
this file will never exist.
EOF

deleteall
M 644 inline h/e/l/lo
data <<EOF
$file5_data
EOF

INPUT_END
test_expect_success \
    'H: deletall, add 1' \
    'git-fast-import <input &&
	 git-whatchanged H'
test_expect_success \
	'H: verify pack' \
	'for p in .git/objects/pack/*.pack;do git-verify-pack $p||exit;done'

cat >expect <<EOF
:100755 000000 f1fb5da718392694d0076d677d6d0e364c79b0bc 0000000000000000000000000000000000000000 D	file2/newf
:100644 000000 7123f7f44e39be127c5eb701e5968176ee9d78b1 0000000000000000000000000000000000000000 D	file2/oldf
:100755 000000 85df50785d62d3b05ab03d9cbf7e4a0b49449730 0000000000000000000000000000000000000000 D	file4
:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 R100	newdir/interesting	h/e/l/lo
:100755 000000 e74b7d465e52746be2b4bae983670711e6e66657 0000000000000000000000000000000000000000 D	newdir/exec.sh
EOF
git-diff-tree -M -r H^ H >actual
test_expect_success \
	'H: validate old files removed, new files added' \
	'compare_diff_raw expect actual'

echo "$file5_data" >expect
test_expect_success \
	'H: verify file' \
	'git-cat-file blob H:h/e/l/lo >actual &&
	 diff -u expect actual'

###
### series I
###

cat >input <<INPUT_END
commit refs/heads/export-boundary
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
we have a border.  its only 40 characters wide.
COMMIT

from refs/heads/branch

INPUT_END
test_expect_success \
    'I: export-pack-edges' \
    'git-fast-import --export-pack-edges=edges.list <input'

cat >expect <<EOF
.git/objects/pack/pack-.pack: `git-rev-parse --verify export-boundary`
EOF
test_expect_success \
	'I: verify edge list' \
	'sed -e s/pack-.*pack/pack-.pack/ edges.list >actual &&
	 diff -u expect actual'

###
### series J
###

cat >input <<INPUT_END
commit refs/heads/J
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
create J
COMMIT

from refs/heads/branch

reset refs/heads/J

commit refs/heads/J
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
initialize J
COMMIT

INPUT_END
test_expect_success \
    'J: reset existing branch creates empty commit' \
    'git-fast-import <input'
test_expect_success \
	'J: branch has 1 commit, empty tree' \
	'test 1 = `git-rev-list J | wc -l` &&
	 test 0 = `git ls-tree J | wc -l`'

###
### series K
###

cat >input <<INPUT_END
commit refs/heads/K
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
create K
COMMIT

from refs/heads/branch

commit refs/heads/K
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
redo K
COMMIT

from refs/heads/branch^1

INPUT_END
test_expect_success \
    'K: reinit branch with from' \
    'git-fast-import <input'
test_expect_success \
    'K: verify K^1 = branch^1' \
    'test `git-rev-parse --verify branch^1` \
		= `git-rev-parse --verify K^1`'

test_done
