#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git fast-export'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success 'setup' '

	echo break it > file0 &&
	git add file0 &&
	test_tick &&
	echo Wohlauf > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo die Luft > file &&
	echo geht frisch > file2 &&
	git add file file2 &&
	test_tick &&
	git commit -m second &&
	echo und > file2 &&
	test_tick &&
	git commit -m third file2 &&
	test_tick &&
	git tag rein &&
	git checkout -b wer HEAD^ &&
	echo lange > file2 &&
	test_tick &&
	git commit -m sitzt file2 &&
	test_tick &&
	git tag -a -m valentin muss &&
	ANNOTATED_TAG_COUNT=1 &&
	git merge -s ours main

'

test_expect_success 'fast-export | fast-import' '

	MAIN=$(git rev-parse --verify main) &&
	REIN=$(git rev-parse --verify rein) &&
	WER=$(git rev-parse --verify wer) &&
	MUSS=$(git rev-parse --verify muss) &&
	mkdir new &&
	git --git-dir=new/.git init &&
	git fast-export --all >actual &&
	git -C new fast-import <actual &&
	test $MAIN = $(git -C new rev-parse --verify refs/heads/main) &&
	test $REIN = $(git -C new rev-parse --verify refs/tags/rein) &&
	test $WER = $(git -C new rev-parse --verify refs/heads/wer) &&
	test $MUSS = $(git -C new rev-parse --verify refs/tags/muss)

'

test_expect_success 'fast-export ^muss^{commit} muss' '
	git fast-export --tag-of-filtered-object=rewrite ^muss^{commit} muss >actual &&
	cat >expected <<-EOF &&
	tag muss
	from $(git rev-parse --verify muss^{commit})
	$(git cat-file tag muss | grep tagger)
	data 9
	valentin

	EOF
	test_cmp expected actual
'

test_expect_success 'fast-export --mark-tags ^muss^{commit} muss' '
	git fast-export --mark-tags --tag-of-filtered-object=rewrite ^muss^{commit} muss >actual &&
	cat >expected <<-EOF &&
	tag muss
	mark :1
	from $(git rev-parse --verify muss^{commit})
	$(git cat-file tag muss | grep tagger)
	data 9
	valentin

	EOF
	test_cmp expected actual
'

test_expect_success 'fast-export main~2..main' '

	git fast-export main~2..main >actual &&
	sed "s/main/partial/" actual | git -C new fast-import &&
	test $MAIN != $(git -C new rev-parse --verify refs/heads/partial) &&
	git -C new diff --exit-code main partial &&
	git -C new diff --exit-code main^ partial^ &&
	test_must_fail git -C new rev-parse partial~2

'

test_expect_success 'fast-export --reference-excluded-parents main~2..main' '

	git fast-export --reference-excluded-parents main~2..main >actual &&
	grep commit.refs/heads/main actual >commit-count &&
	test_line_count = 2 commit-count &&
	sed "s/main/rewrite/" actual | git -C new fast-import &&
	test $MAIN = $(git -C new rev-parse --verify refs/heads/rewrite)
'

test_expect_success 'fast-export --show-original-ids' '

	git fast-export --show-original-ids main >output &&
	grep ^original-oid output| sed -e s/^original-oid.// | sort >actual &&
	git rev-list --objects main muss >objects-and-names &&
	awk "{print \$1}" objects-and-names | sort >commits-trees-blobs &&
	comm -23 actual commits-trees-blobs >unfound &&
	test_must_be_empty unfound
'

test_expect_success 'fast-export --show-original-ids | git fast-import' '

	git fast-export --show-original-ids main muss | git fast-import --quiet &&
	test $MAIN = $(git rev-parse --verify refs/heads/main) &&
	test $MUSS = $(git rev-parse --verify refs/tags/muss)
'

test_expect_success ICONV 'reencoding iso-8859-7' '

	test_when_finished "git reset --hard HEAD~1" &&
	test_config i18n.commitencoding iso-8859-7 &&
	test_tick &&
	echo rosten >file &&
	git commit -s -F "$TEST_DIRECTORY/t9350/simple-iso-8859-7-commit-message.txt" file &&
	git fast-export --reencode=yes wer^..wer >iso-8859-7.fi &&
	sed "s/wer/i18n/" iso-8859-7.fi | git -C new fast-import &&

	# The commit object, if not re-encoded, would be 200 bytes plus hash.
	# Removing the "encoding iso-8859-7\n" header drops 20 bytes.
	# Re-encoding the Pi character from \xF0 (\360) in iso-8859-7
	# to \xCF\x80 (\317\200) in UTF-8 adds a byte.  Check for
	# the expected size.
	test $(($(test_oid hexsz) + 181)) -eq "$(git -C new cat-file -s i18n)" &&
	# ...and for the expected translation of bytes.
	git -C new cat-file commit i18n >actual &&
	grep $(printf "\317\200") actual &&
	# Also make sure the commit does not have the "encoding" header
	! grep ^encoding actual
'

test_expect_success 'aborting on iso-8859-7' '

	test_when_finished "git reset --hard HEAD~1" &&
	test_config i18n.commitencoding iso-8859-7 &&
	echo rosten >file &&
	git commit -s -F "$TEST_DIRECTORY/t9350/simple-iso-8859-7-commit-message.txt" file &&
	test_must_fail git fast-export --reencode=abort wer^..wer >iso-8859-7.fi
'

test_expect_success 'preserving iso-8859-7' '

	test_when_finished "git reset --hard HEAD~1" &&
	test_config i18n.commitencoding iso-8859-7 &&
	echo rosten >file &&
	git commit -s -F "$TEST_DIRECTORY/t9350/simple-iso-8859-7-commit-message.txt" file &&
	git fast-export --reencode=no wer^..wer >iso-8859-7.fi &&
	sed "s/wer/i18n-no-recoding/" iso-8859-7.fi | git -C new fast-import &&

	# The commit object, if not re-encoded, is 200 bytes plus hash.
	# Removing the "encoding iso-8859-7\n" header would drops 20
	# bytes.  Re-encoding the Pi character from \xF0 (\360) in
	# iso-8859-7 to \xCF\x80 (\317\200) in UTF-8 adds a byte.
	# Check for the expected size...
	test $(($(test_oid hexsz) + 200)) -eq "$(git -C new cat-file -s i18n-no-recoding)" &&
	# ...as well as the expected byte.
	git -C new cat-file commit i18n-no-recoding >actual &&
	grep $(printf "\360") actual &&
	# Also make sure the commit has the "encoding" header
	grep ^encoding actual
'

test_expect_success 'encoding preserved if reencoding fails' '

	test_when_finished "git reset --hard HEAD~1" &&
	test_config i18n.commitencoding iso-8859-7 &&
	echo rosten >file &&
	git commit -s -F "$TEST_DIRECTORY/t9350/broken-iso-8859-7-commit-message.txt" file &&
	git fast-export --reencode=yes wer^..wer >iso-8859-7.fi &&
	sed "s/wer/i18n-invalid/" iso-8859-7.fi | git -C new fast-import &&
	git -C new cat-file commit i18n-invalid >actual &&

	# Make sure the commit still has the encoding header
	grep ^encoding actual &&
	# Verify that the commit has the expected size; i.e.
	# that no bytes were re-encoded to a different encoding.
	test $(($(test_oid hexsz) + 212)) -eq "$(git -C new cat-file -s i18n-invalid)" &&
	# ...and check for the original special bytes
	grep $(printf "\360") actual &&
	grep $(printf "\377") actual
'

test_expect_success 'import/export-marks' '

	git checkout -b marks main &&
	git fast-export --export-marks=tmp-marks HEAD &&
	test -s tmp-marks &&
	test_line_count = 3 tmp-marks &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks HEAD >actual &&
	test $(grep ^commit actual | wc -l) -eq 0 &&
	echo change > file &&
	git commit -m "last commit" file &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks HEAD >actual &&
	test $(grep ^commit\  actual | wc -l) -eq 1 &&
	test_line_count = 4 tmp-marks

'

cat > signed-tag-import << EOF
tag sign-your-name
from $(git rev-parse HEAD)
tagger C O Mitter <committer@example.com> 1112911993 -0700
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

	git fast-import <signed-tag-import &&
	ANNOTATED_TAG_COUNT=$((ANNOTATED_TAG_COUNT + 1))

'

test_expect_success 'signed-tags=abort' '

	test_must_fail git fast-export --signed-tags=abort sign-your-name

'

test_expect_success 'signed-tags=verbatim' '

	git fast-export --signed-tags=verbatim sign-your-name > output &&
	grep PGP output

'

test_expect_success 'signed-tags=warn-verbatim' '

	git fast-export --signed-tags=warn-verbatim sign-your-name >output 2>err &&
	grep PGP output &&
	test -s err

'

# 'warn' is a backward-compatibility alias for 'warn-verbatim'; test
# that it keeps working.
test_expect_success 'signed-tags=warn' '

	git fast-export --signed-tags=warn sign-your-name >output 2>err &&
	grep PGP output &&
	test -s err

'

test_expect_success 'signed-tags=strip' '

	git fast-export --signed-tags=strip sign-your-name > output &&
	! grep PGP output

'

test_expect_success 'signed-tags=warn-strip' '
	git fast-export --signed-tags=warn-strip sign-your-name >output 2>err &&
	! grep PGP output &&
	test -s err
'

test_expect_success GPGSM 'setup X.509 signed tag' '
	test_config gpg.format x509 &&
	test_config user.signingkey $GIT_COMMITTER_EMAIL &&

	git tag -s -m "X.509 signed tag" x509-signed $(git rev-parse HEAD) &&
	ANNOTATED_TAG_COUNT=$((ANNOTATED_TAG_COUNT + 1))
'

test_expect_success GPGSM 'signed-tags=verbatim with X.509' '
	git fast-export --signed-tags=verbatim x509-signed > output &&
	test_grep "SIGNED MESSAGE" output
'

test_expect_success GPGSM 'signed-tags=strip with X.509' '
	git fast-export --signed-tags=strip x509-signed > output &&
	test_grep ! "SIGNED MESSAGE" output
'

test_expect_success GPGSSH 'setup SSH signed tag' '
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&

	git tag -s -m "SSH signed tag" ssh-signed $(git rev-parse HEAD) &&
	ANNOTATED_TAG_COUNT=$((ANNOTATED_TAG_COUNT + 1))
'

test_expect_success GPGSSH 'signed-tags=verbatim with SSH' '
	git fast-export --signed-tags=verbatim ssh-signed > output &&
	test_grep "SSH SIGNATURE" output
'

test_expect_success GPGSSH 'signed-tags=strip with SSH' '
	git fast-export --signed-tags=strip ssh-signed > output &&
	test_grep ! "SSH SIGNATURE" output
'

test_expect_success GPG 'set up signed commit' '

	# Generate a commit with both "gpgsig" and "encoding" set, so
	# that we can test that fast-import gets the ordering correct
	# between the two.
	test_config i18n.commitEncoding ISO-8859-1 &&
	git checkout -f -b commit-signing main &&
	echo Sign your name >file-sign &&
	git add file-sign &&
	git commit -S -m "signed commit" &&
	COMMIT_SIGNING=$(git rev-parse --verify commit-signing)

'

test_expect_success GPG 'signed-commits default is same as strip' '
	git fast-export --reencode=no commit-signing >out1 2>err &&
	git fast-export --reencode=no --signed-commits=strip commit-signing >out2 &&
	test_cmp out1 out2
'

test_expect_success GPG 'signed-commits=abort' '

	test_must_fail git fast-export --signed-commits=abort commit-signing

'

test_expect_success GPG 'signed-commits=verbatim' '

	git fast-export --signed-commits=verbatim --reencode=no commit-signing >output &&
	test_grep -E "^gpgsig $GIT_DEFAULT_HASH openpgp" output &&
	grep "encoding ISO-8859-1" output &&
	git -C new fast-import <output &&
	STRIPPED=$(git -C new rev-parse --verify refs/heads/commit-signing) &&
	test $COMMIT_SIGNING = $STRIPPED

'

test_expect_success GPG 'signed-commits=warn-verbatim' '

	git fast-export --signed-commits=warn-verbatim --reencode=no commit-signing >output 2>err &&
	test_grep -E "^gpgsig $GIT_DEFAULT_HASH openpgp" output &&
	grep "encoding ISO-8859-1" output &&
	test -s err &&
	git -C new fast-import <output &&
	STRIPPED=$(git -C new rev-parse --verify refs/heads/commit-signing) &&
	test $COMMIT_SIGNING = $STRIPPED

'

test_expect_success GPG 'signed-commits=strip' '

	git fast-export --signed-commits=strip --reencode=no commit-signing >output &&
	! grep ^gpgsig output &&
	grep "^encoding ISO-8859-1" output &&
	sed "s/commit-signing/commit-strip-signing/" output | git -C new fast-import &&
	STRIPPED=$(git -C new rev-parse --verify refs/heads/commit-strip-signing) &&
	test $COMMIT_SIGNING != $STRIPPED

'

test_expect_success GPG 'signed-commits=warn-strip' '

	git fast-export --signed-commits=warn-strip --reencode=no commit-signing >output 2>err &&
	! grep ^gpgsig output &&
	grep "^encoding ISO-8859-1" output &&
	test -s err &&
	sed "s/commit-signing/commit-strip-signing/" output | git -C new fast-import &&
	STRIPPED=$(git -C new rev-parse --verify refs/heads/commit-strip-signing) &&
	test $COMMIT_SIGNING != $STRIPPED

'

test_expect_success GPGSM 'setup X.509 signed commit' '

	git checkout -b x509-signing main &&
	test_config gpg.format x509 &&
	test_config user.signingkey $GIT_COMMITTER_EMAIL &&
	echo "X.509 content" >file &&
	git add file &&
	git commit -S -m "X.509 signed commit" &&
	X509_COMMIT=$(git rev-parse HEAD) &&
	git checkout main

'

test_expect_success GPGSM 'round-trip X.509 signed commit' '

	git fast-export --signed-commits=verbatim x509-signing >output &&
	test_grep -E "^gpgsig $GIT_DEFAULT_HASH x509" output &&
	git -C new fast-import <output &&
	git -C new cat-file commit refs/heads/x509-signing >actual &&
	grep "^gpgsig" actual &&
	IMPORTED=$(git -C new rev-parse refs/heads/x509-signing) &&
	test $X509_COMMIT = $IMPORTED

'

test_expect_success GPGSSH 'setup SSH signed commit' '

	git checkout -b ssh-signing main &&
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	echo "SSH content" >file &&
	git add file &&
	git commit -S -m "SSH signed commit" &&
	SSH_COMMIT=$(git rev-parse HEAD) &&
	git checkout main

'

test_expect_success GPGSSH 'round-trip SSH signed commit' '

	git fast-export --signed-commits=verbatim ssh-signing >output &&
	test_grep -E "^gpgsig $GIT_DEFAULT_HASH ssh" output &&
	git -C new fast-import <output &&
	git -C new cat-file commit refs/heads/ssh-signing >actual &&
	grep "^gpgsig" actual &&
	IMPORTED=$(git -C new rev-parse refs/heads/ssh-signing) &&
	test $SSH_COMMIT = $IMPORTED

'

test_expect_success 'setup submodule' '

	test_config_global protocol.file.allow always &&
	git checkout -f main &&
	test_might_fail git update-ref -d refs/heads/commit-signing &&
	mkdir sub &&
	(
		cd sub &&
		git init  &&
		echo test file > file &&
		git add file &&
		git commit -m sub_initial
	) &&
	git submodule add "$(pwd)/sub" sub &&
	git commit -m initial &&
	test_tick &&
	(
		cd sub &&
		echo more data >> file &&
		git add file &&
		git commit -m sub_second
	) &&
	git add sub &&
	git commit -m second

'

test_expect_success 'submodule fast-export | fast-import' '

	test_config_global protocol.file.allow always &&
	SUBENT1=$(git ls-tree main^ sub) &&
	SUBENT2=$(git ls-tree main sub) &&
	rm -rf new &&
	mkdir new &&
	git --git-dir=new/.git init &&
	git fast-export --signed-tags=strip --all >actual &&
	git -C new fast-import <actual &&
	test "$SUBENT1" = "$(git -C new ls-tree refs/heads/main^ sub)" &&
	test "$SUBENT2" = "$(git -C new ls-tree refs/heads/main sub)" &&
	git -C new checkout main &&
	git -C new submodule init &&
	git -C new submodule update &&
	cmp new/sub/file sub/file

'

GIT_AUTHOR_NAME='A U Thor'; export GIT_AUTHOR_NAME
GIT_COMMITTER_NAME='C O Mitter'; export GIT_COMMITTER_NAME

test_expect_success 'setup copies' '

	git checkout -b copy rein &&
	git mv file file3 &&
	git commit -m move1 &&
	test_tick &&
	cp file2 file4 &&
	git add file4 &&
	git mv file2 file5 &&
	git commit -m copy1 &&
	test_tick &&
	cp file3 file6 &&
	git add file6 &&
	git commit -m copy2 &&
	test_tick &&
	echo more text >> file6 &&
	echo even more text >> file6 &&
	git add file6 &&
	git commit -m modify &&
	test_tick &&
	cp file6 file7 &&
	echo test >> file7 &&
	git add file7 &&
	git commit -m copy_modify

'

test_expect_success 'fast-export -C -C | fast-import' '

	ENTRY=$(git rev-parse --verify copy) &&
	rm -rf new &&
	mkdir new &&
	git --git-dir=new/.git init &&
	git fast-export -C -C --signed-tags=strip --all > output &&
	grep "^C file2 file4\$" output &&
	git -C new fast-import <output &&
	test $ENTRY = $(git -C new rev-parse --verify refs/heads/copy)

'

test_expect_success 'fast-export | fast-import when main is tagged' '

	git tag -m msg last &&
	ANNOTATED_TAG_COUNT=$((ANNOTATED_TAG_COUNT + 1)) &&
	git fast-export -C -C --signed-tags=strip --all > output &&
	test $(grep -c "^tag " output) = $ANNOTATED_TAG_COUNT

'

cat > tag-content << EOF
object $(git rev-parse HEAD)
type commit
tag rosten
EOF

test_expect_success 'cope with tagger-less tags' '

	TAG=$(git hash-object --literally -t tag -w tag-content) &&
	git update-ref refs/tags/sonnenschein $TAG &&
	ANNOTATED_TAG_COUNT=$((ANNOTATED_TAG_COUNT + 1)) &&
	git fast-export -C -C --signed-tags=strip --all > output &&
	test $(grep -c "^tag " output) = $ANNOTATED_TAG_COUNT &&
	! grep "Unspecified Tagger" output &&
	git fast-export -C -C --signed-tags=strip --all \
		--fake-missing-tagger > output &&
	test $(grep -c "^tag " output) = $ANNOTATED_TAG_COUNT &&
	grep "Unspecified Tagger" output

'

test_expect_success 'setup for limiting exports by PATH' '
	mkdir limit-by-paths &&
	(
		cd limit-by-paths &&
		git init &&
		echo hi > there &&
		git add there &&
		git commit -m "First file" &&
		echo foo > bar &&
		git add bar &&
		git commit -m "Second file" &&
		git tag -a -m msg mytag &&
		echo morefoo >> bar &&
		git add bar &&
		git commit -m "Change to second file"
	)
'

cat > limit-by-paths/expected << EOF
blob
mark :1
data 3
hi

reset refs/tags/mytag
commit refs/tags/mytag
mark :2
author A U Thor <author@example.com> 1112912713 -0700
committer C O Mitter <committer@example.com> 1112912713 -0700
data 11
First file
M 100644 :1 there

EOF

test_expect_success ICONV 'dropping tag of filtered out object' '
(
	cd limit-by-paths &&
	git fast-export --tag-of-filtered-object=drop mytag -- there > output &&
	test_cmp expected output
)
'

cat >> limit-by-paths/expected << EOF
tag mytag
from :2
tagger C O Mitter <committer@example.com> 1112912713 -0700
data 4
msg

EOF

test_expect_success ICONV 'rewriting tag of filtered out object' '
(
	cd limit-by-paths &&
	git fast-export --tag-of-filtered-object=rewrite mytag -- there > output &&
	test_cmp expected output
)
'

test_expect_success 'rewrite tag predating pathspecs to nothing' '
	test_create_repo rewrite_tag_predating_pathspecs &&
	(
		cd rewrite_tag_predating_pathspecs &&

		test_commit initial &&

		git tag -a -m "Some old tag" v0.0.0.0.0.0.1 &&

		test_commit bar &&

		git fast-export --tag-of-filtered-object=rewrite --all -- bar.t >output &&
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
commit refs/heads/main
mark :3
author A U Thor <author@example.com> 1112912713 -0700
committer C O Mitter <committer@example.com> 1112912713 -0700
data 12
Second file
M 100644 :1 bar
M 100644 :2 there

EOF

test_expect_failure 'no exact-ref revisions included' '
	(
		cd limit-by-paths &&
		git fast-export main~2..main~1 > output &&
		test_cmp expected output
	)
'

test_expect_success 'path limiting with import-marks does not lose unmodified files'        '
	git checkout -b simple marks~2 &&
	git fast-export --export-marks=marks simple -- file > /dev/null &&
	echo more content >> file &&
	test_tick &&
	git commit -mnext file &&
	git fast-export --import-marks=marks simple -- file file0 >actual &&
	grep file0 actual
'

test_expect_success 'path limiting works' '
	git fast-export simple -- file >actual &&
	sed -ne "s/^M .* //p" <actual | sort -u >actual.files &&
	echo file >expect &&
	test_cmp expect actual.files
'

test_expect_success 'avoid corrupt stream with non-existent mark' '
	test_create_repo avoid_non_existent_mark &&
	(
		cd avoid_non_existent_mark &&

		test_commit important-path &&

		test_commit ignored &&

		git branch A &&
		git branch B &&

		echo foo >>important-path.t &&
		git add important-path.t &&
		test_commit more changes &&

		git fast-export --all -- important-path.t | git fast-import --force
	)
'

test_expect_success 'full-tree re-shows unmodified files'        '
	git checkout -f simple &&
	git fast-export --full-tree simple >actual &&
	test $(grep -c file0 actual) -eq 3
'

test_expect_success 'set-up a few more tags for tag export tests' '
	git checkout -f main &&
	HEAD_TREE=$(git show -s --pretty=raw HEAD | sed -n "/tree/s/tree //p") &&
	git tag    tree_tag        -m "tagging a tree" $HEAD_TREE &&
	git tag -a tree_tag-obj    -m "tagging a tree" $HEAD_TREE &&
	git tag    tag-obj_tag     -m "tagging a tag" tree_tag-obj &&
	git tag -a tag-obj_tag-obj -m "tagging a tag" tree_tag-obj
'

test_expect_success 'tree_tag'        '
	mkdir result &&
	(cd result && git init) &&
	git fast-export tree_tag > fe-stream &&
	(cd result && git fast-import < ../fe-stream)
'

# NEEDSWORK: not just check return status, but validate the output
# Note that these tests DO NOTHING other than print a warning that
# they are omitting the one tag we asked them to export (because the
# tags resolve to a tree).  They exist just to make sure we do not
# abort but instead just warn.
test_expect_success 'tree_tag-obj'    'git fast-export tree_tag-obj'
test_expect_success 'tag-obj_tag'     'git fast-export tag-obj_tag'
test_expect_success 'tag-obj_tag-obj' 'git fast-export tag-obj_tag-obj'

test_expect_success 'handling tags of blobs' '
	git tag -a -m "Tag of a blob" blobtag $(git rev-parse main:file) &&
	git fast-export blobtag >actual &&
	cat >expect <<-EOF &&
	blob
	mark :1
	data 9
	die Luft

	tag blobtag
	from :1
	tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data 14
	Tag of a blob

	EOF
	test_cmp expect actual
'

test_expect_success 'handling nested tags' '
	git tag -a -m "This is a nested tag" nested muss &&
	git fast-export --mark-tags nested >output &&
	grep "^from $ZERO_OID$" output &&
	grep "^tag nested$" output >tag_lines &&
	test_line_count = 2 tag_lines
'

test_expect_success 'directory becomes symlink'        '
	git init dirtosymlink &&
	git init result &&
	(
		cd dirtosymlink &&
		mkdir foo &&
		mkdir bar &&
		echo hello > foo/world &&
		echo hello > bar/world &&
		git add foo/world bar/world &&
		git commit -q -mone &&
		git rm -r foo &&
		test_ln_s_add bar foo &&
		git commit -q -mtwo
	) &&
	(
		cd dirtosymlink &&
		git fast-export main -- foo |
		(cd ../result && git fast-import --quiet)
	) &&
	(cd result && git show main:foo)
'

test_expect_success PERL_TEST_HELPERS 'fast-export quotes pathnames' '
	git init crazy-paths &&
	test_config -C crazy-paths core.protectNTFS false &&
	(cd crazy-paths &&
	 blob=$(echo foo | git hash-object -w --stdin) &&
	 git -c core.protectNTFS=false update-index --add \
		--cacheinfo 100644 $blob "$(printf "path with\\nnewline")" \
		--cacheinfo 100644 $blob "path with \"quote\"" \
		--cacheinfo 100644 $blob "path with \\backslash" \
		--cacheinfo 100644 $blob "path with space" &&
	 git commit -m addition &&
	 git ls-files -z -s | perl -0pe "s{\\t}{$&subdir/}" >index &&
	 git read-tree --empty &&
	 git update-index -z --index-info <index &&
	 git commit -m rename &&
	 git read-tree --empty &&
	 git commit -m deletion &&
	 git fast-export -M HEAD >export.out &&
	 git rev-list HEAD >expect &&
	 git init result &&
	 cd result &&
	 git -c core.protectNTFS=false fast-import <../export.out &&
	 git rev-list HEAD >actual &&
	 test_cmp ../expect actual
	)
'

test_expect_success 'test bidirectionality' '
	git init marks-test &&
	git fast-export --export-marks=marks-cur --import-marks-if-exists=marks-cur --branches | \
	git --git-dir=marks-test/.git fast-import --export-marks=marks-new --import-marks-if-exists=marks-new &&
	(cd marks-test &&
	git reset --hard &&
	echo Wohlauf > file &&
	git commit -a -m "back in time") &&
	git --git-dir=marks-test/.git fast-export --export-marks=marks-new --import-marks-if-exists=marks-new --branches | \
	git fast-import --export-marks=marks-cur --import-marks-if-exists=marks-cur
'

cat > expected << EOF
blob
mark :13
data 5
bump

commit refs/heads/main
mark :14
author A U Thor <author@example.com> 1112912773 -0700
committer C O Mitter <committer@example.com> 1112912773 -0700
data 5
bump
from :12
M 100644 :13 file

EOF

test_expect_success ICONV 'avoid uninteresting refs' '
	> tmp-marks &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks main > /dev/null &&
	git tag v1.0 &&
	git branch uninteresting &&
	echo bump > file &&
	git commit -a -m bump &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks ^uninteresting ^v1.0 main > actual &&
	test_cmp expected actual
'

cat > expected << EOF
reset refs/heads/main
from :14

EOF

test_expect_success ICONV 'refs are updated even if no commits need to be exported' '
	> tmp-marks &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks main > /dev/null &&
	git fast-export --import-marks=tmp-marks \
		--export-marks=tmp-marks main > actual &&
	test_cmp expected actual
'

test_expect_success 'use refspec' '
	git fast-export --refspec refs/heads/main:refs/heads/foobar main >actual2 &&
	grep "^commit " actual2 | sort | uniq >actual &&
	echo "commit refs/heads/foobar" > expected &&
	test_cmp expected actual
'

test_expect_success 'delete ref because entire history excluded' '
	git branch to-delete &&
	git fast-export to-delete ^to-delete >actual &&
	cat >expected <<-EOF &&
	reset refs/heads/to-delete
	from $ZERO_OID

	EOF
	test_cmp expected actual
'

test_expect_success 'delete refspec' '
	git fast-export --refspec :refs/heads/to-delete >actual &&
	cat >expected <<-EOF &&
	reset refs/heads/to-delete
	from $ZERO_OID

	EOF
	test_cmp expected actual
'

test_expect_success 'when using -C, do not declare copy when source of copy is also modified' '
	test_create_repo src &&
	echo a_line >src/file.txt &&
	git -C src add file.txt &&
	git -C src commit -m 1st_commit &&

	cp src/file.txt src/file2.txt &&
	echo another_line >>src/file.txt &&
	git -C src add file.txt file2.txt &&
	git -C src commit -m 2nd_commit &&

	test_create_repo dst &&
	git -C src fast-export --all -C >actual &&
	git -C dst fast-import <actual &&
	git -C src show >expected &&
	git -C dst show >actual &&
	test_cmp expected actual
'

test_expect_success 'merge commit gets exported with --import-marks' '
	test_create_repo merging &&
	(
		cd merging &&
		test_commit initial &&
		git checkout -b topic &&
		test_commit on-topic &&
		git checkout main &&
		test_commit on-main &&
		test_tick &&
		git merge --no-ff -m Yeah topic &&

		echo ":1 $(git rev-parse HEAD^^)" >marks &&
		git fast-export --import-marks=marks main >out &&
		grep Yeah out
	)
'


test_expect_success 'fast-export --first-parent outputs all revisions output by revision walk' '
	git init first-parent &&
	(
		cd first-parent &&
		test_commit A &&
		git checkout -b topic1 &&
		test_commit B &&
		git checkout main &&
		git merge --no-ff topic1 &&

		git checkout -b topic2 &&
		test_commit C &&
		git checkout main &&
		git merge --no-ff topic2 &&

		test_commit D &&

		git fast-export main -- --first-parent >first-parent-export &&
		git fast-export main -- --first-parent --reverse >first-parent-reverse-export &&
		test_cmp first-parent-export first-parent-reverse-export &&

		git init import &&
		git -C import fast-import <first-parent-export &&

		git log --format="%ad %s" --first-parent main >expected &&
		git -C import log --format="%ad %s" --all >actual &&
		test_cmp expected actual &&
		test_line_count = 4 actual
	)
'

test_expect_success 'fast-export handles --end-of-options' '
	git update-ref refs/heads/nodash HEAD &&
	git update-ref refs/heads/--dashes HEAD &&
	git fast-export --end-of-options nodash >expect &&
	git fast-export --end-of-options --dashes >actual.raw &&
	# fix up lines which mention the ref for comparison
	sed s/--dashes/nodash/ <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success GPG,RUST 'setup a commit with dual signatures on its SHA-1 and SHA-256 formats' '
	# Create a signed SHA-256 commit
	git init --object-format=sha256 explicit-sha256 &&
	git -C explicit-sha256 config extensions.compatObjectFormat sha1 &&
	git -C explicit-sha256 checkout -b dual-signed &&
	test_commit -C explicit-sha256 A &&
	echo B >explicit-sha256/B &&
	git -C explicit-sha256 add B &&
	test_tick &&
	git -C explicit-sha256 commit -S -m "signed" B &&
	SHA256_B=$(git -C explicit-sha256 rev-parse dual-signed) &&

	# Create the corresponding SHA-1 commit
	SHA1_B=$(git -C explicit-sha256 rev-parse --output-object-format=sha1 dual-signed) &&

	# Check that the resulting SHA-1 commit has both signatures
	echo $SHA1_B | git -C explicit-sha256 cat-file --batch >out &&
	test_grep -E "^gpgsig " out &&
	test_grep -E "^gpgsig-sha256 " out
'

test_expect_success GPG,RUST 'export and import of doubly signed commit' '
	git -C explicit-sha256 fast-export --signed-commits=verbatim dual-signed >output &&
	test_grep -E "^gpgsig sha1 openpgp" output &&
	test_grep -E "^gpgsig sha256 openpgp" output &&
	git -C new fast-import <output &&
	git -C new cat-file commit refs/heads/dual-signed >actual &&
	test_grep -E "^gpgsig " actual &&
	test_grep -E "^gpgsig-sha256 " actual &&
	IMPORTED=$(git -C new rev-parse refs/heads/dual-signed) &&
	if test "$GIT_DEFAULT_HASH" = "sha1"
	then
		test $SHA1_B = $IMPORTED
	else
		test $SHA256_B = $IMPORTED
	fi
'

test_done
