#!/bin/sh

test_description='test the Windows-only core.unsetenvvars setting'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

if ! test_have_prereq MINGW
then
	skip_all='skipping Windows-specific tests'
	test_done
fi

test_expect_success 'setup' '
	test_hook --setup pre-commit <<-\EOF
	echo $HOBBES >&2
	EOF
'

test_expect_success 'core.unsetenvvars works' '
	HOBBES=Calvin &&
	export HOBBES &&
	git commit --allow-empty -m with 2>err &&
	grep Calvin err &&
	git -c core.unsetenvvars=FINDUS,HOBBES,CALVIN \
		commit --allow-empty -m without 2>err &&
	! grep Calvin err
'

test_done
