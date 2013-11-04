#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description='Perl gettext interface (Git::I18N)'

. ./lib-gettext.sh

if ! test_have_prereq PERL; then
	skip_all='skipping perl interface tests, perl not available'
	test_done
fi

perl -MTest::More -e 0 2>/dev/null || {
	skip_all="Perl Test::More unavailable, skipping test"
	test_done
}

# The external test will outputs its own plan
test_external_has_tap=1

test_external_without_stderr \
    'Perl Git::I18N API' \
    perl "$TEST_DIRECTORY"/t0202/test.pl

test_done
