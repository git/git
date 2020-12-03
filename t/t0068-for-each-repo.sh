#!/bin/sh

test_description='git for-each-repo builtin'

. ./test-lib.sh

test_expect_success 'run based on configured value' '
	git init one &&
	git init two &&
	git init three &&
	git -C two commit --allow-empty -m "DID NOT RUN" &&
	git config run.key "$TRASH_DIRECTORY/one" &&
	git config --add run.key "$TRASH_DIRECTORY/three" &&
	git for-each-repo --config=run.key commit --allow-empty -m "ran" &&
	git -C one log -1 --pretty=format:%s >message &&
	grep ran message &&
	git -C two log -1 --pretty=format:%s >message &&
	! grep ran message &&
	git -C three log -1 --pretty=format:%s >message &&
	grep ran message &&
	git for-each-repo --config=run.key -- commit --allow-empty -m "ran again" &&
	git -C one log -1 --pretty=format:%s >message &&
	grep again message &&
	git -C two log -1 --pretty=format:%s >message &&
	! grep again message &&
	git -C three log -1 --pretty=format:%s >message &&
	grep again message
'

test_done
