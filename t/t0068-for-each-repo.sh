#!/bin/sh

test_description='but for-each-repo builtin'

. ./test-lib.sh

test_expect_success 'run based on configured value' '
	but init one &&
	but init two &&
	but init three &&
	but -C two cummit --allow-empty -m "DID NOT RUN" &&
	but config run.key "$TRASH_DIRECTORY/one" &&
	but config --add run.key "$TRASH_DIRECTORY/three" &&
	but for-each-repo --config=run.key cummit --allow-empty -m "ran" &&
	but -C one log -1 --pretty=format:%s >message &&
	grep ran message &&
	but -C two log -1 --pretty=format:%s >message &&
	! grep ran message &&
	but -C three log -1 --pretty=format:%s >message &&
	grep ran message &&
	but for-each-repo --config=run.key -- cummit --allow-empty -m "ran again" &&
	but -C one log -1 --pretty=format:%s >message &&
	grep again message &&
	but -C two log -1 --pretty=format:%s >message &&
	! grep again message &&
	but -C three log -1 --pretty=format:%s >message &&
	grep again message
'

test_expect_success 'do nothing on empty config' '
	# the whole thing would fail if for-each-ref iterated even
	# once, because "but help --no-such-option" would fail
	but for-each-repo --config=bogus.config -- help --no-such-option
'

test_done
