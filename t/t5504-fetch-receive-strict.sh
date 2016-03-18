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
!	refs/heads/master:refs/heads/test	[remote rejected] (unpacker error)
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

cat >bogus-commit <<\EOF
tree 4b825dc642cb6eb9a060e54bf8d69288fbee4904
author Bugs Bunny 1234567890 +0000
committer Bugs Bunny <bugs@bun.ni> 1234567890 +0000

This commit object intentionally broken
EOF

test_expect_success 'push with receive.fsck.skipList' '
	commit="$(git hash-object -t commit -w --stdin <bogus-commit)" &&
	git push . $commit:refs/heads/bogus &&
	rm -rf dst &&
	git init dst &&
	git --git-dir=dst/.git config receive.fsckObjects true &&
	test_must_fail git push --porcelain dst bogus &&
	git --git-dir=dst/.git config receive.fsck.skipList SKIP &&
	echo $commit >dst/.git/SKIP &&
	git push --porcelain dst bogus
'

test_expect_success 'push with receive.fsck.missingEmail=warn' '
	commit="$(git hash-object -t commit -w --stdin <bogus-commit)" &&
	git push . $commit:refs/heads/bogus &&
	rm -rf dst &&
	git init dst &&
	git --git-dir=dst/.git config receive.fsckobjects true &&
	test_must_fail git push --porcelain dst bogus &&
	git --git-dir=dst/.git config \
		receive.fsck.missingEmail warn &&
	git push --porcelain dst bogus >act 2>&1 &&
	grep "missingEmail" act &&
	git --git-dir=dst/.git branch -D bogus &&
	git --git-dir=dst/.git config --add \
		receive.fsck.missingEmail ignore &&
	git --git-dir=dst/.git config --add \
		receive.fsck.badDate warn &&
	git push --porcelain dst bogus >act 2>&1 &&
	test_must_fail grep "missingEmail" act
'

test_expect_success \
	'receive.fsck.unterminatedHeader=warn triggers error' '
	rm -rf dst &&
	git init dst &&
	git --git-dir=dst/.git config receive.fsckobjects true &&
	git --git-dir=dst/.git config \
		receive.fsck.unterminatedheader warn &&
	test_must_fail git push --porcelain dst HEAD >act 2>&1 &&
	grep "Cannot demote unterminatedheader" act
'

test_done
