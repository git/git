#!/bin/sh

test_description='cd_to_toplevel'

. ./test-lib.sh

EXEC_PATH="$(git --exec-path)"
test_have_prereq !MINGW ||
case "$EXEC_PATH" in
[A-Za-z]:/*)
	EXEC_PATH="/${EXEC_PATH%%:*}${EXEC_PATH#?:}"
	;;
esac

test_cd_to_toplevel () {
	test_expect_success $3 "$2" '
		(
			cd '"'$1'"' &&
			PATH="$EXEC_PATH:$PATH" &&
			. git-sh-setup &&
			cd_to_toplevel &&
			[ "$(pwd -P)" = "$TOPLEVEL" ]
		)
	'
}

TOPLEVEL="$(pwd -P)/repo"
mkdir -p repo/sub/dir
mv .git repo/
SUBDIRECTORY_OK=1

test_cd_to_toplevel repo 'at physical root'

test_cd_to_toplevel repo/sub/dir 'at physical subdir'

ln -s repo symrepo 2>/dev/null
test_cd_to_toplevel symrepo 'at symbolic root' SYMLINKS

ln -s repo/sub/dir subdir-link 2>/dev/null
test_cd_to_toplevel subdir-link 'at symbolic subdir' SYMLINKS

cd repo
ln -s sub/dir internal-link 2>/dev/null
test_cd_to_toplevel internal-link 'at internal symbolic subdir' SYMLINKS

test_done
