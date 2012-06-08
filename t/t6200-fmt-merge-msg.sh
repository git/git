#!/bin/sh
#
# Copyright (c) 2006, Junio C Hamano
#

test_description='fmt-merge-msg test'

. ./test-lib.sh

test_expect_success setup '
	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m "Initial" &&

	git clone . remote &&

	echo uno >one &&
	echo dos >two &&
	git add two &&
	test_tick &&
	git commit -a -m "Second" &&

	git checkout -b left &&

	echo "c1" >one &&
	test_tick &&
	git commit -a -m "Common #1" &&

	echo "c2" >one &&
	test_tick &&
	git commit -a -m "Common #2" &&

	git branch right &&

	echo "l3" >two &&
	test_tick &&
	GIT_COMMITTER_NAME="Another Committer" \
	GIT_AUTHOR_NAME="Another Author" git commit -a -m "Left #3" &&

	echo "l4" >two &&
	test_tick &&
	GIT_COMMITTER_NAME="Another Committer" \
	GIT_AUTHOR_NAME="Another Author" git commit -a -m "Left #4" &&

	echo "l5" >two &&
	test_tick &&
	GIT_COMMITTER_NAME="Another Committer" \
	GIT_AUTHOR_NAME="Another Author" git commit -a -m "Left #5" &&
	git tag tag-l5 &&

	git checkout right &&

	echo "r3" >three &&
	git add three &&
	test_tick &&
	git commit -a -m "Right #3" &&
	git tag tag-r3 &&

	echo "r4" >three &&
	test_tick &&
	git commit -a -m "Right #4" &&

	echo "r5" >three &&
	test_tick &&
	git commit -a -m "Right #5" &&

	git checkout -b long &&
	i=0 &&
	while test $i -lt 30
	do
		test_commit $i one &&
		i=$(($i+1))
	done &&

	git show-branch &&

	apos="'\''"
'

test_expect_success 'message for merging local branch' '
	echo "Merge branch ${apos}left${apos}" >expected &&

	git checkout master &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'message for merging external branch' '
	echo "Merge branch ${apos}left${apos} of $(pwd)" >expected &&

	git checkout master &&
	git fetch "$(pwd)" left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '[merge] summary/log configuration' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	git config merge.log true &&
	test_might_fail git config --unset-all merge.summary &&

	git checkout master &&
	test_tick &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual1 &&

	test_might_fail git config --unset-all merge.log &&
	git config merge.summary true &&

	git checkout master &&
	test_tick &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual2 &&

	test_cmp expected actual1 &&
	test_cmp expected actual2
'

test_expect_success 'setup: clear [merge] configuration' '
	test_might_fail git config --unset-all merge.log &&
	test_might_fail git config --unset-all merge.summary
'

test_expect_success 'setup FETCH_HEAD' '
	git checkout master &&
	test_tick &&
	git fetch . left
'

test_expect_success 'merge.log=3 limits shortlog length' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left: (5 commits)
	  Left #5
	  Left #4
	  Left #3
	  ...
	EOF

	git -c merge.log=3 fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge.log=5 shows all 5 commits' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	git -c merge.log=5 fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge.log=0 disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected
	git -c merge.log=0 fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=3 limits shortlog length' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left: (5 commits)
	  Left #5
	  Left #4
	  Left #3
	  ...
	EOF

	git fmt-merge-msg --log=3 <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=5 shows all 5 commits' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	git fmt-merge-msg --log=5 <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--no-log disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	git fmt-merge-msg --no-log <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=0 disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	git fmt-merge-msg --no-log <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'fmt-merge-msg -m' '
	echo "Sync with left" >expected &&
	cat >expected.log <<-EOF &&
	Sync with left

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* ${apos}left${apos} of $(pwd):
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_might_fail git config --unset merge.log &&
	test_might_fail git config --unset merge.summary &&
	git checkout master &&
	git fetch "$(pwd)" left &&
	git fmt-merge-msg -m "Sync with left" <.git/FETCH_HEAD >actual &&
	git fmt-merge-msg --log -m "Sync with left" \
					<.git/FETCH_HEAD >actual.log &&
	git config merge.log true &&
	git fmt-merge-msg -m "Sync with left" \
					<.git/FETCH_HEAD >actual.log-config &&
	git fmt-merge-msg --no-log -m "Sync with left" \
					<.git/FETCH_HEAD >actual.nolog &&

	test_cmp expected actual &&
	test_cmp expected.log actual.log &&
	test_cmp expected.log actual.log-config &&
	test_cmp expected actual.nolog
'

test_expect_success 'setup: expected shortlog for two branches' '
	cat >expected <<-EOF
	Merge branches ${apos}left${apos} and ${apos}right${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1

	* right:
	  Right #5
	  Right #4
	  Right #3
	  Common #2
	  Common #1
	EOF
'

test_expect_success 'shortlog for two branches' '
	git config merge.log true &&
	test_might_fail git config --unset-all merge.summary &&
	git checkout master &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual1 &&

	test_might_fail git config --unset-all merge.log &&
	git config merge.summary true &&
	git checkout master &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual2 &&

	git config merge.log yes &&
	test_might_fail git config --unset-all merge.summary &&
	git checkout master &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual3 &&

	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&
	git checkout master &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual4 &&

	test_cmp expected actual1 &&
	test_cmp expected actual2 &&
	test_cmp expected actual3 &&
	test_cmp expected actual4
'

test_expect_success 'merge-msg -F' '
	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&
	git checkout master &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg -F .git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg -F in subdirectory' '
	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&
	git checkout master &&
	test_tick &&
	git fetch . left right &&
	mkdir sub &&
	cp .git/FETCH_HEAD sub/FETCH_HEAD &&
	(
		cd sub &&
		git fmt-merge-msg -F FETCH_HEAD >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'merge-msg with nothing to merge' '
	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&

	>empty &&

	(
		cd remote &&
		git checkout -b unrelated &&
		test_tick &&
		git fetch origin &&
		git fmt-merge-msg <.git/FETCH_HEAD >../actual
	) &&

	test_cmp empty actual
'

test_expect_success 'merge-msg tag' '
	cat >expected <<-EOF &&
	Merge tag ${apos}tag-r3${apos}

	* tag ${apos}tag-r3${apos}:
	  Right #3
	  Common #2
	  Common #1
	EOF

	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&

	git checkout master &&
	test_tick &&
	git fetch . tag tag-r3 &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg two tags' '
	cat >expected <<-EOF &&
	Merge tags ${apos}tag-r3${apos} and ${apos}tag-l5${apos}

	* tag ${apos}tag-r3${apos}:
	  Right #3
	  Common #2
	  Common #1

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* tag ${apos}tag-l5${apos}:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&

	git checkout master &&
	test_tick &&
	git fetch . tag tag-r3 tag tag-l5 &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg tag and branch' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}, tag ${apos}tag-r3${apos}

	* tag ${apos}tag-r3${apos}:
	  Right #3
	  Common #2
	  Common #1

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_might_fail git config --unset-all merge.log &&
	git config merge.summary yes &&

	git checkout master &&
	test_tick &&
	git fetch . tag tag-r3 left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg lots of commits' '
	{
		cat <<-EOF &&
		Merge branch ${apos}long${apos}

		* long: (35 commits)
		EOF

		i=29 &&
		while test $i -gt 9
		do
			echo "  $i" &&
			i=$(($i-1))
		done &&
		echo "  ..."
	} >expected &&

	git checkout master &&
	test_tick &&
	git fetch . long &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_done
