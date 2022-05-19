#!/bin/sh

test_description='but reflog --updateref'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	but init -b main repo &&
	(
		cd repo &&

		test_cummit A &&
		test_cummit B &&
		test_cummit C &&

		cp .but/logs/HEAD HEAD.old &&
		but reset --hard HEAD~ &&
		cp HEAD.old .but/logs/HEAD
	)
'

test_reflog_updateref () {
	exp=$1
	shift
	args="$@"

	test_expect_success REFFILES "get '$exp' with '$args'"  '
		test_when_finished "rm -rf copy" &&
		cp -R repo copy &&

		(
			cd copy &&

			$args &&
			but rev-parse $exp >expect &&
			but rev-parse HEAD >actual &&

			test_cmp expect actual
		)
	'
}

test_reflog_updateref B but reflog delete --updateref HEAD@{0}
test_reflog_updateref B but reflog delete --updateref HEAD@{1}
test_reflog_updateref C but reflog delete --updateref main@{0}
test_reflog_updateref B but reflog delete --updateref main@{1}
test_reflog_updateref B but reflog delete --updateref --rewrite HEAD@{0}
test_reflog_updateref B but reflog delete --updateref --rewrite HEAD@{1}
test_reflog_updateref C but reflog delete --updateref --rewrite main@{0}
test_reflog_updateref B but reflog delete --updateref --rewrite main@{1}
test_reflog_updateref B test_must_fail but reflog expire  HEAD@{0}
test_reflog_updateref B test_must_fail but reflog expire  HEAD@{1}
test_reflog_updateref B test_must_fail but reflog expire  main@{0}
test_reflog_updateref B test_must_fail but reflog expire  main@{1}
test_reflog_updateref B test_must_fail but reflog expire --updateref HEAD@{0}
test_reflog_updateref B test_must_fail but reflog expire --updateref HEAD@{1}
test_reflog_updateref B test_must_fail but reflog expire --updateref main@{0}
test_reflog_updateref B test_must_fail but reflog expire --updateref main@{1}
test_reflog_updateref B test_must_fail but reflog expire --updateref --rewrite HEAD@{0}
test_reflog_updateref B test_must_fail but reflog expire --updateref --rewrite HEAD@{1}
test_reflog_updateref B test_must_fail but reflog expire --updateref --rewrite main@{0}
test_reflog_updateref B test_must_fail but reflog expire --updateref --rewrite main@{1}

test_done
