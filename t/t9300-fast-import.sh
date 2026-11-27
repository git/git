#!/bin/sh
#
# Copyright (c) 2007 Shawn Pearce
#

test_description='test git fast-import utility'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh ;# test-lib chdir's into trash

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

test_expect_success 'A: create pack from stdin' '
	git config fastimport.unpackLimit 0 &&
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
	commit refs/heads/main
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
	from $ZERO_OID

	tag nested
	mark :6
	from :4
	data <<EOF
	Tag of our lovely commit
	EOF

	reset refs/tags/nested
	from $ZERO_OID

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
	git log --raw main
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
	git cat-file commit main | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tree' '
	cat >expect <<-EOF &&
	100644 blob file2
	100644 blob file3
	100755 blob file4
	EOF
	git cat-file -p main^{tree} | sed "s/ [0-9a-f]*	/ /" >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file2' '
	echo "$file2_data" >expect &&
	git cat-file blob main:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file3' '
	echo "$file3_data" >expect &&
	git cat-file blob main:file3 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify file4' '
	printf "$file4_data" >expect &&
	git cat-file blob main:file4 >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag/series-A' '
	cat >expect <<-EOF &&
	object $(git rev-parse refs/heads/main)
	type commit
	tag series-A

	An annotated tag without a tagger
	EOF
	git cat-file tag tags/series-A >actual &&
	test_cmp expect actual
'

test_expect_success 'A: verify tag/series-A-blob' '
	cat >expect <<-EOF &&
	object $(git rev-parse refs/heads/main:file3)
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
	:2 $(git rev-parse --verify main:file2)
	:3 $(git rev-parse --verify main:file3)
	:4 $(git rev-parse --verify main:file4)
	:5 $(git rev-parse --verify main^0)
	:6 $(git cat-file tag nested | grep object | cut -d" " -f 2)
	:7 $(git rev-parse --verify nested)
	:8 $(git rev-parse --verify main^0)
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
	from $(git rev-parse refs/heads/main:file3)
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
	object $(git rev-parse refs/heads/main:file3)
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
	git log --raw verify--import-marks
'

test_expect_success 'A: verify pack' '
	verify_packs
'

test_expect_success 'A: verify diff' '
	copy=$(git rev-parse --verify main:file2) &&
	cat >expect <<-EOF &&
	:000000 100755 $ZERO_OID $copy A	copy-of-file2
	EOF
	git diff-tree -M -r main verify--import-marks >actual &&
	compare_diff_raw expect actual &&
	test $(git rev-parse --verify main:file2) \
	    = $(git rev-parse --verify verify--import-marks:copy-of-file2)
'

###
### series C
###

test_expect_success 'C: incremental import create pack from stdin' '
	newf=$(echo hi newf | git hash-object -w --stdin) &&
	oldf=$(git rev-parse --verify main:file2) &&
	thrf=$(git rev-parse --verify main:file3) &&
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/branch
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	second
	COMMIT

	from refs/heads/main
	M 644 $oldf file2/oldf
	M 755 $newf file2/newf
	D file3

	INPUT_END

	git fast-import <input &&
	git log --raw branch
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
	parent $(git rev-parse --verify main^0)
	author $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE

	second
	EOF

	git cat-file commit branch | sed 1d >actual &&
	test_cmp expect actual
'

test_expect_success 'C: validate rename result' '
	zero=$ZERO_OID &&
	cat >expect <<-EOF &&
	:000000 100755 $zero $newf A	file2/newf
	:100644 100644 $oldf $oldf R100	file2	file2/oldf
	:100644 000000 $thrf $zero D	file3
	EOF
	git diff-tree -M -r main branch >actual &&
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
	git log --raw branch
'

test_expect_success 'D: verify pack' '
	verify_packs
'

test_expect_success 'D: validate new files added' '
	f5id=$(echo "$file5_data" | git hash-object --stdin) &&
	f6id=$(echo "$file6_data" | git hash-object --stdin) &&
	cat >expect <<-EOF &&
	:000000 100755 $ZERO_OID $f6id A	newdir/exec.sh
	:000000 100644 $ZERO_OID $f5id A	newdir/interesting
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
	git log --raw H
'

test_expect_success 'H: verify pack' '
	verify_packs
'

test_expect_success 'H: validate old files removed, new files added' '
	f4id=$(git rev-parse HEAD:file4) &&
	cat >expect <<-EOF &&
	:100755 000000 $newf $zero D	file2/newf
	:100644 000000 $oldf $zero D	file2/oldf
	:100755 000000 $f4id $zero D	file4
	:100644 100644 $f5id $f5id R100	newdir/interesting	h/e/l/lo
	:100755 000000 $f6id $zero D	newdir/exec.sh
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
	:100644 100644 M	b.
	:040000 040000 M	b
	:100644 100644 M	ba
	EXPECT_END

	git -c core.protectNTFS=false fast-import <input &&
	GIT_PRINT_SHA1_ELLIPSIS="yes" git diff-tree --abbrev --raw L^ L >output &&
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
	cut -f 2 <tmp >actual &&
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
	:100755 100755 $newf $newf R100	file2/newf	file2/n.e.w.f
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
	:100755 100755 $newf $newf R100	file2/newf	i/am/new/to/you
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
	:100755 100755 $newf $newf R100	i/am/new/to/you	other/sub/am/new/to/you
	EOF
	git fast-import <input &&
	git diff-tree -M -r M3^ M3 >actual &&
	compare_diff_raw expect actual
'

for root in '""' ''
do
	test_expect_success "M: rename root ($root) to subdirectory" '
		cat >input <<-INPUT_END &&
		commit refs/heads/M4
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		rename root
		COMMIT

		from refs/heads/M2^0
		R $root sub

		INPUT_END

		cat >expect <<-EOF &&
		:100644 100644 $oldf $oldf R100	file2/oldf	sub/file2/oldf
		:100755 100755 $f4id $f4id R100	file4	sub/file4
		:100755 100755 $newf $newf R100	i/am/new/to/you	sub/i/am/new/to/you
		:100755 100755 $f6id $f6id R100	newdir/exec.sh	sub/newdir/exec.sh
		:100644 100644 $f5id $f5id R100	newdir/interesting	sub/newdir/interesting
		EOF
		git fast-import <input &&
		git diff-tree -M -r M4^ M4 >actual &&
		compare_diff_raw expect actual
	'
done

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
	:100755 100755 $newf $newf C100	file2/newf	file2/n.e.w.f
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
	:100644 100644 $f5id $f5id C100	newdir/interesting	file3/file5
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
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
	cat >expect <<-EOF &&
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
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
	cat >expect <<-EOF &&
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
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

for root in '""' ''
do
	test_expect_success "N: copy root ($root) by tree hash" '
		cat >expect <<-EOF &&
		:100755 000000 $newf $zero D	file3/newf
		:100644 000000 $oldf $zero D	file3/oldf
		EOF
		root_tree=$(git rev-parse refs/heads/branch^0^{tree}) &&
		cat >input <<-INPUT_END &&
		commit refs/heads/N6
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		copy root directory by tree hash
		COMMIT

		from refs/heads/branch^0
		M 040000 $root_tree $root
		INPUT_END
		git fast-import <input &&
		git diff-tree -C --find-copies-harder -r N4 N6 >actual &&
		compare_diff_raw expect actual
	'

	test_expect_success "N: copy root ($root) by path" '
		cat >expect <<-EOF &&
		:100755 100755 $newf $newf C100	file2/newf	oldroot/file2/newf
		:100644 100644 $oldf $oldf C100	file2/oldf	oldroot/file2/oldf
		:100755 100755 $f4id $f4id C100	file4	oldroot/file4
		:100755 100755 $f6id $f6id C100	newdir/exec.sh	oldroot/newdir/exec.sh
		:100644 100644 $f5id $f5id C100	newdir/interesting	oldroot/newdir/interesting
		EOF
		cat >input <<-INPUT_END &&
		commit refs/heads/N-copy-root-path
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		copy root directory by (empty) path
		COMMIT

		from refs/heads/branch^0
		C $root oldroot
		INPUT_END
		git fast-import <input &&
		git diff-tree -C --find-copies-harder -r branch N-copy-root-path >actual &&
		compare_diff_raw expect actual
	'
done

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
	cat >expect <<-EOF &&
	:100644 100644 $f5id $f5id C100	newdir/interesting	file3/file5
	:100755 100755 $newf $newf C100	file2/newf	file3/newf
	:100644 100644 $oldf $oldf C100	file2/oldf	file3/oldf
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

for root in '""' ''
do
	test_expect_success "N: copy to root ($root) by id and modify" '
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

		M 040000 $tree $root
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

	test_expect_success "N: extract subtree to the root ($root)" '
		branch=$(git rev-parse --verify refs/heads/branch^{tree}) &&
		cat >input <<-INPUT_END &&
		commit refs/heads/N9
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		extract subtree branch:newdir
		COMMIT

		M 040000 $branch $root
		C "newdir" $root
		INPUT_END
		git fast-import <input &&
		git diff --exit-code branch:newdir N9
	'

	test_expect_success "N: modify subtree, extract it to the root ($root), and modify again" '
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

		M 040000 $tree $root
		M 100644 inline foo/bar/qux
		data <<EOF
		hello, world
		EOF
		R "foo" $root
		C "bar/qux" "bar/quux"
		INPUT_END
		git show N11:bar/baz >actual.baz &&
		git show N11:bar/qux >actual.qux &&
		git show N11:bar/quux >actual.quux &&
		test_cmp expect.baz actual.baz &&
		test_cmp expect.qux actual.qux &&
		test_cmp expect.qux actual.quux
	'
done



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

	f7id=$(echo "blob 1" | git hash-object --stdin) &&
	f8id=$(echo "sleep well" | git hash-object --stdin) &&
	f9id=$(echo "au revoir" | git hash-object --stdin) &&
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
	:100644 000000 $f8id $ZERO_OID D	good/night.txt
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
	:100644 000000 $f9id $ZERO_OID D	good/bye.txt
	EOF

	git diff-tree -M -r U^1 U >actual &&

	compare_diff_raw expect actual
'

for root in '""' ''
do
	test_expect_success "U: filedelete root ($root) succeeds" '
		cat >input <<-INPUT_END &&
		commit refs/heads/U-delete-root
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		must succeed
		COMMIT
		from refs/heads/U^0
		D $root

		INPUT_END

		git fast-import <input
	'

	test_expect_success "U: validate root ($root) delete result" '
		cat >expect <<-EOF &&
		:100644 000000 $f7id $ZERO_OID D	hello.c
		EOF

		git diff-tree -M -r U U-delete-root >actual &&

		compare_diff_raw expect actual
	'
done

test_done
