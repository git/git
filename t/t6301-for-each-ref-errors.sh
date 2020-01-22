#!/bin/sh

test_description='for-each-ref errors for broken refs'

. ./test-lib.sh

ZEROS=$ZERO_OID
MISSING=abababababababababababababababababababab

test_expect_success setup '
	git commit --allow-empty -m "Initial" &&
	git tag testtag &&
	git for-each-ref >full-list &&
	git for-each-ref --format="%(objectname) %(refname)" >brief-list
'

test_expect_success 'Broken refs are reported correctly' '
	r=refs/heads/bogus &&
	: >.git/$r &&
	test_when_finished "rm -f .git/$r" &&
	echo "warning: ignoring broken ref $r" >broken-err &&
	git for-each-ref >out 2>err &&
	test_i18ncmp full-list out &&
	test_i18ncmp broken-err err
'

test_expect_success 'NULL_SHA1 refs are reported correctly' '
	r=refs/heads/zeros &&
	echo $ZEROS >.git/$r &&
	test_when_finished "rm -f .git/$r" &&
	echo "warning: ignoring broken ref $r" >zeros-err &&
	git for-each-ref >out 2>err &&
	test_cmp full-list out &&
	test_i18ncmp zeros-err err &&
	git for-each-ref --format="%(objectname) %(refname)" >brief-out 2>brief-err &&
	test_cmp brief-list brief-out &&
	test_i18ncmp zeros-err brief-err
'

test_expect_success 'Missing objects are reported correctly' '
	r=refs/heads/missing &&
	echo $MISSING >.git/$r &&
	test_when_finished "rm -f .git/$r" &&
	echo "fatal: missing object $MISSING for $r" >missing-err &&
	test_must_fail git for-each-ref 2>err &&
	test_i18ncmp missing-err err &&
	(
		cat brief-list &&
		echo "$MISSING $r"
	) | sort -k 2 >missing-brief-expected &&
	git for-each-ref --format="%(objectname) %(refname)" >brief-out 2>brief-err &&
	test_cmp missing-brief-expected brief-out &&
	test_must_be_empty brief-err
'

test_done
