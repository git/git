#!/bin/sh
test_description='test git fast-import unpack limit'
. ./test-lib.sh

test_expect_success 'create loose objects on import' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/master
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	initial
	COMMIT

	done
	INPUT_END

	git -c fastimport.unpackLimit=2 fast-import --done <input &&
	git fsck --no-progress &&
	test $(find .git/objects/?? -type f | wc -l) -eq 2 &&
	test $(find .git/objects/pack -type f | wc -l) -eq 0
'

test_expect_success 'bigger packs are preserved' '
	test_tick &&
	cat >input <<-INPUT_END &&
	commit refs/heads/master
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	incremental should create a pack
	COMMIT
	from refs/heads/master^0

	commit refs/heads/branch
	committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE
	data <<COMMIT
	branch
	COMMIT

	done
	INPUT_END

	git -c fastimport.unpackLimit=2 fast-import --done <input &&
	git fsck --no-progress &&
	test $(find .git/objects/?? -type f | wc -l) -eq 2 &&
	test $(find .git/objects/pack -type f | wc -l) -eq 2
'

test_done
