#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='per path merge controlled by merge attribute'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	for f in text binary union
	do
		echo Initial >$f && git add $f || return 1
	done &&
	test_tick &&
	git commit -m Initial &&

	git branch side &&
	for f in text binary union
	do
		echo Main >>$f && git add $f || return 1
	done &&
	test_tick &&
	git commit -m Main &&

	git checkout side &&
	for f in text binary union
	do
		echo Side >>$f && git add $f || return 1
	done &&
	test_tick &&
	git commit -m Side &&

	git tag anchor &&

	cat >./custom-merge <<-\EOF &&
	#!/bin/sh

	orig="$1" ours="$2" theirs="$3" exit="$4" path=$5
	orig_name="$6" our_name="$7" their_name="$8"
	(
		echo "orig is $orig"
		echo "ours is $ours"
		echo "theirs is $theirs"
		echo "path is $path"
		echo "orig_name is $orig_name"
		echo "our_name is $our_name"
		echo "their_name is $their_name"
		echo "=== orig ==="
		cat "$orig"
		echo "=== ours ==="
		cat "$ours"
		echo "=== theirs ==="
		cat "$theirs"
	) >"$ours+"
	cat "$ours+" >"$ours"
	rm -f "$ours+"

	if test -f ./please-abort
	then
		echo >>./please-abort killing myself
		kill -9 $$
	fi
	exit "$exit"
	EOF
	chmod +x ./custom-merge
'

test_expect_success merge '

	cat >.gitattributes <<-\EOF &&
	binary -merge
	union merge=union
	EOF

	if git merge main
	then
		echo Gaah, should have conflicted
		false
	else
		echo Ok, conflicted.
	fi
'

test_expect_success 'check merge result in index' '

	git ls-files -u | grep binary &&
	git ls-files -u | grep text &&
	! (git ls-files -u | grep union)

'

test_expect_success 'check merge result in working tree' '

	git cat-file -p HEAD:binary >binary-orig &&
	grep "<<<<<<<" text &&
	cmp binary-orig binary &&
	! grep "<<<<<<<" union &&
	grep Main union &&
	grep Side union

'

test_expect_success 'retry the merge with longer context' '
	echo text conflict-marker-size=32 >>.gitattributes &&
	git checkout -m text &&
	sed -ne "/^\([<=>]\)\1\1\1*/{
		s/ .*$//
		p
	}" >actual text &&
	grep ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" actual &&
	grep "================================" actual &&
	grep "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" actual
'

test_expect_success 'invalid conflict-marker-size 3a' '
	cp .gitattributes .gitattributes.bak &&
	echo "text conflict-marker-size=3a" >>.gitattributes &&
	test_when_finished "mv .gitattributes.bak .gitattributes" &&
	git checkout -m text 2>err &&
	test_grep "warning: invalid marker-size ${SQ}3a${SQ}, expecting an integer" err
'

test_expect_success 'custom merge backend' '

	echo "* merge=union" >.gitattributes &&
	echo "text merge=custom" >>.gitattributes &&

	git reset --hard anchor &&
	git config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 0 %P %S %X %Y" &&
	git config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	git merge main &&

	cmp binary union &&
	sed -e 1,3d text >check-1 &&
	o=$(git unpack-file main^:text) &&
	a=$(git unpack-file side^:text) &&
	b=$(git unpack-file main:text) &&
	base_revid=$(git rev-parse --short main^) &&
	sh -c "./custom-merge $o $a $b 0 text $base_revid HEAD main" &&
	sed -e 1,3d $a >check-2 &&
	cmp check-1 check-2 &&
	rm -f $o $a $b
'

test_expect_success 'custom merge backend' '

	git reset --hard anchor &&
	git config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 1 %P %S %X %Y" &&
	git config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	if git merge main
	then
		echo "Eh? should have conflicted"
		false
	else
		echo "Ok, conflicted"
	fi &&

	cmp binary union &&
	sed -e 1,3d text >check-1 &&
	o=$(git unpack-file main^:text) &&
	a=$(git unpack-file anchor:text) &&
	b=$(git unpack-file main:text) &&
	base_revid=$(git rev-parse --short main^) &&
	sh -c "./custom-merge $o $a $b 0 text $base_revid HEAD main" &&
	sed -e 1,3d $a >check-2 &&
	cmp check-1 check-2 &&
	sed -e 1,3d -e 4q $a >check-3 &&
	echo "path is text" >expect &&
	cmp expect check-3 &&
	rm -f $o $a $b
'

test_expect_success !WINDOWS 'custom merge driver that is killed with a signal' '
	test_when_finished "rm -f output please-abort" &&

	git reset --hard anchor &&
	git config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 0 %P %S %X %Y" &&
	git config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	>./please-abort &&
	echo "* merge=custom" >.gitattributes &&
	test_expect_code 2 git merge main 2>err &&
	grep "^error: failed to execute internal merge" err &&
	git ls-files -u >output &&
	git diff --name-only HEAD >>output &&
	test_must_be_empty output
'

test_expect_success 'up-to-date merge without common ancestor' '
	git init repo1 &&
	git init repo2 &&
	test_tick &&
	(
		cd repo1 &&
		>a &&
		git add a &&
		git commit -m initial
	) &&
	test_tick &&
	(
		cd repo2 &&
		git commit --allow-empty -m initial
	) &&
	test_tick &&
	(
		cd repo1 &&
		git fetch ../repo2 main &&
		git merge --allow-unrelated-histories FETCH_HEAD
	)
'

test_expect_success 'custom merge does not lock index' '
	git reset --hard anchor &&
	write_script sleep-an-hour.sh <<-\EOF &&
		sleep 3600 &
		echo $! >sleep.pid
	EOF

	test_write_lines >.gitattributes \
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
	git merge main
'

test_expect_success 'binary files with union attribute' '
	git checkout -b bin-main &&
	printf "base\0" >bin.txt &&
	echo "bin.txt merge=union" >.gitattributes &&
	git add bin.txt .gitattributes &&
	git commit -m base &&

	printf "one\0" >bin.txt &&
	git commit -am one &&

	git checkout -b bin-side HEAD^ &&
	printf "two\0" >bin.txt &&
	git commit -am two &&

	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
	then
		test_must_fail git merge bin-main >output
	else
		test_must_fail git merge bin-main 2>output
	fi &&
	grep -i "warning.*cannot merge.*HEAD vs. bin-main" output
'

test_expect_success !WINDOWS 'custom merge driver that is killed with a signal on recursive merge' '
	test_when_finished "rm -f output please-abort" &&
	test_when_finished "git checkout side" &&

	git reset --hard anchor &&

	git checkout -b base-a main^ &&
	echo base-a >text &&
	git commit -m base-a text &&

	git checkout -b base-b main^ &&
	echo base-b >text &&
	git commit -m base-b text &&

	git checkout -b recursive-a base-a &&
	test_must_fail git merge base-b &&
	echo recursive-a >text &&
	git add text &&
	git commit -m recursive-a &&

	git checkout -b recursive-b base-b &&
	test_must_fail git merge base-a &&
	echo recursive-b >text &&
	git add text &&
	git commit -m recursive-b &&

	git config --replace-all \
	merge.custom.driver "./custom-merge %O %A %B 0 %P %S %X %Y" &&
	git config --replace-all \
	merge.custom.name "custom merge driver for testing" &&

	>./please-abort &&
	echo "* merge=custom" >.gitattributes &&
	test_expect_code 2 git merge recursive-a 2>err &&
	grep "error: failed to execute internal merge" err &&
	git ls-files -u >output &&
	git diff --name-only HEAD >>output &&
	test_must_be_empty output
'

test_done
