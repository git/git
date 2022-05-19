#!/bin/sh

test_description='but svn authorship'
. ./lib-but-svn.sh

test_expect_success 'setup svn repository' '
	svn_cmd checkout "$svnrepo" work.svn &&
	(
		cd work.svn &&
		echo >file &&
		svn_cmd add file &&
		svn_cmd cummit -m "first cummit" file
	)
'

test_expect_success 'interact with it via but svn' '
	mkdir work.but &&
	(
		cd work.but &&
		but svn init "$svnrepo" &&
		but svn fetch &&

		echo modification >file &&
		test_tick &&
		but cummit -a -m second &&

		test_tick &&
		but svn dcummit &&

		echo "further modification" >file &&
		test_tick &&
		but cummit -a -m third &&

		test_tick &&
		but svn --add-author-from dcummit &&

		echo "yet further modification" >file &&
		test_tick &&
		but cummit -a -m fourth &&

		test_tick &&
		but svn --add-author-from --use-log-author dcummit &&

		but log &&

		but show -s HEAD^^ >../actual.2 &&
		but show -s HEAD^  >../actual.3 &&
		but show -s HEAD   >../actual.4

	) &&

	# Make sure that --add-author-from without --use-log-author
	# did not affect the authorship information
	myself=$(grep "^Author: " actual.2) &&
	unaffected=$(grep "^Author: " actual.3) &&
	test "z$myself" = "z$unaffected" &&

	# Make sure lack of --add-author-from did not add cruft
	! grep "^    From: A U Thor " actual.2 &&

	# Make sure --add-author-from added cruft
	grep "^    From: A U Thor " actual.3 &&
	grep "^    From: A U Thor " actual.4 &&

	# Make sure --add-author-from with --use-log-author affected
	# the authorship information
	grep "^Author: A U Thor " actual.4 &&

	# Make sure there are no cummit messages with excess blank lines
	test $(grep "^ " actual.2 | wc -l) = 3 &&
	test $(grep "^ " actual.3 | wc -l) = 5 &&
	test $(grep "^ " actual.4 | wc -l) = 5 &&

	# Make sure there are no svn cummit messages with excess blank lines
	(
		cd work.svn &&
		svn_cmd up &&
		
		test $(svn_cmd log -r2:2 | wc -l) = 5 &&
		test $(svn_cmd log -r4:4 | wc -l) = 7
	)
'

test_done
