#!/bin/sh

test_description='compare address parsing with and without Mail::Address'
. ./test-lib.sh

if ! test_have_prereq PERL; then
	skip_all='skipping perl interface tests, perl not available'
	test_done
fi

perl -MTest::More -e 0 2>/dev/null || {
	skip_all="Perl Test::More unavailable, skipping test"
	test_done
}

perl -MMail::Address -e 0 2>/dev/null || {
	skip_all="Perl Mail::Address unavailable, skipping test"
	test_done
}

test_external_has_tap=1

test_external_without_stderr \
	'Perl address parsing function' \
	perl "$TEST_DIRECTORY"/t9000/test.pl

test_done
