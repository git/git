#!/bin/sh

test_description='cd_to_toplevel'

. ./test-lib.sh

test_cd_to_toplevel () {
	test_expect_success "$2" '
		(
			cd '"'$1'"' &&
			. git-sh-setup &&
			cd_to_toplevel &&
			[ "$(/bin/pwd)" = "$TOPLEVEL" ]
		)
	'
}

TOPLEVEL="$(/bin/pwd)/repo"
mkdir -p repo/sub/dir
mv .git repo/
SUBDIRECTORY_OK=1

test_cd_to_toplevel repo 'at physical root'

test_cd_to_toplevel repo/sub/dir 'at physical subdir'

if test "$no_symlinks"; then
	say "symbolic links not supported - skipping tests"
else

ln -s repo symrepo
test_cd_to_toplevel symrepo 'at symbolic root'

ln -s repo/sub/dir subdir-link
test_cd_to_toplevel subdir-link 'at symbolic subdir'

cd repo
ln -s sub/dir internal-link
test_cd_to_toplevel internal-link 'at internal symbolic subdir'

fi	# $no_symlinks

test_done
