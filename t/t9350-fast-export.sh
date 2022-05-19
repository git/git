#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='but fast-export'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '

	echo break it > file0 &&
	but add file0 &&
	test_tick &&
	echo Wohlauf > file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	echo die Luft > file &&
	echo geht frisch > file2 &&
	but add file file2 &&
	test_tick &&
	but cummit -m second &&
	echo und > file2 &&
	test_tick &&
	but cummit -m third file2 &&
	test_tick &&
	but tag rein &&
	but checkout -b wer HEAD^ &&
	echo lange > file2 &&
	test_tick &&
	but cummit -m sitzt file2 &&
	test_tick &&
	but tag -a -m valentin muss &&
	but merge -s ours main

'

test_expect_success 'fast-export | fast-import' '

	MAIN=$(but rev-parse --verify main) &&
	REIN=$(but rev-parse --verify rein) &&
	WER=$(but rev-parse --verify wer) &&
	MUSS=$(but rev-parse --verify muss) &&
	mkdir new &&
	but --but-dir=new/.but init &&
	but fast-export --all >actual &&
	(cd new &&
	 but fast-import &&
	 test $MAIN = $(but rev-parse --verify refs/heads/main) &&
	 test $REIN = $(but rev-parse --verify refs/tags/rein) &&
	 test $WER = $(but rev-parse --verify refs/heads/wer) &&
	 test $MUSS = $(but rev-parse --verify refs/tags/muss)) <actual

'

test_expect_success 'fast-export ^muss^{cummit} muss' '
	but fast-export --tag-of-filtered-object=rewrite ^muss^{cummit} muss >actual &&
	cat >expected <<-EOF &&
	tag muss
	from $(but rev-parse --verify muss^{cummit})
	$(but cat-file tag muss | grep tagger)
	data 9
	valentin

	EOF
	test_cmp expected actual
'

test_expect_success 'fast-export --mark-tags ^muss^{cummit} muss' '
	but fast-export --mark-tags --tag-of-filtered-object=rewrite ^muss^{cummit} muss >actual &&
	cat >expected <<-EOF &&
	tag muss
	mark :1
	from $(but rev-parse --verify muss^{cummit})
	$(but cat-file tag muss | grep tagger)
	data 9
	valentin

	EOF
	test_cmp expected actual
'

test_expect_success 'fast-export main~2..main' '

	but fast-export main~2..main >actual &&
	sed "s/main/partial/" actual |
		(cd new &&
		 but fast-import &&
		 test $MAIN != $(but rev-parse --verify refs/heads/partial) &&
		 but diff --exit-code main partial &&
		 but diff --exit-code main^ partial^ &&
		 test_must_fail but rev-parse partial~2)

'

test_expect_success 'fast-export --reference-excluded-parents main~2..main' '

	but fast-export --reference-excluded-parents main~2..main >actual &&
	grep cummit.refs/heads/main actual >cummit-count &&
	test_line_count = 2 cummit-count &&
	sed "s/main/rewrite/" actual |
		(cd new &&
		 but fast-import &&
		 test $MAIN = $(but rev-parse --verify refs/heads/rewrite))
'

test_expect_success 'fast-export --show-original-ids' '

	but fast-export --show-original-ids main >output &&
	grep ^original-oid output| sed -e s/^original-oid.// | sort >actual &&
	but rev-list --objects main muss >objects-and-names &&
	awk "{print \$1}" objects-and-names | sort >cummits-trees-blobs &&
	comm -23 actual cummits-trees-blobs >unfound &&
	test_must_be_empty unfound
'

test_expect_success 'fast-export --show-original-ids | but fast-import' '

	but fast-export --show-original-ids main muss | but fast-import --quiet &&
	test $MAIN = $(but rev-parse --verify refs/heads/main) &&
	test $MUSS = $(but rev-parse --verify refs/tags/muss)
'

test_expect_success 'reencoding iso-8859-7' '

	test_when_finished "but reset --hard HEAD~1" &&
	test_config i18n.cummitencoding iso-8859-7 &&
	test_tick &&
	echo rosten >file &&
	but cummit -s -F "$TEST_DIRECTORY/t9350/simple-iso-8859-7-cummit-message.txt" file &&
	but fast-export --reencode=yes wer^..wer >iso-8859-7.fi &&
	sed "s/wer/i18n/" iso-8859-7.fi |
		(cd new &&
		 but fast-import &&
		 # The cummit object, if not re-encoded, would be 200 bytes plus hash.
		 # Removing the "encoding iso-8859-7\n" header drops 20 bytes.
		 # Re-encoding the Pi character from \xF0 (\360) in iso-8859-7
		 # to \xCF\x80 (\317\200) in UTF-8 adds a byte.  Check for
		 # the expected size.
		 test $(($(test_oid hexsz) + 181)) -eq "$(but cat-file -s i18n)" &&
		 # ...and for the expected translation of bytes.
		 but cat-file cummit i18n >actual &&
		 grep $(printf "\317\200") actual &&
		 # Also make sure the cummit does not have the "encoding" header
		 ! grep ^encoding actual)
'

test_expect_success 'aborting on iso-8859-7' '

	test_when_finished "but reset --hard HEAD~1" &&
	test_config i18n.cummitencoding iso-8859-7 &&
	echo rosten >file &&
	but cummit -s -F "$TEST_DIRECTORY/t9350/simple-iso-8859-7-cummit-message.txt" file &&
	test_must_fail but fast-export --reencode=abort wer^..wer >iso-8859-7.fi
'

test_expect_success 'preserving iso-8859-7' '

	test_when_finished "but reset --hard HEAD~1" &&
	test_config i18n.cummitencoding iso-8859-7 &&
	echo rosten >file &&
	but cummit -s -F "$TEST_DIRECTORY/t9350/simple-iso-8859-7-cummit-message.txt" file &&
	but fast-export --reencode=no wer^..wer >iso-8859-7.fi &&
	sed "s/wer/i18n-no-recoding/" iso-8859-7.fi |
		(cd new &&
		 but fast-import &&
		 # The cummit object, if not re-encoded, is 200 bytes plus hash.
		 # Removing the "encoding iso-8859-7\n" header would drops 20
		 # bytes.  Re-encoding the Pi character from \xF0 (\360) in
		 # iso-8859-7 to \xCF\x80 (\317\200) in UTF-8 adds a byte.
		 # Check for the expected size...
		 test $(($(test_oid hexsz) + 200)) -eq "$(but cat-file -s i18n-no-recoding)" &&
		 # ...as well as the expected byte.
		 but cat-file cummit i18n-no-recoding >actual &&
		 grep $(printf "\360") actual &&
		 # Also make sure the commit has the "encoding" header
		 grep ^encoding actual)
'

test_expect_success 'encoding preserved if reencoding fails' '

	test_when_finished "but reset --hard HEAD~1" &&
	test_config i18n.cummitencoding iso-8859-7 &&
	echo rosten >file &&
	but cummit -s -F "$TEST_DIRECTORY/t9350/broken-iso-8859-7-cummit-message.txt" file &&
	but fast-export --reencode=yes wer^..wer >iso-8859-7.fi &&
	sed "s/wer/i18n-invalid/" iso-8859-7.fi |
		(cd new &&
		 but fast-import &&
		 but cat-file cummit i18n-invalid >actual &&
		 # Make sure the cummit still has the encoding header
		 grep ^encoding actual &&
		 # Verify that the commit has the expected size; i.e.
		 # that no bytes were re-encoded to a different encoding.
		 test $(($(test_oid hexsz) + 212)) -eq "$(but cat-file -s i18n-invalid)" &&
		 # ...and check for the original special bytes
		 grep $(printf "\360") actual &&
		 grep $(printf "\377") actual)
'

test_expect_success 'import/export-marks' '

	but checkout -b marks main &&
	but fast-export --export-marks=tmp-marks HEAD &&
	test -s tmp-marks &&
	test_line_count = 3 tmp-marks &&
	but fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks HEAD >actual &&
	test $(grep ^cummit actual | wc -l) -eq 0 &&
	echo change > file &&
	but cummit -m "last cummit" file &&
	but fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks HEAD >actual &&
	test $(grep ^cummit\  actual | wc -l) -eq 1 &&
	test_line_count = 4 tmp-marks

'

cat > signed-tag-import << EOF
tag sign-your-name
from $(but rev-parse HEAD)
tagger C O Mitter <cummitter@example.com> 1112911993 -0700
data 210
A message for a sign
-----BEGIN PGP SIGNATURE-----
Version: GnuPG v1.4.5 (GNU/Linux)

fakedsignaturefakedsignaturefakedsignaturefakedsignaturfakedsign
aturefakedsignaturefake=
=/59v
-----END PGP SIGNATURE-----
EOF

test_expect_success 'set up faked signed tag' '

	cat signed-tag-import | but fast-import

'

test_expect_success 'signed-tags=abort' '

	test_must_fail but fast-export --signed-tags=abort sign-your-name

'

test_expect_success 'signed-tags=verbatim' '

	but fast-export --signed-tags=verbatim sign-your-name > output &&
	grep PGP output

'

test_expect_success 'signed-tags=strip' '

	but fast-export --signed-tags=strip sign-your-name > output &&
	! grep PGP output

'

test_expect_success 'signed-tags=warn-strip' '
	but fast-export --signed-tags=warn-strip sign-your-name >output 2>err &&
	! grep PGP output &&
	test -s err
'

test_expect_success 'setup submodule' '

	but checkout -f main &&
	mkdir sub &&
	(
		cd sub &&
		but init  &&
		echo test file > file &&
		but add file &&
		but cummit -m sub_initial
	) &&
	but submodule add "$(pwd)/sub" sub &&
	but cummit -m initial &&
	test_tick &&
	(
		cd sub &&
		echo more data >> file &&
		but add file &&
		but cummit -m sub_second
	) &&
	but add sub &&
	but cummit -m second

'

test_expect_success 'submodule fast-export | fast-import' '

	SUBENT1=$(but ls-tree main^ sub) &&
	SUBENT2=$(but ls-tree main sub) &&
	rm -rf new &&
	mkdir new &&
	but --but-dir=new/.but init &&
	but fast-export --signed-tags=strip --all >actual &&
	(cd new &&
	 but fast-import &&
	 test "$SUBENT1" = "$(but ls-tree refs/heads/main^ sub)" &&
	 test "$SUBENT2" = "$(but ls-tree refs/heads/main sub)" &&
	 but checkout main &&
	 but submodule init &&
	 but submodule update &&
	 cmp sub/file ../sub/file) <actual

'

GIT_AUTHOR_NAME='A U Thor'; export GIT_AUTHOR_NAME
GIT_CUMMITTER_NAME='C O Mitter'; export GIT_CUMMITTER_NAME

test_expect_success 'setup copies' '

	but checkout -b copy rein &&
	but mv file file3 &&
	but cummit -m move1 &&
	test_tick &&
	cp file2 file4 &&
	but add file4 &&
	but mv file2 file5 &&
	but cummit -m copy1 &&
	test_tick &&
	cp file3 file6 &&
	but add file6 &&
	but cummit -m copy2 &&
	test_tick &&
	echo more text >> file6 &&
	echo even more text >> file6 &&
	but add file6 &&
	but cummit -m modify &&
	test_tick &&
	cp file6 file7 &&
	echo test >> file7 &&
	but add file7 &&
	but cummit -m copy_modify

'

test_expect_success 'fast-export -C -C | fast-import' '

	ENTRY=$(but rev-parse --verify copy) &&
	rm -rf new &&
	mkdir new &&
	but --but-dir=new/.but init &&
	but fast-export -C -C --signed-tags=strip --all > output &&
	grep "^C file2 file4\$" output &&
	cat output |
	(cd new &&
	 but fast-import &&
	 test $ENTRY = $(but rev-parse --verify refs/heads/copy))

'

test_expect_success 'fast-export | fast-import when main is tagged' '

	but tag -m msg last &&
	but fast-export -C -C --signed-tags=strip --all > output &&
	test $(grep -c "^tag " output) = 3

'

cat > tag-content << EOF
object $(but rev-parse HEAD)
type cummit
tag rosten
EOF

test_expect_success 'cope with tagger-less tags' '

	TAG=$(but hash-object -t tag -w tag-content) &&
	but update-ref refs/tags/sonnenschein $TAG &&
	but fast-export -C -C --signed-tags=strip --all > output &&
	test $(grep -c "^tag " output) = 4 &&
	! grep "Unspecified Tagger" output &&
	but fast-export -C -C --signed-tags=strip --all \
		--fake-missing-tagger > output &&
	test $(grep -c "^tag " output) = 4 &&
	grep "Unspecified Tagger" output

'

test_expect_success 'setup for limiting exports by PATH' '
	mkdir limit-by-paths &&
	(
		cd limit-by-paths &&
		but init &&
		echo hi > there &&
		but add there &&
		but cummit -m "First file" &&
		echo foo > bar &&
		but add bar &&
		but cummit -m "Second file" &&
		but tag -a -m msg mytag &&
		echo morefoo >> bar &&
		but add bar &&
		but cummit -m "Change to second file"
	)
'

cat > limit-by-paths/expected << EOF
blob
mark :1
data 3
hi

reset refs/tags/mytag
cummit refs/tags/mytag
mark :2
author A U Thor <author@example.com> 1112912713 -0700
cummitter C O Mitter <cummitter@example.com> 1112912713 -0700
data 11
First file
M 100644 :1 there

EOF

test_expect_success 'dropping tag of filtered out object' '
(
	cd limit-by-paths &&
	but fast-export --tag-of-filtered-object=drop mytag -- there > output &&
	test_cmp expected output
)
'

cat >> limit-by-paths/expected << EOF
tag mytag
from :2
tagger C O Mitter <cummitter@example.com> 1112912713 -0700
data 4
msg

EOF

test_expect_success 'rewriting tag of filtered out object' '
(
	cd limit-by-paths &&
	but fast-export --tag-of-filtered-object=rewrite mytag -- there > output &&
	test_cmp expected output
)
'

test_expect_success 'rewrite tag predating pathspecs to nothing' '
	test_create_repo rewrite_tag_predating_pathspecs &&
	(
		cd rewrite_tag_predating_pathspecs &&

		test_cummit initial &&

		but tag -a -m "Some old tag" v0.0.0.0.0.0.1 &&

		test_cummit bar &&

		but fast-export --tag-of-filtered-object=rewrite --all -- bar.t >output &&
		grep from.$ZERO_OID output
	)
'

cat > limit-by-paths/expected << EOF
blob
mark :1
data 4
foo

blob
mark :2
data 3
hi

reset refs/heads/main
cummit refs/heads/main
mark :3
author A U Thor <author@example.com> 1112912713 -0700
cummitter C O Mitter <cummitter@example.com> 1112912713 -0700
data 12
Second file
M 100644 :1 bar
M 100644 :2 there

EOF

test_expect_failure 'no exact-ref revisions included' '
	(
		cd limit-by-paths &&
		but fast-export main~2..main~1 > output &&
		test_cmp expected output
	)
'

test_expect_success 'path limiting with import-marks does not lose unmodified files'        '
	but checkout -b simple marks~2 &&
	but fast-export --export-marks=marks simple -- file > /dev/null &&
	echo more content >> file &&
	test_tick &&
	but cummit -mnext file &&
	but fast-export --import-marks=marks simple -- file file0 >actual &&
	grep file0 actual
'

test_expect_success 'path limiting works' '
	but fast-export simple -- file >actual &&
	sed -ne "s/^M .* //p" <actual | sort -u >actual.files &&
	echo file >expect &&
	test_cmp expect actual.files
'

test_expect_success 'avoid corrupt stream with non-existent mark' '
	test_create_repo avoid_non_existent_mark &&
	(
		cd avoid_non_existent_mark &&

		test_cummit important-path &&

		test_cummit ignored &&

		but branch A &&
		but branch B &&

		echo foo >>important-path.t &&
		but add important-path.t &&
		test_cummit more changes &&

		but fast-export --all -- important-path.t | but fast-import --force
	)
'

test_expect_success 'full-tree re-shows unmodified files'        '
	but checkout -f simple &&
	but fast-export --full-tree simple >actual &&
	test $(grep -c file0 actual) -eq 3
'

test_expect_success 'set-up a few more tags for tag export tests' '
	but checkout -f main &&
	HEAD_TREE=$(but show -s --pretty=raw HEAD | grep tree | sed "s/tree //") &&
	but tag    tree_tag        -m "tagging a tree" $HEAD_TREE &&
	but tag -a tree_tag-obj    -m "tagging a tree" $HEAD_TREE &&
	but tag    tag-obj_tag     -m "tagging a tag" tree_tag-obj &&
	but tag -a tag-obj_tag-obj -m "tagging a tag" tree_tag-obj
'

test_expect_success 'tree_tag'        '
	mkdir result &&
	(cd result && but init) &&
	but fast-export tree_tag > fe-stream &&
	(cd result && but fast-import < ../fe-stream)
'

# NEEDSWORK: not just check return status, but validate the output
# Note that these tests DO NOTHING other than print a warning that
# they are omitting the one tag we asked them to export (because the
# tags resolve to a tree).  They exist just to make sure we do not
# abort but instead just warn.
test_expect_success 'tree_tag-obj'    'but fast-export tree_tag-obj'
test_expect_success 'tag-obj_tag'     'but fast-export tag-obj_tag'
test_expect_success 'tag-obj_tag-obj' 'but fast-export tag-obj_tag-obj'

test_expect_success 'handling tags of blobs' '
	but tag -a -m "Tag of a blob" blobtag $(but rev-parse main:file) &&
	but fast-export blobtag >actual &&
	cat >expect <<-EOF &&
	blob
	mark :1
	data 9
	die Luft

	tag blobtag
	from :1
	tagger $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE
	data 14
	Tag of a blob

	EOF
	test_cmp expect actual
'

test_expect_success 'handling nested tags' '
	but tag -a -m "This is a nested tag" nested muss &&
	but fast-export --mark-tags nested >output &&
	grep "^from $ZERO_OID$" output &&
	grep "^tag nested$" output >tag_lines &&
	test_line_count = 2 tag_lines
'

test_expect_success 'directory becomes symlink'        '
	but init dirtosymlink &&
	but init result &&
	(
		cd dirtosymlink &&
		mkdir foo &&
		mkdir bar &&
		echo hello > foo/world &&
		echo hello > bar/world &&
		but add foo/world bar/world &&
		but cummit -q -mone &&
		but rm -r foo &&
		test_ln_s_add bar foo &&
		but cummit -q -mtwo
	) &&
	(
		cd dirtosymlink &&
		but fast-export main -- foo |
		(cd ../result && but fast-import --quiet)
	) &&
	(cd result && but show main:foo)
'

test_expect_success 'fast-export quotes pathnames' '
	but init crazy-paths &&
	test_config -C crazy-paths core.protectNTFS false &&
	(cd crazy-paths &&
	 blob=$(echo foo | but hash-object -w --stdin) &&
	 but -c core.protectNTFS=false update-index --add \
		--cacheinfo 100644 $blob "$(printf "path with\\nnewline")" \
		--cacheinfo 100644 $blob "path with \"quote\"" \
		--cacheinfo 100644 $blob "path with \\backslash" \
		--cacheinfo 100644 $blob "path with space" &&
	 but cummit -m addition &&
	 but ls-files -z -s | perl -0pe "s{\\t}{$&subdir/}" >index &&
	 but read-tree --empty &&
	 but update-index -z --index-info <index &&
	 but cummit -m rename &&
	 but read-tree --empty &&
	 but cummit -m deletion &&
	 but fast-export -M HEAD >export.out &&
	 but rev-list HEAD >expect &&
	 but init result &&
	 cd result &&
	 but fast-import <../export.out &&
	 but rev-list HEAD >actual &&
	 test_cmp ../expect actual
	)
'

test_expect_success 'test bidirectionality' '
	but init marks-test &&
	but fast-export --export-marks=marks-cur --import-marks-if-exists=marks-cur --branches | \
	but --but-dir=marks-test/.but fast-import --export-marks=marks-new --import-marks-if-exists=marks-new &&
	(cd marks-test &&
	but reset --hard &&
	echo Wohlauf > file &&
	but cummit -a -m "back in time") &&
	but --but-dir=marks-test/.but fast-export --export-marks=marks-new --import-marks-if-exists=marks-new --branches | \
	but fast-import --export-marks=marks-cur --import-marks-if-exists=marks-cur
'

cat > expected << EOF
blob
mark :13
data 5
bump

cummit refs/heads/main
mark :14
author A U Thor <author@example.com> 1112912773 -0700
cummitter C O Mitter <cummitter@example.com> 1112912773 -0700
data 5
bump
from :12
M 100644 :13 file

EOF

test_expect_success 'avoid uninteresting refs' '
	> tmp-marks &&
	but fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks main > /dev/null &&
	but tag v1.0 &&
	but branch uninteresting &&
	echo bump > file &&
	but cummit -a -m bump &&
	but fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks ^uninteresting ^v1.0 main > actual &&
	test_cmp expected actual
'

cat > expected << EOF
reset refs/heads/main
from :14

EOF

test_expect_success 'refs are updated even if no cummits need to be exported' '
	> tmp-marks &&
	but fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks main > /dev/null &&
	but fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks main > actual &&
	test_cmp expected actual
'

test_expect_success 'use refspec' '
	but fast-export --refspec refs/heads/main:refs/heads/foobar main >actual2 &&
	grep "^cummit " actual2 | sort | uniq >actual &&
	echo "cummit refs/heads/foobar" > expected &&
	test_cmp expected actual
'

test_expect_success 'delete ref because entire history excluded' '
	but branch to-delete &&
	but fast-export to-delete ^to-delete >actual &&
	cat >expected <<-EOF &&
	reset refs/heads/to-delete
	from $ZERO_OID

	EOF
	test_cmp expected actual
'

test_expect_success 'delete refspec' '
	but fast-export --refspec :refs/heads/to-delete >actual &&
	cat >expected <<-EOF &&
	reset refs/heads/to-delete
	from $ZERO_OID

	EOF
	test_cmp expected actual
'

test_expect_success 'when using -C, do not declare copy when source of copy is also modified' '
	test_create_repo src &&
	echo a_line >src/file.txt &&
	but -C src add file.txt &&
	but -C src cummit -m 1st_cummit &&

	cp src/file.txt src/file2.txt &&
	echo another_line >>src/file.txt &&
	but -C src add file.txt file2.txt &&
	but -C src cummit -m 2nd_cummit &&

	test_create_repo dst &&
	but -C src fast-export --all -C >actual &&
	but -C dst fast-import <actual &&
	but -C src show >expected &&
	but -C dst show >actual &&
	test_cmp expected actual
'

test_expect_success 'merge cummit gets exported with --import-marks' '
	test_create_repo merging &&
	(
		cd merging &&
		test_cummit initial &&
		but checkout -b topic &&
		test_cummit on-topic &&
		but checkout main &&
		test_cummit on-main &&
		test_tick &&
		but merge --no-ff -m Yeah topic &&

		echo ":1 $(but rev-parse HEAD^^)" >marks &&
		but fast-export --import-marks=marks main >out &&
		grep Yeah out
	)
'


test_expect_success 'fast-export --first-parent outputs all revisions output by revision walk' '
	but init first-parent &&
	(
		cd first-parent &&
		test_cummit A &&
		but checkout -b topic1 &&
		test_cummit B &&
		but checkout main &&
		but merge --no-ff topic1 &&

		but checkout -b topic2 &&
		test_cummit C &&
		but checkout main &&
		but merge --no-ff topic2 &&

		test_cummit D &&

		but fast-export main -- --first-parent >first-parent-export &&
		but fast-export main -- --first-parent --reverse >first-parent-reverse-export &&
		test_cmp first-parent-export first-parent-reverse-export &&

		but init import &&
		but -C import fast-import <first-parent-export &&

		but log --format="%ad %s" --first-parent main >expected &&
		but -C import log --format="%ad %s" --all >actual &&
		test_cmp expected actual &&
		test_line_count = 4 actual
	)
'

test_done
