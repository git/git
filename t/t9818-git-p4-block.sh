#!/bin/sh

test_description='git p4 fetching changes in multiple blocks'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'Create a repo with ~100 changes' '
	(
		cd "$cli" &&
		>file.txt &&
		p4 add file.txt &&
		p4 submit -d "Add file.txt" &&
		for i in $(test_seq 0 9)
		do
			>outer$i.txt &&
			p4 add outer$i.txt &&
			p4 submit -d "Adding outer$i.txt" &&
			for j in $(test_seq 0 9)
			do
				p4 edit file.txt &&
				echo $i$j >file.txt &&
				p4 submit -d "Commit $i$j" || exit
			done || exit
		done
	)
'

test_expect_success 'Clone the repo' '
	git p4 clone --dest="$git" --changes-block-size=10 --verbose //depot@all
'

test_expect_success 'All files are present' '
	echo file.txt >expected &&
	test_write_lines outer0.txt outer1.txt outer2.txt outer3.txt outer4.txt >>expected &&
	test_write_lines outer5.txt outer6.txt outer7.txt outer8.txt outer9.txt >>expected &&
	ls "$git" >current &&
	test_cmp expected current
'

test_expect_success 'file.txt is correct' '
	echo 99 >expected &&
	test_cmp expected "$git/file.txt"
'

test_expect_success 'Correct number of commits' '
	(cd "$git" && git log --oneline) >log &&
	test_line_count = 111 log
'

test_expect_success 'Previous version of file.txt is correct' '
	(cd "$git" && git checkout HEAD^^) &&
	echo 97 >expected &&
	test_cmp expected "$git/file.txt"
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
