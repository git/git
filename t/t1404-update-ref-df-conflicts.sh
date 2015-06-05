#!/bin/sh

test_description='Test git update-ref with D/F conflicts'
. ./test-lib.sh

test_update_rejected () {
	prefix="$1" &&
	before="$2" &&
	pack="$3" &&
	create="$4" &&
	error="$5" &&
	printf "create $prefix/%s $C\n" $before |
	git update-ref --stdin &&
	git for-each-ref $prefix >unchanged &&
	if $pack
	then
		git pack-refs --all
	fi &&
	printf "create $prefix/%s $C\n" $create >input &&
	test_must_fail git update-ref --stdin <input 2>output.err &&
	grep -F "$error" output.err &&
	git for-each-ref $prefix >actual &&
	test_cmp unchanged actual
}

Q="'"

test_expect_success 'setup' '

	git commit --allow-empty -m Initial &&
	C=$(git rev-parse HEAD)

'

test_expect_success 'existing loose ref is a simple prefix of new' '

	prefix=refs/1l &&
	test_update_rejected $prefix "a c e" false "b c/x d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x$Q"

'

test_expect_success 'existing packed ref is a simple prefix of new' '

	prefix=refs/1p &&
	test_update_rejected $prefix "a c e" true "b c/x d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x$Q"

'

test_expect_success 'existing loose ref is a deeper prefix of new' '

	prefix=refs/2l &&
	test_update_rejected $prefix "a c e" false "b c/x/y d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x/y$Q"

'

test_expect_success 'existing packed ref is a deeper prefix of new' '

	prefix=refs/2p &&
	test_update_rejected $prefix "a c e" true "b c/x/y d" \
		"$Q$prefix/c$Q exists; cannot create $Q$prefix/c/x/y$Q"

'

test_expect_success 'new ref is a simple prefix of existing loose' '

	prefix=refs/3l &&
	test_update_rejected $prefix "a c/x e" false "b c d" \
		"$Q$prefix/c/x$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'new ref is a simple prefix of existing packed' '

	prefix=refs/3p &&
	test_update_rejected $prefix "a c/x e" true "b c d" \
		"$Q$prefix/c/x$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'new ref is a deeper prefix of existing loose' '

	prefix=refs/4l &&
	test_update_rejected $prefix "a c/x/y e" false "b c d" \
		"$Q$prefix/c/x/y$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'new ref is a deeper prefix of existing packed' '

	prefix=refs/4p &&
	test_update_rejected $prefix "a c/x/y e" true "b c d" \
		"$Q$prefix/c/x/y$Q exists; cannot create $Q$prefix/c$Q"

'

test_expect_success 'one new ref is a simple prefix of another' '

	prefix=refs/5 &&
	test_update_rejected $prefix "a e" false "b c c/x d" \
		"cannot process $Q$prefix/c$Q and $Q$prefix/c/x$Q at the same time"

'

test_done
