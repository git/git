#!/bin/sh

test_description='git-p4 tests'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'add p4 files' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "file1" &&
		echo file2 >file2 &&
		p4 add file2 &&
		p4 submit -d "file2"
	)
'

test_expect_success 'basic git-p4 clone' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git log --oneline >lines &&
		test_line_count = 1 lines
	)
'

test_expect_success 'git-p4 clone @all' '
	"$GITP4" clone --dest="$git" //depot@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git log --oneline >lines &&
		test_line_count = 2 lines
	)
'

test_expect_success 'git-p4 sync uninitialized repo' '
	test_create_repo "$git" &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		test_must_fail "$GITP4" sync
	)
'

#
# Create a git repo by hand.  Add a commit so that HEAD is valid.
# Test imports a new p4 repository into a new git branch.
#
test_expect_success 'git-p4 sync new branch' '
	test_create_repo "$git" &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		test_commit head &&
		"$GITP4" sync --branch=refs/remotes/p4/depot //depot@all &&
		git log --oneline p4/depot >lines &&
		test_line_count = 2 lines
	)
'

test_expect_success 'clone two dirs' '
	(
		cd "$cli" &&
		mkdir sub1 sub2 &&
		echo sub1/f1 >sub1/f1 &&
		echo sub2/f2 >sub2/f2 &&
		p4 add sub1/f1 &&
		p4 submit -d "sub1/f1" &&
		p4 add sub2/f2 &&
		p4 submit -d "sub2/f2"
	) &&
	"$GITP4" clone --dest="$git" //depot/sub1 //depot/sub2 &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git ls-files >lines &&
		test_line_count = 2 lines &&
		git log --oneline p4/master >lines &&
		test_line_count = 1 lines
	)
'

test_expect_success 'clone two dirs, @all' '
	(
		cd "$cli" &&
		echo sub1/f3 >sub1/f3 &&
		p4 add sub1/f3 &&
		p4 submit -d "sub1/f3"
	) &&
	"$GITP4" clone --dest="$git" //depot/sub1@all //depot/sub2@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git ls-files >lines &&
		test_line_count = 3 lines &&
		git log --oneline p4/master >lines &&
		test_line_count = 3 lines
	)
'

test_expect_success 'clone two dirs, @all, conflicting files' '
	(
		cd "$cli" &&
		echo sub2/f3 >sub2/f3 &&
		p4 add sub2/f3 &&
		p4 submit -d "sub2/f3"
	) &&
	"$GITP4" clone --dest="$git" //depot/sub1@all //depot/sub2@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git ls-files >lines &&
		test_line_count = 3 lines &&
		git log --oneline p4/master >lines &&
		test_line_count = 4 lines &&
		echo sub2/f3 >expected &&
		test_cmp expected f3
	)
'

test_expect_success 'exit when p4 fails to produce marshaled output' '
	badp4dir="$TRASH_DIRECTORY/badp4dir" &&
	mkdir "$badp4dir" &&
	test_when_finished "rm \"$badp4dir/p4\" && rmdir \"$badp4dir\"" &&
	cat >"$badp4dir"/p4 <<-EOF &&
	#!$SHELL_PATH
	exit 1
	EOF
	chmod 755 "$badp4dir"/p4 &&
	PATH="$badp4dir:$PATH" "$GITP4" clone --dest="$git" //depot >errs 2>&1 ; retval=$? &&
	test $retval -eq 1 &&
	test_must_fail grep -q Traceback errs
'

test_expect_success 'add p4 files with wildcards in the names' '
	(
		cd "$cli" &&
		echo file-wild-hash >file-wild#hash &&
		echo file-wild-star >file-wild\*star &&
		echo file-wild-at >file-wild@at &&
		echo file-wild-percent >file-wild%percent &&
		p4 add -f file-wild* &&
		p4 submit -d "file wildcards"
	)
'

test_expect_success 'wildcard files git-p4 clone' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		test -f file-wild#hash &&
		test -f file-wild\*star &&
		test -f file-wild@at &&
		test -f file-wild%percent
	)
'

test_expect_success 'clone bare' '
	"$GITP4" clone --dest="$git" --bare //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		test ! -d .git &&
		bare=`git config --get core.bare` &&
		test "$bare" = true
	)
'

p4_add_user() {
	name=$1 fullname=$2 &&
	p4 user -f -i <<-EOF &&
	User: $name
	Email: $name@localhost
	FullName: $fullname
	EOF
	p4 passwd -P secret $name
}

p4_grant_admin() {
	name=$1 &&
	{
		p4 protect -o &&
		echo "    admin user $name * //depot/..."
	} | p4 protect -i
}

p4_check_commit_author() {
	file=$1 user=$2 &&
	p4 changes -m 1 //depot/$file | grep -q $user
}

make_change_by_user() {
	file=$1 name=$2 email=$3 &&
	echo "username: a change by $name" >>"$file" &&
	git add "$file" &&
	git commit --author "$name <$email>" -m "a change by $name"
}

# Test username support, submitting as user 'alice'
test_expect_success 'preserve users' '
	p4_add_user alice Alice &&
	p4_add_user bob Bob &&
	p4_grant_admin alice &&
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		echo "username: a change by alice" >>file1 &&
		echo "username: a change by bob" >>file2 &&
		git commit --author "Alice <alice@localhost>" -m "a change by alice" file1 &&
		git commit --author "Bob <bob@localhost>" -m "a change by bob" file2 &&
		git config git-p4.skipSubmitEditCheck true &&
		P4EDITOR=touch P4USER=alice P4PASSWD=secret "$GITP4" commit --preserve-user &&
		p4_check_commit_author file1 alice &&
		p4_check_commit_author file2 bob
	)
'

# Test username support, submitting as bob, who lacks admin rights. Should
# not submit change to p4 (git diff should show deltas).
test_expect_success 'refuse to preserve users without perms' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		echo "username-noperms: a change by alice" >>file1 &&
		git commit --author "Alice <alice@localhost>" -m "perms: a change by alice" file1 &&
		P4EDITOR=touch P4USER=bob P4PASSWD=secret &&
		export P4EDITOR P4USER P4PASSWD &&
		test_must_fail "$GITP4" commit --preserve-user &&
		! git diff --exit-code HEAD..p4/master
	)
'

# What happens with unknown author? Without allowMissingP4Users it should fail.
test_expect_success 'preserve user where author is unknown to p4' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		echo "username-bob: a change by bob" >>file1 &&
		git commit --author "Bob <bob@localhost>" -m "preserve: a change by bob" file1 &&
		echo "username-unknown: a change by charlie" >>file1 &&
		git commit --author "Charlie <charlie@localhost>" -m "preserve: a change by charlie" file1 &&
		P4EDITOR=touch P4USER=alice P4PASSWD=secret &&
		export P4EDITOR P4USER P4PASSWD &&
		test_must_fail "$GITP4" commit --preserve-user &&
		! git diff --exit-code HEAD..p4/master &&

		echo "$0: repeat with allowMissingP4Users enabled" &&
		git config git-p4.allowMissingP4Users true &&
		git config git-p4.preserveUser true &&
		"$GITP4" commit &&
		git diff --exit-code HEAD..p4/master &&
		p4_check_commit_author file1 alice
	)
'

# If we're *not* using --preserve-user, git-p4 should warn if we're submitting
# changes that are not all ours.
# Test: user in p4 and user unknown to p4.
# Test: warning disabled and user is the same.
test_expect_success 'not preserving user with mixed authorship' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		p4_add_user derek Derek &&

		make_change_by_user usernamefile3 Derek derek@localhost &&
		P4EDITOR=cat P4USER=alice P4PASSWD=secret &&
		export P4EDITOR P4USER P4PASSWD &&
		"$GITP4" commit |\
		grep "git author derek@localhost does not match" &&

		make_change_by_user usernamefile3 Charlie charlie@localhost &&
		"$GITP4" commit |\
		grep "git author charlie@localhost does not match" &&

		make_change_by_user usernamefile3 alice alice@localhost &&
		"$GITP4" commit |\
		test_must_fail grep "git author.*does not match" &&

		git config git-p4.skipUserNameCheck true &&
		make_change_by_user usernamefile3 Charlie charlie@localhost &&
		"$GITP4" commit |\
		test_must_fail grep "git author.*does not match" &&

		p4_check_commit_author usernamefile3 alice
	)
'

marshal_dump() {
	what=$1
	"$PYTHON_PATH" -c 'import marshal, sys; d = marshal.load(sys.stdin); print d["'$what'"]'
}

# Sleep a bit so that the top-most p4 change did not happen "now".  Then
# import the repo and make sure that the initial import has the same time
# as the top-most change.
test_expect_success 'initial import time from top change time' '
	p4change=$(p4 -G changes -m 1 //depot/... | marshal_dump change) &&
	p4time=$(p4 -G changes -m 1 //depot/... | marshal_dump time) &&
	sleep 3 &&
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		gittime=$(git show -s --raw --pretty=format:%at HEAD) &&
		echo $p4time $gittime &&
		test $p4time = $gittime
	)
'

# Rename a file and confirm that rename is not detected in P4.
# Rename the new file again with detectRenames option enabled and confirm that
# this is detected in P4.
# Rename the new file again adding an extra line, configure a big threshold in
# detectRenames and confirm that rename is not detected in P4.
# Repeat, this time with a smaller threshold and confirm that the rename is
# detected in P4.
test_expect_success 'detect renames' '
	"$GITP4" clone --dest="$git" //depot@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&

		git mv file1 file4 &&
		git commit -a -m "Rename file1 to file4" &&
		git diff-tree -r -M HEAD &&
		"$GITP4" submit &&
		p4 filelog //depot/file4 &&
		p4 filelog //depot/file4 | test_must_fail grep -q "branch from" &&

		git mv file4 file5 &&
		git commit -a -m "Rename file4 to file5" &&
		git diff-tree -r -M HEAD &&
		git config git-p4.detectRenames true &&
		"$GITP4" submit &&
		p4 filelog //depot/file5 &&
		p4 filelog //depot/file5 | grep -q "branch from //depot/file4" &&

		git mv file5 file6 &&
		echo update >>file6 &&
		git add file6 &&
		git commit -a -m "Rename file5 to file6 with changes" &&
		git diff-tree -r -M HEAD &&
		level=$(git diff-tree -r -M HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/R0*//") &&
		test -n "$level" && test "$level" -gt 0 && test "$level" -lt 98 &&
		git config git-p4.detectRenames $(($level + 2)) &&
		"$GITP4" submit &&
		p4 filelog //depot/file6 &&
		p4 filelog //depot/file6 | test_must_fail grep -q "branch from" &&

		git mv file6 file7 &&
		echo update >>file7 &&
		git add file7 &&
		git commit -a -m "Rename file6 to file7 with changes" &&
		git diff-tree -r -M HEAD &&
		level=$(git diff-tree -r -M HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/R0*//") &&
		test -n "$level" && test "$level" -gt 2 && test "$level" -lt 100 &&
		git config git-p4.detectRenames $(($level - 2)) &&
		"$GITP4" submit &&
		p4 filelog //depot/file7 &&
		p4 filelog //depot/file7 | grep -q "branch from //depot/file6"
	)
'

# Copy a file and confirm that copy is not detected in P4.
# Copy a file with detectCopies option enabled and confirm that copy is not
# detected in P4.
# Modify and copy a file with detectCopies option enabled and confirm that copy
# is detected in P4.
# Copy a file with detectCopies and detectCopiesHarder options enabled and
# confirm that copy is detected in P4.
# Modify and copy a file, configure a bigger threshold in detectCopies and
# confirm that copy is not detected in P4.
# Modify and copy a file, configure a smaller threshold in detectCopies and
# confirm that copy is detected in P4.
test_expect_success 'detect copies' '
	"$GITP4" clone --dest="$git" //depot@all &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&

		cp file2 file8 &&
		git add file8 &&
		git commit -a -m "Copy file2 to file8" &&
		git diff-tree -r -C HEAD &&
		"$GITP4" submit &&
		p4 filelog //depot/file8 &&
		p4 filelog //depot/file8 | test_must_fail grep -q "branch from" &&

		cp file2 file9 &&
		git add file9 &&
		git commit -a -m "Copy file2 to file9" &&
		git diff-tree -r -C HEAD &&
		git config git-p4.detectCopies true &&
		"$GITP4" submit &&
		p4 filelog //depot/file9 &&
		p4 filelog //depot/file9 | test_must_fail grep -q "branch from" &&

		echo "file2" >>file2 &&
		cp file2 file10 &&
		git add file2 file10 &&
		git commit -a -m "Modify and copy file2 to file10" &&
		git diff-tree -r -C HEAD &&
		"$GITP4" submit &&
		p4 filelog //depot/file10 &&
		p4 filelog //depot/file10 | grep -q "branch from //depot/file" &&

		cp file2 file11 &&
		git add file11 &&
		git commit -a -m "Copy file2 to file11" &&
		git diff-tree -r -C --find-copies-harder HEAD &&
		src=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
		test "$src" = file10 &&
		git config git-p4.detectCopiesHarder true &&
		"$GITP4" submit &&
		p4 filelog //depot/file11 &&
		p4 filelog //depot/file11 | grep -q "branch from //depot/file" &&

		cp file2 file12 &&
		echo "some text" >>file12 &&
		git add file12 &&
		git commit -a -m "Copy file2 to file12 with changes" &&
		git diff-tree -r -C --find-copies-harder HEAD &&
		level=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/C0*//") &&
		test -n "$level" && test "$level" -gt 0 && test "$level" -lt 98 &&
		src=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
		test "$src" = file10 &&
		git config git-p4.detectCopies $(($level + 2)) &&
		"$GITP4" submit &&
		p4 filelog //depot/file12 &&
		p4 filelog //depot/file12 | test_must_fail grep -q "branch from" &&

		cp file2 file13 &&
		echo "different text" >>file13 &&
		git add file13 &&
		git commit -a -m "Copy file2 to file13 with changes" &&
		git diff-tree -r -C --find-copies-harder HEAD &&
		level=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/C0*//") &&
		test -n "$level" && test "$level" -gt 2 && test "$level" -lt 100 &&
		src=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
		test "$src" = file10 &&
		git config git-p4.detectCopies $(($level - 2)) &&
		"$GITP4" submit &&
		p4 filelog //depot/file13 &&
		p4 filelog //depot/file13 | grep -q "branch from //depot/file"
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
