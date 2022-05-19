#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description="The Git C functions aren't broken by setlocale(3)"

. ./lib-gettext.sh

test_expect_success 'but show a ISO-8859-1 cummit under C locale' '
	. "$TEST_DIRECTORY"/t3901/8859-1.txt &&
	test_cummit "iso-c-cummit" iso-under-c &&
	but show >out 2>err &&
	test_must_be_empty err &&
	grep -q "iso-c-cummit" out
'

test_expect_success GETTEXT_LOCALE 'but show a ISO-8859-1 cummit under a UTF-8 locale' '
	. "$TEST_DIRECTORY"/t3901/8859-1.txt &&
	test_cummit "iso-utf8-cummit" iso-under-utf8 &&
	LANGUAGE=is LC_ALL="$is_IS_locale" but show >out 2>err &&
	test_must_be_empty err &&
	grep -q "iso-utf8-cummit" out
'

test_done
