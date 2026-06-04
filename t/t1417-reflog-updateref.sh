#!/bin/sh

test_description='git reflog --updateref'

. ./test-lib.sh

test_expect_success 'setup' '
	git init -b main repo &&
	(
		cd repo &&

		test_commit A &&
		test_commit B &&
		test_commit C &&

		git reflog HEAD >expect &&
		git reset --hard HEAD~ &&
		# Make sure that the reflog does not point to the same commit
		# as HEAD.
		git reflog delete HEAD@{0} &&
		git reflog HEAD >actual &&
		test_cmp expect actual
	)
'

test_reflog_updateref () {
	exp=$1
	shift
	args="$@"

	test_expect_success "get '$exp' with '$args'"  '
		test_when_finished "rm -rf copy" &&
		cp -R repo copy &&

		(
			cd copy &&

			$args &&
			git rev-parse $exp >expect &&
			git rev-parse HEAD >actual &&

			test_cmp expect actual
		)
	'
}

test_reflog_updateref B git reflog delete --updateref HEAD@{0}
test_reflog_updateref B git reflog delete --updateref HEAD@{1}
test_reflog_updateref C git reflog delete --updateref main@{0}
test_reflog_updateref B git reflog delete --updateref main@{1}
test_reflog_updateref B git reflog delete --updateref --rewrite HEAD@{0}
test_reflog_updateref B git reflog delete --updateref --rewrite HEAD@{1}
test_reflog_updateref C git reflog delete --updateref --rewrite main@{0}
test_reflog_updateref B git reflog delete --updateref --rewrite main@{1}
test_reflog_updateref B test_must_fail git reflog expire  HEAD@{0}
test_reflog_updateref B test_must_fail git reflog expire  HEAD@{1}
test_reflog_updateref B test_must_fail git reflog expire  main@{0}
test_reflog_updateref B test_must_fail git reflog expire  main@{1}
test_reflog_updateref B test_must_fail git reflog expire --updateref HEAD@{0}
test_reflog_updateref B test_must_fail git reflog expire --updateref HEAD@{1}
test_reflog_updateref B test_must_fail git reflog expire --updateref main@{0}
test_reflog_updateref B test_must_fail git reflog expire --updateref main@{1}
test_reflog_updateref B test_must_fail git reflog expire --updateref --rewrite HEAD@{0}
test_reflog_updateref B test_must_fail git reflog expire --updateref --rewrite HEAD@{1}
test_reflog_updateref B test_must_fail git reflog expire --updateref --rewrite main@{0}
test_reflog_updateref B test_must_fail git reflog expire --updateref --rewrite main@{1}

test_done
