#!/bin/sh
(
	cd ../../../t
	test_description='git-credential-netrc'
	. ./test-lib.sh
	. "$TEST_DIRECTORY"/lib-perl.sh

	skip_all_if_no_Test_More

	# set up test repository

	test_expect_success \
		'set up test repository' \
		'git config --add gpg.program test.git-config-gpg'

	export PERL5LIB="$GITPERLLIB"
	test_expect_success 'git-credential-netrc' '
		perl "$GIT_BUILD_DIR"/contrib/credential/netrc/test.pl
	'

	test_done
)
