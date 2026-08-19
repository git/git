#!/bin/sh

test_description='git p4 error encoding

This test checks that the import process handles inconsistent text
encoding in p4 error messages without failing'

. ./lib-git-p4.sh

###############################
## SECTION REPEATED IN t9835 ##
###############################

# These tests require Perforce with non-unicode setup.
out=$(2>&1 P4CHARSET=utf8 p4 client -o)
if test $? -eq 0
then
	skip_all="skipping git p4 error encoding tests; Perforce is setup with unicode"
	test_done
fi

# These tests are specific to Python 3. Write a custom script that executes
# git-p4 directly with the Python 3 interpreter to ensure that we use that
# version even if Git was compiled with Python 2.
python_target_binary=$(which python3)
if test -n "$python_target_binary"
then
	mkdir temp_python
	PATH="$(pwd)/temp_python:$PATH"
	export PATH

	write_script temp_python/git-p4-python3 <<-EOF
	exec "$python_target_binary" "$(git --exec-path)/git-p4" "\$@"
	EOF
fi

git p4-python3 >err
if ! grep 'valid commands' err
then
	skip_all="skipping python3 git p4 tests; python3 not available"
	test_done
fi

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'see if Perforce error with characters not convertable to utf-8 will be processed correctly' '
	test_when_finished cleanup_git &&
	$python_target_binary "$TEST_DIRECTORY"/t9837/git-p4-error-python3.py "$TEST_DIRECTORY"
'

test_done
