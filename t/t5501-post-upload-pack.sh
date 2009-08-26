#!/bin/sh

test_description='post upload-hook'

. ./test-lib.sh

LOGFILE=".git/post-upload-pack-log"

test_expect_success setup '
	test_commit A &&
	test_commit B &&
	git reset --hard A &&
	test_commit C &&
	git branch prev B &&
	mkdir -p .git/hooks &&
	{
		echo "#!$SHELL_PATH" &&
		echo "cat >post-upload-pack-log"
	} >".git/hooks/post-upload-pack" &&
	chmod +x .git/hooks/post-upload-pack
'

test_expect_success initial '
	rm -fr sub &&
	git init sub &&
	(
		cd sub &&
		git fetch --no-tags .. prev
	) &&
	want=$(sed -n "s/^want //p" "$LOGFILE") &&
	test "$want" = "$(git rev-parse --verify B)" &&
	! grep "^have " "$LOGFILE"
'

test_expect_success second '
	rm -fr sub &&
	git init sub &&
	(
		cd sub &&
		git fetch --no-tags .. prev:refs/remotes/prev &&
		git fetch --no-tags .. master
	) &&
	want=$(sed -n "s/^want //p" "$LOGFILE") &&
	test "$want" = "$(git rev-parse --verify C)" &&
	have=$(sed -n "s/^have //p" "$LOGFILE") &&
	test "$have" = "$(git rev-parse --verify B)"
'

test_done
