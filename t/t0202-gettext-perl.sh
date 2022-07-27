#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description='Perl gettext interface (Git::I18N)'

TEST_PASSES_SANITIZE_LEAK=true
. ./lib-gettext.sh
. "$TEST_DIRECTORY"/lib-perl.sh
skip_all_if_no_Test_More

# The external test will outputs its own plan
test_external_has_tap=1

test_external_without_stderr \
    'Perl Git::I18N API' \
    perl "$TEST_DIRECTORY"/t0202/test.pl

test_done
