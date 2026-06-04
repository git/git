#!/bin/sh

test_description='Tests rebase performance'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'setup rebasing on top of a lot of changes' '
	git checkout -f -B base &&
	git checkout -B to-rebase &&
	git checkout -B upstream &&
	test_seq 1000 >content_fwd &&
	sort -nr content_fwd >content_rev &&
	(
		for i in $(test_seq 100)
		do
			test_tick &&
			echo "commit refs/heads/upstream" &&
			echo "committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE" &&
			echo "data <<EOF" &&
			echo "commit$i" &&
			echo "EOF" &&

			if test "$i" = 1; then
				echo "from refs/heads/upstream^0"
			fi &&

			echo "M 100644 inline unrelated-file$i" &&
			echo "data <<EOF" &&
			echo "change$i" &&
			cat content_fwd &&
			echo "EOF" &&

			echo "commit refs/heads/upstream" &&
			echo "committer $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE" &&
			echo "data <<EOF" &&
			echo "commit$i-reverse" &&
			echo "EOF" &&
			echo "M 100644 inline unrelated-file$i" &&
			echo "data <<EOF" &&
			echo "change$i" &&
			cat content_rev &&
			echo "EOF" || exit 1
		done
	) >fast_import_stream &&

	git fast-import <fast_import_stream &&
	git repack -a -d &&
	git checkout -f upstream &&
	git checkout to-rebase &&
	test_commit our-patch interesting-file
'

test_perf 'rebase on top of a lot of unrelated changes' '
	git rebase --onto upstream HEAD^ &&
	git rebase --onto base HEAD^
'

test_expect_success 'setup rebasing many changes without split-index' '
	git config core.splitIndex false &&
	git checkout -B upstream2 to-rebase &&
	git checkout -B to-rebase2 upstream
'

test_perf 'rebase a lot of unrelated changes without split-index' '
	git rebase --onto upstream2 base &&
	git rebase --onto base upstream2
'

test_expect_success 'setup rebasing many changes with split-index' '
	git config core.splitIndex true
'

test_perf 'rebase a lot of unrelated changes with split-index' '
	git rebase --onto upstream2 base &&
	git rebase --onto base upstream2
'

test_done
