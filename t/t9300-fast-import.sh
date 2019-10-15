#!/bin/sh
#
# Copyright (c) 2007 Shawn Pearce
#

test_description='test git fast-import utility'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh ;# test-lib chdir's into trash

verify_packs () {
	for p in .git/objects/pack/*.pack
	do
		git verify-pack "$@" "$p" || return
	done
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

###
### series A
###

test_expect_success 'empty stream succeeds' '
	git config fastimport.unpackLimit 0 &&
	git fast-import </dev/null
'

test_expect_success 'truncated stream complains' '
	echo "tag foo" | test_must_fail git fast-import
'

test_expect_success 'A: create pack from stdin' '
	test_tick &&
	cat >input <<-INPUT_END &&
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

	tag to-be-deleted
	from :3
	data <<EOF
	Another annotated tag that annotates a blob.
	EOF

	reset refs/tags/to-be-deleted
	from 0000000000000000000000000000000000000000

	tag nested
	mark :6
	from :4
	data <<EOF
	Tag of our lovely commit
	EOF

	reset refs/tags/nested
	from 0000000000000000000000000000000000000000

	tag nested
	mark :7
	from :6
	data <<EOF
	Tag of tag of our lovely commit
	EOF

	alias
	mark :8
	to :5

	INPUT_END
	git fast-import --export-marks=marks.out <input &&
	git whatchanged master
'

test_expect_success 'A: verify pack' '
	verify_packs
'

test_expect_success 'A: verify commit' '
	cat >expect <<-EOF &&
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	initial
	EOF
	git cat-file commit master | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tree' '
	cat >expect <<-EOF &&
	100644 blob file2
	100644 blob file3
	100755 blob file4
	EOF
	git cat-file -p master^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file2' '
	echo "$file2_data" >expect &&
	git cat-file blob master:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file3' '
	echo "$file3_data" >expect &&
	git cat-file blob master:file3 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file4' '
	printf "$file4_data" >expect &&
	git cat-file blob master:file4 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag/series-A' '
	cat >expect <<-EOF &&
	object $(git rev-parse refs/heads/master)
	type commit
	tag series-A

	An annotated tag without a tagger
	EOF
	git cat-file tag tags/series-A >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag/series-A-blob' '
	cat >expect <<-EOF &&
	object $(git rev-parse refs/heads/master:file3)
	type blob
	tag series-A-blob

	An annotated tag that annotates a blob.
	EOF
	git cat-file tag tags/series-A-blob >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag deletion is successful' '
	test_must_fail git rev-parse --verify refs/tags/to-be-deleted
'

test_expect_success 'A: verify marks output' '
	cat >expect <<-EOF &&
	:2 $(git rev-parse --verify master:file2)
	:3 $(git rev-parse --verify master:file3)
	:4 $(git rev-parse --verify master:file4)
	:5 $(git rev-parse --verify master^0)
	:6 $(git cat-file tag nested | grep object | cut -d" " -f 2)
	:7 $(git rev-parse --verify nested)
	:8 $(git rev-parse --verify master^0)
	EOF
	test_cmp expect marks.out
'

test_expect_success 'A: verify marks import' '
	git fast-import \
		--import-marks=marks.out \
		--export-marks=marks.new \
		</dev/null &&
	test_cmp expect marks.new
'

test_expect_success 'A: tag blob by sha1' '
	test_tick &&
	new_blob=$(echo testing | git hash-object --stdin) &&
	cat >input <<-INPUT_END &&
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

	cat >expect <<-EOF &&
	object $(git rev-parse refs/heads/master:file3)
	type blob
	tag series-A-blob-2

	Tag blob by sha1.
	object $new_blob
	type blob
	tag series-A-blob-3

	Tag new_blob.
	EOF

	git fast-import <input &&
	git cat-file tag tags/series-A-blob-2 >actual &&
	git cat-file tag tags/series-A-blob-3 >>actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify marks import does not crash' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/verify--import-marks
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	recreate from :5
	COMMIT

	from :5
	M 755 :2 copy-of-file2

	INPUT_END

	git fast-import --import-marks=marks.out <input &&
	git whatchanged verify--import-marks
'

test_expect_success 'A: verify pack' '
	verify_packs
'

test_expect_success 'A: verify diff' '
	cat >expect <<-EOF &&
	:000000 100755 0000000000000000000000000000000000000000 7123f7f44e39be127c5eb701e5968176ee9d78b1 A	copy-of-file2
	EOF
	git diff-tree -M -r master verify--import-marks >actual &&
	compare_diff_raw expect actual &&
	test $(git rev-parse --verify master:file2) \
	    = $(git rev-parse --verify verify--import-marks:copy-of-file2)
'

test_expect_success 'A: export marks with large values' '
	test_tick &&
	mt=$(git hash-object --stdin < /dev/null) &&
	>input.blob &&
	>marks.exp &&
	>tree.exp &&

	cat >input.commit <<-EOF &&
	commit refs/heads/verify--dump-marks
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	test the sparse array dumping routines with exponentially growing marks
	COMMIT
	EOF

	i=0 l=4 m=6 n=7 &&
	while test "$i" -lt 27
	do
		cat >>input.blob <<-EOF &&
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
		echo "M 100644 :$l l$i" >>input.commit &&
		echo "M 100644 :$m m$i" >>input.commit &&
		echo "M 100644 :$n n$i" >>input.commit &&

		echo ":$l $mt" >>marks.exp &&
		echo ":$m $mt" >>marks.exp &&
		echo ":$n $mt" >>marks.exp &&

		printf "100644 blob $mt\tl$i\n" >>tree.exp &&
		printf "100644 blob $mt\tm$i\n" >>tree.exp &&
		printf "100644 blob $mt\tn$i\n" >>tree.exp &&

		l=$(($l + $l)) &&
		m=$(($m + $m)) &&
		n=$(($l + $n)) &&

		i=$((1 + $i)) || return 1
	done &&

	sort tree.exp > tree.exp_s &&

	cat input.blob input.commit | git fast-import --export-marks=marks.large &&
	git ls-tree refs/heads/verify--dump-marks >tree.out &&
	test_cmp tree.exp_s tree.out &&
	test_cmp marks.exp marks.large
'

###
### series B
###

test_expect_success 'B: fail on invalid blob sha1' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/branch
	mark :1
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	corrupt
	COMMIT

	from refs/heads/master
	M 755 0000000000000000000000000000000000000001 zero1

	INPUT_END

	test_when_finished "rm -f .git/objects/pack_* .git/objects/index_*" &&
	test_must_fail git fast-import <input
'

test_expect_success 'B: accept branch name "TEMP_TAG"' '
	cat >input <<-INPUT_END &&
	commit TEMP_TAG
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	tag base
	COMMIT

	from refs/heads/master

	INPUT_END

	test_when_finished "rm -f .git/TEMP_TAG
		git gc
		git prune" &&
	git fast-import <input &&
	test -f .git/TEMP_TAG &&
	test $(git rev-parse master) = $(git rev-parse TEMP_TAG^)
'

test_expect_success 'B: accept empty committer' '
	cat >input <<-INPUT_END &&
	commit refs/heads/empty-committer-1
	committer  <> $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/empty-committer-1
		git gc
		git prune" &&
	git fast-import <input &&
	out=$(git fsck) &&
	echo "$out" &&
	test -z "$out"
'

test_expect_success 'B: accept and fixup committer with no name' '
	cat >input <<-INPUT_END &&
	commit refs/heads/empty-committer-2
	committer <a@b.com> $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/empty-committer-2
		git gc
		git prune" &&
	git fast-import <input &&
	out=$(git fsck) &&
	echo "$out" &&
	test -z "$out"
'

test_expect_success 'B: fail on invalid committer (1)' '
	cat >input <<-INPUT_END &&
	commit refs/heads/invalid-committer
	committer Name email> $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/invalid-committer" &&
	test_must_fail git fast-import <input
'

test_expect_success 'B: fail on invalid committer (2)' '
	cat >input <<-INPUT_END &&
	commit refs/heads/invalid-committer
	committer Name <e<mail> $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/invalid-committer" &&
	test_must_fail git fast-import <input
'

test_expect_success 'B: fail on invalid committer (3)' '
	cat >input <<-INPUT_END &&
	commit refs/heads/invalid-committer
	committer Name <email>> $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/invalid-committer" &&
	test_must_fail git fast-import <input
'

test_expect_success 'B: fail on invalid committer (4)' '
	cat >input <<-INPUT_END &&
	commit refs/heads/invalid-committer
	committer Name <email $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/invalid-committer" &&
	test_must_fail git fast-import <input
'

test_expect_success 'B: fail on invalid committer (5)' '
	cat >input <<-INPUT_END &&
	commit refs/heads/invalid-committer
	committer Name<email> $GIT_COMMITTER_DATE
	data <<COMMIT
	empty commit
	COMMIT
	INPUT_END

	test_when_finished "git update-ref -d refs/heads/invalid-committer" &&
	test_must_fail git fast-import <input
'

###
### series C
###

test_expect_success 'C: incremental import create pack from stdin' '
	newf=$(echo hi newf | git hash-object -w --stdin) &&
	oldf=$(git rev-parse --verify master:file2) &&
	test_tick &&
	cat >input <<-INPUT_END &&
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

	git fast-import <input &&
	git whatchanged branch
'

test_expect_success 'C: verify pack' '
	verify_packs
'

test_expect_success 'C: validate reuse existing blob' '
	test $newf = $(git rev-parse --verify branch:file2/newf) &&
	test $oldf = $(git rev-parse --verify branch:file2/oldf)
'

test_expect_success 'C: verify commit' '
	cat >expect <<-EOF &&
	parent $(git rev-parse --verify master^0)
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	second
	EOF

	git cat-file commit branch | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'C: validate rename result' '
	cat >expect <<-EOF &&
	:000000 100755 0000000000000000000000000000000000000000 f1fb5da718392694d0076d677d6d0e364c79b0bc A	file2/newf
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 R100	file2	file2/oldf
	:100644 000000 0d92e9f3374ae2947c23aa477cbc68ce598135f1 0000000000000000000000000000000000000000 D	file3
	EOF
	git diff-tree -M -r master branch >actual &&
	compare_diff_raw expect actual
'

###
### series D
###

test_expect_success 'D: inline data in commit' '
	test_tick &&
	cat >input <<-INPUT_END &&
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

	git fast-import <input &&
	git whatchanged branch
'

test_expect_success 'D: verify pack' '
	verify_packs
'

test_expect_success 'D: validate new files added' '
	cat >expect <<-EOF &&
	:000000 100755 0000000000000000000000000000000000000000 e74b7d465e52746be2b4bae983670711e6e66657 A	newdir/exec.sh
	:000000 100644 0000000000000000000000000000000000000000 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 A	newdir/interesting
	EOF
	git diff-tree -M -r branch^ branch >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'D: verify file5' '
	echo "$file5_data" >expect &&
	git cat-file blob branch:newdir/interesting >actual &&
	test_cmp expect actual
'

test_expect_success 'D: verify file6' '
	echo "$file6_data" >expect &&
	git cat-file blob branch:newdir/exec.sh >actual &&
	test_cmp expect actual
'

###
### series E
###

test_expect_success 'E: rfc2822 date, --date-format=raw' '
	cat >input <<-INPUT_END &&
	commit refs/heads/branch
	author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> Tue Feb 6 11:22:18 2007 -0500
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> Tue Feb 6 12:35:02 2007 -0500
	data <<COMMIT
	RFC 2822 type date
	COMMIT

	from refs/heads/branch^0

	INPUT_END

	test_must_fail git fast-import --date-format=raw <input
'
test_expect_success 'E: rfc2822 date, --date-format=rfc2822' '
	git fast-import --date-format=rfc2822 <input
'

test_expect_success 'E: verify pack' '
	verify_packs
'

test_expect_success 'E: verify commit' '
	cat >expect <<-EOF &&
	author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> 1170778938 -0500
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1170783302 -0500

	RFC 2822 type date
	EOF
	git cat-file commit branch | sed 1,2d >actual &&
	test_cmp expect actual
'

###
### series F
###

test_expect_success 'F: non-fast-forward update skips' '
	old_branch=$(git rev-parse --verify branch^0) &&
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/branch
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	losing things already?
	COMMIT

	from refs/heads/branch~1

	reset refs/heads/other
	from refs/heads/branch

	INPUT_END

	test_must_fail git fast-import <input &&
	# branch must remain unaffected
	test $old_branch = $(git rev-parse --verify branch^0)
'

test_expect_success 'F: verify pack' '
	verify_packs
'

test_expect_success 'F: verify other commit' '
	cat >expect <<-EOF &&
	tree $(git rev-parse branch~1^{tree})
	parent $(git rev-parse branch~1)
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	losing things already?
	EOF
	git cat-file commit other >actual &&
	test_cmp expect actual
'

###
### series G
###

test_expect_success 'G: non-fast-forward update forced' '
	old_branch=$(git rev-parse --verify branch^0) &&
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/branch
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	losing things already?
	COMMIT

	from refs/heads/branch~1

	INPUT_END
	git fast-import --force <input
'

test_expect_success 'G: verify pack' '
	verify_packs
'

test_expect_success 'G: branch changed, but logged' '
	test $old_branch != $(git rev-parse --verify branch^0) &&
	test $old_branch = $(git rev-parse --verify branch@{1})
'

###
### series H
###

test_expect_success 'H: deletall, add 1' '
	test_tick &&
	cat >input <<-INPUT_END &&
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
	git fast-import <input &&
	git whatchanged H
'

test_expect_success 'H: verify pack' '
	verify_packs
'

test_expect_success 'H: validate old files removed, new files added' '
	cat >expect <<-EOF &&
	:100755 000000 f1fb5da718392694d0076d677d6d0e364c79b0bc 0000000000000000000000000000000000000000 D	file2/newf
	:100644 000000 7123f7f44e39be127c5eb701e5968176ee9d78b1 0000000000000000000000000000000000000000 D	file2/oldf
	:100755 000000 85df50785d62d3b05ab03d9cbf7e4a0b49449730 0000000000000000000000000000000000000000 D	file4
	:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 R100	newdir/interesting	h/e/l/lo
	:100755 000000 e74b7d465e52746be2b4bae983670711e6e66657 0000000000000000000000000000000000000000 D	newdir/exec.sh
	EOF
	git diff-tree -M -r H^ H >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'H: verify file' '
	echo "$file5_data" >expect &&
	git cat-file blob H:h/e/l/lo >actual &&
	test_cmp expect actual
'

###
### series I
###

test_expect_success 'I: export-pack-edges' '
	cat >input <<-INPUT_END &&
	commit refs/heads/export-boundary
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	we have a border.  its only 40 characters wide.
	COMMIT

	from refs/heads/branch

	INPUT_END
	git fast-import --export-pack-edges=edges.list <input
'

test_expect_success 'I: verify edge list' '
	cat >expect <<-EOF &&
	.git/objects/pack/pack-.pack: $(git rev-parse --verify export-boundary)
	EOF
	sed -e s/pack-.*pack/pack-.pack/ edges.list >actual &&
	test_cmp expect actual
'

###
### series J
###

test_expect_success 'J: reset existing branch creates empty commit' '
	cat >input <<-INPUT_END &&
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
	git fast-import <input
'
test_expect_success 'J: branch has 1 commit, empty tree' '
	test 1 = $(git rev-list J | wc -l) &&
	test 0 = $(git ls-tree J | wc -l)
'

test_expect_success 'J: tag must fail on empty branch' '
	cat >input <<-INPUT_END &&
	reset refs/heads/J2

	tag wrong_tag
	from refs/heads/J2
	data <<EOF
	Tag branch that was reset.
	EOF
	INPUT_END
	test_must_fail git fast-import <input
'

###
### series K
###

test_expect_success 'K: reinit branch with from' '
	cat >input <<-INPUT_END &&
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
	git fast-import <input
'
test_expect_success 'K: verify K^1 = branch^1' '
	test $(git rev-parse --verify branch^1) \
		= $(git rev-parse --verify K^1)
'

###
### series L
###

test_expect_success 'L: verify internal tree sorting' '
	cat >input <<-INPUT_END &&
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

	cat >expect <<-EXPECT_END &&
	:100644 100644 4268632... 55d3a52... M	b.
	:040000 040000 0ae5cac... 443c768... M	b
	:100644 100644 4268632... 55d3a52... M	ba
	EXPECT_END

	git fast-import <input &&
	GIT_PRINT_SHA1_ELLIPSIS="yes" git diff-tree --abbrev --raw L^ L >output &&
	test_cmp expect output
'

test_expect_success 'L: nested tree copy does not corrupt deltas' '
	cat >input <<-INPUT_END &&
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

	cat >expect <<-\EOF &&
	g/b/f
	g/b/h
	EOF

	test_when_finished "git update-ref -d refs/heads/L2" &&
	git fast-import <input &&
	git ls-tree L2 g/b/ >tmp &&
	cat tmp | cut -f 2 >actual &&
	test_cmp expect actual &&
	git fsck $(git rev-parse L2)
'

###
### series M
###

test_expect_success 'M: rename file in same subdirectory' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/M1
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	file rename
	COMMIT

	from refs/heads/branch^0
	R file2/newf file2/n.e.w.f

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	file2/newf	file2/n.e.w.f
	EOF
	git fast-import <input &&
	git diff-tree -M -r M1^ M1 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'M: rename file to new subdirectory' '
	cat >input <<-INPUT_END &&
	commit refs/heads/M2
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	file rename
	COMMIT

	from refs/heads/branch^0
	R file2/newf i/am/new/to/you

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	file2/newf	i/am/new/to/you
	EOF
	git fast-import <input &&
	git diff-tree -M -r M2^ M2 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'M: rename subdirectory to new subdirectory' '
	cat >input <<-INPUT_END &&
	commit refs/heads/M3
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	file rename
	COMMIT

	from refs/heads/M2^0
	R i other/sub

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	i/am/new/to/you	other/sub/am/new/to/you
	EOF
	git fast-import <input &&
	git diff-tree -M -r M3^ M3 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'M: rename root to subdirectory' '
	cat >input <<-INPUT_END &&
	commit refs/heads/M4
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	rename root
	COMMIT

	from refs/heads/M2^0
	R "" sub

	INPUT_END

	cat >expect <<-EOF &&
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 R100	file2/oldf	sub/file2/oldf
	:100755 100755 85df50785d62d3b05ab03d9cbf7e4a0b49449730 85df50785d62d3b05ab03d9cbf7e4a0b49449730 R100	file4	sub/file4
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc R100	i/am/new/to/you	sub/i/am/new/to/you
	:100755 100755 e74b7d465e52746be2b4bae983670711e6e66657 e74b7d465e52746be2b4bae983670711e6e66657 R100	newdir/exec.sh	sub/newdir/exec.sh
	:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 R100	newdir/interesting	sub/newdir/interesting
	EOF
	git fast-import <input &&
	git diff-tree -M -r M4^ M4 >actual &&
	cat actual &&
	compare_diff_raw expect actual
'

###
### series N
###

test_expect_success 'N: copy file in same subdirectory' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/N1
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	file copy
	COMMIT

	from refs/heads/branch^0
	C file2/newf file2/n.e.w.f

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file2/n.e.w.f
	EOF
	git fast-import <input &&
	git diff-tree -C --find-copies-harder -r N1^ N1 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: copy then modify subdirectory' '
	cat >input <<-INPUT_END &&
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

	cat >expect <<-EOF &&
	:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 C100	newdir/interesting	file3/file5
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	file3/newf
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 C100	file2/oldf	file3/oldf
	EOF
	git fast-import <input &&
	git diff-tree -C --find-copies-harder -r N2^^ N2 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: copy dirty subdirectory' '
	cat >input <<-INPUT_END &&
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

	git fast-import <input &&
	test $(git rev-parse N2^{tree}) = $(git rev-parse N3^{tree})
'

test_expect_success 'N: copy directory by id' '
	cat >expect <<-\EOF &&
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
	compare_diff_raw expect actual
'

test_expect_success PIPE 'N: read and copy directory' '
	cat >expect <<-\EOF &&
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
	sed "s/$OID_REGEX/OBJNAME/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'N: copy root directory by tree hash' '
	cat >expect <<-\EOF &&
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
	compare_diff_raw expect actual
'

test_expect_success 'N: copy root by path' '
	cat >expect <<-\EOF &&
	:100755 100755 f1fb5da718392694d0076d677d6d0e364c79b0bc f1fb5da718392694d0076d677d6d0e364c79b0bc C100	file2/newf	oldroot/file2/newf
	:100644 100644 7123f7f44e39be127c5eb701e5968176ee9d78b1 7123f7f44e39be127c5eb701e5968176ee9d78b1 C100	file2/oldf	oldroot/file2/oldf
	:100755 100755 85df50785d62d3b05ab03d9cbf7e4a0b49449730 85df50785d62d3b05ab03d9cbf7e4a0b49449730 C100	file4	oldroot/file4
	:100755 100755 e74b7d465e52746be2b4bae983670711e6e66657 e74b7d465e52746be2b4bae983670711e6e66657 C100	newdir/exec.sh	oldroot/newdir/exec.sh
	:100644 100644 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 fcf778cda181eaa1cbc9e9ce3a2e15ee9f9fe791 C100	newdir/interesting	oldroot/newdir/interesting
	EOF
	cat >input <<-INPUT_END &&
	commit refs/heads/N-copy-root-path
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy root directory by (empty) path
	COMMIT

	from refs/heads/branch^0
	C "" oldroot
	INPUT_END
	git fast-import <input &&
	git diff-tree -C --find-copies-harder -r branch N-copy-root-path >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: delete directory by copying' '
	cat >expect <<-\EOF &&
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
		sed -e "s/$OID_REGEX/OBJID/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'N: modify copied tree' '
	cat >expect <<-\EOF &&
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
	compare_diff_raw expect actual
'

test_expect_success 'N: reject foo/ syntax' '
	subdir=$(git rev-parse refs/heads/branch^0:file2) &&
	test_must_fail git fast-import <<-INPUT_END
	commit refs/heads/N5B
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy with invalid syntax
	COMMIT

	from refs/heads/branch^0
	M 040000 $subdir file3/
	INPUT_END
'

test_expect_success 'N: reject foo/ syntax in copy source' '
	test_must_fail git fast-import <<-INPUT_END
	commit refs/heads/N5C
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy with invalid syntax
	COMMIT

	from refs/heads/branch^0
	C file2/ file3
	INPUT_END
'

test_expect_success 'N: reject foo/ syntax in rename source' '
	test_must_fail git fast-import <<-INPUT_END
	commit refs/heads/N5D
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	rename with invalid syntax
	COMMIT

	from refs/heads/branch^0
	R file2/ file3
	INPUT_END
'

test_expect_success 'N: reject foo/ syntax in ls argument' '
	test_must_fail git fast-import <<-INPUT_END
	commit refs/heads/N5E
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	copy with invalid syntax
	COMMIT

	from refs/heads/branch^0
	ls "file2/"
	INPUT_END
'

test_expect_success 'N: copy to root by id and modify' '
	echo "hello, world" >expect.foo &&
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
	test_cmp expect.bar actual.bar
'

test_expect_success 'N: extract subtree' '
	branch=$(git rev-parse --verify refs/heads/branch^{tree}) &&
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
	git diff --exit-code branch:newdir N9
'

test_expect_success 'N: modify subtree, extract it, and modify again' '
	echo hello >expect.baz &&
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

test_expect_success 'O: comments are all skipped' '
	cat >input <<-INPUT_END &&
	#we will
	commit refs/heads/O1
	# -- ignore all of this text
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	# $GIT_COMMITTER_NAME has inserted here for his benefit.
	data <<COMMIT
	dirty directory copy
	COMMIT

	# do not forget the import blank line!
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

	# do not forget to copy file2 to file3
	C file2 file3
	#
	# or to delete file5 from file2.
	D file2/file5
	# are we done yet?

	INPUT_END

	git fast-import <input &&
	test $(git rev-parse N3) = $(git rev-parse O1)
'

test_expect_success 'O: blank lines not necessary after data commands' '
	cat >input <<-INPUT_END &&
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

	git fast-import <input &&
	test $(git rev-parse N3) = $(git rev-parse O2)
'

test_expect_success 'O: repack before next test' '
	git repack -a -d
'

test_expect_success 'O: blank lines not necessary after other commands' '
	cat >input <<-INPUT_END &&
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

	cat >expect <<-INPUT_END &&
	string
	of
	empty
	commits
	INPUT_END

	git fast-import <input &&
	test 8 = $(find .git/objects/pack -type f | grep -v multi-pack-index | wc -l) &&
	test $(git rev-parse refs/tags/O3-2nd) = $(git rev-parse O3^) &&
	git log --reverse --pretty=oneline O3 | sed s/^.*z// >actual &&
	test_cmp expect actual
'

test_expect_success 'O: progress outputs as requested by input' '
	cat >input <<-INPUT_END &&
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
	progress done!
	INPUT_END
	git fast-import <input >actual &&
	grep "progress " <input >expect &&
	test_cmp expect actual
'

###
### series P (gitlinks)
###

test_expect_success 'P: superproject & submodule mix' '
	cat >input <<-INPUT_END &&
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
		url = "$(pwd)/sub"
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

	git fast-import <input &&
	git checkout subuse1 &&
	rm -rf sub &&
	mkdir sub &&
	(
		cd sub &&
		git init &&
		git fetch --update-head-ok .. refs/heads/sub:refs/heads/master &&
		git checkout master
	) &&
	git submodule init &&
	git submodule update
'

test_expect_success 'P: verbatim SHA gitlinks' '
	SUBLAST=$(git rev-parse --verify sub) &&
	SUBPREV=$(git rev-parse --verify sub^) &&

	cat >input <<-INPUT_END &&
	blob
	mark :1
	data <<DATAEND
	[submodule "sub"]
		path = sub
		url = "$(pwd)/sub"
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

	git branch -D sub &&
	git gc &&
	git prune &&
	git fast-import <input &&
	test $(git rev-parse --verify subuse2) = $(git rev-parse --verify subuse1)
'

test_expect_success 'P: fail on inline gitlink' '
	test_tick &&
	cat >input <<-INPUT_END &&
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

	test_must_fail git fast-import <input
'

test_expect_success 'P: fail on blob mark in gitlink' '
	test_tick &&
	cat >input <<-INPUT_END &&
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

	test_must_fail git fast-import <input
'

###
### series Q (notes)
###

test_expect_success 'Q: commit notes' '
	note1_data="The first note for the first commit" &&
	note2_data="The first note for the second commit" &&
	note3_data="The first note for the third commit" &&
	note1b_data="The second note for the first commit" &&
	note1c_data="The third note for the first commit" &&
	note2b_data="The second note for the second commit" &&

	test_tick &&
	cat >input <<-INPUT_END &&
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

	git fast-import <input &&
	git whatchanged notes-test
'

test_expect_success 'Q: verify pack' '
	verify_packs
'

test_expect_success 'Q: verify first commit' '
	commit1=$(git rev-parse notes-test~2) &&
	commit2=$(git rev-parse notes-test^) &&
	commit3=$(git rev-parse notes-test) &&

	cat >expect <<-EOF &&
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	first (:3)
	EOF
	git cat-file commit notes-test~2 | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second commit' '
	cat >expect <<-EOF &&
	parent $commit1
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	second (:5)
	EOF
	git cat-file commit notes-test^ | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third commit' '
	cat >expect <<-EOF &&
	parent $commit2
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	third (:6)
	EOF
	git cat-file commit notes-test | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first notes commit' '
	cat >expect <<-EOF &&
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	notes (:9)
	EOF
	git cat-file commit refs/notes/foobar~2 | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $commit1
	100644 blob $commit2
	100644 blob $commit3
	EOF
	cat expect.unsorted | sort >expect &&
	git cat-file -p refs/notes/foobar~2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for first commit' '
	echo "$note1_data" >expect &&
	git cat-file blob refs/notes/foobar~2:$commit1 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for second commit' '
	echo "$note2_data" >expect &&
	git cat-file blob refs/notes/foobar~2:$commit2 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for third commit' '
	echo "$note3_data" >expect &&
	git cat-file blob refs/notes/foobar~2:$commit3 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second notes commit' '
	cat >expect <<-EOF &&
	parent $(git rev-parse --verify refs/notes/foobar~2)
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	notes (:10)
	EOF
	git cat-file commit refs/notes/foobar^ | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $commit1
	100644 blob $commit2
	100644 blob $commit3
	EOF
	cat expect.unsorted | sort >expect &&
	git cat-file -p refs/notes/foobar^^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second note for first commit' '
	echo "$note1b_data" >expect &&
	git cat-file blob refs/notes/foobar^:$commit1 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for second commit' '
	echo "$note2_data" >expect &&
	git cat-file blob refs/notes/foobar^:$commit2 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for third commit' '
	echo "$note3_data" >expect &&
	git cat-file blob refs/notes/foobar^:$commit3 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third notes commit' '
	cat >expect <<-EOF &&
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	notes (:11)
	EOF
	git cat-file commit refs/notes/foobar2 | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $commit1
	EOF
	cat expect.unsorted | sort >expect &&
	git cat-file -p refs/notes/foobar2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third note for first commit' '
	echo "$note1c_data" >expect &&
	git cat-file blob refs/notes/foobar2:$commit1 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify fourth notes commit' '
	cat >expect <<-EOF &&
	parent $(git rev-parse --verify refs/notes/foobar^)
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	notes (:12)
	EOF
	git cat-file commit refs/notes/foobar | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify fourth notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $commit2
	EOF
	cat expect.unsorted | sort >expect &&
	git cat-file -p refs/notes/foobar^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second note for second commit' '
	echo "$note2b_data" >expect &&
	git cat-file blob refs/notes/foobar:$commit2 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: deny note on empty branch' '
	cat >input <<-EOF &&
	reset refs/heads/Q0

	commit refs/heads/note-Q0
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	Note for an empty branch.
	COMMIT

	N inline refs/heads/Q0
	data <<NOTE
	some note
	NOTE
	EOF
	test_must_fail git fast-import <input
'
###
### series R (feature and option)
###

test_expect_success 'R: abort on unsupported feature' '
	cat >input <<-EOF &&
	feature no-such-feature-exists
	EOF

	test_must_fail git fast-import <input
'

test_expect_success 'R: supported feature is accepted' '
	cat >input <<-EOF &&
	feature date-format=now
	EOF

	git fast-import <input
'

test_expect_success 'R: abort on receiving feature after data command' '
	cat >input <<-EOF &&
	blob
	data 3
	hi
	feature date-format=now
	EOF

	test_must_fail git fast-import <input
'

test_expect_success 'R: only one import-marks feature allowed per stream' '
	cat >input <<-EOF &&
	feature import-marks=git.marks
	feature import-marks=git2.marks
	EOF

	test_must_fail git fast-import <input
'

test_expect_success 'R: export-marks feature results in a marks file being created' '
	cat >input <<-EOF &&
	feature export-marks=git.marks
	blob
	mark :1
	data 3
	hi

	EOF

	cat input | git fast-import &&
	grep :1 git.marks
'

test_expect_success 'R: export-marks options can be overridden by commandline options' '
	cat input | git fast-import --export-marks=other.marks &&
	grep :1 other.marks
'

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

	git fast-import --export-marks=io.marks <<-\EOF &&
	feature import-marks-if-exists=not_io.marks
	EOF
	test_must_be_empty io.marks &&

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

	git fast-import --import-marks-if-exists=not_io.marks \
			--export-marks=io.marks <<-\EOF &&
	feature import-marks-if-exists=io.marks
	EOF
	test_must_be_empty io.marks
'

test_expect_success 'R: import to output marks works without any content' '
	cat >input <<-EOF &&
	feature import-marks=marks.out
	feature export-marks=marks.new
	EOF

	cat input | git fast-import &&
	test_cmp marks.out marks.new
'

test_expect_success 'R: import marks prefers commandline marks file over the stream' '
	cat >input <<-EOF &&
	feature import-marks=nonexistent.marks
	feature export-marks=marks.new
	EOF

	cat input | git fast-import --import-marks=marks.out &&
	test_cmp marks.out marks.new
'


test_expect_success 'R: multiple --import-marks= should be honoured' '
	cat >input <<-EOF &&
	feature import-marks=nonexistent.marks
	feature export-marks=combined.marks
	EOF

	head -n2 marks.out > one.marks &&
	tail -n +3 marks.out > two.marks &&
	git fast-import --import-marks=one.marks --import-marks=two.marks <input &&
	test_cmp marks.out combined.marks
'

test_expect_success 'R: feature relative-marks should be honoured' '
	cat >input <<-EOF &&
	feature relative-marks
	feature import-marks=relative.in
	feature export-marks=relative.out
	EOF

	mkdir -p .git/info/fast-import/ &&
	cp marks.new .git/info/fast-import/relative.in &&
	git fast-import <input &&
	test_cmp marks.new .git/info/fast-import/relative.out
'

test_expect_success 'R: feature no-relative-marks should be honoured' '
	cat >input <<-EOF &&
	feature relative-marks
	feature import-marks=relative.in
	feature no-relative-marks
	feature export-marks=non-relative.out
	EOF

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

test_expect_success !MINGW 'R: print old blob' '
	blob=$(echo "yes it can" | git hash-object -w --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 11
	yes it can

	EOF
	echo "cat-blob $blob" |
	git fast-import --cat-blob-fd=6 6>actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'R: in-stream cat-blob-fd not respected' '
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
	test_must_be_empty actual.1 &&
	git fast-import 3>actual.3 >actual.1 <<-EOF &&
	option cat-blob-fd=3
	cat-blob $blob
	EOF
	test_must_be_empty actual.3 &&
	test_cmp expect actual.1
'

test_expect_success !MINGW 'R: print mark for new blob' '
	echo "effluentish" | git hash-object --stdin >expect &&
	git fast-import --cat-blob-fd=6 6>actual <<-\EOF &&
	blob
	mark :1
	data <<BLOB_END
	effluentish
	BLOB_END
	get-mark :1
	EOF
	test_cmp expect actual
'

test_expect_success !MINGW 'R: print new blob' '
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

test_expect_success !MINGW 'R: print new blob by sha1' '
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
		test_copy_bytes $size >blob <&3 &&
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
		test_copy_bytes $size >actual <&3 &&
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
		test_copy_bytes $size >actual <&3 &&
		read newline <&3 &&

		echo deleteall
	) |
	git fast-import --cat-blob-fd=3 3>blobs &&
	test_cmp expect actual
'

test_expect_success 'R: quiet option results in no stats being output' '
	cat >input <<-EOF &&
	option git quiet
	blob
	data 3
	hi

	EOF

	cat input | git fast-import 2> output &&
	test_must_be_empty output
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
	sed -e "s/$OID_REGEX/OBJID/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'R: die on unknown option' '
	cat >input <<-EOF &&
	option git non-existing-option
	EOF

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

test_expect_success 'R: ignore non-git options' '
	cat >input <<-EOF &&
	option non-existing-vcs non-existing-option
	EOF

	git fast-import <input
'

test_expect_success 'R: corrupt lines do not mess marks file' '
	rm -f io.marks &&
	blob=$(echo hi | git hash-object --stdin) &&
	cat >expect <<-EOF &&
	:3 0000000000000000000000000000000000000000
	:1 $blob
	:2 $blob
	EOF
	cp expect io.marks &&
	test_must_fail git fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF &&

	EOF
	test_cmp expect io.marks
'

##
## R: very large blobs
##
test_expect_success 'R: blob bigger than threshold' '
	blobsize=$((2*1024*1024 + 53)) &&
	test-tool genrandom bar $blobsize >expect &&
	cat >input <<-INPUT_END &&
	commit refs/heads/big-file
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	R - big file
	COMMIT

	M 644 inline big1
	data $blobsize
	INPUT_END
	cat expect >>input &&
	cat >>input <<-INPUT_END &&
	M 644 inline big2
	data $blobsize
	INPUT_END
	cat expect >>input &&
	echo >>input &&

	test_create_repo R &&
	git --git-dir=R/.git config fastimport.unpackLimit 0 &&
	git --git-dir=R/.git fast-import --big-file-threshold=1 <input
'

test_expect_success 'R: verify created pack' '
	(
		cd R &&
		verify_packs -v > ../verify
	)
'

test_expect_success 'R: verify written objects' '
	git --git-dir=R/.git cat-file blob big-file:big1 >actual &&
	test_cmp_bin expect actual &&
	a=$(git --git-dir=R/.git rev-parse big-file:big1) &&
	b=$(git --git-dir=R/.git rev-parse big-file:big2) &&
	test $a = $b
'

test_expect_success 'R: blob appears only once' '
	n=$(grep $a verify | wc -l) &&
	test 1 = $n
'

###
### series S
###
#
# Make sure missing spaces and EOLs after mark references
# cause errors.
#
# Setup:
#
#   1--2--4
#    \   /
#     -3-
#
#   commit marks:  301, 302, 303, 304
#   blob marks:              403, 404, resp.
#   note mark:          202
#
# The error message when a space is missing not at the
# end of the line is:
#
#   Missing space after ..
#
# or when extra characters come after the mark at the end
# of the line:
#
#   Garbage after ..
#
# or when the dataref is neither "inline " or a known SHA1,
#
#   Invalid dataref ..
#
test_expect_success 'S: initialize for S tests' '
	test_tick &&

	cat >input <<-INPUT_END &&
	commit refs/heads/S
	mark :301
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit 1
	COMMIT
	M 100644 inline hello.c
	data <<BLOB
	blob 1
	BLOB

	commit refs/heads/S
	mark :302
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit 2
	COMMIT
	from :301
	M 100644 inline hello.c
	data <<BLOB
	blob 2
	BLOB

	blob
	mark :403
	data <<BLOB
	blob 3
	BLOB

	blob
	mark :202
	data <<BLOB
	note 2
	BLOB
	INPUT_END

	git fast-import --export-marks=marks <input
'

#
# filemodify, three datarefs
#
test_expect_success 'S: filemodify with garbage after mark must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit N
	COMMIT
	M 100644 :403x hello.c
	EOF
	test_i18ngrep "space after mark" err
'

# inline is misspelled; fast-import thinks it is some unknown dataref
test_expect_success 'S: filemodify with garbage after inline must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit N
	COMMIT
	M 100644 inlineX hello.c
	data <<BLOB
	inline
	BLOB
	EOF
	test_i18ngrep "nvalid dataref" err
'

test_expect_success 'S: filemodify with garbage after sha1 must fail' '
	sha1=$(grep :403 marks | cut -d\  -f2) &&
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit N
	COMMIT
	M 100644 ${sha1}x hello.c
	EOF
	test_i18ngrep "space after SHA1" err
'

#
# notemodify, three ways to say dataref
#
test_expect_success 'S: notemodify with garbage after mark dataref must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit S note dataref markref
	COMMIT
	N :202x :302
	EOF
	test_i18ngrep "space after mark" err
'

test_expect_success 'S: notemodify with garbage after inline dataref must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit S note dataref inline
	COMMIT
	N inlineX :302
	data <<BLOB
	note blob
	BLOB
	EOF
	test_i18ngrep "nvalid dataref" err
'

test_expect_success 'S: notemodify with garbage after sha1 dataref must fail' '
	sha1=$(grep :202 marks | cut -d\  -f2) &&
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit S note dataref sha1
	COMMIT
	N ${sha1}x :302
	EOF
	test_i18ngrep "space after SHA1" err
'

#
# notemodify, mark in commit-ish
#
test_expect_success 'S: notemodify with garbage after mark commit-ish must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/Snotes
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit S note commit-ish
	COMMIT
	N :202 :302x
	EOF
	test_i18ngrep "after mark" err
'

#
# from
#
test_expect_success 'S: from with garbage after mark must fail' '
	test_must_fail \
	git fast-import --import-marks=marks --export-marks=marks <<-EOF 2>err &&
	commit refs/heads/S2
	mark :303
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit 3
	COMMIT
	from :301x
	M 100644 :403 hello.c
	EOF


	# go create the commit, need it for merge test
	git fast-import --import-marks=marks --export-marks=marks <<-EOF &&
	commit refs/heads/S2
	mark :303
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	commit 3
	COMMIT
	from :301
	M 100644 :403 hello.c
	EOF

	# now evaluate the error
	test_i18ngrep "after mark" err
'


#
# merge
#
test_expect_success 'S: merge with garbage after mark must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	commit refs/heads/S
	mark :304
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	merge 4
	COMMIT
	from :302
	merge :303x
	M 100644 :403 hello.c
	EOF
	test_i18ngrep "after mark" err
'

#
# tag, from markref
#
test_expect_success 'S: tag with garbage after mark must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	tag refs/tags/Stag
	from :302x
	tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<TAG
	tag S
	TAG
	EOF
	test_i18ngrep "after mark" err
'

#
# cat-blob markref
#
test_expect_success 'S: cat-blob with garbage after mark must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	cat-blob :403x
	EOF
	test_i18ngrep "after mark" err
'

#
# ls markref
#
test_expect_success 'S: ls with garbage after mark must fail' '
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	ls :302x hello.c
	EOF
	test_i18ngrep "space after mark" err
'

test_expect_success 'S: ls with garbage after sha1 must fail' '
	sha1=$(grep :302 marks | cut -d\  -f2) &&
	test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
	ls ${sha1}x hello.c
	EOF
	test_i18ngrep "space after tree-ish" err
'

###
### series T (ls)
###
# Setup is carried over from series S.

test_expect_success 'T: ls root tree' '
	sed -e "s/Z\$//" >expect <<-EOF &&
	040000 tree $(git rev-parse S^{tree})	Z
	EOF
	sha1=$(git rev-parse --verify S) &&
	git fast-import --import-marks=marks <<-EOF >actual &&
	ls $sha1 ""
	EOF
	test_cmp expect actual
'

test_expect_success 'T: delete branch' '
	git branch to-delete &&
	git fast-import <<-EOF &&
	reset refs/heads/to-delete
	from 0000000000000000000000000000000000000000
	EOF
	test_must_fail git rev-parse --verify refs/heads/to-delete
'

test_expect_success 'T: empty reset doesnt delete branch' '
	git branch not-to-delete &&
	git fast-import <<-EOF &&
	reset refs/heads/not-to-delete
	EOF
	git show-ref &&
	git rev-parse --verify refs/heads/not-to-delete
'

###
### series U (filedelete)
###

test_expect_success 'U: initialize for U tests' '
	cat >input <<-INPUT_END &&
	commit refs/heads/U
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	test setup
	COMMIT
	M 100644 inline hello.c
	data <<BLOB
	blob 1
	BLOB
	M 100644 inline good/night.txt
	data <<BLOB
	sleep well
	BLOB
	M 100644 inline good/bye.txt
	data <<BLOB
	au revoir
	BLOB

	INPUT_END

	git fast-import <input
'

test_expect_success 'U: filedelete file succeeds' '
	cat >input <<-INPUT_END &&
	commit refs/heads/U
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	delete good/night.txt
	COMMIT
	from refs/heads/U^0
	D good/night.txt

	INPUT_END

	git fast-import <input
'

test_expect_success 'U: validate file delete result' '
	cat >expect <<-EOF &&
	:100644 000000 2907ebb4bf85d91bf0716bb3bd8a68ef48d6da76 0000000000000000000000000000000000000000 D	good/night.txt
	EOF

	git diff-tree -M -r U^1 U >actual &&

	compare_diff_raw expect actual
'

test_expect_success 'U: filedelete directory succeeds' '
	cat >input <<-INPUT_END &&
	commit refs/heads/U
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	delete good dir
	COMMIT
	from refs/heads/U^0
	D good

	INPUT_END

	git fast-import <input
'

test_expect_success 'U: validate directory delete result' '
	cat >expect <<-EOF &&
	:100644 000000 69cb75792f55123d8389c156b0b41c2ff00ed507 0000000000000000000000000000000000000000 D	good/bye.txt
	EOF

	git diff-tree -M -r U^1 U >actual &&

	compare_diff_raw expect actual
'

test_expect_success 'U: filedelete root succeeds' '
	cat >input <<-INPUT_END &&
	commit refs/heads/U
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	must succeed
	COMMIT
	from refs/heads/U^0
	D ""

	INPUT_END

	git fast-import <input
'

test_expect_success 'U: validate root delete result' '
	cat >expect <<-EOF &&
	:100644 000000 c18147dc648481eeb65dc5e66628429a64843327 0000000000000000000000000000000000000000 D	hello.c
	EOF

	git diff-tree -M -r U^1 U >actual &&

	compare_diff_raw expect actual
'

###
### series V (checkpoint)
###

# The commands in input_file should not produce any output on the file
# descriptor set with --cat-blob-fd (or stdout if unspecified).
#
# To make sure you're observing the side effects of checkpoint *before*
# fast-import terminates (and thus writes out its state), check that the
# fast-import process is still running using background_import_still_running
# *after* evaluating the test conditions.
background_import_then_checkpoint () {
	options=$1
	input_file=$2

	mkfifo V.input
	exec 8<>V.input
	rm V.input

	mkfifo V.output
	exec 9<>V.output
	rm V.output

	git fast-import $options <&8 >&9 &
	echo $! >V.pid
	# We don't mind if fast-import has already died by the time the test
	# ends.
	test_when_finished "
		exec 8>&-; exec 9>&-;
		kill $(cat V.pid) && wait $(cat V.pid)
		true"

	# Start in the background to ensure we adhere strictly to (blocking)
	# pipes writing sequence. We want to assume that the write below could
	# block, e.g. if fast-import blocks writing its own output to &9
	# because there is no reader on &9 yet.
	(
		cat "$input_file"
		echo "checkpoint"
		echo "progress checkpoint"
	) >&8 &

	error=1 ;# assume the worst
	while read output <&9
	do
		if test "$output" = "progress checkpoint"
		then
			error=0
			break
		fi
		# otherwise ignore cruft
		echo >&2 "cruft: $output"
	done

	if test $error -eq 1
	then
		false
	fi
}

background_import_still_running () {
	if ! kill -0 "$(cat V.pid)"
	then
		echo >&2 "background fast-import terminated too early"
		false
	fi
}

test_expect_success PIPE 'V: checkpoint helper does not get stuck with extra output' '
	cat >input <<-INPUT_END &&
	progress foo
	progress bar

	INPUT_END

	background_import_then_checkpoint "" input &&
	background_import_still_running
'

test_expect_success PIPE 'V: checkpoint updates refs after reset' '
	cat >input <<-\INPUT_END &&
	reset refs/heads/V
	from refs/heads/U

	INPUT_END

	background_import_then_checkpoint "" input &&
	test "$(git rev-parse --verify V)" = "$(git rev-parse --verify U)" &&
	background_import_still_running
'

test_expect_success PIPE 'V: checkpoint updates refs and marks after commit' '
	cat >input <<-INPUT_END &&
	commit refs/heads/V
	mark :1
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0
	from refs/heads/U

	INPUT_END

	background_import_then_checkpoint "--export-marks=marks.actual" input &&

	echo ":1 $(git rev-parse --verify V)" >marks.expected &&

	test "$(git rev-parse --verify V^)" = "$(git rev-parse --verify U)" &&
	test_cmp marks.expected marks.actual &&
	background_import_still_running
'

# Re-create the exact same commit, but on a different branch: no new object is
# created in the database, but the refs and marks still need to be updated.
test_expect_success PIPE 'V: checkpoint updates refs and marks after commit (no new objects)' '
	cat >input <<-INPUT_END &&
	commit refs/heads/V2
	mark :2
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0
	from refs/heads/U

	INPUT_END

	background_import_then_checkpoint "--export-marks=marks.actual" input &&

	echo ":2 $(git rev-parse --verify V2)" >marks.expected &&

	test "$(git rev-parse --verify V2)" = "$(git rev-parse --verify V)" &&
	test_cmp marks.expected marks.actual &&
	background_import_still_running
'

test_expect_success PIPE 'V: checkpoint updates tags after tag' '
	cat >input <<-INPUT_END &&
	tag Vtag
	from refs/heads/V
	tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 0

	INPUT_END

	background_import_then_checkpoint "" input &&
	git show-ref -d Vtag &&
	background_import_still_running
'

###
### series W (get-mark and empty orphan commits)
###

cat >>W-input <<-W_INPUT_END
	commit refs/heads/W-branch
	mark :1
	author Full Name <user@company.tld> 1000000000 +0100
	committer Full Name <user@company.tld> 1000000000 +0100
	data 27
	Intentionally empty commit
	LFsget-mark :1
	W_INPUT_END

test_expect_success !MINGW 'W: get-mark & empty orphan commit with no newlines' '
	sed -e s/LFs// W-input | tr L "\n" | git fast-import
'

test_expect_success !MINGW 'W: get-mark & empty orphan commit with one newline' '
	sed -e s/LFs/L/ W-input | tr L "\n" | git fast-import
'

test_expect_success !MINGW 'W: get-mark & empty orphan commit with ugly second newline' '
	# Technically, this should fail as it has too many linefeeds
	# according to the grammar in fast-import.txt.  But, for whatever
	# reason, it works.  Since using the correct number of newlines
	# does not work with older (pre-2.22) versions of git, allow apps
	# that used this second-newline workaround to keep working by
	# checking it with this test...
	sed -e s/LFs/LL/ W-input | tr L "\n" | git fast-import
'

test_expect_success !MINGW 'W: get-mark & empty orphan commit with erroneous third newline' '
	# ...but do NOT allow more empty lines than that (see previous test).
	sed -e s/LFs/LLL/ W-input | tr L "\n" | test_must_fail git fast-import
'

###
### series X (other new features)
###

test_expect_success 'X: handling encoding' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/encoding
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	encoding iso-8859-7
	data <<COMMIT
	INPUT_END

	printf "Pi: \360\nCOMMIT\n" >>input &&

	git fast-import <input &&
	git cat-file -p encoding | grep $(printf "\360") &&
	git log -1 --format=%B encoding | grep $(printf "\317\200")
'

test_done
