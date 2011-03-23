#!/bin/sh

test_description='git-p4 tests'

. ./test-lib.sh

( p4 -h && p4d -h ) >/dev/null 2>&1 || {
	skip_all='skipping git-p4 tests; no p4 or p4d'
	test_done
}

GITP4=$GIT_BUILD_DIR/contrib/fast-import/git-p4
P4DPORT=10669

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

test_expect_success 'basic git-p4 clone' '
	"$GITP4" clone --dest="$git" //depot &&
	cd "$git" &&
	git log --oneline >lines &&
	test_line_count = 1 lines &&
	cd .. &&
	rm -rf "$git" && mkdir "$git"
'

test_expect_success 'git-p4 clone @all' '
	"$GITP4" clone --dest="$git" //depot@all &&
	cd "$git" &&
	git log --oneline >lines &&
	test_line_count = 2 lines &&
	cd .. &&
	rm -rf "$git" && mkdir "$git"
'

test_expect_success 'git-p4 sync uninitialized repo' '
	test_create_repo "$git" &&
	cd "$git" &&
	test_must_fail "$GITP4" sync &&
	rm -rf "$git" && mkdir "$git"
'

#
# Create a git repo by hand.  Add a commit so that HEAD is valid.
# Test imports a new p4 repository into a new git branch.
#
test_expect_success 'git-p4 sync new branch' '
	test_create_repo "$git" &&
	cd "$git" &&
	test_commit head &&
	"$GITP4" sync --branch=refs/remotes/p4/depot //depot@all &&
	git log --oneline p4/depot >lines &&
	cat lines &&
	test_line_count = 2 lines &&
	cd .. &&
	rm -rf "$git" && mkdir "$git"
'

test_expect_success 'exit when p4 fails to produce marshaled output' '
	badp4dir="$TRASH_DIRECTORY/badp4dir" &&
	mkdir -p "$badp4dir" &&
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
	p4 submit -d "file wildcards" &&
	cd "$TRASH_DIRECTORY"
'

test_expect_success 'wildcard files git-p4 clone' '
	"$GITP4" clone --dest="$git" //depot &&
	cd "$git" &&
	test -f file-wild#hash &&
	test -f file-wild\*star &&
	test -f file-wild@at &&
	test -f file-wild%percent &&
	cd "$TRASH_DIRECTORY" &&
	rm -rf "$git" && mkdir "$git"
'

test_expect_success 'clone bare' '
	"$GITP4" clone --dest="$git" --bare //depot &&
	cd "$git" &&
	test ! -d .git &&
	bare=`git config --get core.bare` &&
	test "$bare" = true &&
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
