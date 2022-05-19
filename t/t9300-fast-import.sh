#!/bin/sh
#
# Copyright (c) 2007 Shawn Pearce
#

test_description='test but fast-import utility'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh ;# test-lib chdir's into trash

verify_packs () {
	for p in .but/objects/pack/*.pack
	do
		but verify-pack "$@" "$p" || return
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
	but config fastimport.unpackLimit 0 &&
	but fast-import </dev/null
'

test_expect_success 'truncated stream complains' '
	echo "tag foo" | test_must_fail but fast-import
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
	cummit refs/heads/main
	mark :5
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	initial
	cummit

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
	from $ZERO_OID

	tag nested
	mark :6
	from :4
	data <<EOF
	Tag of our lovely cummit
	EOF

	reset refs/tags/nested
	from $ZERO_OID

	tag nested
	mark :7
	from :6
	data <<EOF
	Tag of tag of our lovely cummit
	EOF

	alias
	mark :8
	to :5

	INPUT_END
	but fast-import --export-marks=marks.out <input &&
	but whatchanged main
'

test_expect_success 'A: verify pack' '
	verify_packs
'

test_expect_success 'A: verify cummit' '
	cat >expect <<-EOF &&
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	initial
	EOF
	but cat-file cummit main | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tree' '
	cat >expect <<-EOF &&
	100644 blob file2
	100644 blob file3
	100755 blob file4
	EOF
	but cat-file -p main^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file2' '
	echo "$file2_data" >expect &&
	but cat-file blob main:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file3' '
	echo "$file3_data" >expect &&
	but cat-file blob main:file3 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file4' '
	printf "$file4_data" >expect &&
	but cat-file blob main:file4 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag/series-A' '
	cat >expect <<-EOF &&
	object $(but rev-parse refs/heads/main)
	type cummit
	tag series-A

	An annotated tag without a tagger
	EOF
	but cat-file tag tags/series-A >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag/series-A-blob' '
	cat >expect <<-EOF &&
	object $(but rev-parse refs/heads/main:file3)
	type blob
	tag series-A-blob

	An annotated tag that annotates a blob.
	EOF
	but cat-file tag tags/series-A-blob >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag deletion is successful' '
	test_must_fail but rev-parse --verify refs/tags/to-be-deleted
'

test_expect_success 'A: verify marks output' '
	cat >expect <<-EOF &&
	:2 $(but rev-parse --verify main:file2)
	:3 $(but rev-parse --verify main:file3)
	:4 $(but rev-parse --verify main:file4)
	:5 $(but rev-parse --verify main^0)
	:6 $(but cat-file tag nested | grep object | cut -d" " -f 2)
	:7 $(but rev-parse --verify nested)
	:8 $(but rev-parse --verify main^0)
	EOF
	test_cmp expect marks.out
'

test_expect_success 'A: verify marks import' '
	but fast-import \
		--import-marks=marks.out \
		--export-marks=marks.new \
		</dev/null &&
	test_cmp expect marks.new
'

test_expect_success 'A: tag blob by sha1' '
	test_tick &&
	new_blob=$(echo testing | but hash-object --stdin) &&
	cat >input <<-INPUT_END &&
	tag series-A-blob-2
	from $(but rev-parse refs/heads/main:file3)
	data <<EOF
	Tag blob by sha1.
	EOF

	blob
	mark :6
	data <<EOF
	testing
	EOF

	cummit refs/heads/new_blob
	cummitter  <> 0 +0000
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
	object $(but rev-parse refs/heads/main:file3)
	type blob
	tag series-A-blob-2

	Tag blob by sha1.
	object $new_blob
	type blob
	tag series-A-blob-3

	Tag new_blob.
	EOF

	but fast-import <input &&
	but cat-file tag tags/series-A-blob-2 >actual &&
	but cat-file tag tags/series-A-blob-3 >>actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify marks import does not crash' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/verify--import-marks
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	recreate from :5
	cummit

	from :5
	M 755 :2 copy-of-file2

	INPUT_END

	but fast-import --import-marks=marks.out <input &&
	but whatchanged verify--import-marks
'

test_expect_success 'A: verify pack' '
	verify_packs
'

test_expect_success 'A: verify diff' '
	copy=$(but rev-parse --verify main:file2) &&
	cat >expect <<-EOF &&
	:000000 100755 $ZERO_OID $copy A	copy-of-file2
	EOF
	but diff-tree -M -r main verify--import-marks >actual &&
	compare_diff_raw expect actual &&
	test $(but rev-parse --verify main:file2) \
	    = $(but rev-parse --verify verify--import-marks:copy-of-file2)
'

test_expect_success 'A: export marks with large values' '
	test_tick &&
	mt=$(but hash-object --stdin < /dev/null) &&
	>input.blob &&
	>marks.exp &&
	>tree.exp &&

	cat >input.cummit <<-EOF &&
	cummit refs/heads/verify--dump-marks
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	test the sparse array dumping routines with exponentially growing marks
	cummit
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
		echo "M 100644 :$l l$i" >>input.cummit &&
		echo "M 100644 :$m m$i" >>input.cummit &&
		echo "M 100644 :$n n$i" >>input.cummit &&

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

	cat input.blob input.cummit | but fast-import --export-marks=marks.large &&
	but ls-tree refs/heads/verify--dump-marks >tree.out &&
	test_cmp tree.exp_s tree.out &&
	test_cmp marks.exp marks.large
'

###
### series B
###

test_expect_success 'B: fail on invalid blob sha1' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/branch
	mark :1
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	corrupt
	cummit

	from refs/heads/main
	M 755 $(echo $ZERO_OID | sed -e "s/0$/1/") zero1

	INPUT_END

	test_when_finished "rm -f .but/objects/pack_* .but/objects/index_*" &&
	test_must_fail but fast-import <input
'

test_expect_success 'B: accept branch name "TEMP_TAG"' '
	cat >input <<-INPUT_END &&
	cummit TEMP_TAG
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	tag base
	cummit

	from refs/heads/main

	INPUT_END

	test_when_finished "rm -f .but/TEMP_TAG
		but gc
		but prune" &&
	but fast-import <input &&
	test $(test-tool ref-store main resolve-ref TEMP_TAG 0 | cut -f1 -d " " ) != "$ZERO_OID" &&
	test $(but rev-parse main) = $(but rev-parse TEMP_TAG^)
'

test_expect_success 'B: accept empty cummitter' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/empty-cummitter-1
	cummitter  <> $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/empty-cummitter-1
		but gc
		but prune" &&
	but fast-import <input &&
	out=$(but fsck) &&
	echo "$out" &&
	test -z "$out"
'

test_expect_success 'B: reject invalid timezone' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-timezone
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> 1234567890 +051800
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/invalid-timezone" &&
	test_must_fail but fast-import <input
'

test_expect_success 'B: accept invalid timezone with raw-permissive' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-timezone
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> 1234567890 +051800
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	but init invalid-timezone &&
	but -C invalid-timezone fast-import --date-format=raw-permissive <input &&
	but -C invalid-timezone cat-file -p invalid-timezone >out &&
	grep "1234567890 [+]051800" out
'

test_expect_success 'B: accept and fixup cummitter with no name' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/empty-cummitter-2
	cummitter <a@b.com> $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/empty-cummitter-2
		but gc
		but prune" &&
	but fast-import <input &&
	out=$(but fsck) &&
	echo "$out" &&
	test -z "$out"
'

test_expect_success 'B: fail on invalid cummitter (1)' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-cummitter
	cummitter Name email> $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/invalid-cummitter" &&
	test_must_fail but fast-import <input
'

test_expect_success 'B: fail on invalid cummitter (2)' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-cummitter
	cummitter Name <e<mail> $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/invalid-cummitter" &&
	test_must_fail but fast-import <input
'

test_expect_success 'B: fail on invalid cummitter (3)' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-cummitter
	cummitter Name <email>> $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/invalid-cummitter" &&
	test_must_fail but fast-import <input
'

test_expect_success 'B: fail on invalid cummitter (4)' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-cummitter
	cummitter Name <email $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/invalid-cummitter" &&
	test_must_fail but fast-import <input
'

test_expect_success 'B: fail on invalid cummitter (5)' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/invalid-cummitter
	cummitter Name<email> $BUT_CUMMITTER_DATE
	data <<cummit
	empty cummit
	cummit
	INPUT_END

	test_when_finished "but update-ref -d refs/heads/invalid-cummitter" &&
	test_must_fail but fast-import <input
'

###
### series C
###

test_expect_success 'C: incremental import create pack from stdin' '
	newf=$(echo hi newf | but hash-object -w --stdin) &&
	oldf=$(but rev-parse --verify main:file2) &&
	thrf=$(but rev-parse --verify main:file3) &&
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/branch
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	second
	cummit

	from refs/heads/main
	M 644 $oldf file2/oldf
	M 755 $newf file2/newf
	D file3

	INPUT_END

	but fast-import <input &&
	but whatchanged branch
'

test_expect_success 'C: verify pack' '
	verify_packs
'

test_expect_success 'C: validate reuse existing blob' '
	test $newf = $(but rev-parse --verify branch:file2/newf) &&
	test $oldf = $(but rev-parse --verify branch:file2/oldf)
'

test_expect_success 'C: verify cummit' '
	cat >expect <<-EOF &&
	parent $(but rev-parse --verify main^0)
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	second
	EOF

	but cat-file cummit branch | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'C: validate rename result' '
	zero=$ZERO_OID &&
	cat >expect <<-EOF &&
	:000000 100755 $zero $newf A	file2/newf
	:100644 100644 $oldf $oldf R100	file2	file2/oldf
	:100644 000000 $thrf $zero D	file3
	EOF
	but diff-tree -M -r main branch >actual &&
	compare_diff_raw expect actual
'

###
### series D
###

test_expect_success 'D: inline data in cummit' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/branch
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	third
	cummit

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

	but fast-import <input &&
	but whatchanged branch
'

test_expect_success 'D: verify pack' '
	verify_packs
'

test_expect_success 'D: validate new files added' '
	f5id=$(echo "$file5_data" | but hash-object --stdin) &&
	f6id=$(echo "$file6_data" | but hash-object --stdin) &&
	cat >expect <<-EOF &&
	:000000 100755 $ZERO_OID $f6id A	newdir/exec.sh
	:000000 100644 $ZERO_OID $f5id A	newdir/interesting
	EOF
	but diff-tree -M -r branch^ branch >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'D: verify file5' '
	echo "$file5_data" >expect &&
	but cat-file blob branch:newdir/interesting >actual &&
	test_cmp expect actual
'

test_expect_success 'D: verify file6' '
	echo "$file6_data" >expect &&
	but cat-file blob branch:newdir/exec.sh >actual &&
	test_cmp expect actual
'

###
### series E
###

test_expect_success 'E: rfc2822 date, --date-format=raw' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/branch
	author $BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL> Tue Feb 6 11:22:18 2007 -0500
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> Tue Feb 6 12:35:02 2007 -0500
	data <<cummit
	RFC 2822 type date
	cummit

	from refs/heads/branch^0

	INPUT_END

	test_must_fail but fast-import --date-format=raw <input
'
test_expect_success 'E: rfc2822 date, --date-format=rfc2822' '
	but fast-import --date-format=rfc2822 <input
'

test_expect_success 'E: verify pack' '
	verify_packs
'

test_expect_success 'E: verify cummit' '
	cat >expect <<-EOF &&
	author $BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL> 1170778938 -0500
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> 1170783302 -0500

	RFC 2822 type date
	EOF
	but cat-file cummit branch | sed 1,2d >actual &&
	test_cmp expect actual
'

###
### series F
###

test_expect_success 'F: non-fast-forward update skips' '
	old_branch=$(but rev-parse --verify branch^0) &&
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/branch
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	losing things already?
	cummit

	from refs/heads/branch~1

	reset refs/heads/other
	from refs/heads/branch

	INPUT_END

	test_must_fail but fast-import <input &&
	# branch must remain unaffected
	test $old_branch = $(but rev-parse --verify branch^0)
'

test_expect_success 'F: verify pack' '
	verify_packs
'

test_expect_success 'F: verify other cummit' '
	cat >expect <<-EOF &&
	tree $(but rev-parse branch~1^{tree})
	parent $(but rev-parse branch~1)
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	losing things already?
	EOF
	but cat-file cummit other >actual &&
	test_cmp expect actual
'

###
### series G
###

test_expect_success 'G: non-fast-forward update forced' '
	old_branch=$(but rev-parse --verify branch^0) &&
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/branch
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	losing things already?
	cummit

	from refs/heads/branch~1

	INPUT_END
	but fast-import --force <input
'

test_expect_success 'G: verify pack' '
	verify_packs
'

test_expect_success 'G: branch changed, but logged' '
	test $old_branch != $(but rev-parse --verify branch^0) &&
	test $old_branch = $(but rev-parse --verify branch@{1})
'

###
### series H
###

test_expect_success 'H: deletall, add 1' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/H
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	third
	cummit

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
	but fast-import <input &&
	but whatchanged H
'

test_expect_success 'H: verify pack' '
	verify_packs
'

test_expect_success 'H: validate old files removed, new files added' '
	f4id=$(but rev-parse HEAD:file4) &&
	cat >expect <<-EOF &&
	:100755 000000 $newf $zero D	file2/newf
	:100644 000000 $oldf $zero D	file2/oldf
	:100755 000000 $f4id $zero D	file4
	:100644 100644 $f5id $f5id R100	newdir/interesting	h/e/l/lo
	:100755 000000 $f6id $zero D	newdir/exec.sh
	EOF
	but diff-tree -M -r H^ H >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'H: verify file' '
	echo "$file5_data" >expect &&
	but cat-file blob H:h/e/l/lo >actual &&
	test_cmp expect actual
'

###
### series I
###

test_expect_success 'I: export-pack-edges' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/export-boundary
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	we have a border.  its only 40 characters wide.
	cummit

	from refs/heads/branch

	INPUT_END
	but fast-import --export-pack-edges=edges.list <input
'

test_expect_success 'I: verify edge list' '
	cat >expect <<-EOF &&
	.but/objects/pack/pack-.pack: $(but rev-parse --verify export-boundary)
	EOF
	sed -e s/pack-.*pack/pack-.pack/ edges.list >actual &&
	test_cmp expect actual
'

###
### series J
###

test_expect_success 'J: reset existing branch creates empty cummit' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/J
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	create J
	cummit

	from refs/heads/branch

	reset refs/heads/J

	cummit refs/heads/J
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	initialize J
	cummit

	INPUT_END
	but fast-import <input
'
test_expect_success 'J: branch has 1 cummit, empty tree' '
	test 1 = $(but rev-list J | wc -l) &&
	test 0 = $(but ls-tree J | wc -l)
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
	test_must_fail but fast-import <input
'

###
### series K
###

test_expect_success 'K: reinit branch with from' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/K
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	create K
	cummit

	from refs/heads/branch

	cummit refs/heads/K
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	redo K
	cummit

	from refs/heads/branch^1

	INPUT_END
	but fast-import <input
'
test_expect_success 'K: verify K^1 = branch^1' '
	test $(but rev-parse --verify branch^1) \
		= $(but rev-parse --verify K^1)
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

	cummit refs/heads/L
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	create L
	cummit

	M 644 :1 b.
	M 644 :1 b/other
	M 644 :1 ba

	cummit refs/heads/L
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	update L
	cummit

	M 644 :2 b.
	M 644 :2 b/other
	M 644 :2 ba
	INPUT_END

	cat >expect <<-EXPECT_END &&
	:100644 100644 M	b.
	:040000 040000 M	b
	:100644 100644 M	ba
	EXPECT_END

	but fast-import <input &&
	BUT_PRINT_SHA1_ELLIPSIS="yes" but diff-tree --abbrev --raw L^ L >output &&
	cut -d" " -f1,2,5 output >actual &&
	test_cmp expect actual
'

test_expect_success 'L: nested tree copy does not corrupt deltas' '
	cat >input <<-INPUT_END &&
	blob
	mark :1
	data <<EOF
	the data
	EOF

	cummit refs/heads/L2
	cummitter C O Mitter <cummitter@example.com> 1112912473 -0700
	data <<cummit
	init L2
	cummit
	M 644 :1 a/b/c
	M 644 :1 a/b/d
	M 644 :1 a/e/f

	cummit refs/heads/L2
	cummitter C O Mitter <cummitter@example.com> 1112912473 -0700
	data <<cummit
	update L2
	cummit
	C a g
	C a/e g/b
	M 644 :1 g/b/h
	INPUT_END

	cat >expect <<-\EOF &&
	g/b/f
	g/b/h
	EOF

	test_when_finished "but update-ref -d refs/heads/L2" &&
	but fast-import <input &&
	but ls-tree L2 g/b/ >tmp &&
	cat tmp | cut -f 2 >actual &&
	test_cmp expect actual &&
	but fsck $(but rev-parse L2)
'

###
### series M
###

test_expect_success 'M: rename file in same subdirectory' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/M1
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	file rename
	cummit

	from refs/heads/branch^0
	R file2/newf file2/n.e.w.f

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 $newf $newf R100	file2/newf	file2/n.e.w.f
	EOF
	but fast-import <input &&
	but diff-tree -M -r M1^ M1 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'M: rename file to new subdirectory' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/M2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	file rename
	cummit

	from refs/heads/branch^0
	R file2/newf i/am/new/to/you

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 $newf $newf R100	file2/newf	i/am/new/to/you
	EOF
	but fast-import <input &&
	but diff-tree -M -r M2^ M2 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'M: rename subdirectory to new subdirectory' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/M3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	file rename
	cummit

	from refs/heads/M2^0
	R i other/sub

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 $newf $newf R100	i/am/new/to/you	other/sub/am/new/to/you
	EOF
	but fast-import <input &&
	but diff-tree -M -r M3^ M3 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'M: rename root to subdirectory' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/M4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	rename root
	cummit

	from refs/heads/M2^0
	R "" sub

	INPUT_END

	cat >expect <<-EOF &&
	:100644 100644 $oldf $oldf R100	file2/oldf	sub/file2/oldf
	:100755 100755 $f4id $f4id R100	file4	sub/file4
	:100755 100755 $newf $newf R100	i/am/new/to/you	sub/i/am/new/to/you
	:100755 100755 $f6id $f6id R100	newdir/exec.sh	sub/newdir/exec.sh
	:100644 100644 $f5id $f5id R100	newdir/interesting	sub/newdir/interesting
	EOF
	but fast-import <input &&
	but diff-tree -M -r M4^ M4 >actual &&
	compare_diff_raw expect actual
'

###
### series N
###

test_expect_success 'N: copy file in same subdirectory' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/N1
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	file copy
	cummit

	from refs/heads/branch^0
	C file2/newf file2/n.e.w.f

	INPUT_END

	cat >expect <<-EOF &&
	:100755 100755 $newf $newf C100	file2/newf	file2/n.e.w.f
	EOF
	but fast-import <input &&
	but diff-tree -C --find-copies-harder -r N1^ N1 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: copy then modify subdirectory' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/N2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	clean directory copy
	cummit

	from refs/heads/branch^0
	C file2 file3

	cummit refs/heads/N2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	modify directory copy
	cummit

	M 644 inline file3/file5
	data <<EOF
	$file5_data
	EOF

	INPUT_END

	cat >expect <<-EOF &&
	:100644 100644 $f5id $f5id C100	newdir/interesting	file3/file5
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
	EOF
	but fast-import <input &&
	but diff-tree -C --find-copies-harder -r N2^^ N2 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: copy dirty subdirectory' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/N3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	dirty directory copy
	cummit

	from refs/heads/branch^0
	M 644 inline file2/file5
	data <<EOF
	$file5_data
	EOF

	C file2 file3
	D file2/file5

	INPUT_END

	but fast-import <input &&
	test $(but rev-parse N2^{tree}) = $(but rev-parse N3^{tree})
'

test_expect_success 'N: copy directory by id' '
	cat >expect <<-EOF &&
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
	EOF
	subdir=$(but rev-parse refs/heads/branch^0:file2) &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/N4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy by tree hash
	cummit

	from refs/heads/branch^0
	M 040000 $subdir file3
	INPUT_END
	but fast-import <input &&
	but diff-tree -C --find-copies-harder -r N4^ N4 >actual &&
	compare_diff_raw expect actual
'

test_expect_success PIPE 'N: read and copy directory' '
	cat >expect <<-EOF &&
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
	EOF
	but update-ref -d refs/heads/N4 &&
	rm -f backflow &&
	mkfifo backflow &&
	(
		exec <backflow &&
		cat <<-EOF &&
		cummit refs/heads/N4
		cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
		data <<cummit
		copy by tree hash, part 2
		cummit

		from refs/heads/branch^0
		ls "file2"
		EOF
		read mode type tree filename &&
		echo "M 040000 $tree file3"
	) |
	but fast-import --cat-blob-fd=3 3>backflow &&
	but diff-tree -C --find-copies-harder -r N4^ N4 >actual &&
	compare_diff_raw expect actual
'

test_expect_success PIPE 'N: empty directory reads as missing' '
	cat <<-\EOF >expect &&
	OBJNAME
	:000000 100644 OBJNAME OBJNAME A	unrelated
	EOF
	echo "missing src" >expect.response &&
	but update-ref -d refs/heads/read-empty &&
	rm -f backflow &&
	mkfifo backflow &&
	(
		exec <backflow &&
		cat <<-EOF &&
		cummit refs/heads/read-empty
		cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
		data <<cummit
		read "empty" (missing) directory
		cummit

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
	but fast-import --cat-blob-fd=3 3>backflow &&
	test_cmp expect.response response &&
	but rev-list read-empty |
	but diff-tree -r --root --stdin |
	sed "s/$OID_REGEX/OBJNAME/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'N: copy root directory by tree hash' '
	cat >expect <<-EOF &&
	:100755 000000 $newf $zero D	file3/newf
	:100644 000000 $oldf $zero D	file3/oldf
	EOF
	root=$(but rev-parse refs/heads/branch^0^{tree}) &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/N6
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy root directory by tree hash
	cummit

	from refs/heads/branch^0
	M 040000 $root ""
	INPUT_END
	but fast-import <input &&
	but diff-tree -C --find-copies-harder -r N4 N6 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: copy root by path' '
	cat >expect <<-EOF &&
	:100755 100755 $newf $newf C100	file2/newf	oldroot/file2/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	oldroot/file2/oldf
	:100755 100755 $f4id $f4id C100	file4	oldroot/file4
	:100755 100755 $f6id $f6id C100	newdir/exec.sh	oldroot/newdir/exec.sh
	:100644 100644 $f5id $f5id C100	newdir/interesting	oldroot/newdir/interesting
	EOF
	cat >input <<-INPUT_END &&
	cummit refs/heads/N-copy-root-path
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy root directory by (empty) path
	cummit

	from refs/heads/branch^0
	C "" oldroot
	INPUT_END
	but fast-import <input &&
	but diff-tree -C --find-copies-harder -r branch N-copy-root-path >actual &&
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
	empty_tree=$(but mktree </dev/null) &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/N-delete
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	collect data to be deleted
	cummit

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

	cummit refs/heads/N-delete
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	delete subdirectory
	cummit

	M 040000 $empty_tree foo/bar/qux
	INPUT_END
	but fast-import <input &&
	but rev-list N-delete |
		but diff-tree -r --stdin --root --always |
		sed -e "s/$OID_REGEX/OBJID/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'N: modify copied tree' '
	cat >expect <<-EOF &&
	:100644 100644 $f5id $f5id C100	newdir/interesting	file3/file5
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
	EOF
	subdir=$(but rev-parse refs/heads/branch^0:file2) &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/N5
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy by tree hash
	cummit

	from refs/heads/branch^0
	M 040000 $subdir file3

	cummit refs/heads/N5
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	modify directory copy
	cummit

	M 644 inline file3/file5
	data <<EOF
	$file5_data
	EOF
	INPUT_END
	but fast-import <input &&
	but diff-tree -C --find-copies-harder -r N5^^ N5 >actual &&
	compare_diff_raw expect actual
'

test_expect_success 'N: reject foo/ syntax' '
	subdir=$(but rev-parse refs/heads/branch^0:file2) &&
	test_must_fail but fast-import <<-INPUT_END
	cummit refs/heads/N5B
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy with invalid syntax
	cummit

	from refs/heads/branch^0
	M 040000 $subdir file3/
	INPUT_END
'

test_expect_success 'N: reject foo/ syntax in copy source' '
	test_must_fail but fast-import <<-INPUT_END
	cummit refs/heads/N5C
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy with invalid syntax
	cummit

	from refs/heads/branch^0
	C file2/ file3
	INPUT_END
'

test_expect_success 'N: reject foo/ syntax in rename source' '
	test_must_fail but fast-import <<-INPUT_END
	cummit refs/heads/N5D
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	rename with invalid syntax
	cummit

	from refs/heads/branch^0
	R file2/ file3
	INPUT_END
'

test_expect_success 'N: reject foo/ syntax in ls argument' '
	test_must_fail but fast-import <<-INPUT_END
	cummit refs/heads/N5E
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy with invalid syntax
	cummit

	from refs/heads/branch^0
	ls "file2/"
	INPUT_END
'

test_expect_success 'N: copy to root by id and modify' '
	echo "hello, world" >expect.foo &&
	echo hello >expect.bar &&
	but fast-import <<-SETUP_END &&
	cummit refs/heads/N7
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	hello, tree
	cummit

	deleteall
	M 644 inline foo/bar
	data <<EOF
	hello
	EOF
	SETUP_END

	tree=$(but rev-parse --verify N7:) &&
	but fast-import <<-INPUT_END &&
	cummit refs/heads/N8
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy to root by id and modify
	cummit

	M 040000 $tree ""
	M 644 inline foo/foo
	data <<EOF
	hello, world
	EOF
	INPUT_END
	but show N8:foo/foo >actual.foo &&
	but show N8:foo/bar >actual.bar &&
	test_cmp expect.foo actual.foo &&
	test_cmp expect.bar actual.bar
'

test_expect_success 'N: extract subtree' '
	branch=$(but rev-parse --verify refs/heads/branch^{tree}) &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/N9
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	extract subtree branch:newdir
	cummit

	M 040000 $branch ""
	C "newdir" ""
	INPUT_END
	but fast-import <input &&
	but diff --exit-code branch:newdir N9
'

test_expect_success 'N: modify subtree, extract it, and modify again' '
	echo hello >expect.baz &&
	echo hello, world >expect.qux &&
	but fast-import <<-SETUP_END &&
	cummit refs/heads/N10
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	hello, tree
	cummit

	deleteall
	M 644 inline foo/bar/baz
	data <<EOF
	hello
	EOF
	SETUP_END

	tree=$(but rev-parse --verify N10:) &&
	but fast-import <<-INPUT_END &&
	cummit refs/heads/N11
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	copy to root by id and modify
	cummit

	M 040000 $tree ""
	M 100644 inline foo/bar/qux
	data <<EOF
	hello, world
	EOF
	R "foo" ""
	C "bar/qux" "bar/quux"
	INPUT_END
	but show N11:bar/baz >actual.baz &&
	but show N11:bar/qux >actual.qux &&
	but show N11:bar/quux >actual.quux &&
	test_cmp expect.baz actual.baz &&
	test_cmp expect.qux actual.qux &&
	test_cmp expect.qux actual.quux'

###
### series O
###

test_expect_success 'O: comments are all skipped' '
	cat >input <<-INPUT_END &&
	#we will
	cummit refs/heads/O1
	# -- ignore all of this text
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	dirty directory copy
	cummit

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

	but fast-import <input &&
	test $(but rev-parse N3) = $(but rev-parse O1)
'

test_expect_success 'O: blank lines not necessary after data commands' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/O2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	dirty directory copy
	cummit
	from refs/heads/branch^0
	M 644 inline file2/file5
	data <<EOF
	$file5_data
	EOF
	C file2 file3
	D file2/file5

	INPUT_END

	but fast-import <input &&
	test $(but rev-parse N3) = $(but rev-parse O2)
'

test_expect_success 'O: repack before next test' '
	but repack -a -d
'

test_expect_success 'O: blank lines not necessary after other commands' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/O3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zstring
	cummit
	cummit refs/heads/O3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zof
	cummit
	checkpoint
	cummit refs/heads/O3
	mark :5
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zempty
	cummit
	checkpoint
	cummit refs/heads/O3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zcummits
	cummit
	reset refs/tags/O3-2nd
	from :5
	reset refs/tags/O3-3rd
	from :5
	INPUT_END

	cat >expect <<-INPUT_END &&
	string
	of
	empty
	cummits
	INPUT_END

	but fast-import <input &&
	ls -la .but/objects/pack/pack-*.pack >packlist &&
	ls -la .but/objects/pack/pack-*.pack >idxlist &&
	test_line_count = 4 idxlist &&
	test_line_count = 4 packlist &&
	test $(but rev-parse refs/tags/O3-2nd) = $(but rev-parse O3^) &&
	but log --reverse --pretty=oneline O3 | sed s/^.*z// >actual &&
	test_cmp expect actual
'

test_expect_success 'O: progress outputs as requested by input' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/O4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zstring
	cummit
	cummit refs/heads/O4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zof
	cummit
	progress Two cummits down, 2 to go!
	cummit refs/heads/O4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zempty
	cummit
	progress Three cummits down, 1 to go!
	cummit refs/heads/O4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	zcummits
	cummit
	progress done!
	INPUT_END
	but fast-import <input >actual &&
	grep "progress " <input >expect &&
	test_cmp expect actual
'

###
### series P (butlinks)
###

test_expect_success 'P: superproject & submodule mix' '
	cat >input <<-INPUT_END &&
	blob
	mark :1
	data 10
	test file

	reset refs/heads/sub
	cummit refs/heads/sub
	mark :2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
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

	cummit refs/heads/subuse1
	mark :4
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 8
	initial
	from refs/heads/main
	M 100644 :3 .butmodules
	M 160000 :2 sub

	blob
	mark :5
	data 20
	test file
	more data

	cummit refs/heads/sub
	mark :6
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 11
	sub_second
	from :2
	M 100644 :5 file

	cummit refs/heads/subuse1
	mark :7
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 7
	second
	from :4
	M 160000 :6 sub

	INPUT_END

	but fast-import <input &&
	but checkout subuse1 &&
	rm -rf sub &&
	mkdir sub &&
	(
		cd sub &&
		but init &&
		but fetch --update-head-ok .. refs/heads/sub:refs/heads/main &&
		but checkout main
	) &&
	but submodule init &&
	but submodule update
'

test_expect_success 'P: verbatim SHA butlinks' '
	SUBLAST=$(but rev-parse --verify sub) &&
	SUBPREV=$(but rev-parse --verify sub^) &&

	cat >input <<-INPUT_END &&
	blob
	mark :1
	data <<DATAEND
	[submodule "sub"]
		path = sub
		url = "$(pwd)/sub"
	DATAEND

	cummit refs/heads/subuse2
	mark :2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 8
	initial
	from refs/heads/main
	M 100644 :1 .butmodules
	M 160000 $SUBPREV sub

	cummit refs/heads/subuse2
	mark :3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 7
	second
	from :2
	M 160000 $SUBLAST sub

	INPUT_END

	but branch -D sub &&
	but gc &&
	but prune &&
	but fast-import <input &&
	test $(but rev-parse --verify subuse2) = $(but rev-parse --verify subuse1)
'

test_expect_success 'P: fail on inline butlink' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/subuse3
	mark :1
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	corrupt
	cummit

	from refs/heads/subuse2
	M 160000 inline sub
	data <<DATA
	$SUBPREV
	DATA

	INPUT_END

	test_must_fail but fast-import <input
'

test_expect_success 'P: fail on blob mark in butlink' '
	test_tick &&
	cat >input <<-INPUT_END &&
	blob
	mark :1
	data <<DATA
	$SUBPREV
	DATA

	cummit refs/heads/subuse3
	mark :2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	corrupt
	cummit

	from refs/heads/subuse2
	M 160000 :1 sub

	INPUT_END

	test_must_fail but fast-import <input
'

###
### series Q (notes)
###

test_expect_success 'Q: cummit notes' '
	note1_data="The first note for the first cummit" &&
	note2_data="The first note for the second cummit" &&
	note3_data="The first note for the third cummit" &&
	note1b_data="The second note for the first cummit" &&
	note1c_data="The third note for the first cummit" &&
	note2b_data="The second note for the second cummit" &&

	test_tick &&
	cat >input <<-INPUT_END &&
	blob
	mark :2
	data <<EOF
	$file2_data
	EOF

	cummit refs/heads/notes-test
	mark :3
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	first (:3)
	cummit

	M 644 :2 file2

	blob
	mark :4
	data $file4_len
	$file4_data
	cummit refs/heads/notes-test
	mark :5
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	second (:5)
	cummit

	M 644 :4 file4

	cummit refs/heads/notes-test
	mark :6
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	third (:6)
	cummit

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

	cummit refs/notes/foobar
	mark :9
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	notes (:9)
	cummit

	N :7 :3
	N :8 :5
	N inline :6
	data <<EOF
	$note3_data
	EOF

	cummit refs/notes/foobar
	mark :10
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	notes (:10)
	cummit

	N inline :3
	data <<EOF
	$note1b_data
	EOF

	cummit refs/notes/foobar2
	mark :11
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	notes (:11)
	cummit

	N inline :3
	data <<EOF
	$note1c_data
	EOF

	cummit refs/notes/foobar
	mark :12
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	notes (:12)
	cummit

	deleteall
	N inline :5
	data <<EOF
	$note2b_data
	EOF

	INPUT_END

	but fast-import <input &&
	but whatchanged notes-test
'

test_expect_success 'Q: verify pack' '
	verify_packs
'

test_expect_success 'Q: verify first cummit' '
	cummit1=$(but rev-parse notes-test~2) &&
	cummit2=$(but rev-parse notes-test^) &&
	cummit3=$(but rev-parse notes-test) &&

	cat >expect <<-EOF &&
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	first (:3)
	EOF
	but cat-file cummit notes-test~2 | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second cummit' '
	cat >expect <<-EOF &&
	parent $cummit1
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	second (:5)
	EOF
	but cat-file cummit notes-test^ | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third cummit' '
	cat >expect <<-EOF &&
	parent $cummit2
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	third (:6)
	EOF
	but cat-file cummit notes-test | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first notes cummit' '
	cat >expect <<-EOF &&
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	notes (:9)
	EOF
	but cat-file cummit refs/notes/foobar~2 | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $cummit1
	100644 blob $cummit2
	100644 blob $cummit3
	EOF
	cat expect.unsorted | sort >expect &&
	but cat-file -p refs/notes/foobar~2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for first cummit' '
	echo "$note1_data" >expect &&
	but cat-file blob refs/notes/foobar~2:$cummit1 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for second cummit' '
	echo "$note2_data" >expect &&
	but cat-file blob refs/notes/foobar~2:$cummit2 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for third cummit' '
	echo "$note3_data" >expect &&
	but cat-file blob refs/notes/foobar~2:$cummit3 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second notes cummit' '
	cat >expect <<-EOF &&
	parent $(but rev-parse --verify refs/notes/foobar~2)
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	notes (:10)
	EOF
	but cat-file cummit refs/notes/foobar^ | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $cummit1
	100644 blob $cummit2
	100644 blob $cummit3
	EOF
	cat expect.unsorted | sort >expect &&
	but cat-file -p refs/notes/foobar^^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second note for first cummit' '
	echo "$note1b_data" >expect &&
	but cat-file blob refs/notes/foobar^:$cummit1 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for second cummit' '
	echo "$note2_data" >expect &&
	but cat-file blob refs/notes/foobar^:$cummit2 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify first note for third cummit' '
	echo "$note3_data" >expect &&
	but cat-file blob refs/notes/foobar^:$cummit3 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third notes cummit' '
	cat >expect <<-EOF &&
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	notes (:11)
	EOF
	but cat-file cummit refs/notes/foobar2 | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $cummit1
	EOF
	cat expect.unsorted | sort >expect &&
	but cat-file -p refs/notes/foobar2^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify third note for first cummit' '
	echo "$note1c_data" >expect &&
	but cat-file blob refs/notes/foobar2:$cummit1 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify fourth notes cummit' '
	cat >expect <<-EOF &&
	parent $(but rev-parse --verify refs/notes/foobar^)
	author $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE

	notes (:12)
	EOF
	but cat-file cummit refs/notes/foobar | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify fourth notes tree' '
	cat >expect.unsorted <<-EOF &&
	100644 blob $cummit2
	EOF
	cat expect.unsorted | sort >expect &&
	but cat-file -p refs/notes/foobar^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: verify second note for second cummit' '
	echo "$note2b_data" >expect &&
	but cat-file blob refs/notes/foobar:$cummit2 >actual &&
	test_cmp expect actual
'

test_expect_success 'Q: deny note on empty branch' '
	cat >input <<-EOF &&
	reset refs/heads/Q0

	cummit refs/heads/note-Q0
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	Note for an empty branch.
	cummit

	N inline refs/heads/Q0
	data <<NOTE
	some note
	NOTE
	EOF
	test_must_fail but fast-import <input
'
###
### series R (feature and option)
###

test_expect_success 'R: abort on unsupported feature' '
	cat >input <<-EOF &&
	feature no-such-feature-exists
	EOF

	test_must_fail but fast-import <input
'

test_expect_success 'R: supported feature is accepted' '
	cat >input <<-EOF &&
	feature date-format=now
	EOF

	but fast-import <input
'

test_expect_success 'R: abort on receiving feature after data command' '
	cat >input <<-EOF &&
	blob
	data 3
	hi
	feature date-format=now
	EOF

	test_must_fail but fast-import <input
'

test_expect_success 'R: import-marks features forbidden by default' '
	>but.marks &&
	echo "feature import-marks=but.marks" >input &&
	test_must_fail but fast-import <input &&
	echo "feature import-marks-if-exists=but.marks" >input &&
	test_must_fail but fast-import <input
'

test_expect_success 'R: only one import-marks feature allowed per stream' '
	>but.marks &&
	>but2.marks &&
	cat >input <<-EOF &&
	feature import-marks=but.marks
	feature import-marks=but2.marks
	EOF

	test_must_fail but fast-import --allow-unsafe-features <input
'

test_expect_success 'R: export-marks feature forbidden by default' '
	echo "feature export-marks=but.marks" >input &&
	test_must_fail but fast-import <input
'

test_expect_success 'R: export-marks feature results in a marks file being created' '
	cat >input <<-EOF &&
	feature export-marks=but.marks
	blob
	mark :1
	data 3
	hi

	EOF

	but fast-import --allow-unsafe-features <input &&
	grep :1 but.marks
'

test_expect_success 'R: export-marks options can be overridden by commandline options' '
	cat >input <<-\EOF &&
	feature export-marks=feature-sub/but.marks
	blob
	mark :1
	data 3
	hi

	EOF
	but fast-import --allow-unsafe-features \
			--export-marks=cmdline-sub/other.marks <input &&
	grep :1 cmdline-sub/other.marks &&
	test_path_is_missing feature-sub
'

test_expect_success 'R: catch typo in marks file name' '
	test_must_fail but fast-import --import-marks=nonexistent.marks </dev/null &&
	echo "feature import-marks=nonexistent.marks" |
	test_must_fail but fast-import --allow-unsafe-features
'

test_expect_success 'R: import and output marks can be the same file' '
	rm -f io.marks &&
	blob=$(echo hi | but hash-object --stdin) &&
	cat >expect <<-EOF &&
	:1 $blob
	:2 $blob
	EOF
	but fast-import --export-marks=io.marks <<-\EOF &&
	blob
	mark :1
	data 3
	hi

	EOF
	but fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF &&
	blob
	mark :2
	data 3
	hi

	EOF
	test_cmp expect io.marks
'

test_expect_success 'R: --import-marks=foo --output-marks=foo to create foo fails' '
	rm -f io.marks &&
	test_must_fail but fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF
	blob
	mark :1
	data 3
	hi

	EOF
'

test_expect_success 'R: --import-marks-if-exists' '
	rm -f io.marks &&
	blob=$(echo hi | but hash-object --stdin) &&
	echo ":1 $blob" >expect &&
	but fast-import --import-marks-if-exists=io.marks --export-marks=io.marks <<-\EOF &&
	blob
	mark :1
	data 3
	hi

	EOF
	test_cmp expect io.marks
'

test_expect_success 'R: feature import-marks-if-exists' '
	rm -f io.marks &&

	but fast-import --export-marks=io.marks \
			--allow-unsafe-features <<-\EOF &&
	feature import-marks-if-exists=not_io.marks
	EOF
	test_must_be_empty io.marks &&

	blob=$(echo hi | but hash-object --stdin) &&

	echo ":1 $blob" >io.marks &&
	echo ":1 $blob" >expect &&
	echo ":2 $blob" >>expect &&

	but fast-import --export-marks=io.marks \
			--allow-unsafe-features <<-\EOF &&
	feature import-marks-if-exists=io.marks
	blob
	mark :2
	data 3
	hi

	EOF
	test_cmp expect io.marks &&

	echo ":3 $blob" >>expect &&

	but fast-import --import-marks=io.marks \
			--export-marks=io.marks \
			--allow-unsafe-features <<-\EOF &&
	feature import-marks-if-exists=not_io.marks
	blob
	mark :3
	data 3
	hi

	EOF
	test_cmp expect io.marks &&

	but fast-import --import-marks-if-exists=not_io.marks \
			--export-marks=io.marks \
			--allow-unsafe-features <<-\EOF &&
	feature import-marks-if-exists=io.marks
	EOF
	test_must_be_empty io.marks
'

test_expect_success 'R: import to output marks works without any content' '
	cat >input <<-EOF &&
	feature import-marks=marks.out
	feature export-marks=marks.new
	EOF

	but fast-import --allow-unsafe-features <input &&
	test_cmp marks.out marks.new
'

test_expect_success 'R: import marks prefers commandline marks file over the stream' '
	cat >input <<-EOF &&
	feature import-marks=nonexistent.marks
	feature export-marks=marks.new
	EOF

	but fast-import --import-marks=marks.out --allow-unsafe-features <input &&
	test_cmp marks.out marks.new
'


test_expect_success 'R: multiple --import-marks= should be honoured' '
	cat >input <<-EOF &&
	feature import-marks=nonexistent.marks
	feature export-marks=combined.marks
	EOF

	head -n2 marks.out > one.marks &&
	tail -n +3 marks.out > two.marks &&
	but fast-import --import-marks=one.marks --import-marks=two.marks \
		--allow-unsafe-features <input &&
	test_cmp marks.out combined.marks
'

test_expect_success 'R: feature relative-marks should be honoured' '
	cat >input <<-EOF &&
	feature relative-marks
	feature import-marks=relative.in
	feature export-marks=relative.out
	EOF

	mkdir -p .but/info/fast-import/ &&
	cp marks.new .but/info/fast-import/relative.in &&
	but fast-import --allow-unsafe-features <input &&
	test_cmp marks.new .but/info/fast-import/relative.out
'

test_expect_success 'R: feature no-relative-marks should be honoured' '
	cat >input <<-EOF &&
	feature relative-marks
	feature import-marks=relative.in
	feature no-relative-marks
	feature export-marks=non-relative.out
	EOF

	but fast-import --allow-unsafe-features <input &&
	test_cmp marks.new non-relative.out
'

test_expect_success 'R: feature ls supported' '
	echo "feature ls" |
	but fast-import
'

test_expect_success 'R: feature cat-blob supported' '
	echo "feature cat-blob" |
	but fast-import
'

test_expect_success 'R: cat-blob-fd must be a nonnegative integer' '
	test_must_fail but fast-import --cat-blob-fd=-1 </dev/null
'

test_expect_success !MINGW 'R: print old blob' '
	blob=$(echo "yes it can" | but hash-object -w --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 11
	yes it can

	EOF
	echo "cat-blob $blob" |
	but fast-import --cat-blob-fd=6 6>actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'R: in-stream cat-blob-fd not respected' '
	echo hello >greeting &&
	blob=$(but hash-object -w greeting) &&
	cat >expect <<-EOF &&
	${blob} blob 6
	hello

	EOF
	but fast-import --cat-blob-fd=3 3>actual.3 >actual.1 <<-EOF &&
	cat-blob $blob
	EOF
	test_cmp expect actual.3 &&
	test_must_be_empty actual.1 &&
	but fast-import 3>actual.3 >actual.1 <<-EOF &&
	option cat-blob-fd=3
	cat-blob $blob
	EOF
	test_must_be_empty actual.3 &&
	test_cmp expect actual.1
'

test_expect_success !MINGW 'R: print mark for new blob' '
	echo "effluentish" | but hash-object --stdin >expect &&
	but fast-import --cat-blob-fd=6 6>actual <<-\EOF &&
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
	blob=$(echo "yep yep yep" | but hash-object --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 12
	yep yep yep

	EOF
	but fast-import --cat-blob-fd=6 6>actual <<-\EOF &&
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
	blob=$(echo "a new blob named by sha1" | but hash-object --stdin) &&
	cat >expect <<-EOF &&
	${blob} blob 25
	a new blob named by sha1

	EOF
	but fast-import --cat-blob-fd=6 6>actual <<-EOF &&
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
	blob1=$(but hash-object big) &&
	blob1_len=$(wc -c <big) &&
	blob2=$(echo hello | but hash-object --stdin) &&
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
	but fast-import >actual &&
	test_cmp expect actual
'

test_expect_success PIPE 'R: copy using cat-file' '
	expect_id=$(but hash-object big) &&
	expect_len=$(wc -c <big) &&
	echo $expect_id blob $expect_len >expect.response &&

	rm -f blobs &&

	mkfifo blobs &&
	(
		export BUT_CUMMITTER_NAME BUT_CUMMITTER_EMAIL BUT_CUMMITTER_DATE &&
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
		cummit refs/heads/copied
		cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
		data <<cummit
		copy big file as file3
		cummit
		M 644 inline file3
		data <<BLOB
		EOF
		cat blob &&
		echo BLOB
	) 3<blobs |
	but fast-import --cat-blob-fd=3 3>blobs &&
	but show copied:file3 >actual &&
	test_cmp expect.response response &&
	test_cmp big actual
'

test_expect_success PIPE 'R: print blob mid-cummit' '
	rm -f blobs &&
	echo "A blob from _before_ the cummit." >expect &&
	mkfifo blobs &&
	(
		exec 3<blobs &&
		cat <<-EOF &&
		feature cat-blob
		blob
		mark :1
		data <<BLOB
		A blob from _before_ the cummit.
		BLOB
		cummit refs/heads/temporary
		cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
		data <<cummit
		Empty cummit
		cummit
		cat-blob :1
		EOF

		read blob_id type size <&3 &&
		test_copy_bytes $size >actual <&3 &&
		read newline <&3 &&

		echo
	) |
	but fast-import --cat-blob-fd=3 3>blobs &&
	test_cmp expect actual
'

test_expect_success PIPE 'R: print staged blob within cummit' '
	rm -f blobs &&
	echo "A blob from _within_ the cummit." >expect &&
	mkfifo blobs &&
	(
		exec 3<blobs &&
		cat <<-EOF &&
		feature cat-blob
		cummit refs/heads/within
		cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
		data <<cummit
		Empty cummit
		cummit
		M 644 inline within
		data <<BLOB
		A blob from _within_ the cummit.
		BLOB
		EOF

		to_get=$(
			echo "A blob from _within_ the cummit." |
			but hash-object --stdin
		) &&
		echo "cat-blob $to_get" &&

		read blob_id type size <&3 &&
		test_copy_bytes $size >actual <&3 &&
		read newline <&3 &&

		echo deleteall
	) |
	but fast-import --cat-blob-fd=3 3>blobs &&
	test_cmp expect actual
'

test_expect_success 'R: quiet option results in no stats being output' '
	cat >input <<-EOF &&
	option but quiet
	blob
	data 3
	hi

	EOF

	but fast-import 2>output <input &&
	test_must_be_empty output
'

test_expect_success 'R: feature done means terminating "done" is mandatory' '
	echo feature done | test_must_fail but fast-import &&
	test_must_fail but fast-import --done </dev/null
'

test_expect_success 'R: terminating "done" with trailing gibberish is ok' '
	but fast-import <<-\EOF &&
	feature done
	done
	trailing gibberish
	EOF
	but fast-import <<-\EOF
	done
	more trailing gibberish
	EOF
'

test_expect_success 'R: terminating "done" within cummit' '
	cat >expect <<-\EOF &&
	OBJID
	:000000 100644 OBJID OBJID A	hello.c
	:000000 100644 OBJID OBJID A	hello2.c
	EOF
	but fast-import <<-EOF &&
	cummit refs/heads/done-ends
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<EOT
	cummit terminated by "done" command
	EOT
	M 100644 inline hello.c
	data <<EOT
	Hello, world.
	EOT
	C hello.c hello2.c
	done
	EOF
	but rev-list done-ends |
	but diff-tree -r --stdin --root --always |
	sed -e "s/$OID_REGEX/OBJID/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'R: die on unknown option' '
	cat >input <<-EOF &&
	option but non-existing-option
	EOF

	test_must_fail but fast-import <input
'

test_expect_success 'R: unknown commandline options are rejected' '\
	test_must_fail but fast-import --non-existing-option < /dev/null
'

test_expect_success 'R: die on invalid option argument' '
	echo "option but active-branches=-5" |
	test_must_fail but fast-import &&
	echo "option but depth=" |
	test_must_fail but fast-import &&
	test_must_fail but fast-import --depth="5 elephants" </dev/null
'

test_expect_success 'R: ignore non-but options' '
	cat >input <<-EOF &&
	option non-existing-vcs non-existing-option
	EOF

	but fast-import <input
'

test_expect_success 'R: corrupt lines do not mess marks file' '
	rm -f io.marks &&
	blob=$(echo hi | but hash-object --stdin) &&
	cat >expect <<-EOF &&
	:3 $ZERO_OID
	:1 $blob
	:2 $blob
	EOF
	cp expect io.marks &&
	test_must_fail but fast-import --import-marks=io.marks --export-marks=io.marks <<-\EOF &&

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
	cummit refs/heads/big-file
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	R - big file
	cummit

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
	but --but-dir=R/.but config fastimport.unpackLimit 0 &&
	but --but-dir=R/.but fast-import --big-file-threshold=1 <input
'

test_expect_success 'R: verify created pack' '
	(
		cd R &&
		verify_packs -v > ../verify
	)
'

test_expect_success 'R: verify written objects' '
	but --but-dir=R/.but cat-file blob big-file:big1 >actual &&
	test_cmp_bin expect actual &&
	a=$(but --but-dir=R/.but rev-parse big-file:big1) &&
	b=$(but --but-dir=R/.but rev-parse big-file:big2) &&
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
#   cummit marks:  301, 302, 303, 304
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
	cummit refs/heads/S
	mark :301
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit 1
	cummit
	M 100644 inline hello.c
	data <<BLOB
	blob 1
	BLOB

	cummit refs/heads/S
	mark :302
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit 2
	cummit
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

	but fast-import --export-marks=marks <input
'

#
# filemodify, three datarefs
#
test_expect_success 'S: filemodify with garbage after mark must fail' '
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit N
	cummit
	M 100644 :403x hello.c
	EOF
	test_i18ngrep "space after mark" err
'

# inline is misspelled; fast-import thinks it is some unknown dataref
test_expect_success 'S: filemodify with garbage after inline must fail' '
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit N
	cummit
	M 100644 inlineX hello.c
	data <<BLOB
	inline
	BLOB
	EOF
	test_i18ngrep "nvalid dataref" err
'

test_expect_success 'S: filemodify with garbage after sha1 must fail' '
	sha1=$(grep :403 marks | cut -d\  -f2) &&
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit N
	cummit
	M 100644 ${sha1}x hello.c
	EOF
	test_i18ngrep "space after SHA1" err
'

#
# notemodify, three ways to say dataref
#
test_expect_success 'S: notemodify with garbage after mark dataref must fail' '
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit S note dataref markref
	cummit
	N :202x :302
	EOF
	test_i18ngrep "space after mark" err
'

test_expect_success 'S: notemodify with garbage after inline dataref must fail' '
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit S note dataref inline
	cummit
	N inlineX :302
	data <<BLOB
	note blob
	BLOB
	EOF
	test_i18ngrep "nvalid dataref" err
'

test_expect_success 'S: notemodify with garbage after sha1 dataref must fail' '
	sha1=$(grep :202 marks | cut -d\  -f2) &&
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit S note dataref sha1
	cummit
	N ${sha1}x :302
	EOF
	test_i18ngrep "space after SHA1" err
'

#
# notemodify, mark in cummit-ish
#
test_expect_success 'S: notemodify with garbage after mark cummit-ish must fail' '
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/Snotes
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit S note cummit-ish
	cummit
	N :202 :302x
	EOF
	test_i18ngrep "after mark" err
'

#
# from
#
test_expect_success 'S: from with garbage after mark must fail' '
	test_must_fail \
	but fast-import --import-marks=marks --export-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S2
	mark :303
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit 3
	cummit
	from :301x
	M 100644 :403 hello.c
	EOF


	# go create the cummit, need it for merge test
	but fast-import --import-marks=marks --export-marks=marks <<-EOF &&
	cummit refs/heads/S2
	mark :303
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	cummit 3
	cummit
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
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cummit refs/heads/S
	mark :304
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	merge 4
	cummit
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
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	tag refs/tags/Stag
	from :302x
	tagger $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
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
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	cat-blob :403x
	EOF
	test_i18ngrep "after mark" err
'

#
# ls markref
#
test_expect_success 'S: ls with garbage after mark must fail' '
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
	ls :302x hello.c
	EOF
	test_i18ngrep "space after mark" err
'

test_expect_success 'S: ls with garbage after sha1 must fail' '
	sha1=$(grep :302 marks | cut -d\  -f2) &&
	test_must_fail but fast-import --import-marks=marks <<-EOF 2>err &&
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
	040000 tree $(but rev-parse S^{tree})	Z
	EOF
	sha1=$(but rev-parse --verify S) &&
	but fast-import --import-marks=marks <<-EOF >actual &&
	ls $sha1 ""
	EOF
	test_cmp expect actual
'

test_expect_success 'T: delete branch' '
	but branch to-delete &&
	but fast-import <<-EOF &&
	reset refs/heads/to-delete
	from $ZERO_OID
	EOF
	test_must_fail but rev-parse --verify refs/heads/to-delete
'

test_expect_success 'T: empty reset doesnt delete branch' '
	but branch not-to-delete &&
	but fast-import <<-EOF &&
	reset refs/heads/not-to-delete
	EOF
	but show-ref &&
	but rev-parse --verify refs/heads/not-to-delete
'

###
### series U (filedelete)
###

test_expect_success 'U: initialize for U tests' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/U
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	test setup
	cummit
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

	f7id=$(echo "blob 1" | but hash-object --stdin) &&
	f8id=$(echo "sleep well" | but hash-object --stdin) &&
	f9id=$(echo "au revoir" | but hash-object --stdin) &&
	but fast-import <input
'

test_expect_success 'U: filedelete file succeeds' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/U
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	delete good/night.txt
	cummit
	from refs/heads/U^0
	D good/night.txt

	INPUT_END

	but fast-import <input
'

test_expect_success 'U: validate file delete result' '
	cat >expect <<-EOF &&
	:100644 000000 $f8id $ZERO_OID D	good/night.txt
	EOF

	but diff-tree -M -r U^1 U >actual &&

	compare_diff_raw expect actual
'

test_expect_success 'U: filedelete directory succeeds' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/U
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	delete good dir
	cummit
	from refs/heads/U^0
	D good

	INPUT_END

	but fast-import <input
'

test_expect_success 'U: validate directory delete result' '
	cat >expect <<-EOF &&
	:100644 000000 $f9id $ZERO_OID D	good/bye.txt
	EOF

	but diff-tree -M -r U^1 U >actual &&

	compare_diff_raw expect actual
'

test_expect_success 'U: filedelete root succeeds' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/U
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data <<cummit
	must succeed
	cummit
	from refs/heads/U^0
	D ""

	INPUT_END

	but fast-import <input
'

test_expect_success 'U: validate root delete result' '
	cat >expect <<-EOF &&
	:100644 000000 $f7id $ZERO_OID D	hello.c
	EOF

	but diff-tree -M -r U^1 U >actual &&

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

	(
		but fast-import $options <&8 >&9 &
		echo $! >&9
		wait $!
		echo >&2 "background fast-import terminated too early with exit code $?"
		# Un-block the read loop in the main shell process.
		echo >&9 UNEXPECTED
	) &
	sh_pid=$!
	read fi_pid <&9
	# We don't mind if fast-import has already died by the time the test
	# ends.
	test_when_finished "
		exec 8>&-; exec 9>&-;
		kill $sh_pid && wait $sh_pid
		kill $fi_pid && wait $fi_pid
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
		elif test "$output" = "UNEXPECTED"
		then
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
	if ! kill -0 "$fi_pid"
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
	test "$(but rev-parse --verify V)" = "$(but rev-parse --verify U)" &&
	background_import_still_running
'

test_expect_success PIPE 'V: checkpoint updates refs and marks after cummit' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/V
	mark :1
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 0
	from refs/heads/U

	INPUT_END

	background_import_then_checkpoint "--export-marks=marks.actual" input &&

	echo ":1 $(but rev-parse --verify V)" >marks.expected &&

	test "$(but rev-parse --verify V^)" = "$(but rev-parse --verify U)" &&
	test_cmp marks.expected marks.actual &&
	background_import_still_running
'

# Re-create the exact same cummit, but on a different branch: no new object is
# created in the database, but the refs and marks still need to be updated.
test_expect_success PIPE 'V: checkpoint updates refs and marks after cummit (no new objects)' '
	cat >input <<-INPUT_END &&
	cummit refs/heads/V2
	mark :2
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 0
	from refs/heads/U

	INPUT_END

	background_import_then_checkpoint "--export-marks=marks.actual" input &&

	echo ":2 $(but rev-parse --verify V2)" >marks.expected &&

	test "$(but rev-parse --verify V2)" = "$(but rev-parse --verify V)" &&
	test_cmp marks.expected marks.actual &&
	background_import_still_running
'

test_expect_success PIPE 'V: checkpoint updates tags after tag' '
	cat >input <<-INPUT_END &&
	tag Vtag
	from refs/heads/V
	tagger $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	data 0

	INPUT_END

	background_import_then_checkpoint "" input &&
	but show-ref -d Vtag &&
	background_import_still_running
'

###
### series W (get-mark and empty orphan cummits)
###

cat >>W-input <<-W_INPUT_END
	cummit refs/heads/W-branch
	mark :1
	author Full Name <user@company.tld> 1000000000 +0100
	cummitter Full Name <user@company.tld> 1000000000 +0100
	data 27
	Intentionally empty cummit
	LFsget-mark :1
	W_INPUT_END

test_expect_success !MINGW 'W: get-mark & empty orphan cummit with no newlines' '
	sed -e s/LFs// W-input | tr L "\n" | but fast-import
'

test_expect_success !MINGW 'W: get-mark & empty orphan cummit with one newline' '
	sed -e s/LFs/L/ W-input | tr L "\n" | but fast-import
'

test_expect_success !MINGW 'W: get-mark & empty orphan cummit with ugly second newline' '
	# Technically, this should fail as it has too many linefeeds
	# according to the grammar in fast-import.txt.  But, for whatever
	# reason, it works.  Since using the correct number of newlines
	# does not work with older (pre-2.22) versions of but, allow apps
	# that used this second-newline workaround to keep working by
	# checking it with this test...
	sed -e s/LFs/LL/ W-input | tr L "\n" | but fast-import
'

test_expect_success !MINGW 'W: get-mark & empty orphan cummit with erroneous third newline' '
	# ...but do NOT allow more empty lines than that (see previous test).
	sed -e s/LFs/LLL/ W-input | tr L "\n" | test_must_fail but fast-import
'

###
### series X (other new features)
###

test_expect_success 'X: handling encoding' '
	test_tick &&
	cat >input <<-INPUT_END &&
	cummit refs/heads/encoding
	cummitter $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL> $BUT_CUMMITTER_DATE
	encoding iso-8859-7
	data <<cummit
	INPUT_END

	printf "Pi: \360\ncummit\n" >>input &&

	but fast-import <input &&
	but cat-file -p encoding | grep $(printf "\360") &&
	but log -1 --format=%B encoding | grep $(printf "\317\200")
'

###
### series Y (submodules and hash algorithms)
###

cat >Y-sub-input <<\Y_INPUT_END
blob
mark :1
data 4
foo

reset refs/heads/main
cummit refs/heads/main
mark :2
author Full Name <user@company.tld> 1000000000 +0100
cummitter Full Name <user@company.tld> 1000000000 +0100
data 24
Test submodule cummit 1
M 100644 :1 file

blob
mark :3
data 8
foo
bar

cummit refs/heads/main
mark :4
author Full Name <user@company.tld> 1000000001 +0100
cummitter Full Name <user@company.tld> 1000000001 +0100
data 24
Test submodule cummit 2
from :2
M 100644 :3 file
Y_INPUT_END

# Note that the submodule object IDs are intentionally not translated.
cat >Y-main-input <<\Y_INPUT_END
blob
mark :1
data 4
foo

reset refs/heads/main
cummit refs/heads/main
mark :2
author Full Name <user@company.tld> 2000000000 +0100
cummitter Full Name <user@company.tld> 2000000000 +0100
data 14
Test cummit 1
M 100644 :1 file

blob
mark :3
data 73
[submodule "sub1"]
	path = sub1
	url = https://void.example.com/main.but

cummit refs/heads/main
mark :4
author Full Name <user@company.tld> 2000000001 +0100
cummitter Full Name <user@company.tld> 2000000001 +0100
data 14
Test cummit 2
from :2
M 100644 :3 .butmodules
M 160000 0712c5be7cf681388e355ef47525aaf23aee1a6d sub1

blob
mark :5
data 8
foo
bar

cummit refs/heads/main
mark :6
author Full Name <user@company.tld> 2000000002 +0100
cummitter Full Name <user@company.tld> 2000000002 +0100
data 14
Test cummit 3
from :4
M 100644 :5 file
M 160000 ff729f5e62f72c0c3978207d9a80e5f3a65f14d7 sub1
Y_INPUT_END

cat >Y-marks <<\Y_INPUT_END
:2 0712c5be7cf681388e355ef47525aaf23aee1a6d
:4 ff729f5e62f72c0c3978207d9a80e5f3a65f14d7
Y_INPUT_END

test_expect_success 'Y: setup' '
	test_oid_cache <<-EOF
	Ymain sha1:9afed2f9161ddf416c0a1863b8b0725b00070504
	Ymain sha256:c0a1010da1df187b2e287654793df01b464bd6f8e3f17fc1481a7dadf84caee3
	EOF
'

test_expect_success 'Y: rewrite submodules' '
	but init main1 &&
	(
		cd main1 &&
		but init sub2 &&
		but -C sub2 fast-import --export-marks=../sub2-marks <../Y-sub-input &&
		but fast-import --rewrite-submodules-from=sub:../Y-marks \
			--rewrite-submodules-to=sub:sub2-marks <../Y-main-input &&
		test "$(but rev-parse main)" = "$(test_oid Ymain)"
	)
'

test_done
