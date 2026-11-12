#!/bin/sh

test_description='git fast-import validation'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'fail on invalid blob sha1' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit base &&

		test_tick &&
		cat >input <<-INPUT_END &&
		commit refs/heads/branch
		mark :1
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		corrupt
		COMMIT

		from refs/heads/main
		M 755 $(echo $ZERO_OID | sed -e "s/0$/1/") zero1

		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'accept branch name "TEMP_TAG"' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit new &&

		cat >input <<-INPUT_END &&
		commit TEMP_TAG
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		tag base
		COMMIT

		from refs/heads/main

		INPUT_END

		git fast-import <input &&
		test $(test-tool ref-store main resolve-ref TEMP_TAG 0 | cut -f1 -d " " ) != "$ZERO_OID" &&
		test $(git rev-parse main) = $(git rev-parse TEMP_TAG^)
	)
'

test_expect_success 'accept empty committer' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >input <<-INPUT_END &&
		commit refs/heads/empty-committer-1
		committer  <> $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		git fast-import <input &&
		out=$(git fsck) &&
		echo "$out" &&
		test -z "$out"
	)
'

test_expect_success 'reject invalid timezone' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-timezone
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1234567890 +051800
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'accept invalid timezone with raw-permissive' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-timezone
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1234567890 +051800
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		git fast-import --date-format=raw-permissive <input &&
		git cat-file -p invalid-timezone >out &&
		test_grep "1234567890 [+]051800" out
	)
'

test_expect_success 'accept and fixup committer with no name' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >input <<-INPUT_END &&
		commit refs/heads/empty-committer-2
		committer <a@b.com> $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		git fast-import <input &&
		out=$(git fsck) &&
		echo "$out" &&
		test -z "$out"
	)
'

test_expect_success 'fail on invalid committer (1)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-committer
		committer Name email> $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid committer (2)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-committer
		committer Name <e<mail> $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid committer (3)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-committer
		committer Name <email>> $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid committer (4)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-committer
		committer Name <email $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid committer (5)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		commit refs/heads/invalid-committer
		committer Name<email> $GIT_COMMITTER_DATE
		data <<COMMIT
		empty commit
		COMMIT
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid file path of ..' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<EOF
		File contents
		EOF

		commit refs/heads/badpath
		committer Name <email> $GIT_COMMITTER_DATE
		data <<COMMIT
		Commit Message
		COMMIT
		M 100644 :1 ../invalid-path
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid file path of .' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<EOF
		File contents
		EOF

		commit refs/heads/badpath
		committer Name <email> $GIT_COMMITTER_DATE
		data <<COMMIT
		Good path
		COMMIT
		M 100644 :1 ok-path

		commit refs/heads/badpath
		committer Name <email> $GIT_COMMITTER_DATE
		data <<COMMIT
		Bad path
		COMMIT
		R ok-path ./invalid-path
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success WINDOWS 'fail on invalid file path of C:' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<EOF
		File contents
		EOF

		commit refs/heads/badpath
		committer Name <email> $GIT_COMMITTER_DATE
		data <<COMMIT
		Commit Message
		COMMIT
		M 100644 :1 C:/invalid-path
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid file path of .git' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<EOF
		File contents
		EOF

		commit refs/heads/badpath
		committer Name <email> $GIT_COMMITTER_DATE
		data <<COMMIT
		Commit Message
		COMMIT
		M 100644 :1 .git/invalid-path
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'fail on invalid file path of .gitmodules' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-INPUT_END &&
		blob
		mark :1
		data <<EOF
		File contents
		EOF

		commit refs/heads/badpath
		committer Name <email> $GIT_COMMITTER_DATE
		data <<COMMIT
		Commit Message
		COMMIT
		M 120000 :1 .gitmodules
		INPUT_END

		test_must_fail git fast-import <input
	)
'

test_expect_success 'abort on unsupported feature' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		feature no-such-feature-exists
		EOF

		test_must_fail git fast-import <input
	)
'

test_expect_success 'supported feature is accepted' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		feature date-format=now
		EOF

		git fast-import <input
	)
'

test_expect_success 'abort on receiving feature after data command' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		blob
		data 3
		hi
		feature date-format=now
		EOF

		test_must_fail git fast-import <input
	)
'

test_expect_success 'import-marks features forbidden by default' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		>git.marks &&
		echo "feature import-marks=git.marks" >input &&
		test_must_fail git fast-import <input &&
		echo "feature import-marks-if-exists=git.marks" >input &&
		test_must_fail git fast-import <input
	)
'

test_expect_success 'only one import-marks feature allowed per stream' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		>git.marks &&
		>git2.marks &&
		cat >input <<-EOF &&
		feature import-marks=git.marks
		feature import-marks=git2.marks
		EOF

		test_must_fail git fast-import --allow-unsafe-features <input
	)
'

test_expect_success 'export-marks feature forbidden by default' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature export-marks=git.marks" >input &&
		test_must_fail git fast-import <input
	)
'

test_expect_success 'feature ls supported' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature ls" |
		git fast-import
	)
'

test_expect_success 'feature cat-blob supported' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature cat-blob" |
		git fast-import
	)
'

test_expect_success 'cat-blob-fd must be a nonnegative integer' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_must_fail git fast-import --cat-blob-fd=-1 </dev/null
	)
'

test_expect_success 'die on unknown option' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		option git non-existing-option
		EOF

		test_must_fail git fast-import <input
	)
'

test_expect_success 'unknown commandline options are rejected' '\
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_must_fail git fast-import --non-existing-option < /dev/null
	)
'

test_expect_success 'feature-only names are rejected on the command line' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		for opt in --alias --get-mark --ls --notes
		do
			test_must_fail git fast-import "$opt" </dev/null || return 1
		done
	)
'

test_expect_success 'die on invalid option argument' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "option git active-branches=-5" |
		test_must_fail git fast-import &&
		echo "option git depth=" |
		test_must_fail git fast-import &&
		test_must_fail git fast-import --depth="5 elephants" </dev/null
	)
'

test_expect_success 'ignore non-git options' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		cat >input <<-EOF &&
		option non-existing-vcs non-existing-option
		EOF

		git fast-import <input
	)
'

###
### series S (mark and path parsing)
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
#   missing space after ..
#
# or when extra characters come after the mark at the end
# of the line:
#
#   garbage after ..
#
# or when the dataref is neither "inline " or a known SHA1,
#
#   invalid dataref ..
#
setup_marks_repo () {
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_tick &&

	cat >repo/input <<-INPUT_END &&
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

	git -C repo fast-import --export-marks=marks <repo/input
}

#
# filemodify, three datarefs
#
test_expect_success 'filemodify with garbage after mark must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		commit refs/heads/S
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit N
		COMMIT
		M 100644 :403x hello.c
		EOF
		test_grep "space after mark" err
	)
'

# inline is misspelled; fast-import thinks it is some unknown dataref
test_expect_success 'filemodify with garbage after inline must fail' '
	setup_marks_repo &&
	(
		cd repo &&

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
		test_grep "nvalid dataref" err
	)
'

test_expect_success 'filemodify with garbage after sha1 must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		sha1=$(grep :403 marks | cut -d\  -f2) &&
		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		commit refs/heads/S
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit N
		COMMIT
		M 100644 ${sha1}x hello.c
		EOF
		test_grep "space after SHA1" err
	)
'

#
# notemodify, three ways to say dataref
#
test_expect_success 'notemodify with garbage after mark dataref must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		commit refs/heads/S
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit S note dataref markref
		COMMIT
		N :202x :302
		EOF
		test_grep "space after mark" err
	)
'

test_expect_success 'notemodify with garbage after inline dataref must fail' '
	setup_marks_repo &&
	(
		cd repo &&

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
		test_grep "nvalid dataref" err
	)
'

test_expect_success 'notemodify with garbage after sha1 dataref must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		sha1=$(grep :202 marks | cut -d\  -f2) &&
		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		commit refs/heads/S
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit S note dataref sha1
		COMMIT
		N ${sha1}x :302
		EOF
		test_grep "space after SHA1" err
	)
'

#
# notemodify, mark in commit-ish
#
test_expect_success 'notemodify with garbage after mark commit-ish must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		commit refs/heads/Snotes
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit S note commit-ish
		COMMIT
		N :202 :302x
		EOF
		test_grep "after mark" err
	)
'

#
# from
#
test_expect_success 'from with garbage after mark must fail' '
	setup_marks_repo &&
	(
		cd repo &&

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

		test_grep "after mark" err
	)
'


#
# merge
#
test_expect_success 'merge with garbage after mark must fail' '
	setup_marks_repo &&
	(
		cd repo &&

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
		test_grep "after mark" err
	)
'

#
# tag, from markref
#
test_expect_success 'tag with garbage after mark must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		tag refs/tags/Stag
		from :302x
		tagger $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<TAG
		tag S
		TAG
		EOF
		test_grep "after mark" err
	)
'

#
# cat-blob markref
#
test_expect_success 'cat-blob with garbage after mark must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		cat-blob :403x
		EOF
		test_grep "after mark" err
	)
'

#
# ls markref
#
test_expect_success 'ls with garbage after mark must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		ls :302x hello.c
		EOF
		test_grep "space after mark" err
	)
'

test_expect_success 'ls with garbage after sha1 must fail' '
	setup_marks_repo &&
	(
		cd repo &&

		sha1=$(grep :302 marks | cut -d\  -f2) &&
		test_must_fail git fast-import --import-marks=marks <<-EOF 2>err &&
		ls ${sha1}x hello.c
		EOF
		test_grep "space after tree-ish" err
	)
'

#
# Path parsing
#
# There are two sorts of ways a path can be parsed, depending on whether it is
# the last field on the line. Additionally, ls without a <dataref> has a special
# case. Test every occurrence of <path> in the grammar against every error case.
# Paths for the root (empty strings) are tested elsewhere.
#

#
# Valid paths at the end of a line: filemodify, filedelete, filecopy (dest),
# filerename (dest), and ls.
#
# commit :301 from root -- modify hello.c (for setup)
# commit :302 from :301 -- modify $path
# commit :303 from :302 -- delete $path
# commit :304 from :301 -- copy hello.c $path
# commit :305 from :301 -- rename hello.c $path
# ls :305 $path
#
test_path_eol_success () {
	local test="$1" path="$2" unquoted_path="$3"
	test_expect_success "paths at EOL with $test must work" '
		test_when_finished "git branch -D S-path-eol" &&

		git -c core.protectNTFS=false fast-import --export-marks=marks.out <<-EOF >out 2>err &&
		blob
		mark :401
		data <<BLOB
		hello world
		BLOB

		blob
		mark :402
		data <<BLOB
		hallo welt
		BLOB

		commit refs/heads/S-path-eol
		mark :301
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		initial commit
		COMMIT
		M 100644 :401 hello.c

		commit refs/heads/S-path-eol
		mark :302
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit filemodify
		COMMIT
		from :301
		M 100644 :402 $path

		commit refs/heads/S-path-eol
		mark :303
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit filedelete
		COMMIT
		from :302
		D $path

		commit refs/heads/S-path-eol
		mark :304
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit filecopy dest
		COMMIT
		from :301
		C hello.c $path

		commit refs/heads/S-path-eol
		mark :305
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit filerename dest
		COMMIT
		from :301
		R hello.c $path

		ls :305 $path
		EOF

		commit_m=$(grep :302 marks.out | cut -d\  -f2) &&
		commit_d=$(grep :303 marks.out | cut -d\  -f2) &&
		commit_c=$(grep :304 marks.out | cut -d\  -f2) &&
		commit_r=$(grep :305 marks.out | cut -d\  -f2) &&
		blob1=$(grep :401 marks.out | cut -d\  -f2) &&
		blob2=$(grep :402 marks.out | cut -d\  -f2) &&

		(
			printf "100644 blob $blob2\t$unquoted_path\n" &&
			printf "100644 blob $blob1\thello.c\n"
		) | sort >tree_m.exp &&
		git ls-tree $commit_m | sort >tree_m.out &&
		test_cmp tree_m.exp tree_m.out &&

		printf "100644 blob $blob1\thello.c\n" >tree_d.exp &&
		git ls-tree $commit_d >tree_d.out &&
		test_cmp tree_d.exp tree_d.out &&

		(
			printf "100644 blob $blob1\t$unquoted_path\n" &&
			printf "100644 blob $blob1\thello.c\n"
		) | sort >tree_c.exp &&
		git ls-tree $commit_c | sort >tree_c.out &&
		test_cmp tree_c.exp tree_c.out &&

		printf "100644 blob $blob1\t$unquoted_path\n" >tree_r.exp &&
		git ls-tree $commit_r >tree_r.out &&
		test_cmp tree_r.exp tree_r.out &&

		test_cmp out tree_r.exp
	'
}

test_path_eol_success 'quoted spaces'   '" hello world.c "'  ' hello world.c '
test_path_eol_success 'unquoted spaces' ' hello world.c '    ' hello world.c '
test_path_eol_success 'octal escapes'   '"\150\151\056\143"' 'hi.c'

#
# Valid paths before a space: filecopy (source) and filerename (source).
#
# commit :301 from root -- modify $path (for setup)
# commit :302 from :301 -- copy $path hello2.c
# commit :303 from :301 -- rename $path hello2.c
#
test_path_space_success () {
	local test="$1" path="$2" unquoted_path="$3"
	test_expect_success "paths before space with $test must work" '
		test_when_finished "git branch -D S-path-space" &&

		git -c core.protectNTFS=false fast-import --export-marks=marks.out <<-EOF 2>err &&
		blob
		mark :401
		data <<BLOB
		hello world
		BLOB

		commit refs/heads/S-path-space
		mark :301
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		initial commit
		COMMIT
		M 100644 :401 $path

		commit refs/heads/S-path-space
		mark :302
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit filecopy source
		COMMIT
		from :301
		C $path hello2.c

		commit refs/heads/S-path-space
		mark :303
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit filerename source
		COMMIT
		from :301
		R $path hello2.c

		EOF

		commit_c=$(grep :302 marks.out | cut -d\  -f2) &&
		commit_r=$(grep :303 marks.out | cut -d\  -f2) &&
		blob=$(grep :401 marks.out | cut -d\  -f2) &&

		(
			printf "100644 blob $blob\t$unquoted_path\n" &&
			printf "100644 blob $blob\thello2.c\n"
		) | sort >tree_c.exp &&
		git ls-tree $commit_c | sort >tree_c.out &&
		test_cmp tree_c.exp tree_c.out &&

		printf "100644 blob $blob\thello2.c\n" >tree_r.exp &&
		git ls-tree $commit_r >tree_r.out &&
		test_cmp tree_r.exp tree_r.out
	'
}

test_path_space_success 'quoted spaces'      '" hello world.c "'  ' hello world.c '
test_path_space_success 'no unquoted spaces' 'hello_world.c'      'hello_world.c'
test_path_space_success 'octal escapes'      '"\150\151\056\143"' 'hi.c'

#
# Test a single commit change with an invalid path. Run it with all occurrences
# of <path> in the grammar against all error kinds.
#
test_path_fail () {
	local change="$1" what="$2" prefix="$3" path="$4" suffix="$5" err_grep="$6"
	test_expect_success "$change with $what must fail" '
		test_must_fail git fast-import <<-EOF 2>err &&
		blob
		mark :1
		data <<BLOB
		hello world
		BLOB

		commit refs/heads/S-path-fail
		mark :2
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit setup
		COMMIT
		M 100644 :1 hello.c

		commit refs/heads/S-path-fail
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
		data <<COMMIT
		commit with bad path
		COMMIT
		from :2
		$prefix$path$suffix
		EOF

		test_grep "$err_grep" err
	'
}

test_path_base_fail () {
	local change="$1" prefix="$2" field="$3" suffix="$4"
	test_path_fail "$change" 'unclosed " in '"$field"          "$prefix" '"hello.c'    "$suffix" "invalid $field"
	test_path_fail "$change" "invalid escape in quoted $field" "$prefix" '"hello\xff"' "$suffix" "invalid $field"
	test_path_fail "$change" "escaped NUL in quoted $field"    "$prefix" '"hello\000"' "$suffix" "NUL in $field"
}
test_path_eol_quoted_fail () {
	local change="$1" prefix="$2" field="$3"
	test_path_base_fail "$change" "$prefix" "$field" ''
	test_path_fail "$change" "garbage after quoted $field" "$prefix" '"hello.c"' 'x' "garbage after $field"
	test_path_fail "$change" "space after quoted $field"   "$prefix" '"hello.c"' ' ' "garbage after $field"
}
test_path_eol_fail () {
	local change="$1" prefix="$2" field="$3"
	test_path_eol_quoted_fail "$change" "$prefix" "$field"
}
test_path_space_fail () {
	local change="$1" prefix="$2" field="$3"
	test_path_base_fail "$change" "$prefix" "$field" ' world.c'
	test_path_fail "$change" "missing space after quoted $field"   "$prefix" '"hello.c"' 'x world.c' "missing space after $field"
	test_path_fail "$change" "missing space after unquoted $field" "$prefix" 'hello.c'   ''          "missing space after $field"
}

test_path_eol_fail   filemodify       'M 100644 :1 ' path
test_path_eol_fail   filedelete       'D '           path
test_path_space_fail filecopy         'C '           source
test_path_eol_fail   filecopy         'C hello.c '   dest
test_path_space_fail filerename       'R '           source
test_path_eol_fail   filerename       'R hello.c '   dest
test_path_eol_fail   'ls (in commit)' 'ls :2 '       path

# When 'ls' has no <dataref>, the <path> must be quoted.
test_path_eol_quoted_fail 'ls (without dataref in commit)' 'ls ' path

test_expect_success 'rfc2822 date, --date-format=raw' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit one &&

		cat >input <<-INPUT_END &&
		commit refs/heads/main
		author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> Tue Feb 6 11:22:18 2007 -0500
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> Tue Feb 6 12:35:02 2007 -0500
		data <<COMMIT
		RFC 2822 type date
		COMMIT

		from refs/heads/main^0

		INPUT_END

		test_must_fail git fast-import --date-format=raw <input
	)
'

test_expect_success 'rfc2822 date, --date-format=rfc2822' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit one &&

		cat >input <<-INPUT_END &&
		commit refs/heads/main
		author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> Tue Feb 6 11:22:18 2007 -0500
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> Tue Feb 6 12:35:02 2007 -0500
		data <<COMMIT
		RFC 2822 type date
		COMMIT

		from refs/heads/main^0

		INPUT_END

		git -c fastimport.unpackLimit=0 fast-import --date-format=rfc2822 <input &&

		cat >expect <<-EOF &&
		author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> 1170778938 -0500
		committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1170783302 -0500

		RFC 2822 type date
		EOF
		git cat-file commit main | sed 1,2d >actual &&
		test_cmp expect actual &&

		for p in .git/objects/pack/*.pack
		do
			git verify-pack "$p" || return 1
		done
	)
'

test_expect_success '--allow-unsafe-features found after a value' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature import-marks-if-exists=nonexistent.marks" >input &&
		git fast-import --allow-unsafe-features <input &&
		git fast-import --depth=5 --allow-unsafe-features <input &&
		git fast-import --depth 5 --allow-unsafe-features <input &&
		git fast-import --date-format raw --allow-unsafe-features <input
	)
'

test_expect_success '--allow-unsafe-features can be abbreviated' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature import-marks-if-exists=nonexistent.marks" >input &&
		GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
		git fast-import --allow-unsafe <input
	)
'

test_expect_success 'disallowed abbreviation of --allow-unsafe-features' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature import-marks-if-exists=nonexistent.marks" >input &&
		test_must_fail env GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=true \
			git fast-import --allow-unsafe <input 2>err &&
		test_grep "forbidden in input without --allow-unsafe-features" err
	)
'

test_expect_success '--allow-unsafe-features as the value of an abbreviation' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature import-marks-if-exists=nonexistent.marks" >input &&
		test_must_fail env GIT_TEST_DISALLOW_ABBREVIATED_OPTIONS=false \
			git fast-import --quiet --export-pack \
			--allow-unsafe-features <input 2>err &&
		test_grep "forbidden in input without --allow-unsafe-features" err
	)
'

test_expect_success '--allow-unsafe-features after a non-option' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		echo "feature import-marks-if-exists=nonexistent.marks" >input &&
		test_must_fail git fast-import --depth 5 extra \
			--allow-unsafe-features <input 2>err &&
		test_grep "forbidden in input without --allow-unsafe-features" err
	)
'

test_done
