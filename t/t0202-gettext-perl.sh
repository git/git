#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description='Perl gettext interface (Git::I18N)'

TEST_PASSES_SANITIZE_LEAK=true
. ./lib-gettext.sh
. "$TEST_DIRECTORY"/lib-perl.sh
skip_all_if_no_Test_More

test_expect_success 'run t0202/test.pl to test Git::I18N.pm' '
	"$PERL_PATH" "$TEST_DIRECTORY"/t0202/test.pl 2>stderr &&
	test_must_be_empty stderr
'

test_done
