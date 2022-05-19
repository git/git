#!/bin/sh

test_description='avoid rewriting packed-refs unnecessarily'

. ./test-lib.sh

# Add an identifying mark to the packed-refs file header line. This
# shouldn't upset readers, and it should be omitted if the file is
# ever rewritten.
mark_packed_refs () {
	sed -e "s/^\(#.*\)/\1 t1409 /" .but/packed-refs >.but/packed-refs.new &&
	mv .but/packed-refs.new .but/packed-refs
}

# Verify that the packed-refs file is still marked.
check_packed_refs_marked () {
	grep -q '^#.* t1409 ' .but/packed-refs
}

test_expect_success 'setup' '
	but cummit --allow-empty -m "cummit A" &&
	A=$(but rev-parse HEAD) &&
	but cummit --allow-empty -m "cummit B" &&
	B=$(but rev-parse HEAD) &&
	but cummit --allow-empty -m "cummit C" &&
	C=$(but rev-parse HEAD)
'

test_expect_success 'do not create packed-refs file gratuitously' '
	test_path_is_missing .but/packed-refs &&
	but update-ref refs/heads/foo $A &&
	test_path_is_missing .but/packed-refs &&
	but update-ref refs/heads/foo $B &&
	test_path_is_missing .but/packed-refs &&
	but update-ref refs/heads/foo $C $B &&
	test_path_is_missing .but/packed-refs &&
	but update-ref -d refs/heads/foo &&
	test_path_is_missing .but/packed-refs
'

test_expect_success 'check that marking the packed-refs file works' '
	but for-each-ref >expected &&
	but pack-refs --all &&
	mark_packed_refs &&
	check_packed_refs_marked &&
	but for-each-ref >actual &&
	test_cmp expected actual &&
	but pack-refs --all &&
	! check_packed_refs_marked &&
	but for-each-ref >actual2 &&
	test_cmp expected actual2
'

test_expect_success 'leave packed-refs untouched on update of packed' '
	but update-ref refs/heads/packed-update $A &&
	but pack-refs --all &&
	mark_packed_refs &&
	but update-ref refs/heads/packed-update $B &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on checked update of packed' '
	but update-ref refs/heads/packed-checked-update $A &&
	but pack-refs --all &&
	mark_packed_refs &&
	but update-ref refs/heads/packed-checked-update $B $A &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on verify of packed' '
	but update-ref refs/heads/packed-verify $A &&
	but pack-refs --all &&
	mark_packed_refs &&
	echo "verify refs/heads/packed-verify $A" | but update-ref --stdin &&
	check_packed_refs_marked
'

test_expect_success 'touch packed-refs on delete of packed' '
	but update-ref refs/heads/packed-delete $A &&
	but pack-refs --all &&
	mark_packed_refs &&
	but update-ref -d refs/heads/packed-delete &&
	! check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on update of loose' '
	but pack-refs --all &&
	but update-ref refs/heads/loose-update $A &&
	mark_packed_refs &&
	but update-ref refs/heads/loose-update $B &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on checked update of loose' '
	but pack-refs --all &&
	but update-ref refs/heads/loose-checked-update $A &&
	mark_packed_refs &&
	but update-ref refs/heads/loose-checked-update $B $A &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on verify of loose' '
	but pack-refs --all &&
	but update-ref refs/heads/loose-verify $A &&
	mark_packed_refs &&
	echo "verify refs/heads/loose-verify $A" | but update-ref --stdin &&
	check_packed_refs_marked
'

test_expect_success 'leave packed-refs untouched on delete of loose' '
	but pack-refs --all &&
	but update-ref refs/heads/loose-delete $A &&
	mark_packed_refs &&
	but update-ref -d refs/heads/loose-delete &&
	check_packed_refs_marked
'

test_done
