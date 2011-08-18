#!/bin/sh

test_description='git-p4 tests'

. ./test-lib.sh

( p4 -h && p4d -h ) >/dev/null 2>&1 || {
	skip_all='skipping git-p4 tests; no p4 or p4d'
	test_done
}

GITP4=$GIT_BUILD_DIR/contrib/fast-import/git-p4
P4DPORT=10669

export P4PORT=localhost:$P4DPORT

db="$TRASH_DIRECTORY/db"
cli="$TRASH_DIRECTORY/cli"
git="$TRASH_DIRECTORY/git"

test_debug 'echo p4d -q -d -r "$db" -p $P4DPORT'
test_expect_success setup '
	mkdir -p "$db" &&
	p4d -q -d -r "$db" -p $P4DPORT &&
	mkdir -p "$cli" &&
	mkdir -p "$git" &&
	export P4PORT=localhost:$P4DPORT
'

test_expect_success 'add p4 files' '
	cd "$cli" &&
	p4 client -i <<-EOF &&
	Client: client
	Description: client
	Root: $cli
	View: //depot/... //client/...
	EOF
	export P4CLIENT=client &&
	echo file1 >file1 &&
	p4 add file1 &&
	p4 submit -d "file1" &&
	echo file2 >file2 &&
	p4 add file2 &&
	p4 submit -d "file2" &&
	cd "$TRASH_DIRECTORY"
'

cleanup_git() {
	cd "$TRASH_DIRECTORY" &&
	rm -rf "$git" &&
	mkdir "$git"
}

test_expect_success 'basic git-p4 clone' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	git log --oneline >lines &&
	test_line_count = 1 lines
'

test_expect_success 'git-p4 clone @all' '
	"$GITP4" clone --dest="$git" //depot@all &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	git log --oneline >lines &&
	test_line_count = 2 lines
'

test_expect_success 'git-p4 sync uninitialized repo' '
	test_create_repo "$git" &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	test_must_fail "$GITP4" sync
'

#
# Create a git repo by hand.  Add a commit so that HEAD is valid.
# Test imports a new p4 repository into a new git branch.
#
test_expect_success 'git-p4 sync new branch' '
	test_create_repo "$git" &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	test_commit head &&
	"$GITP4" sync --branch=refs/remotes/p4/depot //depot@all &&
	git log --oneline p4/depot >lines &&
	test_line_count = 2 lines
'

test_expect_success 'exit when p4 fails to produce marshaled output' '
	badp4dir="$TRASH_DIRECTORY/badp4dir" &&
	mkdir -p "$badp4dir" &&
	test_when_finished "rm -rf $badp4dir" &&
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
	cd "$cli" &&
	echo file-wild-hash >file-wild#hash &&
	echo file-wild-star >file-wild\*star &&
	echo file-wild-at >file-wild@at &&
	echo file-wild-percent >file-wild%percent &&
	p4 add -f file-wild* &&
	p4 submit -d "file wildcards"
'

test_expect_success 'wildcard files git-p4 clone' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	test -f file-wild#hash &&
	test -f file-wild\*star &&
	test -f file-wild@at &&
	test -f file-wild%percent
'

test_expect_success 'clone bare' '
	"$GITP4" clone --dest="$git" --bare //depot &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	test ! -d .git &&
	bare=`git config --get core.bare` &&
	test "$bare" = true
'

p4_add_user() {
    name=$1
    fullname=$2
    p4 user -f -i <<EOF &&
User: $name
Email: $name@localhost
FullName: $fullname
EOF
    p4 passwd -P secret $name
}

p4_grant_admin() {
    name=$1
    p4 protect -o |\
	awk "{print}END{print \"    admin user $name * //depot/...\"}" |\
	p4 protect -i
}

p4_check_commit_author() {
    file=$1
    user=$2
    if p4 changes -m 1 //depot/$file | grep $user > /dev/null ; then
	return 0
    else
	echo "file $file not modified by user $user" 1>&2
	return 1
    fi
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
	cd "$git" &&
	echo "username: a change by alice" >> file1 &&
	echo "username: a change by bob" >> file2 &&
	git commit --author "Alice <alice@localhost>" -m "a change by alice" file1 &&
	git commit --author "Bob <bob@localhost>" -m "a change by bob" file2 &&
	git config git-p4.skipSubmitEditCheck true &&
	P4EDITOR=touch P4USER=alice P4PASSWD=secret "$GITP4" commit --preserve-user &&
	p4_check_commit_author file1 alice &&
	p4_check_commit_author file2 bob
'

# Test username support, submitting as bob, who lacks admin rights. Should
# not submit change to p4 (git diff should show deltas).
test_expect_success 'refuse to preserve users without perms' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	git config git-p4.skipSubmitEditCheck true &&
	echo "username-noperms: a change by alice" >> file1 &&
	git commit --author "Alice <alice@localhost>" -m "perms: a change by alice" file1 &&
	! P4EDITOR=touch P4USER=bob P4PASSWD=secret "$GITP4" commit --preserve-user &&
	! git diff --exit-code HEAD..p4/master > /dev/null
'

# What happens with unknown author? Without allowMissingP4Users it should fail.
test_expect_success 'preserve user where author is unknown to p4' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	git config git-p4.skipSubmitEditCheck true &&
	echo "username-bob: a change by bob" >> file1 &&
	git commit --author "Bob <bob@localhost>" -m "preserve: a change by bob" file1 &&
	echo "username-unknown: a change by charlie" >> file1 &&
	git commit --author "Charlie <charlie@localhost>" -m "preserve: a change by charlie" file1 &&
	! P4EDITOR=touch P4USER=alice P4PASSWD=secret "$GITP4" commit --preserve-user &&
	! git diff --exit-code HEAD..p4/master > /dev/null &&
	echo "$0: repeat with allowMissingP4Users enabled" &&
	git config git-p4.allowMissingP4Users true &&
	git config git-p4.preserveUser true &&
	P4EDITOR=touch P4USER=alice P4PASSWD=secret "$GITP4" commit &&
	git diff --exit-code HEAD..p4/master > /dev/null &&
	p4_check_commit_author file1 alice
'

# If we're *not* using --preserve-user, git-p4 should warn if we're submitting
# changes that are not all ours.
# Test: user in p4 and user unknown to p4.
# Test: warning disabled and user is the same.
test_expect_success 'not preserving user with mixed authorship' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	cd "$git" &&
	git config git-p4.skipSubmitEditCheck true &&
	p4_add_user derek Derek &&

	make_change_by_user usernamefile3 Derek derek@localhost &&
	P4EDITOR=cat P4USER=alice P4PASSWD=secret "$GITP4" commit >actual &&
	grep "git author derek@localhost does not match" actual &&

	make_change_by_user usernamefile3 Charlie charlie@localhost &&
	P4EDITOR=cat P4USER=alice P4PASSWD=secret "$GITP4" commit >actual &&
	grep "git author charlie@localhost does not match" actual &&

	make_change_by_user usernamefile3 alice alice@localhost &&
	P4EDITOR=cat P4USER=alice P4PASSWD=secret "$GITP4" commit >actual &&
	! grep "git author.*does not match" actual &&

	git config git-p4.skipUserNameCheck true &&
	make_change_by_user usernamefile3 Charlie charlie@localhost &&
	P4EDITOR=cat P4USER=alice P4PASSWD=secret "$GITP4" commit >actual &&
	! grep "git author.*does not match" actual &&

	p4_check_commit_author usernamefile3 alice
'

marshal_dump() {
	what=$1
	python -c 'import marshal, sys; d = marshal.load(sys.stdin); print d["'$what'"]'
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
	cd "$git" &&
	gittime=$(git show -s --raw --pretty=format:%at HEAD) &&
	echo $p4time $gittime &&
	test $p4time = $gittime
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
	cd "$git" &&
	git config git-p4.skipSubmitEditCheck true &&

	git mv file1 file4 &&
	git commit -a -m "Rename file1 to file4" &&
	git diff-tree -r -M HEAD &&
	"$GITP4" submit &&
	p4 filelog //depot/file4 &&
	! p4 filelog //depot/file4 | grep -q "branch from" &&

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
	git config git-p4.detectRenames $((level + 2)) &&
	"$GITP4" submit &&
	p4 filelog //depot/file6 &&
	! p4 filelog //depot/file6 | grep -q "branch from" &&

	git mv file6 file7 &&
	echo update >>file7 &&
	git add file7 &&
	git commit -a -m "Rename file6 to file7 with changes" &&
	git diff-tree -r -M HEAD &&
	level=$(git diff-tree -r -M HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/R0*//") &&
	test -n "$level" && test "$level" -gt 2 && test "$level" -lt 100 &&
	git config git-p4.detectRenames $((level - 2)) &&
	"$GITP4" submit &&
	p4 filelog //depot/file7 &&
	p4 filelog //depot/file7 | grep -q "branch from //depot/file6"
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
	cd "$git" &&
	git config git-p4.skipSubmitEditCheck true &&

	cp file2 file8 &&
	git add file8 &&
	git commit -a -m "Copy file2 to file8" &&
	git diff-tree -r -C HEAD &&
	"$GITP4" submit &&
	p4 filelog //depot/file8 &&
	! p4 filelog //depot/file8 | grep -q "branch from" &&

	cp file2 file9 &&
	git add file9 &&
	git commit -a -m "Copy file2 to file9" &&
	git diff-tree -r -C HEAD &&
	git config git-p4.detectCopies true &&
	"$GITP4" submit &&
	p4 filelog //depot/file9 &&
	! p4 filelog //depot/file9 | grep -q "branch from" &&

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
	git config git-p4.detectCopies $((level + 2)) &&
	"$GITP4" submit &&
	p4 filelog //depot/file12 &&
	! p4 filelog //depot/file12 | grep -q "branch from" &&

	cp file2 file13 &&
	echo "different text" >>file13 &&
	git add file13 &&
	git commit -a -m "Copy file2 to file13 with changes" &&
	git diff-tree -r -C --find-copies-harder HEAD &&
	level=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f1 | cut -d" " -f5 | sed "s/C0*//") &&
	test -n "$level" && test "$level" -gt 2 && test "$level" -lt 100 &&
	src=$(git diff-tree -r -C --find-copies-harder HEAD | sed 1d | cut -f2) &&
	test "$src" = file10 &&
	git config git-p4.detectCopies $((level - 2)) &&
	"$GITP4" submit &&
	p4 filelog //depot/file13 &&
	p4 filelog //depot/file13 | grep -q "branch from //depot/file"
'

# Create a simple branch structure in P4 depot to check if it is correctly
# cloned.
test_expect_success 'add simple p4 branches' '
	cd "$cli" &&
	mkdir branch1 &&
	cd branch1 &&
	echo file1 >file1 &&
	echo file2 >file2 &&
	p4 add file* &&
	p4 submit -d "branch1" &&
	p4 integrate //depot/branch1/... //depot/branch2/... &&
	p4 submit -d "branch2" &&
	echo file3 >file3 &&
	p4 add file3 &&
	p4 submit -d "add file3 in branch1" &&
	p4 open file2 &&
	echo update >>file2 &&
	p4 submit -d "update file2 in branch1" &&
	p4 integrate //depot/branch1/... //depot/branch3/... &&
	p4 submit -d "branch3" &&
	cd "$TRASH_DIRECTORY"
'

# Configure branches through git-config and clone them.
# All files are tested to make sure branches were cloned correctly.
# Finally, make an update to branch1 on P4 side to check if it is imported
# correctly by git-p4.
test_expect_success 'git-p4 clone simple branches' '
	git init "$git" &&
	cd "$git" &&
	git config git-p4.branchList branch1:branch2 &&
	git config --add git-p4.branchList branch1:branch3 &&
	cd "$TRASH_DIRECTORY" &&
	"$GITP4" clone --dest="$git" --detect-branches //depot@all &&
	cd "$git" &&
	git log --all --graph --decorate --stat &&
	git reset --hard p4/depot/branch1 &&
	test -f file1 &&
	test -f file2 &&
	test -f file3 &&
	grep -q update file2 &&
	git reset --hard p4/depot/branch2 &&
	test -f file1 &&
	test -f file2 &&
	test \! -z file3 &&
	! grep -q update file2 &&
	git reset --hard p4/depot/branch3 &&
	test -f file1 &&
	test -f file2 &&
	test -f file3 &&
	grep -q update file2 &&
	cd "$cli" &&
	cd branch1 &&
	p4 edit file2 &&
	echo file2_ >> file2 &&
	p4 submit -d "update file2 in branch3" &&
	cd "$git" &&
	git reset --hard p4/depot/branch1 &&
	"$GITP4" rebase &&
	grep -q file2_ file2 &&
	cd "$TRASH_DIRECTORY" &&
	rm -rf "$git" && mkdir "$git"
'

test_expect_success 'shutdown' '
	pid=`pgrep -f p4d` &&
	test -n "$pid" &&
	test_debug "ps wl `echo $pid`" &&
	kill $pid
'

test_done
