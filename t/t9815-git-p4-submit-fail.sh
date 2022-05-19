#!/bin/sh

test_description='but p4 submit failure handling'

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		echo line1 >file1 &&
		p4 add file1 &&
		p4 submit -d "line1 in file1"
	)
'

test_expect_success 'conflict on one cummit' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line2 >>file1 &&
		p4 submit -d "line2 in file1"
	) &&
	(
		# now this cummit should cause a conflict
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		echo line3 >>file1 &&
		but add file1 &&
		but cummit -m "line3 in file1 will conflict" &&
		test_expect_code 1 but p4 submit >out &&
		test_i18ngrep "No cummits applied" out
	)
'

test_expect_success 'conflict on second of two cummits' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line3 >>file1 &&
		p4 submit -d "line3 in file1"
	) &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		# this cummit is okay
		test_cummit "first_cummit_okay" &&
		# now this submit should cause a conflict
		echo line4 >>file1 &&
		but add file1 &&
		but cummit -m "line4 in file1 will conflict" &&
		test_expect_code 1 but p4 submit >out &&
		test_i18ngrep "Applied only the cummits" out
	)
'

test_expect_success 'conflict on first of two cummits, skip' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line4 >>file1 &&
		p4 submit -d "line4 in file1"
	) &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		# this submit should cause a conflict
		echo line5 >>file1 &&
		but add file1 &&
		but cummit -m "line5 in file1 will conflict" &&
		# but this cummit is okay
		test_cummit "okay_cummit_after_skip" &&
		echo s | test_expect_code 1 but p4 submit >out &&
		test_i18ngrep "Applied only the cummits" out
	)
'

test_expect_success 'conflict on first of two cummits, quit' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line7 >>file1 &&
		p4 submit -d "line7 in file1"
	) &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		# this submit should cause a conflict
		echo line8 >>file1 &&
		but add file1 &&
		but cummit -m "line8 in file1 will conflict" &&
		# but this cummit is okay
		test_cummit "okay_cummit_after_quit" &&
		echo q | test_expect_code 1 but p4 submit >out &&
		test_i18ngrep "No cummits applied" out
	)
'

test_expect_success 'conflict cli and config options' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but p4 submit --conflict=ask &&
		but p4 submit --conflict=skip &&
		but p4 submit --conflict=quit &&
		test_expect_code 2 but p4 submit --conflict=foo &&
		test_expect_code 2 but p4 submit --conflict &&
		but config but-p4.conflict foo &&
		test_expect_code 1 but p4 submit &&
		but config --unset but-p4.conflict &&
		but p4 submit
	)
'

test_expect_success 'conflict on first of two cummits, --conflict=skip' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line9 >>file1 &&
		p4 submit -d "line9 in file1"
	) &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		# this submit should cause a conflict
		echo line10 >>file1 &&
		but add file1 &&
		but cummit -m "line10 in file1 will conflict" &&
		# but this cummit is okay
		test_cummit "okay_cummit_after_auto_skip" &&
		test_expect_code 1 but p4 submit --conflict=skip >out &&
		test_i18ngrep "Applied only the cummits" out
	)
'

test_expect_success 'conflict on first of two cummits, --conflict=quit' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line11 >>file1 &&
		p4 submit -d "line11 in file1"
	) &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		# this submit should cause a conflict
		echo line12 >>file1 &&
		but add file1 &&
		but cummit -m "line12 in file1 will conflict" &&
		# but this cummit is okay
		test_cummit "okay_cummit_after_auto_quit" &&
		test_expect_code 1 but p4 submit --conflict=quit >out &&
		test_i18ngrep "No cummits applied" out
	)
'

#
# Cleanup after submit fail, all cases.  Some modifications happen
# before trying to apply the patch.  Make sure these are unwound
# properly.  Put each one in a diff along with something that will
# obviously conflict.  Make sure it is back to normal after.
#

test_expect_success 'cleanup edit p4 populate' '
	(
		cd "$cli" &&
		echo text file >text &&
		p4 add text &&
		echo text+x file >text+x &&
		chmod 755 text+x &&
		p4 add text+x &&
		p4 submit -d "populate p4"
	)
'

setup_conflict() {
	# clone before modifying file1 to force it to conflict
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	# ticks outside subshells
	test_tick &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo $test_tick >>file1 &&
		p4 submit -d "$test_tick in file1"
	) &&
	test_tick &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		# easy conflict
		echo $test_tick >>file1 &&
		but add file1
		# caller will add more and submit
	)
}

test_expect_success 'cleanup edit after submit fail' '
	setup_conflict &&
	(
		cd "$but" &&
		echo another line >>text &&
		but add text &&
		but cummit -m "conflict" &&
		test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		# make sure it is not open
		! p4 fstat -T action text
	)
'

test_expect_success 'cleanup add after submit fail' '
	setup_conflict &&
	(
		cd "$but" &&
		echo new file >textnew &&
		but add textnew &&
		but cummit -m "conflict" &&
		test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		# make sure it is not there
		# and that p4 thinks it is not added
		#   P4 returns 0 both for "not there but added" and
		#   "not there", so grep.
		test_path_is_missing textnew &&
		p4 fstat -T action textnew 2>&1 | grep "no such file"
	)
'

test_expect_success 'cleanup delete after submit fail' '
	setup_conflict &&
	(
		cd "$but" &&
		but rm text+x &&
		but cummit -m "conflict" &&
		test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		# make sure it is there
		test_path_is_file text+x &&
		! p4 fstat -T action text+x
	)
'

test_expect_success 'cleanup copy after submit fail' '
	setup_conflict &&
	(
		cd "$but" &&
		cp text text2 &&
		but add text2 &&
		but cummit -m "conflict" &&
		but config but-p4.detectCopies true &&
		but config but-p4.detectCopiesHarder true &&
		# make sure setup is okay
		but diff-tree -r -C --find-copies-harder HEAD | grep text2 | grep C100 &&
		test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing text2 &&
		p4 fstat -T action text2 2>&1 | grep "no such file"
	)
'

test_expect_success 'cleanup rename after submit fail' '
	setup_conflict &&
	(
		cd "$but" &&
		but mv text text2 &&
		but cummit -m "conflict" &&
		but config but-p4.detectRenames true &&
		# make sure setup is okay
		but diff-tree -r -M HEAD | grep text2 | grep R100 &&
		test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing text2 &&
		p4 fstat -T action text2 2>&1 | grep "no such file"
	)
'

#
# Cleanup after deciding not to submit during editTemplate.  This
# involves unwinding more work, because files have been added, deleted
# and chmod-ed now.  Same approach as above.
#

test_expect_success 'cleanup edit after submit cancel' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo line >>text &&
		but add text &&
		but cummit -m text &&
		echo n | test_expect_code 1 but p4 submit &&
		but reset --hard HEAD^
	) &&
	(
		cd "$cli" &&
		! p4 fstat -T action text &&
		test_cmp "$but"/text text
	)
'

test_expect_success 'cleanup add after submit cancel' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		echo line >textnew &&
		but add textnew &&
		but cummit -m textnew &&
		echo n | test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing textnew &&
		p4 fstat -T action textnew 2>&1 | grep "no such file"
	)
'

test_expect_success 'cleanup delete after submit cancel' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but rm text &&
		but cummit -m "rm text" &&
		echo n | test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file text &&
		! p4 fstat -T action text
	)
'

test_expect_success 'cleanup copy after submit cancel' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		cp text text2 &&
		but add text2 &&
		but cummit -m text2 &&
		but config but-p4.detectCopies true &&
		but config but-p4.detectCopiesHarder true &&
		but diff-tree -r -C --find-copies-harder HEAD | grep text2 | grep C100 &&
		echo n | test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing text2 &&
		p4 fstat -T action text2 2>&1 | grep "no such file"
	)
'

test_expect_success 'cleanup rename after submit cancel' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but mv text text2 &&
		but cummit -m text2 &&
		but config but-p4.detectRenames true &&
		but diff-tree -r -M HEAD | grep text2 | grep R100 &&
		echo n | test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing text2 &&
		p4 fstat -T action text2 2>&1 | grep "no such file" &&
		test_path_is_file text &&
		! p4 fstat -T action text
	)
'

test_expect_success 'cleanup chmod after submit cancel' '
	test_when_finished cleanup_but &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		test_chmod +x text &&
		test_chmod -x text+x &&
		but add text text+x &&
		but cummit -m "chmod texts" &&
		echo n | test_expect_code 1 but p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file text &&
		! p4 fstat -T action text &&
		test_path_is_file text+x &&
		! p4 fstat -T action text+x &&
		ls -l text | egrep ^-r-- &&
		ls -l text+x | egrep ^-r-x
	)
'

test_done
