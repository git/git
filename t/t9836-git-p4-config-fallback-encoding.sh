#!/bin/sh

test_description='test git-p4.fallbackEncoding config'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'add Unicode description' '
	cd "$cli" &&
	echo file1 >file1 &&
	p4 add file1 &&
	p4 submit -d documentación
'

# Unicode descriptions cause "git p4 clone" to crash with a UnicodeDecodeError in some
# environments. This test determines if that is the case in our environment. If so,
# we create a file called "clone_fails". In subsequent tests, we check whether that
# file exists to determine what behavior to expect.

clone_fails="$TRASH_DIRECTORY/clone_fails"

# If clone fails with git-p4.fallbackEncoding set to "none", create the "clone_fails" file,
# and make sure the error message is correct

test_expect_success 'clone with git-p4.fallbackEncoding set to "none"' '
	git config --global git-p4.fallbackEncoding none &&
	test_when_finished cleanup_git && {
		git p4 clone --dest="$git" //depot@all 2>error || (
			>"$clone_fails" &&
			grep "UTF-8 decoding failed. Consider using git config git-p4.fallbackEncoding" error
		)
	}
'

# If clone fails with git-p4.fallbackEncoding set to "none", it should also fail when it's unset,
# also with the correct error message.  Otherwise the clone should succeed.

test_expect_success 'clone with git-p4.fallbackEncoding unset' '
	git config --global --unset git-p4.fallbackEncoding &&
	test_when_finished cleanup_git && {
		(
			test -f "$clone_fails" &&
			test_must_fail git p4 clone --dest="$git" //depot@all 2>error &&
			grep "UTF-8 decoding failed. Consider using git config git-p4.fallbackEncoding" error
		) ||
		(
			! test -f "$clone_fails" &&
			git p4 clone --dest="$git" //depot@all 2>error
		)
	}
'

# Whether or not "clone_fails" exists, setting git-p4.fallbackEncoding
# to "cp1252" should cause clone to succeed and get the right description

test_expect_success 'clone with git-p4.fallbackEncoding set to "cp1252"' '
	git config --global git-p4.fallbackEncoding cp1252 &&
	test_when_finished cleanup_git &&
	(
		git p4 clone --dest="$git" //depot@all &&
		cd "$git" &&
		git log --oneline >log &&
		desc=$(head -1 log | cut -d" " -f2) &&
		test "$desc" = "documentación"
	)
'

# Setting git-p4.fallbackEncoding to "replace" should always cause clone to succeed.
# If "clone_fails" exists, the description should contain the Unicode replacement
# character, otherwise the description should be correct (since we're on a system that
# doesn't have the Unicode issue)

test_expect_success 'clone with git-p4.fallbackEncoding set to "replace"' '
	git config --global git-p4.fallbackEncoding replace &&
	test_when_finished cleanup_git &&
	(
		git p4 clone --dest="$git" //depot@all &&
		cd "$git" &&
		git log --oneline >log &&
		desc=$(head -1 log | cut -d" " -f2) &&
		{
			(test -f "$clone_fails" &&
				test "$desc" = "documentaci�n"
			) ||
			(! test -f "$clone_fails" &&
				test "$desc" = "documentación"
			)
		}
	)
'

test_done
