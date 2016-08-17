#!/bin/sh

test_description='help'

. ./test-lib.sh

test_expect_success "pass --help to unknown command" "
	cat <<-EOF >expected &&
		git: '123' is not a git command. See 'git --help'.
	EOF
	(git 123 --help 2>actual || true) &&
	test_i18ncmp expected actual
"

test_done
