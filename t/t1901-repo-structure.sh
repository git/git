#!/bin/sh

test_description='test git repo structure'

. ./test-lib.sh

test_expect_success 'empty repository' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		cat >expect <<-\EOF &&
		| Repository structure | Value |
		| -------------------- | ----- |
		| * References         |       |
		|   * Count            |     0 |
		|     * Branches       |     0 |
		|     * Tags           |     0 |
		|     * Remotes        |     0 |
		|     * Others         |     0 |
		EOF

		git repo structure >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_expect_success 'repository with references' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git commit --allow-empty -m init &&
		git tag -a foo -m bar &&

		oid="$(git rev-parse HEAD)" &&
		git update-ref refs/remotes/origin/foo "$oid" &&

		git notes add -m foo &&

		cat >expect <<-\EOF &&
		| Repository structure | Value |
		| -------------------- | ----- |
		| * References         |       |
		|   * Count            |     4 |
		|     * Branches       |     1 |
		|     * Tags           |     1 |
		|     * Remotes        |     1 |
		|     * Others         |     1 |
		EOF

		git repo structure >out 2>err &&

		test_cmp expect out &&
		test_line_count = 0 err
	)
'

test_done
