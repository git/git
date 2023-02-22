#!/bin/sh

test_description='check environment showed to remote side of transports'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'set up "remote" push situation' '
	test_commit one &&
	git config push.default current &&
	git init remote
'

test_expect_success 'set up fake ssh' '
	GIT_SSH_COMMAND="f() {
		cd \"\$TRASH_DIRECTORY\" &&
		eval \"\$2\"
	}; f" &&
	export GIT_SSH_COMMAND &&
	export TRASH_DIRECTORY
'

# due to receive.denyCurrentBranch=true
test_expect_success 'confirm default push fails' '
	test_must_fail git push remote
'

test_expect_success 'config does not travel over same-machine push' '
	test_must_fail git -c receive.denyCurrentBranch=false push remote
'

test_expect_success 'config does not travel over ssh push' '
	test_must_fail git -c receive.denyCurrentBranch=false push host:remote
'

test_done
