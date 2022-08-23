#!/bin/sh

test_description='grep multibyte characters'

. ./test-lib.sh

# Multibyte regex search is only supported with a native regex library
# that supports it.
# (The supplied compatibility library is compiled with NO_MBSUPPORT.)
test -z "$NO_REGEX" &&
  LC_ALL=en_US.UTF-8 test-tool regex '^.$' '¿' &&
  test_set_prereq MB_REGEX

if ! test_have_prereq MB_REGEX
then
  skip_all='multibyte grep tests; Git compiled with NO_REGEX, NO_MBSUPPORT'
  test_done
fi

test_expect_success 'setup' '
	test_write_lines "¿" >file &&
	git add file &&
	LC_ALL="en_US.UTF-8" &&
	export LC_ALL
'
test_expect_success 'grep exactly one char in single-char multibyte file' '
	git grep "^.$"
'

test_expect_success 'grep two chars in single-char multibyte file' '
	test_expect_code 1 git grep ".."
'

test_done
