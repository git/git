#!/bin/sh

test_description='merge-recursive space options

* [main] Clarify
 ! [remote] Remove cruft
--
 + [remote] Remove cruft
*  [main] Clarify
*+ [remote^] Initial revision
*   ok 1: setup
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_have_prereq SED_STRIPS_CR && SED_OPTIONS=-b
if test_have_prereq GREP_STRIPS_CR
then
	GREP_OPTIONS=-U
	export GREP_OPTIONS
fi

test_expect_success 'setup' '
	conflict_hunks () {
		sed $SED_OPTIONS -n -e "
			/^<<<</ b conflict
			b
			: conflict
			p
			/^>>>>/ b
			n
			b conflict
		" "$@"
	} &&

	cat <<-\EOF >text.txt &&
	    Hope, he says, cherishes the soul of him who lives in
	    justice and holiness and is the nurse of his age and the
	    companion of his journey;--hope which is mightiest to sway
	    the restless soul of man.

	How admirable are his words!  And the great blessing of riches, I do
	not say to every man, but to a good man, is, that he has had no
	occasion to deceive or to defraud others, either intentionally or
	unintentionally; and when he departs to the world below he is not in
	any apprehension about offerings due to the gods or debts which he owes
	to men.  Now to this peace of mind the possession of wealth greatly
	contributes; and therefore I say, that, setting one thing against
	another, of the many advantages which wealth has to give, to a man of
	sense this is in my opinion the greatest.

	Well said, Cephalus, I replied; but as concerning justice, what is
	it?--to speak the truth and to pay your debts--no more than this?  And
	even to this are there not exceptions?  Suppose that a friend when in
	his right mind has deposited arms with me and he asks for them when he
	is not in his right mind, ought I to give them back to him?  No one
	would say that I ought or that I should be right in doing so, any more
	than they would say that I ought always to speak the truth to one who
	is in his condition.

	You are quite right, he replied.

	But then, I said, speaking the truth and paying your debts is not a
	correct definition of justice.

	CEPHALUS - SOCRATES - POLEMARCHUS

	Quite correct, Socrates, if Simonides is to be believed, said
	Polemarchus interposing.

	I fear, said Cephalus, that I must go now, for I have to look after the
	sacrifices, and I hand over the argument to Polemarchus and the company.
	EOF
	git add text.txt &&
	test_tick &&
	git commit -m "Initial revision" &&

	git checkout -b remote &&
	sed -e "
			s/\.  /\. /g
			s/[?]  /? /g
			s/    /	/g
			s/--/---/g
			s/but as concerning/but as con cerning/
			/CEPHALUS - SOCRATES - POLEMARCHUS/ d
		" text.txt >text.txt+ &&
	mv text.txt+ text.txt &&
	git commit -a -m "Remove cruft" &&

	git checkout main &&
	sed -e "
			s/\(not in his right mind\),\(.*\)/\1;\2Q/
			s/Quite correct\(.*\)/It is too correct\1Q/
			s/unintentionally/un intentionally/
			/un intentionally/ s/$/Q/
			s/Polemarchus interposing./Polemarchus, interposing.Q/
			/justice and holiness/ s/$/Q/
			/pay your debts/ s/$/Q/
		" text.txt | q_to_cr >text.txt+ &&
	mv text.txt+ text.txt &&
	git commit -a -m "Clarify" &&
	git show-branch --all
'

test_expect_success 'naive merge fails' '
	git read-tree --reset -u HEAD &&
	test_must_fail git merge-recursive HEAD^ -- HEAD remote &&
	test_must_fail git update-index --refresh &&
	grep "<<<<<<" text.txt
'

test_expect_success '--ignore-space-change makes merge succeed' '
	git read-tree --reset -u HEAD &&
	git merge-recursive --ignore-space-change HEAD^ -- HEAD remote
'

test_expect_success 'naive cherry-pick fails' '
	git read-tree --reset -u HEAD &&
	test_must_fail git cherry-pick --no-commit remote &&
	git read-tree --reset -u HEAD &&
	test_must_fail git cherry-pick remote &&
	test_must_fail git update-index --refresh &&
	grep "<<<<<<" text.txt
'

test_expect_success '-Xignore-space-change makes cherry-pick succeed' '
	git read-tree --reset -u HEAD &&
	git cherry-pick --no-commit -Xignore-space-change remote
'

test_expect_success '--ignore-space-change: our w/s-only change wins' '
	q_to_cr <<-\EOF >expected &&
	    justice and holiness and is the nurse of his age and theQ
	EOF

	git read-tree --reset -u HEAD &&
	git merge-recursive --ignore-space-change HEAD^ -- HEAD remote &&
	grep "justice and holiness" text.txt >actual &&
	test_cmp expected actual
'

test_expect_success '--ignore-space-change: their real change wins over w/s' '
	cat <<-\EOF >expected &&
	it?---to speak the truth and to pay your debts---no more than this? And
	EOF

	git read-tree --reset -u HEAD &&
	git merge-recursive --ignore-space-change HEAD^ -- HEAD remote &&
	grep "pay your debts" text.txt >actual &&
	test_cmp expected actual
'

test_expect_success '--ignore-space-change: does not ignore new spaces' '
	cat <<-\EOF >expected1 &&
	Well said, Cephalus, I replied; but as con cerning justice, what is
	EOF
	q_to_cr <<-\EOF >expected2 &&
	un intentionally; and when he departs to the world below he is not inQ
	EOF

	git read-tree --reset -u HEAD &&
	git merge-recursive --ignore-space-change HEAD^ -- HEAD remote &&
	grep "Well said" text.txt >actual1 &&
	grep "when he departs" text.txt >actual2 &&
	test_cmp expected1 actual1 &&
	test_cmp expected2 actual2
'

test_expect_success '--ignore-all-space drops their new spaces' '
	cat <<-\EOF >expected &&
	Well said, Cephalus, I replied; but as concerning justice, what is
	EOF

	git read-tree --reset -u HEAD &&
	git merge-recursive --ignore-all-space HEAD^ -- HEAD remote &&
	grep "Well said" text.txt >actual &&
	test_cmp expected actual
'

test_expect_success '--ignore-all-space keeps our new spaces' '
	q_to_cr <<-\EOF >expected &&
	un intentionally; and when he departs to the world below he is not inQ
	EOF

	git read-tree --reset -u HEAD &&
	git merge-recursive --ignore-all-space HEAD^ -- HEAD remote &&
	grep "when he departs" text.txt >actual &&
	test_cmp expected actual
'

test_expect_success '--ignore-space-at-eol' '
	q_to_cr <<-\EOF >expected &&
	<<<<<<< HEAD
	is not in his right mind; ought I to give them back to him?  No oneQ
	=======
	is not in his right mind, ought I to give them back to him? No one
	>>>>>>> remote
	EOF

	git read-tree --reset -u HEAD &&
	test_must_fail git merge-recursive --ignore-space-at-eol \
						 HEAD^ -- HEAD remote &&
	conflict_hunks text.txt >actual &&
	test_cmp expected actual
'

test_done
