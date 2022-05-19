#!/bin/sh

test_description='but p4 label tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

# Basic p4 label import tests.
#
test_expect_success 'basic p4 labels' '
	test_when_finished cleanup_but &&
	(
		cd "$cli" &&
		mkdir -p main &&

		echo f1 >main/f1 &&
		p4 add main/f1 &&
		p4 submit -d "main/f1" &&

		echo f2 >main/f2 &&
		p4 add main/f2 &&
		p4 submit -d "main/f2" &&

		echo f3 >main/file_with_\$metachar &&
		p4 add main/file_with_\$metachar &&
		p4 submit -d "file with metachar" &&

		p4 tag -l TAG_F1_ONLY main/f1 &&
		p4 tag -l TAG_WITH\$_SHELL_CHAR main/... &&
		p4 tag -l this_tag_will_be\ skipped main/... &&

		echo f4 >main/f4 &&
		p4 add main/f4 &&
		p4 submit -d "main/f4" &&

		p4 label -i <<-EOF &&
		Label: TAG_LONG_LABEL
		Description:
		   A Label first line
		   A Label second line
		View:	//depot/...
		EOF

		p4 tag -l TAG_LONG_LABEL ... &&

		p4 labels ... &&

		but p4 clone --dest="$but" //depot@all &&
		cd "$but" &&
		but config but-p4.labelImportRegexp ".*TAG.*" &&
		but p4 sync --import-labels --verbose &&

		but tag &&
		but tag >taglist &&
		test_line_count = 3 taglist &&

		cd main &&
		but checkout TAG_F1_ONLY &&
		! test -f f2 &&
		but checkout TAG_WITH\$_SHELL_CHAR &&
		test -f f1 && test -f f2 && test -f file_with_\$metachar &&

		but show TAG_LONG_LABEL | grep -q "A Label second line"
	)
'
# Test some label corner cases:
#
# - two tags on the same file; both should be available
# - a tag that is only on one file; this kind of tag
#   cannot be imported (at least not easily).

test_expect_success 'two labels on the same changelist' '
	test_when_finished cleanup_but &&
	(
		cd "$cli" &&
		mkdir -p main &&

		p4 edit main/f1 main/f2 &&
		echo "hello world" >main/f1 &&
		echo "not in the tag" >main/f2 &&
		p4 submit -d "main/f[12]: testing two labels" &&

		p4 tag -l TAG_F1_1 main/... &&
		p4 tag -l TAG_F1_2 main/... &&

		p4 labels ... &&

		but p4 clone --dest="$but" //depot@all &&
		cd "$but" &&
		but p4 sync --import-labels &&

		but tag | grep TAG_F1 &&
		but tag | grep -q TAG_F1_1 &&
		but tag | grep -q TAG_F1_2 &&

		cd main &&

		but checkout TAG_F1_1 &&
		ls &&
		test -f f1 &&

		but checkout TAG_F1_2 &&
		ls &&
		test -f f1
	)
'

# Export some but tags to p4
test_expect_success 'export but tags to p4' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot@all &&
	(
		cd "$but" &&
		but tag -m "A tag created in but:xyzzy" GIT_TAG_1 &&
		echo "hello world" >main/f10 &&
		but add main/f10 &&
		but cummit -m "Adding file for export test" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit &&
		but tag -m "Another but tag" GIT_TAG_2 &&
		but tag LIGHTWEIGHT_TAG &&
		but p4 rebase --import-labels --verbose &&
		but p4 submit --export-labels --verbose
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		p4 labels ... | grep GIT_TAG_1 &&
		p4 labels ... | grep GIT_TAG_2 &&
		p4 labels ... | grep LIGHTWEIGHT_TAG &&
		p4 label -o GIT_TAG_1 | grep "tag created in but:xyzzy" &&
		p4 sync ...@GIT_TAG_1 &&
		! test -f main/f10 &&
		p4 sync ...@GIT_TAG_2 &&
		test -f main/f10
	)
'

# Export a tag from but where an affected file is deleted later on
# Need to create but tags after rebase, since only then can the
# but cummits be mapped to p4 changelists.
test_expect_success 'export but tags to p4 with deletion' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot@all &&
	(
		cd "$but" &&
		but p4 sync --import-labels &&
		echo "deleted file" >main/deleted_file &&
		but add main/deleted_file &&
		but cummit -m "create deleted file" &&
		but rm main/deleted_file &&
		echo "new file" >main/f11 &&
		but add main/f11 &&
		but cummit -m "delete the deleted file" &&
		but config but-p4.skipSubmitEdit true &&
		but p4 submit &&
		but p4 rebase --import-labels --verbose &&
		but tag -m "tag on deleted file" GIT_TAG_ON_DELETED HEAD~1 &&
		but tag -m "tag after deletion" GIT_TAG_AFTER_DELETION HEAD &&
		but p4 submit --export-labels --verbose
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		p4 sync ...@GIT_TAG_ON_DELETED &&
		test -f main/deleted_file &&
		p4 sync ...@GIT_TAG_AFTER_DELETION &&
		! test -f main/deleted_file &&
		echo "checking label contents" &&
		p4 label -o GIT_TAG_ON_DELETED | grep "tag on deleted file"
	)
'

# Create a tag in but that cannot be exported to p4
test_expect_success 'tag that cannot be exported' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot@all &&
	(
		cd "$but" &&
		but checkout -b a_branch &&
		echo "hello" >main/f12 &&
		but add main/f12 &&
		but cummit -m "adding f12" &&
		but tag -m "tag on a_branch" GIT_TAG_ON_A_BRANCH &&
		but checkout main &&
		but p4 submit --export-labels
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		! p4 labels | grep GIT_TAG_ON_A_BRANCH
	)
'

test_expect_success 'use but config to enable import/export of tags' '
	but p4 clone --verbose --dest="$but" //depot@all &&
	(
		cd "$but" &&
		but config but-p4.exportLabels true &&
		but config but-p4.importLabels true &&
		but tag CFG_A_GIT_TAG &&
		but p4 rebase --verbose &&
		but p4 submit --verbose &&
		but tag &&
		but tag | grep TAG_F1_1
	) &&
	(
		cd "$cli" &&
		p4 labels &&
		p4 labels | grep CFG_A_GIT_TAG
	)
'

p4_head_revision() {
	p4 changes -m 1 "$@" | awk '{print $2}'
}

# Importing a label that references a P4 cummit that
# has not been seen. The presence of a label on a cummit
# we haven't seen should not cause but-p4 to fail. It should
# merely skip that label, and still import other labels.
test_expect_success 'importing labels with missing revisions' '
	test_when_finished cleanup_but &&
	(
		rm -fr "$cli" "$but" &&
		mkdir "$cli" &&
		P4CLIENT=missing-revision &&
		client_view "//depot/missing-revision/... //missing-revision/..." &&
		cd "$cli" &&
		>f1 && p4 add f1 && p4 submit -d "start" &&

		p4 tag -l TAG_S0 ... &&

		>f2 && p4 add f2 && p4 submit -d "second" &&

		startrev=$(p4_head_revision //depot/missing-revision/...) &&

		>f3 && p4 add f3 && p4 submit -d "third" &&

		p4 edit f2 && date >f2 && p4 submit -d "change" f2 &&

		endrev=$(p4_head_revision //depot/missing-revision/...) &&

		p4 tag -l TAG_S1 ... &&

		# we should skip TAG_S0 since it is before our startpoint,
		# but pick up TAG_S1.

		but p4 clone --dest="$but" --import-labels -v \
			//depot/missing-revision/...@$startrev,$endrev &&
		(
			cd "$but" &&
			but rev-parse TAG_S1 &&
			! but rev-parse TAG_S0
		)
	)
'

test_done
