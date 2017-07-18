#!/bin/sh

test_description='git merge

Testing merge when using a custom message for the merge commit.'

. ./test-lib.sh

create_merge_msgs() {
	echo >exp.subject "custom message"

	cp exp.subject exp.log &&
	echo >>exp.log "" &&
	echo >>exp.log "* tag 'c2':" &&
	echo >>exp.log "  c2"
}

test_expect_success 'setup' '
	echo c0 > c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 > c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 > c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2 &&
	create_merge_msgs
'


test_expect_success 'merge c2 with a custom message' '
	git reset --hard c1 &&
	git merge -m "$(cat exp.subject)" c2 &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	test_cmp exp.subject actual
'

test_expect_success 'merge --log appends to custom message' '
	git reset --hard c1 &&
	git merge --log -m "$(cat exp.subject)" c2 &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	test_cmp exp.log actual
'

test_done
