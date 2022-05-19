#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='per path merge controlled by merge attribute'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	for f in text binary union
	do
		echo Initial >$f && but add $f || return 1
	done &&
	test_tick &&
	but cummit -m Initial &&

	but branch side &&
	for f in text binary union
	do
		echo Main >>$f && but add $f || return 1
	done &&
	test_tick &&
	but cummit -m Main &&

	but checkout side &&
	for f in text binary union
	do
		echo Side >>$f && but add $f || return 1
	done &&
	test_tick &&
	but cummit -m Side &&

	but tag anchor &&

	cat >./custom-merge <<-\EOF &&
	#!/bin/sh

	orig="$1" ours="$2" theirs="$3" exit="$4" path=$5
	(
		echo "orig is $orig"
		echo "ours is $ours"
		echo "theirs is $theirs"
		echo "path is $path"
		echo "=== orig ==="
		cat "$orig"
		echo "=== ours ==="
		cat "$ours"
		echo "=== theirs ==="
		cat "$theirs"
	) >"$ours+"
	cat "$ours+" >"$ours"
	rm -f "$ours+"
	exit "$exit"
	EOF
	chmod +x ./custom-merge
'

test_expect_success merge '

	cat >.butattributes <<-\EOF &&
	binary -merge
	union merge=union
	EOF

	if but merge main
	then
		echo Gaah, should have conflicted
		false
	else
		echo Ok, conflicted.
	fi
'

test_expect_success 'check merge result in index' '

	but ls-files -u | grep binary &&
	but ls-files -u | grep text &&
	! (but ls-files -u | grep union)

'

test_expect_success 'check merge result in working tree' '

	but cat-file -p HEAD:binary >binary-orig &&
	grep "<<<<<<<" text &&
	cmp binary-orig binary &&
	! grep "<<<<<<<" union &&
	grep Main union &&
	grep Side union

'

test_expect_success 'retry the merge with longer context' '
	echo text conflict-marker-size=32 >>.butattributes &&
	but checkout -m text &&
	sed -ne "/^\([<=>]\)\1\1\1*/{
		s/ .*$//
		p
	}" >actual text &&
	grep ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" actual &&
	grep "================================" actual &&
	grep "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" actual
'

test_expect_success 'custom merge backend' '

	echo "* merge=union" >.butattributes &&
	echo "text merge=custom" >>.butattributes &&

	but reset --hard anchor &&
	but config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 0 %P" &&
	but config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	but merge main &&

	cmp binary union &&
	sed -e 1,3d text >check-1 &&
	o=$(but unpack-file main^:text) &&
	a=$(but unpack-file side^:text) &&
	b=$(but unpack-file main:text) &&
	sh -c "./custom-merge $o $a $b 0 text" &&
	sed -e 1,3d $a >check-2 &&
	cmp check-1 check-2 &&
	rm -f $o $a $b
'

test_expect_success 'custom merge backend' '

	but reset --hard anchor &&
	but config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 1 %P" &&
	but config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	if but merge main
	then
		echo "Eh? should have conflicted"
		false
	else
		echo "Ok, conflicted"
	fi &&

	cmp binary union &&
	sed -e 1,3d text >check-1 &&
	o=$(but unpack-file main^:text) &&
	a=$(but unpack-file anchor:text) &&
	b=$(but unpack-file main:text) &&
	sh -c "./custom-merge $o $a $b 0 text" &&
	sed -e 1,3d $a >check-2 &&
	cmp check-1 check-2 &&
	sed -e 1,3d -e 4q $a >check-3 &&
	echo "path is text" >expect &&
	cmp expect check-3 &&
	rm -f $o $a $b
'

test_expect_success 'up-to-date merge without common ancestor' '
	test_create_repo repo1 &&
	test_create_repo repo2 &&
	test_tick &&
	(
		cd repo1 &&
		>a &&
		but add a &&
		but cummit -m initial
	) &&
	test_tick &&
	(
		cd repo2 &&
		but cummit --allow-empty -m initial
	) &&
	test_tick &&
	(
		cd repo1 &&
		but fetch ../repo2 main &&
		but merge --allow-unrelated-histories FETCH_HEAD
	)
'

test_expect_success 'custom merge does not lock index' '
	but reset --hard anchor &&
	write_script sleep-an-hour.sh <<-\EOF &&
		sleep 3600 &
		echo $! >sleep.pid
	EOF

	test_write_lines >.butattributes \
		"* merge=ours" "text merge=sleep-an-hour" &&
	test_config merge.ours.driver true &&
	test_config merge.sleep-an-hour.driver ./sleep-an-hour.sh &&

	# We are testing that the custom merge driver does not block
	# index.lock on Windows due to an inherited file handle.
	# To ensure that the backgrounded process ran sufficiently
	# long (and has been started in the first place), we do not
	# ignore the result of the kill command.
	# By packaging the command in test_when_finished, we get both
	# the correctness check and the clean-up.
	test_when_finished "kill \$(cat sleep.pid)" &&
	but merge main
'

test_expect_success 'binary files with union attribute' '
	but checkout -b bin-main &&
	printf "base\0" >bin.txt &&
	echo "bin.txt merge=union" >.butattributes &&
	but add bin.txt .butattributes &&
	but cummit -m base &&

	printf "one\0" >bin.txt &&
	but cummit -am one &&

	but checkout -b bin-side HEAD^ &&
	printf "two\0" >bin.txt &&
	but cummit -am two &&

	if test "$BUT_TEST_MERGE_ALGORITHM" = ort
	then
		test_must_fail but merge bin-main >output
	else
		test_must_fail but merge bin-main 2>output
	fi &&
	grep -i "warning.*cannot merge.*HEAD vs. bin-main" output
'

test_done
