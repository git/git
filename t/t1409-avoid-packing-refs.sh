#!/bin/sh

test_description='avoid rewriting packed-refs unnecessarily'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Add an identifying mark to the packed-refs file header line. This
# shouldn't upset readers, and it should be omitted if the file is
# ever rewritten.
mark_packed_refs () {
	if test "$GIT_TEST_PACKED_REFS_VERSION" = "2"
	then
		size=$(wc -c < .git/packed-refs) &&
		pos=$(expr $size - 4) &&
		printf "FAKE" | dd of=".git/packed-refs" bs=1 seek="$pos" conv=notrunc
	else
		sed -e "s/^\(#.*\)/\1 t1409 /" .git/packed-refs >.git/packed-refs.new &&
		mv .git/packed-refs.new .git/packed-refs
	fi
}

# Verify that the packed-refs file is still marked.
check_packed_refs_marked () {
	if test "$GIT_TEST_PACKED_REFS_VERSION" = "2"
	then
		size=$(wc -c < .git/packed-refs) &&
		pos=$(expr $size - 4) &&
		tail -c 4 .git/packed-refs >actual &&
		printf "FAKE" >expect &&
		test_cmp expect actual
	else
		grep -q '^#.* t1409 ' .git/packed-refs
	fi
}

test_expect_success 'setup' '
	git commit --allow-empty -m "Commit A" &&
	A=$(git rev-parse HEAD) &&
	git commit --allow-empty -m "Commit B" &&
	B=$(git rev-parse HEAD) &&
	git commit --allow-empty -m "Commit C" &&
	C=$(git rev-parse HEAD)
'

test_expect_success 'do not create packed-refs file gratuitously' '
	test_path_is_missing .git/packed-refs &&
	git update-ref refs/heads/foo $A &&
	test_path_is_missing .git/packed-refs &&
	git update-ref refs/heads/foo $B &&
	test_path_is_missing .git/packed-refs &&
	git update-ref refs/heads/foo $C $B &&
	test_path_is_missing .git/packed-refs &&
	git update-ref -d refs/heads/foo &&
	test_path_is_missing .git/packed-refs
'

test_expect_success 'check that marking the packed-refs file works' '
	git for-each-ref >expected &&
	git pack-refs --all &&
	mark_packed_refs &&
	check_packed_refs_marked &&
	git for-each-ref >actual &&
	test_cmp expected actual &&
	git pack-refs --all &&
	! check_packed_refs_marked &&
	git for-each-ref >actual2 &&
	test_cmp expected actual2
'

test_expect_success 'leave packed-refs untouched on update of packed' '
	git update-ref refs/heads/packed-update $A &&
	git pack-refs --all &&
	mark_packed_refs &&
	git update-ref refs/heads/packed-update $B &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on checked update of packed' '
	git update-ref refs/heads/packed-checked-update $A &&
	git pack-refs --all &&
	mark_packed_refs &&
	git update-ref refs/heads/packed-checked-update $B $A &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on verify of packed' '
	git update-ref refs/heads/packed-verify $A &&
	git pack-refs --all &&
	mark_packed_refs &&
	echo "verify refs/heads/packed-verify $A" | git update-ref --stdin &&
	check_packed_refs_marked
'

test_expect_success 'touch packed-refs on delete of packed' '
	git update-ref refs/heads/packed-delete $A &&
	git pack-refs --all &&
	mark_packed_refs &&
	git update-ref -d refs/heads/packed-delete &&
	! check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on update of loose' '
	git pack-refs --all &&
	git update-ref refs/heads/loose-update $A &&
	mark_packed_refs &&
	git update-ref refs/heads/loose-update $B &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on checked update of loose' '
	git pack-refs --all &&
	git update-ref refs/heads/loose-checked-update $A &&
	mark_packed_refs &&
	git update-ref refs/heads/loose-checked-update $B $A &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on verify of loose' '
	git pack-refs --all &&
	git update-ref refs/heads/loose-verify $A &&
	mark_packed_refs &&
	echo "verify refs/heads/loose-verify $A" | git update-ref --stdin &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on delete of loose' '
	git pack-refs --all &&
	git update-ref refs/heads/loose-delete $A &&
	mark_packed_refs &&
	git update-ref -d refs/heads/loose-delete &&
	check_packed_refs_marked
'

test_done
