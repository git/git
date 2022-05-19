#!/bin/sh

test_description='filter-branch removal of trees with null sha1'
. ./test-lib.sh

test_expect_success 'setup: base cummits' '
	test_cummit one &&
	test_cummit two &&
	test_cummit three
'

test_expect_success 'setup: a cummit with a bogus null sha1 in the tree' '
	{
		git ls-tree HEAD &&
		printf "160000 cummit $ZERO_OID\\tbroken\\n"
	} >broken-tree &&
	echo "add broken entry" >msg &&

	tree=$(git mktree <broken-tree) &&
	test_tick &&
	cummit=$(git cummit-tree $tree -p HEAD <msg) &&
	git update-ref HEAD "$cummit"
'

# we have to make one more cummit on top removing the broken
# entry, since otherwise our index does not match HEAD (and filter-branch will
# complain). We could make the index match HEAD, but doing so would involve
# writing a null sha1 into the index.
test_expect_success 'setup: bring HEAD and index in sync' '
	test_tick &&
	git cummit -a -m "back to normal"
'

test_expect_success 'noop filter-branch complains' '
	test_must_fail git filter-branch \
		--force --prune-empty \
		--index-filter "true"
'

test_expect_success 'filter commands are still checked' '
	test_must_fail git filter-branch \
		--force --prune-empty \
		--index-filter "git rm --cached --ignore-unmatch three.t"
'

test_expect_success 'removing the broken entry works' '
	echo three >expect &&
	git filter-branch \
		--force --prune-empty \
		--index-filter "git rm --cached --ignore-unmatch broken" &&
	git log -1 --format=%s >actual &&
	test_cmp expect actual
'

test_done
