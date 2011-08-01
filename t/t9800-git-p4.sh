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

test_expect_success 'shutdown' '
	pid=`pgrep -f p4d` &&
	test -n "$pid" &&
	test_debug "ps wl `echo $pid`" &&
	kill $pid
'

test_done
