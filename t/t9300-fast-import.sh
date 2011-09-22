#!/bin/sh
#
# Copyright (c) 2007 Shawn Pearce
#

test_description='test git fast-import utility'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh ;# test-lib chdir's into trash

# Print $1 bytes from stdin to stdout.
#
# This could be written as "head -c $1", but IRIX "head" does not
# support the -c option.
head_c () {
	perl -e '
		my $len = $ARGV[1];
		while ($len > 0) {
			my $s;
			my $nread = sysread(STDIN, $s, $len);
			die "cannot read: $!" unless defined($nread);
			print $s;
			$len -= $nread;
		}
	' - "$1"
}

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

>empty

test_expect_success 'setup: have pipes?' '
	rm -f frob &&
	if mkfifo frob
	then
		test_set_prereq PIPE
	fi
'

###
### series A
###

test_tick

test_expect_success 'empty stream succeeds' '
	git fast-import </dev/null
'

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

tag series-A
from :5
data <<EOF
An annotated tag without a tagger
EOF

tag series-A-blob
from :3
data <<EOF
An annotated tag that annotates a blob.
EOF

INPUT_END
test_expect_success \
    'A: create pack from stdin' \
    'git fast-import --export-marks=marks.out <input &&
	 git whatchanged master'
test_expect_success \
	'A: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'

cat >expect <<EOF
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

initial
EOF
test_expect_success \
	'A: verify commit' \
	'git cat-file commit master | sed 1d >actual &&
	test_cmp expect actual'

cat >expect <<EOF
100644 blob file2
100644 blob file3
100755 blob file4
EOF
test_expect_success \
	'A: verify tree' \
	'git cat-file -p master^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	 test_cmp expect actual'

echo "$file2_data" >expect
test_expect_success \
	'A: verify file2' \
	'git cat-file blob master:file2 >actual && test_cmp expect actual'

echo "$file3_data" >expect
test_expect_success \
	'A: verify file3' \
	'git cat-file blob master:file3 >actual && test_cmp expect actual'

printf "$file4_data" >expect
test_expect_success \
	'A: verify file4' \
	'git cat-file blob master:file4 >actual && test_cmp expect actual'

cat >expect <<EOF
object $(git rev-parse refs/heads/master)
type commit
tag series-A

An annotated tag without a tagger
EOF
test_expect_success 'A: verify tag/series-A' '
	git cat-file tag tags/series-A >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
object $(git rev-parse refs/heads/master:file3)
type blob
tag series-A-blob

An annotated tag that annotates a blob.
EOF
test_expect_success 'A: verify tag/series-A-blob' '
	git cat-file tag tags/series-A-blob >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
:2 `git rev-parse --verify master:file2`
:3 `git rev-parse --verify master:file3`
:4 `git rev-parse --verify master:file4`
:5 `git rev-parse --verify master^0`
EOF
test_expect_success \
	'A: verify marks output' \
	'test_cmp expect marks.out'

test_expect_success \
	'A: verify marks import' \
	'git fast-import \
		--import-marks=marks.out \
		--export-marks=marks.new \
		</dev/null &&
	test_cmp expect marks.new'

test_tick
new_blob=$(echo testing | git hash-object --stdin)
cat >input <<INPUT_END
tag series-A-blob-2
from $(git rev-parse refs/heads/master:file3)
data <<EOF
Tag blob by sha1.
EOF

blob
mark :6
data <<EOF
testing
EOF

commit refs/heads/new_blob
committer  <> 0 +0000
data 0
M 644 :6 new_blob
#pretend we got sha1 from fast-import
ls "new_blob"

tag series-A-blob-3
from $new_blob
data <<EOF
Tag new_blob.
EOF
INPUT_END

cat >expect <<EOF
object $(git rev-parse refs/heads/master:file3)
type blob
tag series-A-blob-2

Tag blob by sha1.
object $new_blob
type blob
tag series-A-blob-3

Tag new_blob.
EOF

test_expect_success \
	'A: tag blob by sha1' \
	'git fast-import <input &&
	git cat-file tag tags/series-A-blob-2 >actual &&
	git cat-file tag tags/series-A-blob-3 >>actual &&
	test_cmp expect actual'

test_tick
cat >input <<INPUT_END
commit refs/heads/verify--import-marks
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
recreate from :5
COMMIT

from :5
M 755 :2 copy-of-file2

INPUT_END
test_expect_success \
	'A: verify marks import does not crash' \
	'git fast-import --import-marks=marks.out <input &&
	 git whatchanged verify--import-marks'
test_expect_success \
	'A: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'
cat >expect <<EOF
:000000 100755 0000000000000000000000000000000000000000 7123f7f44e39be127c5eb701e5968176ee9d78b1 A	copy-of-file2
EOF
git diff-tree -M -r master verify--import-marks >actual
test_expect_success \
	'A: verify diff' \
	'compare_diff_raw expect actual &&
	 test `git rev-parse --verify master:file2` \
	    = `git rev-parse --verify verify--import-marks:copy-of-file2`'

test_tick
mt=$(git hash-object --stdin < /dev/null)
: >input.blob
: >marks.exp
: >tree.exp

cat >input.commit <<EOF
commit refs/heads/verify--dump-marks
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
test the sparse array dumping routines with exponentially growing marks
COMMIT
EOF

i=0
l=4
m=6
n=7
while test "$i" -lt 27; do
    cat >>input.blob <<EOF
blob
mark :$l
data 0
blob
mark :$m
data 0
blob
mark :$n
data 0
EOF
    echo "M 100644 :$l l$i" >>input.commit
    echo "M 100644 :$m m$i" >>input.commit
    echo "M 100644 :$n n$i" >>input.commit

    echo ":$l $mt" >>marks.exp
    echo ":$m $mt" >>marks.exp
    echo ":$n $mt" >>marks.exp

    printf "100644 blob $mt\tl$i\n" >>tree.exp
    printf "100644 blob $mt\tm$i\n" >>tree.exp
    printf "100644 blob $mt\tn$i\n" >>tree.exp

    l=$(($l + $l))
    m=$(($m + $m))
    n=$(($l + $n))

    i=$((1 + $i))
done

sort tree.exp > tree.exp_s

test_expect_success 'A: export marks with large values' '
	cat input.blob input.commit | git fast-import --export-marks=marks.large &&
	git ls-tree refs/heads/verify--dump-marks >tree.out &&
	test_cmp tree.exp_s tree.out &&
	test_cmp marks.exp marks.large'

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
test_expect_success 'B: fail on invalid blob sha1' '
    test_must_fail git fast-import <input
'
rm -f .git/objects/pack_* .git/objects/index_*

cat >input <<INPUT_END
commit .badbranchname
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
corrupt
COMMIT

from refs/heads/master

INPUT_END
test_expect_success 'B: fail on invalid branch name ".badbranchname"' '
    test_must_fail git fast-import <input
'
rm -f .git/objects/pack_* .git/objects/index_*

cat >input <<INPUT_END
commit bad[branch]name
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
corrupt
COMMIT

from refs/heads/master

INPUT_END
test_expect_success 'B: fail on invalid branch name "bad[branch]name"' '
    test_must_fail git fast-import <input
'
rm -f .git/objects/pack_* .git/objects/index_*

cat >input <<INPUT_END
commit TEMP_TAG
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
tag base
COMMIT

from refs/heads/master

INPUT_END
test_expect_success \
    'B: accept branch name "TEMP_TAG"' \
    'git fast-import <input &&
	 test -f .git/TEMP_TAG &&
	 test `git rev-parse master` = `git rev-parse TEMP_TAG^`'
rm -f .git/TEMP_TAG

git gc 2>/dev/null >/dev/null
git prune 2>/dev/null >/dev/null

cat >input <<INPUT_END
commit refs/heads/empty-committer-1
committer  <> $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: accept empty committer' '
	git fast-import <input &&
	out=$(git fsck) &&
	echo "$out" &&
	test -z "$out"
'
git update-ref -d refs/heads/empty-committer-1 || true

git gc 2>/dev/null >/dev/null
git prune 2>/dev/null >/dev/null

cat >input <<INPUT_END
commit refs/heads/empty-committer-2
committer <a@b.com> $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: accept and fixup committer with no name' '
	git fast-import <input &&
	out=$(git fsck) &&
	echo "$out" &&
	test -z "$out"
'
git update-ref -d refs/heads/empty-committer-2 || true

git gc 2>/dev/null >/dev/null
git prune 2>/dev/null >/dev/null

cat >input <<INPUT_END
commit refs/heads/invalid-committer
committer Name email> $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: fail on invalid committer (1)' '
	test_must_fail git fast-import <input
'
git update-ref -d refs/heads/invalid-committer || true

cat >input <<INPUT_END
commit refs/heads/invalid-committer
committer Name <e<mail> $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: fail on invalid committer (2)' '
	test_must_fail git fast-import <input
'
git update-ref -d refs/heads/invalid-committer || true

cat >input <<INPUT_END
commit refs/heads/invalid-committer
committer Name <email>> $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: fail on invalid committer (3)' '
	test_must_fail git fast-import <input
'
git update-ref -d refs/heads/invalid-committer || true

cat >input <<INPUT_END
commit refs/heads/invalid-committer
committer Name <email $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: fail on invalid committer (4)' '
	test_must_fail git fast-import <input
'
git update-ref -d refs/heads/invalid-committer || true

cat >input <<INPUT_END
commit refs/heads/invalid-committer
committer Name<email> $GIT_COMMITTER_DATE
data <<COMMIT
empty commit
COMMIT
INPUT_END
test_expect_success 'B: fail on invalid committer (5)' '
	test_must_fail git fast-import <input
'
git update-ref -d refs/heads/invalid-committer || true

###
### series C
###

newf=`echo hi newf | git hash-object -w --stdin`
oldf=`git rev-parse --verify master:file2`
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
    'git fast-import <input &&
	 git whatchanged branch'
test_expect_success \
	'C: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'
test_expect_success \
	'C: validate reuse existing blob' \
	'test $newf = `git rev-parse --verify branch:file2/newf` &&
	 test $oldf = `git rev-parse --verify branch:file2/oldf`'

cat >expect <<EOF
parent `git rev-parse --verify master^0`
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

second
EOF
test_expect_success \
	'C: verify commit' \
	'git cat-file commit branch | sed 1d >actual &&
	 test_cmp expect actual'

cat >expect <<EOF
:000000 100755 0000000000000000000000000000000000000000 f1fb5da718392694d0076d677d6d0e364c79b0bc A	file2/newf
:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 R100	file2	file2/oldf
:100644 000000 0d92e9f3374ae2947c23aa477cbc68ce598135f1 0000000000000000000000000000000000000000 D	file3
EOF
git diff-tree -M -r master branch >actual
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
    'git fast-import <input &&
	 git whatchanged branch'
test_expect_success \
	'D: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'

cat >expect <<EOF
:000000 100755 0000000000000000000000000000000000000000 35a59026a33beac1569b1c7f66f3090ce9c09afc A	newdir/exec.sh
:000000 100644 0000000000000000000000000000000000000000 046d0371e9220107917db0d0e030628de8a1de9b A	newdir/interesting
EOF
git diff-tree -M -r branch^ branch >actual
test_expect_success \
	'D: validate new files added' \
	'compare_diff_raw expect actual'

echo "$file5_data" >expect
test_expect_success \
	'D: verify file5' \
	'git cat-file blob branch:newdir/interesting >actual &&
	 test_cmp expect actual'

echo "$file6_data" >expect
test_expect_success \
	'D: verify file6' \
	'git cat-file blob branch:newdir/exec.sh >actual &&
	 test_cmp expect actual'

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
test_expect_success 'E: rfc2822 date, --date-format=raw' '
    test_must_fail git fast-import --date-format=raw <input
'
test_expect_success \
    'E: rfc2822 date, --date-format=rfc2822' \
    'git fast-import --date-format=rfc2822 <input'
test_expect_success \
	'E: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'

cat >expect <<EOF
author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> 1170778938 -0500
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1170783302 -0500

RFC 2822 type date
EOF
test_expect_success \
	'E: verify commit' \
	'git cat-file commit branch | sed 1,2d >actual &&
	test_cmp expect actual'

###
### series F
###

old_branch=`git rev-parse --verify branch^0`
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
    'if git fast-import <input
	 then
		echo BAD gfi did not fail
		return 1
	 else
		if test $old_branch = `git rev-parse --verify branch^0`
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
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'

cat >expect <<EOF
tree `git rev-parse branch~1^{tree}`
parent `git rev-parse branch~1`
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

losing things already?
EOF
test_expect_success \
	'F: verify other commit' \
	'git cat-file commit other >actual &&
	test_cmp expect actual'

###
### series G
###

old_branch=`git rev-parse --verify branch^0`
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
    'git fast-import --force <input'
test_expect_success \
	'G: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'
test_expect_success \
	'G: branch changed, but logged' \
	'test $old_branch != `git rev-parse --verify branch^0` &&
	 test $old_branch = `git rev-parse --verify branch@{1}`'

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
    'git fast-import <input &&
	 git whatchanged H'
test_expect_success \
	'H: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'

cat >expect <<EOF
:100755 000000 f1fb5da718392694d0076d677d6d0e364c79b0bc 0000000000000000000000000000000000000000 D	file2/newf
:100644 000000 7123f7f44e39be127c5eb701e5968176ee9d78b1 0000000000000000000000000000000000000000 D	file2/oldf
:100755 000000 85df50785d62d3b05ab03d9cbf7e4a0b49449730 0000000000000000000000000000000000000000 D	file4
:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 R100	newdir/interesting	h/e/l/lo
:100755 000000 e74b7d465e52746be2b4bae983670711e6e66657 0000000000000000000000000000000000000000 D	newdir/exec.sh
EOF
git diff-tree -M -r H^ H >actual
test_expect_success \
	'H: validate old files removed, new files added' \
	'compare_diff_raw expect actual'

echo "$file5_data" >expect
test_expect_success \
	'H: verify file' \
	'git cat-file blob H:h/e/l/lo >actual &&
	 test_cmp expect actual'

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
    'git fast-import --export-pack-edges=edges.list <input'

cat >expect <<EOF
.git/objects/pack/pack-.pack: `git rev-parse --verify export-boundary`
EOF
test_expect_success \
	'I: verify edge list' \
	'sed -e s/pack-.*pack/pack-.pack/ edges.list >actual &&
	 test_cmp expect actual'

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
    'git fast-import <input'
test_expect_success \
	'J: branch has 1 commit, empty tree' \
	'test 1 = `git rev-list J | wc -l` &&
	 test 0 = `git ls-tree J | wc -l`'

cat >input <<INPUT_END
reset refs/heads/J2

tag wrong_tag
from refs/heads/J2
data <<EOF
Tag branch that was reset.
EOF
INPUT_END
test_expect_success \
	'J: tag must fail on empty branch' \
	'test_must_fail git fast-import <input'
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
    'git fast-import <input'
test_expect_success \
    'K: verify K^1 = branch^1' \
    'test `git rev-parse --verify branch^1` \
		= `git rev-parse --verify K^1`'

###
### series L
###

cat >input <<INPUT_END
blob
mark :1
data <<EOF
some data
EOF

blob
mark :2
data <<EOF
other data
EOF

commit refs/heads/L
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
create L
COMMIT

M 644 :1 b.
M 644 :1 b/other
M 644 :1 ba

commit refs/heads/L
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
update L
COMMIT

M 644 :2 b.
M 644 :2 b/other
M 644 :2 ba
INPUT_END

cat >expect <<EXPECT_END
:100644 100644 4268632... 55d3a52... M	b.
:040000 040000 0ae5cac... 443c768... M	b
:100644 100644 4268632... 55d3a52... M	ba
EXPECT_END

test_expect_success \
    'L: verify internal tree sorting' \
	'git fast-import <input &&
	 git diff-tree --abbrev --raw L^ L >output &&
	 test_cmp expect output'

cat >input <<INPUT_END
blob
mark :1
data <<EOF
the data
EOF

commit refs/heads/L2
committer C O Mitter <committer@example.com> 1112912473 -0700
data <<COMMIT
init L2
COMMIT
M 644 :1 a/b/c
M 644 :1 a/b/d
M 644 :1 a/e/f

commit refs/heads/L2
committer C O Mitter <committer@example.com> 1112912473 -0700
data <<COMMIT
update L2
COMMIT
C a g
C a/e g/b
M 644 :1 g/b/h
INPUT_END

cat <<EOF >expect
g/b/f
g/b/h
EOF

test_expect_success \
    'L: nested tree copy does not corrupt deltas' \
	'git fast-import <input &&
	git ls-tree L2 g/b/ >tmp &&
	cat tmp | cut -f 2 >actual &&
	test_cmp expect actual &&
	git fsck `git rev-parse L2`'

git update-ref -d refs/heads/L2

###
### series M
###

test_tick
cat >input <<INPUT_END
commit refs/heads/M1
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
file rename
COMMIT

from refs/heads/branch^0
R file2/newf file2/n.e.w.f

INPUT_END

cat >expect <<EOF
:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	file2/newf	file2/n.e.w.f
EOF
test_expect_success \
	'M: rename file in same subdirectory' \
	'git fast-import <input &&
	 git diff-tree -M -r M1^ M1 >actual &&
	 compare_diff_raw expect actual'

cat >input <<INPUT_END
commit refs/heads/M2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
file rename
COMMIT

from refs/heads/branch^0
R file2/newf i/am/new/to/you

INPUT_END

cat >expect <<EOF
:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	file2/newf	i/am/new/to/you
EOF
test_expect_success \
	'M: rename file to new subdirectory' \
	'git fast-import <input &&
	 git diff-tree -M -r M2^ M2 >actual &&
	 compare_diff_raw expect actual'

cat >input <<INPUT_END
commit refs/heads/M3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
file rename
COMMIT

from refs/heads/M2^0
R i other/sub

INPUT_END

cat >expect <<EOF
:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	i/am/new/to/you	other/sub/am/new/to/you
EOF
test_expect_success \
	'M: rename subdirectory to new subdirectory' \
	'git fast-import <input &&
	 git diff-tree -M -r M3^ M3 >actual &&
	 compare_diff_raw expect actual'

###
### series N
###

test_tick
cat >input <<INPUT_END
commit refs/heads/N1
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
file copy
COMMIT

from refs/heads/branch^0
C file2/newf file2/n.e.w.f

INPUT_END

cat >expect <<EOF
:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file2/n.e.w.f
EOF
test_expect_success \
	'N: copy file in same subdirectory' \
	'git fast-import <input &&
	 git diff-tree -C --find-copies-harder -r N1^ N1 >actual &&
	 compare_diff_raw expect actual'

cat >input <<INPUT_END
commit refs/heads/N2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
clean directory copy
COMMIT

from refs/heads/branch^0
C file2 file3

commit refs/heads/N2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
modify directory copy
COMMIT

M 644 inline file3/file5
data <<EOF
$file5_data
EOF

INPUT_END

cat >expect <<EOF
:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 C100	newdir/interesting	file3/file5
:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file3/newf
:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 C100	file2/oldf	file3/oldf
EOF
test_expect_success \
	'N: copy then modify subdirectory' \
	'git fast-import <input &&
	 git diff-tree -C --find-copies-harder -r N2^^ N2 >actual &&
	 compare_diff_raw expect actual'

cat >input <<INPUT_END
commit refs/heads/N3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
dirty directory copy
COMMIT

from refs/heads/branch^0
M 644 inline file2/file5
data <<EOF
$file5_data
EOF

C file2 file3
D file2/file5

INPUT_END

test_expect_success \
	'N: copy dirty subdirectory' \
	'git fast-import <input &&
	 test `git rev-parse N2^{tree}` = `git rev-parse N3^{tree}`'

test_expect_success \
	'N: copy directory by id' \
	'cat >expect <<-\EOF &&
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file3/newf
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 C100	file2/oldf	file3/oldf
	EOF
	 subdir=$(git rev-parse refs/heads/branch^0:file2) &&
	 cat >input <<-INPUT_END &&
	commit refs/heads/N4
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy by tree hash
	COMMIT

	from refs/heads/branch^0
	M 040000 $subdir file3
	INPUT_END
	 git fast-import <input &&
	 git diff-tree -C --find-copies-harder -r N4^ N4 >actual &&
	 compare_diff_raw expect actual'

test_expect_success PIPE 'N: read and copy directory' '
	cat >expect <<-\EOF
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file3/newf
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 C100	file2/oldf	file3/oldf
	EOF
	git update-ref -d refs/heads/N4 &&
	rm -f backflow &&
	mkfifo backflow &&
	(
		exec <backflow &&
		cat <<-EOF &&
		commit refs/heads/N4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		copy by tree hash, part 2
		COMMIT

		from refs/heads/branch^0
		ls "file2"
		EOF
		read mode type tree filename &&
		echo "M 040000 $tree file3"
	) |
	git fast-import --cat-blob-fd=3 3>backflow &&
	git diff-tree -C --find-copies-harder -r N4^ N4 >actual &&
	compare_diff_raw expect actual
'

test_expect_success PIPE 'N: empty directory reads as missing' '
	cat <<-\EOF >expect &&
	OBJNAME
	:000000 100644 OBJNAME OBJNAME A	unrelated
	EOF
	echo "missing src" >expect.response &&
	git update-ref -d refs/heads/read-empty &&
	rm -f backflow &&
	mkfifo backflow &&
	(
		exec <backflow &&
		cat <<-EOF &&
		commit refs/heads/read-empty
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		read "empty" (missing) directory
		COMMIT

		M 100644 inline src/greeting
		data <<BLOB
		hello
		BLOB
		C src/greeting dst1/non-greeting
		C src/greeting unrelated
		# leave behind "empty" src directory
		D src/greeting
		ls "src"
		EOF
		read -r line &&
		printf "%s\n" "$line" >response &&
		cat <<-\EOF
		D dst1
		D dst2
		EOF
	) |
	git fast-import --cat-blob-fd=3 3>backflow &&
	test_cmp expect.response response &&
	git rev-list read-empty |
	git diff-tree -r --root --stdin |
	sed "s/$_x40/OBJNAME/g" >actual &&
	test_cmp expect actual
'

test_expect_success \
	'N: copy root directory by tree hash' \
	'cat >expect <<-\EOF &&
	:100755 000000 f1fb5da718392694d0076d677d6d0e364c79b0bc 0000000000000000000000000000000000000000 D	file3/newf
	:100644 000000 7123f7f44e39be127c5eb701e5968176ee9d78b1 0000000000000000000000000000000000000000 D	file3/oldf
	EOF
	 root=$(git rev-parse refs/heads/branch^0^{tree}) &&
	 cat >input <<-INPUT_END &&
	commit refs/heads/N6
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy root directory by tree hash
	COMMIT

	from refs/heads/branch^0
	M 040000 $root ""
	INPUT_END
	 git fast-import <input &&
	 git diff-tree -C --find-copies-harder -r N4 N6 >actual &&
	 compare_diff_raw expect actual'

test_expect_success \
	'N: delete directory by copying' \
	'cat >expect <<-\EOF &&
	OBJID
	:100644 000000 OBJID OBJID D	foo/bar/qux
	OBJID
	:000000 100644 OBJID OBJID A	foo/bar/baz
	:000000 100644 OBJID OBJID A	foo/bar/qux
	EOF
	 empty_tree=$(git mktree </dev/null) &&
	 cat >input <<-INPUT_END &&
	commit refs/heads/N-delete
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	collect data to be deleted
	COMMIT

	deleteall
	M 100644 inline foo/bar/baz
	data <<DATA_END
	hello
	DATA_END
	C "foo/bar/baz" "foo/bar/qux"
	C "foo/bar/baz" "foo/bar/quux/1"
	C "foo/bar/baz" "foo/bar/quuux"
	M 040000 $empty_tree foo/bar/quux
	M 040000 $empty_tree foo/bar/quuux

	commit refs/heads/N-delete
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	delete subdirectory
	COMMIT

	M 040000 $empty_tree foo/bar/qux
	INPUT_END
	 git fast-import <input &&
	 git rev-list N-delete |
		git diff-tree -r --stdin --root --always |
		sed -e "s/$_x40/OBJID/g" >actual &&
	 test_cmp expect actual'

test_expect_success \
	'N: modify copied tree' \
	'cat >expect <<-\EOF &&
	:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 C100	newdir/interesting	file3/file5
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file3/newf
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 C100	file2/oldf	file3/oldf
	EOF
	 subdir=$(git rev-parse refs/heads/branch^0:file2) &&
	 cat >input <<-INPUT_END &&
	commit refs/heads/N5
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy by tree hash
	COMMIT

	from refs/heads/branch^0
	M 040000 $subdir file3

	commit refs/heads/N5
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	modify directory copy
	COMMIT

	M 644 inline file3/file5
	data <<EOF
	$file5_data
	EOF
	INPUT_END
	 git fast-import <input &&
	 git diff-tree -C --find-copies-harder -r N5^^ N5 >actual &&
	 compare_diff_raw expect actual'

test_expect_success \
	'N: reject foo/ syntax' \
	'subdir=$(git rev-parse refs/heads/branch^0:file2) &&
	 test_must_fail git fast-import <<-INPUT_END
	commit refs/heads/N5B
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy with invalid syntax
	COMMIT

	from refs/heads/branch^0
	M 040000 $subdir file3/
	INPUT_END'

test_expect_success \
	'N: copy to root by id and modify' \
	'echo "hello, world" >expect.foo &&
	 echo hello >expect.bar &&
	 git fast-import <<-SETUP_END &&
	commit refs/heads/N7
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	hello, tree
	COMMIT

	deleteall
	M 644 inline foo/bar
	data <<EOF
	hello
	EOF
	SETUP_END

	 tree=$(git rev-parse --verify N7:) &&
	 git fast-import <<-INPUT_END &&
	commit refs/heads/N8
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy to root by id and modify
	COMMIT

	M 040000 $tree ""
	M 644 inline foo/foo
	data <<EOF
	hello, world
	EOF
	INPUT_END
	 git show N8:foo/foo >actual.foo &&
	 git show N8:foo/bar >actual.bar &&
	 test_cmp expect.foo actual.foo &&
	 test_cmp expect.bar actual.bar'

test_expect_success \
	'N: extract subtree' \
	'branch=$(git rev-parse --verify refs/heads/branch^{tree}) &&
	 cat >input <<-INPUT_END &&
	commit refs/heads/N9
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	extract subtree branch:newdir
	COMMIT

	M 040000 $branch ""
	C "newdir" ""
	INPUT_END
	 git fast-import <input &&
	 git diff --exit-code branch:newdir N9'

test_expect_success \
	'N: modify subtree, extract it, and modify again' \
	'echo hello >expect.baz &&
	 echo hello, world >expect.qux &&
	 git fast-import <<-SETUP_END &&
	commit refs/heads/N10
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	hello, tree
	COMMIT

	deleteall
	M 644 inline foo/bar/baz
	data <<EOF
	hello
	EOF
	SETUP_END

	 tree=$(git rev-parse --verify N10:) &&
	 git fast-import <<-INPUT_END &&
	commit refs/heads/N11
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy to root by id and modify
	COMMIT

	M 040000 $tree ""
	M 100644 inline foo/bar/qux
	data <<EOF
	hello, world
	EOF
	R "foo" ""
	C "bar/qux" "bar/quux"
	INPUT_END
	 git show N11:bar/baz >actual.baz &&
	 git show N11:bar/qux >actual.qux &&
	 git show N11:bar/quux >actual.quux &&
	 test_cmp expect.baz actual.baz &&
	 test_cmp expect.qux actual.qux &&
	 test_cmp expect.qux actual.quux'

###
### series O
###

cat >input <<INPUT_END
#we will
commit refs/heads/O1
# -- ignore all of this text
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
# $GIT_COMMITTER_NAME has inserted here for his benefit.
data <<COMMIT
dirty directory copy
COMMIT

# don't forget the import blank line!
#
# yes, we started from our usual base of branch^0.
# i like branch^0.
from refs/heads/branch^0
# and we need to reuse file2/file5 from N3 above.
M 644 inline file2/file5
# otherwise the tree will be different
data <<EOF
$file5_data
EOF

# don't forget to copy file2 to file3
C file2 file3
#
# or to delete file5 from file2.
D file2/file5
# are we done yet?

INPUT_END

test_expect_success \
	'O: comments are all skipped' \
	'git fast-import <input &&
	 test `git rev-parse N3` = `git rev-parse O1`'

cat >input <<INPUT_END
commit refs/heads/O2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
dirty directory copy
COMMIT
from refs/heads/branch^0
M 644 inline file2/file5
data <<EOF
$file5_data
EOF
C file2 file3
D file2/file5

INPUT_END

test_expect_success \
	'O: blank lines not necessary after data commands' \
	'git fast-import <input &&
	 test `git rev-parse N3` = `git rev-parse O2`'

test_expect_success \
	'O: repack before next test' \
	'git repack -a -d'

cat >input <<INPUT_END
commit refs/heads/O3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zstring
COMMIT
commit refs/heads/O3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zof
COMMIT
checkpoint
commit refs/heads/O3
mark :5
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zempty
COMMIT
checkpoint
commit refs/heads/O3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zcommits
COMMIT
reset refs/tags/O3-2nd
from :5
reset refs/tags/O3-3rd
from :5
INPUT_END

cat >expect <<INPUT_END
string
of
empty
commits
INPUT_END
test_expect_success \
	'O: blank lines not necessary after other commands' \
	'git fast-import <input &&
	 test 8 = `find .git/objects/pack -type f | wc -l` &&
	 test `git rev-parse refs/tags/O3-2nd` = `git rev-parse O3^` &&
	 git log --reverse --pretty=oneline O3 | sed s/^.*z// >actual &&
	 test_cmp expect actual'

cat >input <<INPUT_END
commit refs/heads/O4
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zstring
COMMIT
commit refs/heads/O4
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zof
COMMIT
progress Two commits down, 2 to go!
commit refs/heads/O4
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zempty
COMMIT
progress Three commits down, 1 to go!
commit refs/heads/O4
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
zcommits
COMMIT
progress I'm done!
INPUT_END
test_expect_success \
	'O: progress outputs as requested by input' \
	'git fast-import <input >actual &&
	 grep "progress " <input >expect &&
	 test_cmp expect actual'

###
### series P (gitlinks)
###

cat >input <<INPUT_END
blob
mark :1
data 10
test file

reset refs/heads/sub
commit refs/heads/sub
mark :2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data 12
sub_initial
M 100644 :1 file

blob
mark :3
data <<DATAEND
[submodule "sub"]
	path = sub
	url = "`pwd`/sub"
DATAEND

commit refs/heads/subuse1
mark :4
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data 8
initial
from refs/heads/master
M 100644 :3 .gitmodules
M 160000 :2 sub

blob
mark :5
data 20
test file
more data

commit refs/heads/sub
mark :6
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data 11
sub_second
from :2
M 100644 :5 file

commit refs/heads/subuse1
mark :7
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data 7
second
from :4
M 160000 :6 sub

INPUT_END

test_expect_success \
	'P: supermodule & submodule mix' \
	'git fast-import <input &&
	 git checkout subuse1 &&
	 rm -rf sub && mkdir sub && (cd sub &&
	 git init &&
	 git fetch --update-head-ok .. refs/heads/sub:refs/heads/master &&
	 git checkout master) &&
	 git submodule init &&
	 git submodule update'

SUBLAST=$(git rev-parse --verify sub)
SUBPREV=$(git rev-parse --verify sub^)

cat >input <<INPUT_END
blob
mark :1
data <<DATAEND
[submodule "sub"]
	path = sub
	url = "`pwd`/sub"
DATAEND

commit refs/heads/subuse2
mark :2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data 8
initial
from refs/heads/master
M 100644 :1 .gitmodules
M 160000 $SUBPREV sub

commit refs/heads/subuse2
mark :3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data 7
second
from :2
M 160000 $SUBLAST sub

INPUT_END

test_expect_success \
	'P: verbatim SHA gitlinks' \
	'git branch -D sub &&
	 git gc && git prune &&
	 git fast-import <input &&
	 test $(git rev-parse --verify subuse2) = $(git rev-parse --verify subuse1)'

test_tick
cat >input <<INPUT_END
commit refs/heads/subuse3
mark :1
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
corrupt
COMMIT

from refs/heads/subuse2
M 160000 inline sub
data <<DATA
$SUBPREV
DATA

INPUT_END

test_expect_success 'P: fail on inline gitlink' '
    test_must_fail git fast-import <input'

test_tick
cat >input <<INPUT_END
blob
mark :1
data <<DATA
$SUBPREV
DATA

commit refs/heads/subuse3
mark :2
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
corrupt
COMMIT

from refs/heads/subuse2
M 160000 :1 sub

INPUT_END

test_expect_success 'P: fail on blob mark in gitlink' '
    test_must_fail git fast-import <input'

###
### series Q (notes)
###

note1_data="The first note for the first commit"
note2_data="The first note for the second commit"
note3_data="The first note for the third commit"
note1b_data="The second note for the first commit"
note1c_data="The third note for the first commit"
note2b_data="The second note for the second commit"

test_tick
cat >input <<INPUT_END
blob
mark :2
data <<EOF
$file2_data
EOF

commit refs/heads/notes-test
mark :3
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
first (:3)
COMMIT

M 644 :2 file2

blob
mark :4
data $file4_len
$file4_data
commit refs/heads/notes-test
mark :5
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
second (:5)
COMMIT

M 644 :4 file4

commit refs/heads/notes-test
mark :6
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
third (:6)
COMMIT

M 644 inline file5
data <<EOF
$file5_data
EOF

M 755 inline file6
data <<EOF
$file6_data
EOF

blob
mark :7
data <<EOF
$note1_data
EOF

blob
mark :8
data <<EOF
$note2_data
EOF

commit refs/notes/foobar
mark :9
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes (:9)
COMMIT

N :7 :3
N :8 :5
N inline :6
data <<EOF
$note3_data
EOF

commit refs/notes/foobar
mark :10
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes (:10)
COMMIT

N inline :3
data <<EOF
$note1b_data
EOF

commit refs/notes/foobar2
mark :11
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes (:11)
COMMIT

N inline :3
data <<EOF
$note1c_data
EOF

commit refs/notes/foobar
mark :12
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
notes (:12)
COMMIT

deleteall
N inline :5
data <<EOF
$note2b_data
EOF

INPUT_END

test_expect_success \
	'Q: commit notes' \
	'git fast-import <input &&
	 git whatchanged notes-test'
test_expect_success \
	'Q: verify pack' \
	'for p in .git/objects/pack/*.pack;do git verify-pack $p||exit;done'

commit1=$(git rev-parse notes-test~2)
commit2=$(git rev-parse notes-test^)
commit3=$(git rev-parse notes-test)

cat >expect <<EOF
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

first (:3)
EOF
test_expect_success \
	'Q: verify first commit' \
	'git cat-file commit notes-test~2 | sed 1d >actual &&
	test_cmp expect actual'

cat >expect <<EOF
parent $commit1
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

second (:5)
EOF
test_expect_success \
	'Q: verify second commit' \
	'git cat-file commit notes-test^ | sed 1d >actual &&
	test_cmp expect actual'

cat >expect <<EOF
parent $commit2
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

third (:6)
EOF
test_expect_success \
	'Q: verify third commit' \
	'git cat-file commit notes-test | sed 1d >actual &&
	test_cmp expect actual'

cat >expect <<EOF
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

notes (:9)
EOF
test_expect_success \
	'Q: verify first notes commit' \
	'git cat-file commit refs/notes/foobar~2 | sed 1d >actual &&
	test_cmp expect actual'

cat >expect.unsorted <<EOF
100644 blob $commit1
100644 blob $commit2
100644 blob $commit3
EOF
cat expect.unsorted | sort >expect
test_expect_success \
	'Q: verify first notes tree' \
	'git cat-file -p refs/notes/foobar~2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	 test_cmp expect actual'

echo "$note1_data" >expect
test_expect_success \
	'Q: verify first note for first commit' \
	'git cat-file blob refs/notes/foobar~2:$commit1 >actual && test_cmp expect actual'

echo "$note2_data" >expect
test_expect_success \
	'Q: verify first note for second commit' \
	'git cat-file blob refs/notes/foobar~2:$commit2 >actual && test_cmp expect actual'

echo "$note3_data" >expect
test_expect_success \
	'Q: verify first note for third commit' \
	'git cat-file blob refs/notes/foobar~2:$commit3 >actual && test_cmp expect actual'

cat >expect <<EOF
parent `git rev-parse --verify refs/notes/foobar~2`
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

notes (:10)
EOF
test_expect_success \
	'Q: verify second notes commit' \
	'git cat-file commit refs/notes/foobar^ | sed 1d >actual &&
	test_cmp expect actual'

cat >expect.unsorted <<EOF
100644 blob $commit1
100644 blob $commit2
100644 blob $commit3
EOF
cat expect.unsorted | sort >expect
test_expect_success \
	'Q: verify second notes tree' \
	'git cat-file -p refs/notes/foobar^^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	 test_cmp expect actual'

echo "$note1b_data" >expect
test_expect_success \
	'Q: verify second note for first commit' \
	'git cat-file blob refs/notes/foobar^:$commit1 >actual && test_cmp expect actual'

echo "$note2_data" >expect
test_expect_success \
	'Q: verify first note for second commit' \
	'git cat-file blob refs/notes/foobar^:$commit2 >actual && test_cmp expect actual'

echo "$note3_data" >expect
test_expect_success \
	'Q: verify first note for third commit' \
	'git cat-file blob refs/notes/foobar^:$commit3 >actual && test_cmp expect actual'

cat >expect <<EOF
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

notes (:11)
EOF
test_expect_success \
	'Q: verify third notes commit' \
	'git cat-file commit refs/notes/foobar2 | sed 1d >actual &&
	test_cmp expect actual'

cat >expect.unsorted <<EOF
100644 blob $commit1
EOF
cat expect.unsorted | sort >expect
test_expect_success \
	'Q: verify third notes tree' \
	'git cat-file -p refs/notes/foobar2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	 test_cmp expect actual'

echo "$note1c_data" >expect
test_expect_success \
	'Q: verify third note for first commit' \
	'git cat-file blob refs/notes/foobar2:$commit1 >actual && test_cmp expect actual'

cat >expect <<EOF
parent `git rev-parse --verify refs/notes/foobar^`
author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

notes (:12)
EOF
test_expect_success \
	'Q: verify fourth notes commit' \
	'git cat-file commit refs/notes/foobar | sed 1d >actual &&
	test_cmp expect actual'

cat >expect.unsorted <<EOF
100644 blob $commit2
EOF
cat expect.unsorted | sort >expect
test_expect_success \
	'Q: verify fourth notes tree' \
	'git cat-file -p refs/notes/foobar^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	 test_cmp expect actual'

echo "$note2b_data" >expect
test_expect_success \
	'Q: verify second note for second commit' \
	'git cat-file blob refs/notes/foobar:$commit2 >actual && test_cmp expect actual'

###
### series R (feature and option)
###

cat >input <<EOF
feature no-such-feature-exists
EOF

test_expect_success 'R: abort on unsupported feature' '
	test_must_fail git fast-import <input
'

cat >input <<EOF
feature date-format=now
EOF

test_expect_success 'R: supported feature is accepted' '
	git fast-import <input
'

cat >input << EOF
blob
data 3
hi
feature date-format=now
EOF

test_expect_success 'R: abort on receiving feature after data command' '
	test_must_fail git fast-import <input
'

cat >input << EOF
feature import-marks=git.marks
feature import-marks=git2.marks
EOF

test_expect_success 'R: only one import-marks feature allowed per stream' '
	test_must_fail git fast-import <input
'

cat >input << EOF
feature export-marks=git.marks
blob
mark :1
data 3
hi

EOF

test_expect_success \
    'R: export-marks feature results in a marks file being created' \
    'cat input | git fast-import &&
    grep :1 git.marks'

test_expect_success \
    'R: export-marks options can be overriden by commandline options' \
    'cat input | git fast-import --export-marks=other.marks &&
    grep :1 other.marks'

test_expect_success 'R: catch typo in marks file name' '
	test_must_fail git fast-import --import-marks=nonexistent.marks </dev/null &&
	echo "feature import-marks=nonexistent.marks" |
	test_must_fail git fast-import
'

test_expect_success 'R: import and output marks can be the same file' '
	rm -f io.marks &&
	blob=$(echo hi | git hash-object --stdin) &&
	cat >expect <<-EOF &&
	:1 $blob
	:2 $blob
	EOF
	git fast-import --export-marks=io.marks <<-\EOF &&
	blob
	mark :1
	data 3
	hi

	EOF
	git fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF &&
	blob
	mark :2
	data 3
	hi

	EOF
	test_cmp expect io.marks
'

test_expect_success 'R: --import-marks=foo --output-marks=foo to create foo fails' '
	rm -f io.marks &&
	test_must_fail git fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF
	blob
	mark :1
	data 3
	hi

	EOF
'

test_expect_success 'R: --import-marks-if-exists' '
	rm -f io.marks &&
	blob=$(echo hi | git hash-object --stdin) &&
	echo ":1 $blob" >expect &&
	git fast-import --import-marks-if-exists=io.marks --export-marks=io.marks <<-\EOF &&
	blob
	mark :1
	data 3
	hi

	EOF
	test_cmp expect io.marks
'

test_expect_success 'R: feature import-marks-if-exists' '
	rm -f io.marks &&
	>expect &&

	git fast-import --export-marks=io.marks <<-\EOF &&
	feature import-marks-if-exists=not_io.marks
	EOF
	test_cmp expect io.marks &&

	blob=$(echo hi | git hash-object --stdin) &&

	echo ":1 $blob" >io.marks &&
	echo ":1 $blob" >expect &&
	echo ":2 $blob" >>expect &&

	git fast-import --export-marks=io.marks <<-\EOF &&
	feature import-marks-if-exists=io.marks
	blob
	mark :2
	data 3
	hi

	EOF
	test_cmp expect io.marks &&

	echo ":3 $blob" >>expect &&

	git fast-import --import-marks=io.marks \
			--export-marks=io.marks <<-\EOF &&
	feature import-marks-if-exists=not_io.marks
	blob
	mark :3
	data 3
	hi

	EOF
	test_cmp expect io.marks &&

	>expect &&

	git fast-import --import-marks-if-exists=not_io.marks \
			--export-marks=io.marks <<-\EOF
	feature import-marks-if-exists=io.marks
	EOF
	test_cmp expect io.marks
'

cat >input << EOF
feature import-marks=marks.out
feature export-marks=marks.new
EOF

test_expect_success \
    'R: import to output marks works without any content' \
    'cat input | git fast-import &&
    test_cmp marks.out marks.new'

cat >input <<EOF
feature import-marks=nonexistent.marks
feature export-marks=marks.new
EOF

test_expect_success \
    'R: import marks prefers commandline marks file over the stream' \
    'cat input | git fast-import --import-marks=marks.out &&
    test_cmp marks.out marks.new'


cat >input <<EOF
feature import-marks=nonexistent.marks
feature export-marks=combined.marks
EOF

test_expect_success 'R: multiple --import-marks= should be honoured' '
    head -n2 marks.out > one.marks &&
    tail -n +3 marks.out > two.marks &&
    git fast-import --import-marks=one.marks --import-marks=two.marks <input &&
    test_cmp marks.out combined.marks
'

cat >input <<EOF
feature relative-marks
feature import-marks=relative.in
feature export-marks=relative.out
EOF

test_expect_success 'R: feature relative-marks should be honoured' '
    mkdir -p .git/info/fast-import/ &&
    cp marks.new .git/info/fast-import/relative.in &&
    git fast-import <input &&
    test_cmp marks.new .git/info/fast-import/relative.out
'

cat >input <<EOF
feature relative-marks
feature import-marks=relative.in
feature no-relative-marks
feature export-marks=non-relative.out
EOF

test_expect_success 'R: feature no-relative-marks should be honoured' '
    git fast-import <input &&
    test_cmp marks.new non-relative.out
'

test_expect_success 'R: feature ls supported' '
	echo "feature ls" |
	git fast-import
'

test_expect_success 'R: feature cat-blob supported' '
	echo "feature cat-blob" |
	git fast-import
'

test_expect_success 'R: cat-blob-fd must be a nonnegative integer' '
	test_must_fail git fast-import --cat-blob-fd=-1 </dev/null
'

test_expect_success 'R: print old blob' '
	blob=$(echo "yes it can" | git hash-object -w --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 11
	yes it can

	EOF
	echo "cat-blob $blob" |
	git fast-import --cat-blob-fd=6 6>actual &&
	test_cmp expect actual
'

test_expect_success 'R: in-stream cat-blob-fd not respected' '
	echo hello >greeting &&
	blob=$(git hash-object -w greeting) &&
	cat >expect <<-EOF &&
	${blob} blob 6
	hello

	EOF
	git fast-import --cat-blob-fd=3 3>actual.3 >actual.1 <<-EOF &&
	cat-blob $blob
	EOF
	test_cmp expect actual.3 &&
	test_cmp empty actual.1 &&
	git fast-import 3>actual.3 >actual.1 <<-EOF &&
	option cat-blob-fd=3
	cat-blob $blob
	EOF
	test_cmp empty actual.3 &&
	test_cmp expect actual.1
'

test_expect_success 'R: print new blob' '
	blob=$(echo "yep yep yep" | git hash-object --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 12
	yep yep yep

	EOF
	git fast-import --cat-blob-fd=6 6>actual <<-\EOF &&
	blob
	mark :1
	data <<BLOB_END
	yep yep yep
	BLOB_END
	cat-blob :1
	EOF
	test_cmp expect actual
'

test_expect_success 'R: print new blob by sha1' '
	blob=$(echo "a new blob named by sha1" | git hash-object --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 25
	a new blob named by sha1

	EOF
	git fast-import --cat-blob-fd=6 6>actual <<-EOF &&
	blob
	data <<BLOB_END
	a new blob named by sha1
	BLOB_END
	cat-blob $blob
	EOF
	test_cmp expect actual
'

test_expect_success 'setup: big file' '
	(
		echo "the quick brown fox jumps over the lazy dog" >big &&
		for i in 1 2 3
		do
			cat big big big big >bigger &&
			cat bigger bigger bigger bigger >big ||
			exit
		done
	)
'

test_expect_success 'R: print two blobs to stdout' '
	blob1=$(git hash-object big) &&
	blob1_len=$(wc -c <big) &&
	blob2=$(echo hello | git hash-object --stdin) &&
	{
		echo ${blob1} blob $blob1_len &&
		cat big &&
		cat <<-EOF

		${blob2} blob 6
		hello

		EOF
	} >expect &&
	{
		cat <<-\END_PART1 &&
			blob
			mark :1
			data <<data_end
		END_PART1
		cat big &&
		cat <<-\EOF
			data_end
			blob
			mark :2
			data <<data_end
			hello
			data_end
			cat-blob :1
			cat-blob :2
		EOF
	} |
	git fast-import >actual &&
	test_cmp expect actual
'

test_expect_success PIPE 'R: copy using cat-file' '
	expect_id=$(git hash-object big) &&
	expect_len=$(wc -c <big) &&
	echo $expect_id blob $expect_len >expect.response &&

	rm -f blobs &&
	cat >frontend <<-\FRONTEND_END &&
	#!/bin/sh
	FRONTEND_END

	mkfifo blobs &&
	(
		export GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_COMMITTER_DATE &&
		cat <<-\EOF &&
		feature cat-blob
		blob
		mark :1
		data <<BLOB
		EOF
		cat big &&
		cat <<-\EOF &&
		BLOB
		cat-blob :1
		EOF

		read blob_id type size <&3 &&
		echo "$blob_id $type $size" >response &&
		head_c $size >blob <&3 &&
		read newline <&3 &&

		cat <<-EOF &&
		commit refs/heads/copied
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		copy big file as file3
		COMMIT
		M 644 inline file3
		data <<BLOB
		EOF
		cat blob &&
		echo BLOB
	) 3<blobs |
	git fast-import --cat-blob-fd=3 3>blobs &&
	git show copied:file3 >actual &&
	test_cmp expect.response response &&
	test_cmp big actual
'

test_expect_success PIPE 'R: print blob mid-commit' '
	rm -f blobs &&
	echo "A blob from _before_ the commit." >expect &&
	mkfifo blobs &&
	(
		exec 3<blobs &&
		cat <<-EOF &&
		feature cat-blob
		blob
		mark :1
		data <<BLOB
		A blob from _before_ the commit.
		BLOB
		commit refs/heads/temporary
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		Empty commit
		COMMIT
		cat-blob :1
		EOF

		read blob_id type size <&3 &&
		head_c $size >actual <&3 &&
		read newline <&3 &&

		echo
	) |
	git fast-import --cat-blob-fd=3 3>blobs &&
	test_cmp expect actual
'

test_expect_success PIPE 'R: print staged blob within commit' '
	rm -f blobs &&
	echo "A blob from _within_ the commit." >expect &&
	mkfifo blobs &&
	(
		exec 3<blobs &&
		cat <<-EOF &&
		feature cat-blob
		commit refs/heads/within
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		Empty commit
		COMMIT
		M 644 inline within
		data <<BLOB
		A blob from _within_ the commit.
		BLOB
		EOF

		to_get=$(
			echo "A blob from _within_ the commit." |
			git hash-object --stdin
		) &&
		echo "cat-blob $to_get" &&

		read blob_id type size <&3 &&
		head_c $size >actual <&3 &&
		read newline <&3 &&

		echo deleteall
	) |
	git fast-import --cat-blob-fd=3 3>blobs &&
	test_cmp expect actual
'

cat >input << EOF
option git quiet
blob
data 3
hi

EOF

test_expect_success 'R: quiet option results in no stats being output' '
    cat input | git fast-import 2> output &&
    test_cmp empty output
'

test_expect_success 'R: feature done means terminating "done" is mandatory' '
	echo feature done | test_must_fail git fast-import &&
	test_must_fail git fast-import --done </dev/null
'

test_expect_success 'R: terminating "done" with trailing gibberish is ok' '
	git fast-import <<-\EOF &&
	feature done
	done
	trailing gibberish
	EOF
	git fast-import <<-\EOF
	done
	more trailing gibberish
	EOF
'

test_expect_success 'R: terminating "done" within commit' '
	cat >expect <<-\EOF &&
	OBJID
	:000000 100644 OBJID OBJID A	hello.c
	:000000 100644 OBJID OBJID A	hello2.c
	EOF
	git fast-import <<-EOF &&
	commit refs/heads/done-ends
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<EOT
	Commit terminated by "done" command
	EOT
	M 100644 inline hello.c
	data <<EOT
	Hello, world.
	EOT
	C hello.c hello2.c
	done
	EOF
	git rev-list done-ends |
	git diff-tree -r --stdin --root --always |
	sed -e "s/$_x40/OBJID/g" >actual &&
	test_cmp expect actual
'

cat >input <<EOF
option git non-existing-option
EOF

test_expect_success 'R: die on unknown option' '
    test_must_fail git fast-import <input
'

test_expect_success 'R: unknown commandline options are rejected' '\
    test_must_fail git fast-import --non-existing-option < /dev/null
'

test_expect_success 'R: die on invalid option argument' '
	echo "option git active-branches=-5" |
	test_must_fail git fast-import &&
	echo "option git depth=" |
	test_must_fail git fast-import &&
	test_must_fail git fast-import --depth="5 elephants" </dev/null
'

cat >input <<EOF
option non-existing-vcs non-existing-option
EOF

test_expect_success 'R: ignore non-git options' '
    git fast-import <input
'

##
## R: very large blobs
##
blobsize=$((2*1024*1024 + 53))
test-genrandom bar $blobsize >expect
cat >input <<INPUT_END
commit refs/heads/big-file
committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
data <<COMMIT
R - big file
COMMIT

M 644 inline big1
data $blobsize
INPUT_END
cat expect >>input
cat >>input <<INPUT_END
M 644 inline big2
data $blobsize
INPUT_END
cat expect >>input
echo >>input

test_expect_success \
	'R: blob bigger than threshold' \
	'test_create_repo R &&
	 git --git-dir=R/.git fast-import --big-file-threshold=1 <input'
test_expect_success \
	'R: verify created pack' \
	': >verify &&
	 for p in R/.git/objects/pack/*.pack;
	 do
	   git verify-pack -v $p >>verify || exit;
	 done'
test_expect_success \
	'R: verify written objects' \
	'git --git-dir=R/.git cat-file blob big-file:big1 >actual &&
	 test_cmp expect actual &&
	 a=$(git --git-dir=R/.git rev-parse big-file:big1) &&
	 b=$(git --git-dir=R/.git rev-parse big-file:big2) &&
	 test $a = $b'
test_expect_success \
	'R: blob appears only once' \
	'n=$(grep $a verify | wc -l) &&
	 test 1 = $n'

test_done
