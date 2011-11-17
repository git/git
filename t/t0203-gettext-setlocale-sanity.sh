#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description="The Git C functions aren't broken by setlocale(3)"

. ./lib-gettext.sh

test_expect_success 'git show a ISO-8859-1 commit under C locale' '
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&
	test_commit "iso-c-commit" iso-under-c &&
	git show >out 2>err &&
	! test -s err &&
	grep -q "iso-c-commit" out
'

test_expect_success GETTEXT_LOCALE 'git show a ISO-8859-1 commit under a UTF-8 locale' '
	. "$TEST_DIRECTORY"/t3901-8859-1.txt &&
	test_commit "iso-utf8-commit" iso-under-utf8 &&
	LANGUAGE=is LC_ALL="$is_IS_locale" git show >out 2>err &&
	! test -s err &&
	grep -q "iso-utf8-commit" out
'

test_done
