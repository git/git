#!/bin/sh

test_description='but merge

Testing merge when using a custom message for the merge cummit.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

create_merge_msgs() {
	echo >exp.subject "custom message"

	cp exp.subject exp.log &&
	echo >>exp.log "" &&
	echo >>exp.log "* tag 'c2':" &&
	echo >>exp.log "  c2"
}

test_expect_success 'setup' '
	echo c0 >c0.c &&
	but add c0.c &&
	but cummit -m c0 &&
	but tag c0 &&
	echo c1 >c1.c &&
	but add c1.c &&
	but cummit -m c1 &&
	but tag c1 &&
	but reset --hard c0 &&
	echo c2 >c2.c &&
	but add c2.c &&
	but cummit -m c2 &&
	but tag c2 &&
	create_merge_msgs
'


test_expect_success 'merge c2 with a custom message' '
	but reset --hard c1 &&
	but merge -m "$(cat exp.subject)" c2 &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp exp.subject actual
'

test_expect_success 'merge --log appends to custom message' '
	but reset --hard c1 &&
	but merge --log -m "$(cat exp.subject)" c2 &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp exp.log actual
'

mesg_with_comment_and_newlines='
# text

'

test_expect_success 'prepare file with comment line and trailing newlines'  '
	printf "%s" "$mesg_with_comment_and_newlines" >expect
'

test_expect_success 'cleanup cummit messages (verbatim option)' '
	but reset --hard c1 &&
	but merge --cleanup=verbatim -F expect c2 &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup cummit messages (whitespace option)' '
	but reset --hard c1 &&
	test_write_lines "" "# text" "" >text &&
	echo "# text" >expect &&
	but merge --cleanup=whitespace -F text c2 &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup merge messages (scissors option)' '
	but reset --hard c1 &&
	cat >text <<-\EOF &&

	# to be kept

	  # ------------------------ >8 ------------------------
	# to be kept, too
	# ------------------------ >8 ------------------------
	to be removed
	# ------------------------ >8 ------------------------
	to be removed, too
	EOF

	cat >expect <<-\EOF &&
	# to be kept

	  # ------------------------ >8 ------------------------
	# to be kept, too
	EOF
	but merge --cleanup=scissors -e -F text c2 &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup cummit messages (strip option)' '
	but reset --hard c1 &&
	test_write_lines "" "# text" "sample" "" >text &&
	echo sample >expect &&
	but merge --cleanup=strip -F text c2 &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_done
