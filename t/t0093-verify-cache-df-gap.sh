#!/bin/sh

test_description='verify_cache() must catch non-adjacent D/F conflicts

Ensure that verify_cache() can complain about bad entries like:

  docs               <-- submodule
  docs-internal/...  <-- sorts here because "-" < "/"
  docs/...           <-- D/F conflict with "docs" above, not adjacent

In order to test verify_cache, we directly construct a corrupt index
(bypassing the D/F safety checks in add_index_entry) and verify that
write-tree rejects it.
'

. ./test-lib.sh

if ! test_have_prereq PERL
then
	skip_all='skipping verify_cache D/F tests; Perl not available'
	test_done
fi

# Build a v2 index from entries on stdin, bypassing D/F checks.
# Each line: "octalmode hex-oid name" (entries must be pre-sorted).
build_corrupt_index () {
	perl "$TEST_DIRECTORY/t0093-direct-index-write.pl" >"$1"
}

test_expect_success 'setup objects' '
	test_commit base &&
	BLOB=$(git rev-parse HEAD:base.t) &&
	SUB_COMMIT=$(git rev-parse HEAD)
'

test_expect_success 'adjacent D/F conflict is caught by verify_cache' '
	cat >index-entries <<-EOF &&
	0160000 $SUB_COMMIT docs
	0100644 $BLOB docs/requirements.txt
	EOF
	build_corrupt_index .git/index <index-entries &&

	test_must_fail git write-tree 2>err &&
	test_grep "You have both docs and docs/requirements.txt" err
'

test_expect_success 'non-adjacent D/F conflict is caught by verify_cache' '
	cat >index-entries <<-EOF &&
	0160000 $SUB_COMMIT docs
	0100644 $BLOB docs-internal/README.md
	0100644 $BLOB docs/requirements.txt
	EOF
	build_corrupt_index .git/index <index-entries &&

	test_must_fail git write-tree 2>err &&
	test_grep "You have both docs and docs/requirements.txt" err
'

test_done
