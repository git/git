#!/bin/sh

test_description='fetch/receive strict mode'
. ./test-lib.sh

test_expect_success setup '
	echo hello >greetings &&
	git add greetings &&
	git commit -m greetings &&

	S=$(git rev-parse :greetings | sed -e "s|^..|&/|") &&
	X=$(echo bye | git hash-object -w --stdin | sed -e "s|^..|&/|") &&
	mv -f .git/objects/$X .git/objects/$S &&

	test_must_fail git fsck
'

test_expect_success 'fetch without strict' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config fetch.fsckobjects false &&
		git config transfer.fsckobjects false &&
		test_must_fail git fetch ../.git master
	)
'

test_expect_success 'fetch with !fetch.fsckobjects' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config fetch.fsckobjects false &&
		git config transfer.fsckobjects true &&
		test_must_fail git fetch ../.git master
	)
'

test_expect_success 'fetch with fetch.fsckobjects' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config fetch.fsckobjects true &&
		git config transfer.fsckobjects false &&
		test_must_fail git fetch ../.git master
	)
'

test_expect_success 'fetch with transfer.fsckobjects' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config transfer.fsckobjects true &&
		test_must_fail git fetch ../.git master
	)
'

cat >exp <<EOF
To dst
!	refs/heads/master:refs/heads/test	[remote rejected] (missing necessary objects)
EOF

test_expect_success 'push without strict' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config fetch.fsckobjects false &&
		git config transfer.fsckobjects false
	) &&
	test_must_fail git push --porcelain dst master:refs/heads/test >act &&
	test_cmp exp act
'

test_expect_success 'push with !receive.fsckobjects' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config receive.fsckobjects false &&
		git config transfer.fsckobjects true
	) &&
	test_must_fail git push --porcelain dst master:refs/heads/test >act &&
	test_cmp exp act
'

cat >exp <<EOF
To dst
!	refs/heads/master:refs/heads/test	[remote rejected] (n/a (unpacker error))
EOF

test_expect_success 'push with receive.fsckobjects' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config receive.fsckobjects true &&
		git config transfer.fsckobjects false
	) &&
	test_must_fail git push --porcelain dst master:refs/heads/test >act &&
	test_cmp exp act
'

test_expect_success 'push with transfer.fsckobjects' '
	rm -rf dst &&
	git init dst &&
	(
		cd dst &&
		git config transfer.fsckobjects true
	) &&
	test_must_fail git push --porcelain dst master:refs/heads/test >act &&
	test_cmp exp act
'

test_done
