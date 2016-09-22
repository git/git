#!/bin/sh

test_description='check receive input limits'
. ./test-lib.sh

# Let's run tests with different unpack limits: 1 and 10000
# When the limit is 1, `git receive-pack` will call `git index-pack`.
# When the limit is 10000, `git receive-pack` will call `git unpack-objects`.

test_pack_input_limit () {
	case "$1" in
	index) unpack_limit=1 ;;
	unpack) unpack_limit=10000 ;;
	esac

	test_expect_success 'prepare destination repository' '
		rm -fr dest &&
		git --bare init dest
	'

	test_expect_success "set unpacklimit to $unpack_limit" '
		git --git-dir=dest config receive.unpacklimit "$unpack_limit"
	'

	test_expect_success 'setting receive.maxInputSize to 512 rejects push' '
		git --git-dir=dest config receive.maxInputSize 512 &&
		test_must_fail git push dest HEAD
	'

	test_expect_success 'bumping limit to 4k allows push' '
		git --git-dir=dest config receive.maxInputSize 4k &&
		git push dest HEAD
	'

	test_expect_success 'prepare destination repository (again)' '
		rm -fr dest &&
		git --bare init dest
	'

	test_expect_success 'lifting the limit allows push' '
		git --git-dir=dest config receive.maxInputSize 0 &&
		git push dest HEAD
	'
}

test_expect_success "create known-size (1024 bytes) commit" '
	test-genrandom foo 1024 >one-k &&
	git add one-k &&
	test_commit one-k
'

test_pack_input_limit index
test_pack_input_limit unpack

test_done
