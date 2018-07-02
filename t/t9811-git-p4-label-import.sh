#!/bin/sh

test_description='git p4 label tests'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

# Basic p4 label import tests.
#
test_expect_success 'basic p4 labels' '
	test_when_finished cleanup_git &&
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

		git p4 clone --dest="$git" //depot@all &&
		cd "$git" &&
		git config git-p4.labelImportRegexp ".*TAG.*" &&
		git p4 sync --import-labels --verbose &&

		git tag &&
		git tag >taglist &&
		test_line_count = 3 taglist &&

		cd main &&
		git checkout TAG_F1_ONLY &&
		! test -f f2 &&
		git checkout TAG_WITH\$_SHELL_CHAR &&
		test -f f1 && test -f f2 && test -f file_with_\$metachar &&

		git show TAG_LONG_LABEL | grep -q "A Label second line"
	)
'
# Test some label corner cases:
#
# - two tags on the same file; both should be available
# - a tag that is only on one file; this kind of tag
#   cannot be imported (at least not easily).

test_expect_success 'two labels on the same changelist' '
	test_when_finished cleanup_git &&
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

		git p4 clone --dest="$git" //depot@all &&
		cd "$git" &&
		git p4 sync --import-labels &&

		git tag | grep TAG_F1 &&
		git tag | grep -q TAG_F1_1 &&
		git tag | grep -q TAG_F1_2 &&

		cd main &&

		git checkout TAG_F1_1 &&
		ls &&
		test -f f1 &&

		git checkout TAG_F1_2 &&
		ls &&
		test -f f1
	)
'

# Export some git tags to p4
test_expect_success 'export git tags to p4' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git tag -m "A tag created in git:xyzzy" GIT_TAG_1 &&
		echo "hello world" >main/f10 &&
		git add main/f10 &&
		git commit -m "Adding file for export test" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit &&
		git tag -m "Another git tag" GIT_TAG_2 &&
		git tag LIGHTWEIGHT_TAG &&
		git p4 rebase --import-labels --verbose &&
		git p4 submit --export-labels --verbose
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		p4 labels ... | grep GIT_TAG_1 &&
		p4 labels ... | grep GIT_TAG_2 &&
		p4 labels ... | grep LIGHTWEIGHT_TAG &&
		p4 label -o GIT_TAG_1 | grep "tag created in git:xyzzy" &&
		p4 sync ...@GIT_TAG_1 &&
		! test -f main/f10 &&
		p4 sync ...@GIT_TAG_2 &&
		test -f main/f10
	)
'

# Export a tag from git where an affected file is deleted later on
# Need to create git tags after rebase, since only then can the
# git commits be mapped to p4 changelists.
test_expect_success 'export git tags to p4 with deletion' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git p4 sync --import-labels &&
		echo "deleted file" >main/deleted_file &&
		git add main/deleted_file &&
		git commit -m "create deleted file" &&
		git rm main/deleted_file &&
		echo "new file" >main/f11 &&
		git add main/f11 &&
		git commit -m "delete the deleted file" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit &&
		git p4 rebase --import-labels --verbose &&
		git tag -m "tag on deleted file" GIT_TAG_ON_DELETED HEAD~1 &&
		git tag -m "tag after deletion" GIT_TAG_AFTER_DELETION HEAD &&
		git p4 submit --export-labels --verbose
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

# Create a tag in git that cannot be exported to p4
test_expect_success 'tag that cannot be exported' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git checkout -b a_branch &&
		echo "hello" >main/f12 &&
		git add main/f12 &&
		git commit -m "adding f12" &&
		git tag -m "tag on a_branch" GIT_TAG_ON_A_BRANCH &&
		git checkout master &&
		git p4 submit --export-labels
	) &&
	(
		cd "$cli" &&
		p4 sync ... &&
		!(p4 labels | grep GIT_TAG_ON_A_BRANCH)
	)
'

test_expect_success 'use git config to enable import/export of tags' '
	git p4 clone --verbose --dest="$git" //depot@all &&
	(
		cd "$git" &&
		git config git-p4.exportLabels true &&
		git config git-p4.importLabels true &&
		git tag CFG_A_GIT_TAG &&
		git p4 rebase --verbose &&
		git p4 submit --verbose &&
		git tag &&
		git tag | grep TAG_F1_1
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

# Importing a label that references a P4 commit that
# has not been seen. The presence of a label on a commit
# we haven't seen should not cause git-p4 to fail. It should
# merely skip that label, and still import other labels.
test_expect_success 'importing labels with missing revisions' '
	test_when_finished cleanup_git &&
	(
		rm -fr "$cli" "$git" &&
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

		git p4 clone --dest="$git" --import-labels -v \
			//depot/missing-revision/...@$startrev,$endrev &&
		(
			cd "$git" &&
			git rev-parse TAG_S1 &&
			! git rev-parse TAG_S0
		)
	)
'


test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
