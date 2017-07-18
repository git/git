#!/bin/sh

test_description='checkout -m -- <conflicted path>

Ensures that checkout -m on a resolved file restores the conflicted file'

. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	test_commit both.txt both.txt initial &&
	git branch topic &&
	test_commit modified_in_master both.txt in_master &&
	test_commit added_in_master each.txt in_master &&
	git checkout topic &&
	test_commit modified_in_topic both.txt in_topic &&
	test_commit added_in_topic each.txt in_topic
'

test_expect_success 'git merge master' '
    test_must_fail git merge master
'

clean_branchnames () {
	# Remove branch names after conflict lines
	sed 's/^\([<>]\{5,\}\) .*$/\1/'
}

test_expect_success '-m restores 2-way conflicted+resolved file' '
	cp each.txt each.txt.conflicted &&
	echo resolved >each.txt &&
	git add each.txt &&
	git checkout -m -- each.txt &&
	clean_branchnames <each.txt >each.txt.cleaned &&
	clean_branchnames <each.txt.conflicted >each.txt.conflicted.cleaned &&
	test_cmp each.txt.conflicted.cleaned each.txt.cleaned
'

test_expect_success '-m restores 3-way conflicted+resolved file' '
	cp both.txt both.txt.conflicted &&
	echo resolved >both.txt &&
	git add both.txt &&
	git checkout -m -- both.txt &&
	clean_branchnames <both.txt >both.txt.cleaned &&
	clean_branchnames <both.txt.conflicted >both.txt.conflicted.cleaned &&
	test_cmp both.txt.conflicted.cleaned both.txt.cleaned
'

test_done
