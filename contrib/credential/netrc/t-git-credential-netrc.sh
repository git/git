#!/bin/sh
(
	cd ../../../t
	test_description='but-credential-netrc'
	. ./test-lib.sh

	if ! test_have_prereq PERL; then
		skip_all='skipping perl interface tests, perl not available'
		test_done
	fi

	perl -MTest::More -e 0 2>/dev/null || {
		skip_all="Perl Test::More unavailable, skipping test"
		test_done
	}

	# set up test repository

	test_expect_success \
		'set up test repository' \
		'but config --add gpg.program test.but-config-gpg'

	# The external test will outputs its own plan
	test_external_has_tap=1

	export PERL5LIB="$BUTPERLLIB"
	test_external \
		'but-credential-netrc' \
		perl "$BUT_BUILD_DIR"/contrib/credential/netrc/test.pl

	test_done
)
