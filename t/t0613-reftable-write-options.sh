#!/bin/sh

test_description='reftable write options'

GIT_TEST_DEFAULT_REF_FORMAT=reftable
export GIT_TEST_DEFAULT_REF_FORMAT
# Disable auto-compaction for all tests as we explicitly control repacking of
# refs.
GIT_TEST_REFTABLE_AUTOCOMPACTION=false
export GIT_TEST_REFTABLE_AUTOCOMPACTION
# Block sizes depend on the hash function, so we force SHA1 here.
GIT_TEST_DEFAULT_HASH=sha1
export GIT_TEST_DEFAULT_HASH
# Block sizes also depend on the actual refs we write, so we force "master" to
# be the default initial branch name.
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'default write options' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		git pack-refs &&
		cat >expect <<-EOF &&
		header:
		  block_size: 4096
		ref:
		  - length: 129
		    restarts: 2
		log:
		  - length: 262
		    restarts: 2
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'disabled reflog writes no log blocks' '
	test_config_global core.logAllRefUpdates false &&
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		git pack-refs &&
		cat >expect <<-EOF &&
		header:
		  block_size: 4096
		ref:
		  - length: 129
		    restarts: 2
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'many refs results in multiple blocks' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		for i in $(test_seq 200)
		do
			printf "update refs/heads/branch-%d HEAD\n" "$i" ||
			return 1
		done >input &&
		git update-ref --stdin <input &&
		git pack-refs &&

		cat >expect <<-EOF &&
		header:
		  block_size: 4096
		ref:
		  - length: 4049
		    restarts: 11
		  - length: 1136
		    restarts: 3
		log:
		  - length: 4041
		    restarts: 4
		  - length: 4015
		    restarts: 3
		  - length: 4014
		    restarts: 3
		  - length: 4012
		    restarts: 3
		  - length: 3289
		    restarts: 3
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'tiny block size leads to error' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		cat >expect <<-EOF &&
		error: unable to compact stack: entry too large
		EOF
		test_must_fail git -c reftable.blockSize=50 pack-refs 2>err &&
		test_cmp expect err
	)
'

test_expect_success 'small block size leads to multiple ref blocks' '
	test_config_global core.logAllRefUpdates false &&
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		test_commit B &&
		git -c reftable.blockSize=100 pack-refs &&

		cat >expect <<-EOF &&
		header:
		  block_size: 100
		ref:
		  - length: 53
		    restarts: 1
		  - length: 74
		    restarts: 1
		  - length: 38
		    restarts: 1
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'small block size fails with large reflog message' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		perl -e "print \"a\" x 500" >logmsg &&
		cat >expect <<-EOF &&
		fatal: update_ref failed for ref ${SQ}refs/heads/logme${SQ}: reftable: transaction failure: entry too large
		EOF
		test_must_fail git -c reftable.blockSize=100 \
			update-ref -m "$(cat logmsg)" refs/heads/logme HEAD 2>err &&
		test_cmp expect err
	)
'

test_expect_success 'block size exceeding maximum supported size' '
	test_config_global core.logAllRefUpdates false &&
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		test_commit B &&
		cat >expect <<-EOF &&
		fatal: reftable block size cannot exceed 16MB
		EOF
		test_must_fail git -c reftable.blockSize=16777216 pack-refs 2>err &&
		test_cmp expect err
	)
'

test_expect_success 'restart interval at every single record' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		for i in $(test_seq 10)
		do
			printf "update refs/heads/branch-%d HEAD\n" "$i" ||
			return 1
		done >input &&
		git update-ref --stdin <input &&
		git -c reftable.restartInterval=1 pack-refs &&

		cat >expect <<-EOF &&
		header:
		  block_size: 4096
		ref:
		  - length: 566
		    restarts: 13
		log:
		  - length: 1393
		    restarts: 12
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'restart interval exceeding maximum supported interval' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		cat >expect <<-EOF &&
		fatal: reftable block size cannot exceed 65535
		EOF
		test_must_fail git -c reftable.restartInterval=65536 pack-refs 2>err &&
		test_cmp expect err
	)
'

test_expect_success 'object index gets written by default with ref index' '
	test_config_global core.logAllRefUpdates false &&
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		for i in $(test_seq 5)
		do
			printf "update refs/heads/branch-%d HEAD\n" "$i" ||
			return 1
		done >input &&
		git update-ref --stdin <input &&
		git -c reftable.blockSize=100 pack-refs &&

		cat >expect <<-EOF &&
		header:
		  block_size: 100
		ref:
		  - length: 53
		    restarts: 1
		  - length: 95
		    restarts: 1
		  - length: 71
		    restarts: 1
		  - length: 80
		    restarts: 1
		obj:
		  - length: 11
		    restarts: 1
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'object index can be disabled' '
	test_config_global core.logAllRefUpdates false &&
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		for i in $(test_seq 5)
		do
			printf "update refs/heads/branch-%d HEAD\n" "$i" ||
			return 1
		done >input &&
		git update-ref --stdin <input &&
		git -c reftable.blockSize=100 -c reftable.indexObjects=false pack-refs &&

		cat >expect <<-EOF &&
		header:
		  block_size: 100
		ref:
		  - length: 53
		    restarts: 1
		  - length: 95
		    restarts: 1
		  - length: 71
		    restarts: 1
		  - length: 80
		    restarts: 1
		EOF
		test-tool dump-reftable -b .git/reftable/*.ref >actual &&
		test_cmp expect actual
	)
'

test_done
