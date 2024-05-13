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

test_done
