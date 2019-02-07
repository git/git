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

mesg_with_comment_and_newlines='
# text

'

test_expect_success 'prepare file with comment line and trailing newlines'  '
	printf "%s" "$mesg_with_comment_and_newlines" >expect
'

test_expect_success 'cleanup commit messages (verbatim option)' '
	git reset --hard c1 &&
	git merge --cleanup=verbatim -F expect c2 &&
	git cat-file -p HEAD |sed -e "1,/^\$/d" >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup commit messages (whitespace option)' '
	git reset --hard c1 &&
	{ echo;echo "# text";echo; } >text &&
	echo "# text" >expect &&
	git merge --cleanup=whitespace -F text c2 &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup merge messages (scissors option)' '
	git reset --hard c1 &&
	cat >text <<EOF &&

# to be kept

  # ------------------------ >8 ------------------------
# to be kept, too
# ------------------------ >8 ------------------------
to be removed
# ------------------------ >8 ------------------------
to be removed, too
EOF

	cat >expect <<EOF &&
# to be kept

  # ------------------------ >8 ------------------------
# to be kept, too
EOF
	git merge --cleanup=scissors -e -F text c2 &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup commit messages (strip option)' '
	git reset --hard c1 &&
	{ echo;echo "# text";echo sample;echo; } >text &&
	echo sample >expect &&
	git merge --cleanup=strip -F text c2 &&
	git cat-file -p HEAD |sed -e "1,/^\$/d">actual &&
	test_cmp expect actual

'

test_done
