#!/bin/sh

test_description='git commit races'
. ./test-lib.sh

test_expect_success 'race to create orphan commit' '
	write_script hare-editor <<-\EOF &&
	git commit --allow-empty -m hare
	EOF
	test_must_fail env EDITOR=./hare-editor git commit --allow-empty -m tortoise -e &&
	git show -s --pretty=format:%s >subject &&
	grep hare subject &&
	test -z "$(git show -s --pretty=format:%P)"
'

test_expect_success 'race to create non-orphan commit' '
	write_script airplane-editor <<-\EOF &&
	git commit --allow-empty -m airplane
	EOF
	git checkout --orphan branch &&
	git commit --allow-empty -m base &&
	git rev-parse HEAD >base &&
	test_must_fail env EDITOR=./airplane-editor git commit --allow-empty -m ship -e &&
	git show -s --pretty=format:%s >subject &&
	grep airplane subject &&
	git rev-parse HEAD^ >parent &&
	test_cmp base parent
'

test_done
