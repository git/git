#!/bin/sh

test_description='for-each-ref errors for broken refs'

. ./test-lib.sh

ZEROS=$ZERO_OID

test_expect_success setup '
	MISSING=$(test_oid deadbeef) &&
	git commit --allow-empty -m "Initial" &&
	git tag testtag &&
	git for-each-ref >full-list &&
	git for-each-ref --format="%(objectname) %(refname)" >brief-list
'

test_expect_success REFFILES 'Broken refs are reported correctly' '
	r=refs/heads/bogus &&
	: >.git/$r &&
	test_when_finished "rm -f .git/$r" &&
	echo "warning: ignoring broken ref $r" >broken-err &&
	git for-each-ref >out 2>err &&
	test_cmp full-list out &&
	test_cmp broken-err err
'

test_expect_success REFFILES 'NULL_SHA1 refs are reported correctly' '
	r=refs/heads/zeros &&
	echo $ZEROS >.git/$r &&
	test_when_finished "rm -f .git/$r" &&
	echo "warning: ignoring broken ref $r" >zeros-err &&
	git for-each-ref >out 2>err &&
	test_cmp full-list out &&
	test_cmp zeros-err err &&
	git for-each-ref --format="%(objectname) %(refname)" >brief-out 2>brief-err &&
	test_cmp brief-list brief-out &&
	test_cmp zeros-err brief-err
'

test_expect_success 'Missing objects are reported correctly' '
	test_when_finished "git update-ref -d refs/heads/missing" &&
	test-tool ref-store main update-ref msg refs/heads/missing "$MISSING" "$ZERO_OID" REF_SKIP_OID_VERIFICATION &&
	echo "fatal: missing object $MISSING for refs/heads/missing" >missing-err &&
	test_must_fail git for-each-ref 2>err &&
	test_cmp missing-err err &&
	(
		cat brief-list &&
		echo "$MISSING refs/heads/missing"
	) | sort -k 2 >missing-brief-expected &&
	git for-each-ref --format="%(objectname) %(refname)" >brief-out 2>brief-err &&
	test_cmp missing-brief-expected brief-out &&
	test_must_be_empty brief-err
'

test_expect_success 'ahead-behind requires an argument' '
	test_must_fail git for-each-ref \
		--format="%(ahead-behind)" 2>err &&
	echo "fatal: expected format: %(ahead-behind:<committish>)" >expect &&
	test_cmp expect err
'

test_expect_success 'missing ahead-behind base' '
	test_must_fail git for-each-ref \
		--format="%(ahead-behind:refs/heads/missing)" 2>err &&
	echo "fatal: failed to find '\''refs/heads/missing'\''" >expect &&
	test_cmp expect err
'

test_done
