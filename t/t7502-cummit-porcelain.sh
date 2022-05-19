#!/bin/sh

test_description='but cummit porcelain-ish'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cummit_msg_is () {
	expect=cummit_msg_is.expect
	actual=cummit_msg_is.actual

	printf "%s" "$(but log --pretty=format:%s%b -1)" >$actual &&
	printf "%s" "$1" >$expect &&
	test_cmp $expect $actual
}

# Arguments: [<prefix] [<cummit message>] [<cummit options>]
check_summary_oneline() {
	test_tick &&
	but cummit ${3+"$3"} -m "$2" >raw &&
	head -n 1 raw >act &&

	# branch name
	SUMMARY_PREFIX="$(but name-rev --name-only HEAD)" &&

	# append the "special" prefix, like "root-cummit", "detached HEAD"
	if test -n "$1"
	then
		SUMMARY_PREFIX="$SUMMARY_PREFIX ($1)"
	fi

	# abbrev SHA-1
	SUMMARY_POSTFIX="$(but log -1 --pretty='format:%h')"
	echo "[$SUMMARY_PREFIX $SUMMARY_POSTFIX] $2" >exp &&

	test_cmp exp act
}

trailer_cummit_base () {
	echo "fun" >>file &&
	but add file &&
	but cummit -s --trailer "Signed-off-by=C1 E1 " \
		--trailer "Helped-by:C2 E2 " \
		--trailer "Reported-by=C3 E3" \
		--trailer "Mentored-by:C4 E4" \
		-m "hello"
}

test_expect_success 'output summary format' '

	echo new >file1 &&
	but add file1 &&
	check_summary_oneline "root-cummit" "initial" &&

	echo change >>file1 &&
	but add file1
'

test_expect_success 'output summary format: root-cummit' '
	check_summary_oneline "" "a change"
'

test_expect_success 'output summary format for cummit with an empty diff' '

	check_summary_oneline "" "empty" "--allow-empty"
'

test_expect_success 'output summary format for merges' '

	but checkout -b recursive-base &&
	test_cummit base file1 &&

	but checkout -b recursive-a recursive-base &&
	test_cummit cummit-a file1 &&

	but checkout -b recursive-b recursive-base &&
	test_cummit cummit-b file1 &&

	# conflict
	but checkout recursive-a &&
	test_must_fail but merge recursive-b &&
	# resolve the conflict
	echo cummit-a >file1 &&
	but add file1 &&
	check_summary_oneline "" "Merge"
'

output_tests_cleanup() {
	# this is needed for "do not fire editor in the presence of conflicts"
	but checkout main &&

	# this is needed for the "partial removal" test to pass
	but rm file1 &&
	but cummit -m "cleanup"
}

test_expect_success 'the basics' '

	output_tests_cleanup &&

	echo doing partial >"cummit is" &&
	mkdir not &&
	echo very much encouraged but we should >not/forbid &&
	but add "cummit is" not &&
	echo update added "cummit is" file >"cummit is" &&
	echo also update another >not/forbid &&
	test_tick &&
	but cummit -a -m "initial with -a" &&

	but cat-file blob HEAD:"cummit is" >current.1 &&
	but cat-file blob HEAD:not/forbid >current.2 &&

	cmp current.1 "cummit is" &&
	cmp current.2 not/forbid

'

test_expect_success 'partial' '

	echo another >"cummit is" &&
	echo another >not/forbid &&
	test_tick &&
	but cummit -m "partial cummit to handle a file" "cummit is" &&

	changed=$(but diff-tree --name-only HEAD^ HEAD) &&
	test "$changed" = "cummit is"

'

test_expect_success 'partial modification in a subdirectory' '

	test_tick &&
	but cummit -m "partial cummit to subdirectory" not &&

	changed=$(but diff-tree -r --name-only HEAD^ HEAD) &&
	test "$changed" = "not/forbid"

'

test_expect_success 'partial removal' '

	but rm not/forbid &&
	but cummit -m "partial cummit to remove not/forbid" not &&

	changed=$(but diff-tree -r --name-only HEAD^ HEAD) &&
	test "$changed" = "not/forbid" &&
	remain=$(but ls-tree -r --name-only HEAD) &&
	test "$remain" = "cummit is"

'

test_expect_success 'sign off' '

	>positive &&
	but add positive &&
	but cummit -s -m "thank you" &&
	but cat-file commit HEAD >cummit.msg &&
	sed -ne "s/Signed-off-by: //p" cummit.msg >actual &&
	but var GIT_CUMMITTER_IDENT >ident &&
	sed -e "s/>.*/>/" ident >expected &&
	test_cmp expected actual

'

test_expect_success 'cummit --trailer with "="' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "replace" as ifexists' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Helped-by: C3 E3
	EOF
	but -c trailer.ifexists="replace" \
		cummit --trailer "Mentored-by: C4 E4" \
		 --trailer "Helped-by: C3 E3" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d"  cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "add" as ifexists' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	but -c trailer.ifexists="add" \
		cummit --trailer "Reported-by: C3 E3" \
		--trailer "Mentored-by: C4 E4" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d"  cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "donothing" as ifexists' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reviewed-by: C6 E6
	EOF
	but -c trailer.ifexists="donothing" \
		cummit --trailer "Mentored-by: C5 E5" \
		--trailer "Reviewed-by: C6 E6" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d"  cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "addIfDifferent" as ifexists' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Mentored-by: C5 E5
	EOF
	but -c trailer.ifexists="addIfDifferent" \
		cummit --trailer "Reported-by: C3 E3" \
		--trailer "Mentored-by: C5 E5" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d"  cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "addIfDifferentNeighbor" as ifexists' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reported-by: C3 E3
	EOF
	but -c trailer.ifexists="addIfDifferentNeighbor" \
		cummit --trailer "Mentored-by: C4 E4" \
		--trailer "Reported-by: C3 E3" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d"  cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "end" as where' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	but -c trailer.where="end" \
		cummit --trailer "Reported-by: C3 E3" \
		--trailer "Mentored-by: C4 E4" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "start" as where' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C1 E1
	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	EOF
	but -c trailer.where="start" \
		cummit --trailer "Signed-off-by: C O Mitter <cummitter@example.com>" \
		--trailer "Signed-off-by: C1 E1" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "after" as where' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Mentored-by: C5 E5
	EOF
	but -c trailer.where="after" \
		cummit --trailer "Mentored-by: C4 E4" \
		--trailer "Mentored-by: C5 E5" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "before" as where' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C2 E2
	Mentored-by: C3 E3
	Mentored-by: C4 E4
	EOF
	but -c trailer.where="before" \
		cummit --trailer "Mentored-by: C3 E3" \
		--trailer "Mentored-by: C2 E2" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "donothing" as ifmissing' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Helped-by: C5 E5
	EOF
	but -c trailer.ifmissing="donothing" \
		cummit --trailer "Helped-by: C5 E5" \
		--trailer "Based-by: C6 E6" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and "add" as ifmissing' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Reported-by: C3 E3
	Mentored-by: C4 E4
	Helped-by: C5 E5
	Based-by: C6 E6
	EOF
	but -c trailer.ifmissing="add" \
		cummit --trailer "Helped-by: C5 E5" \
		--trailer "Based-by: C6 E6" \
		--amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c ack.key ' '
	echo "fun" >>file1 &&
	but add file1 &&
	cat >expected <<-\EOF &&
		hello

		Acked-by: Peff
	EOF
	but -c trailer.ack.key="Acked-by" \
		cummit --trailer "ack = Peff" -m "hello" &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and ":=#" as separators' '
	echo "fun" >>file1 &&
	but add file1 &&
	cat >expected <<-\EOF &&
		I hate bug

		Bug #42
	EOF
	but -c trailer.separators=":=#" \
		-c trailer.bug.key="Bug #" \
		cummit --trailer "bug = 42" -m "I hate bug" &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --trailer with -c and command' '
	trailer_cummit_base &&
	cat >expected <<-\EOF &&
	hello

	Signed-off-by: C O Mitter <cummitter@example.com>
	Signed-off-by: C1 E1
	Helped-by: C2 E2
	Mentored-by: C4 E4
	Reported-by: A U Thor <author@example.com>
	EOF
	but -c trailer.report.key="Reported-by: " \
		-c trailer.report.ifexists="replace" \
		-c trailer.report.command="NAME=\"\$ARG\"; test -n \"\$NAME\" && \
		but log --author=\"\$NAME\" -1 --format=\"format:%aN <%aE>\" || true" \
		cummit --trailer "report = author" --amend &&
	but cat-file commit HEAD >cummit.msg &&
	sed -e "1,/^\$/d" cummit.msg >actual &&
	test_cmp expected actual
'

test_expect_success 'multiple -m' '

	>negative &&
	but add negative &&
	but cummit -m "one" -m "two" -m "three" &&
	actual=$(but cat-file commit HEAD >tmp && sed -e "1,/^\$/d" tmp && rm tmp) &&
	expected=$(test_write_lines "one" "" "two" "" "three") &&
	test "z$actual" = "z$expected"

'

test_expect_success 'verbose' '

	echo minus >negative &&
	but add negative &&
	but status -v >raw &&
	sed -ne "/^diff --but /p" raw >actual &&
	echo "diff --but a/negative b/negative" >expect &&
	test_cmp expect actual

'

test_expect_success 'verbose respects diff config' '

	test_config diff.noprefix true &&
	but status -v >actual &&
	grep "diff --but negative negative" actual
'

mesg_with_comment_and_newlines='
# text

'

test_expect_success 'prepare file with comment line and trailing newlines'  '
	printf "%s" "$mesg_with_comment_and_newlines" >expect
'

test_expect_success 'cleanup cummit messages (verbatim option,-t)' '

	echo >>negative &&
	but cummit --cleanup=verbatim --no-status -t expect -a &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup cummit messages (verbatim option,-F)' '

	echo >>negative &&
	but cummit --cleanup=verbatim -F expect -a &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup cummit messages (verbatim option,-m)' '

	echo >>negative &&
	but cummit --cleanup=verbatim -m "$mesg_with_comment_and_newlines" -a &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup cummit messages (whitespace option,-F)' '

	echo >>negative &&
	test_write_lines "" "# text" "" >text &&
	echo "# text" >expect &&
	but cummit --cleanup=whitespace -F text -a &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup cummit messages (scissors option,-F,-e)' '

	echo >>negative &&
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
	but cummit --cleanup=scissors -e -F text -a &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'cleanup cummit messages (scissors option,-F,-e, scissors on first line)' '

	echo >>negative &&
	cat >text <<-\EOF &&
	# ------------------------ >8 ------------------------
	to be removed
	EOF
	but cummit --cleanup=scissors -e -F text -a --allow-empty-message &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_must_be_empty actual
'

test_expect_success 'cleanup cummit messages (strip option,-F)' '

	echo >>negative &&
	test_write_lines "" "# text" "sample" "" >text &&
	echo sample >expect &&
	but cummit --cleanup=strip -F text -a &&
	but cat-file -p HEAD >raw &&
	sed -e "1,/^\$/d" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'cleanup cummit messages (strip option,-F,-e)' '

	echo >>negative &&
	test_write_lines "" "sample" "" >text &&
	but cummit -e -F text -a &&
	head -n 4 .but/CUMMIT_EDITMSG >actual
'

echo "sample

# Please enter the cummit message for your changes. Lines starting
# with '#' will be ignored, and an empty message aborts the cummit." >expect

test_expect_success 'cleanup cummit messages (strip option,-F,-e): output' '
	test_cmp expect actual
'

test_expect_success 'cleanup cummit message (fail on invalid cleanup mode option)' '
	test_must_fail but cummit --cleanup=non-existent
'

test_expect_success 'cleanup cummit message (fail on invalid cleanup mode configuration)' '
	test_must_fail but -c cummit.cleanup=non-existent cummit
'

test_expect_success 'cleanup cummit message (no config and no option uses default)' '
	echo content >>file &&
	but add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  but cummit --no-status
	) &&
	cummit_msg_is "cummit message"
'

test_expect_success 'cleanup cummit message (option overrides default)' '
	echo content >>file &&
	but add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  but cummit --cleanup=whitespace --no-status
	) &&
	cummit_msg_is "cummit message # comment"
'

test_expect_success 'cleanup cummit message (config overrides default)' '
	echo content >>file &&
	but add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  but -c cummit.cleanup=whitespace cummit --no-status
	) &&
	cummit_msg_is "cummit message # comment"
'

test_expect_success 'cleanup cummit message (option overrides config)' '
	echo content >>file &&
	but add file &&
	(
	  test_set_editor "$TEST_DIRECTORY"/t7500/add-content-and-comment &&
	  but -c cummit.cleanup=whitespace cummit --cleanup=default
	) &&
	cummit_msg_is "cummit message"
'

test_expect_success 'cleanup cummit message (default, -m)' '
	echo content >>file &&
	but add file &&
	but cummit -m "message #comment " &&
	cummit_msg_is "message #comment"
'

test_expect_success 'cleanup cummit message (whitespace option, -m)' '
	echo content >>file &&
	but add file &&
	but cummit --cleanup=whitespace --no-status -m "message #comment " &&
	cummit_msg_is "message #comment"
'

test_expect_success 'cleanup cummit message (whitespace config, -m)' '
	echo content >>file &&
	but add file &&
	but -c cummit.cleanup=whitespace cummit --no-status -m "message #comment " &&
	cummit_msg_is "message #comment"
'

test_expect_success 'message shows author when it is not equal to cummitter' '
	echo >>negative &&
	but cummit -e -m "sample" -a &&
	test_i18ngrep \
	  "^# Author: *A U Thor <author@example.com>\$" \
	  .but/CUMMIT_EDITMSG
'

test_expect_success 'message shows date when it is explicitly set' '
	but cummit --allow-empty -e -m foo --date="2010-01-02T03:04:05" &&
	test_i18ngrep \
	  "^# Date: *Sat Jan 2 03:04:05 2010 +0000" \
	  .but/CUMMIT_EDITMSG
'

test_expect_success AUTOIDENT 'message shows cummitter when it is automatic' '

	echo >>negative &&
	(
		sane_unset GIT_CUMMITTER_EMAIL &&
		sane_unset GIT_CUMMITTER_NAME &&
		but cummit -e -m "sample" -a
	) &&
	# the ident is calculated from the system, so we cannot
	# check the actual value, only that it is there
	test_i18ngrep "^# cummitter: " .but/CUMMIT_EDITMSG
'

write_script .but/FAKE_EDITOR <<EOF
echo editor started >"$(pwd)/.but/result"
exit 0
EOF

test_expect_success !FAIL_PREREQS,!AUTOIDENT 'do not fire editor when cummitter is bogus' '
	>.but/result &&

	echo >>negative &&
	(
		sane_unset GIT_CUMMITTER_EMAIL &&
		sane_unset GIT_CUMMITTER_NAME &&
		GIT_EDITOR="\"$(pwd)/.but/FAKE_EDITOR\"" &&
		export GIT_EDITOR &&
		test_must_fail but cummit -e -m sample -a
	) &&
	test_must_be_empty .but/result
'

test_expect_success 'do not fire editor if -m <msg> was given' '
	echo tick >file &&
	but add file &&
	echo "editor not started" >.but/result &&
	(GIT_EDITOR="\"$(pwd)/.but/FAKE_EDITOR\"" but cummit -m tick) &&
	test "$(cat .but/result)" = "editor not started"
'

test_expect_success 'do not fire editor if -m "" was given' '
	echo tock >file &&
	but add file &&
	echo "editor not started" >.but/result &&
	(GIT_EDITOR="\"$(pwd)/.but/FAKE_EDITOR\"" \
	 but cummit -m "" --allow-empty-message) &&
	test "$(cat .but/result)" = "editor not started"
'

test_expect_success 'do not fire editor in the presence of conflicts' '

	but clean -f &&
	echo f >g &&
	but add g &&
	but cummit -m "add g" &&
	but branch second &&
	echo main >g &&
	echo g >h &&
	but add g h &&
	but cummit -m "modify g and add h" &&
	but checkout second &&
	echo second >g &&
	but add g &&
	but cummit -m second &&
	# Must fail due to conflict
	test_must_fail but cherry-pick -n main &&
	echo "editor not started" >.but/result &&
	(
		GIT_EDITOR="\"$(pwd)/.but/FAKE_EDITOR\"" &&
		export GIT_EDITOR &&
		test_must_fail but cummit
	) &&
	test "$(cat .but/result)" = "editor not started"
'

write_script .but/FAKE_EDITOR <<EOF
# kill -TERM command added below.
EOF

test_expect_success EXECKEEPSPID 'a SIGTERM should break locks' '
	echo >>negative &&
	! "$SHELL_PATH" -c '\''
	  echo kill -TERM $$ >>.but/FAKE_EDITOR
	  GIT_EDITOR=.but/FAKE_EDITOR
	  export GIT_EDITOR
	  exec but cummit -a'\'' &&
	test ! -f .but/index.lock
'

rm -f .but/MERGE_MSG .but/CUMMIT_EDITMSG
but reset -q --hard

test_expect_success 'Hand cummitting of a redundant merge removes dups' '

	but rev-parse second main >expect &&
	test_must_fail but merge second main &&
	but checkout main g &&
	EDITOR=: but cummit -a &&
	but cat-file commit HEAD >raw &&
	sed -n -e "s/^parent //p" -e "/^$/q" raw >actual &&
	test_cmp expect actual

'

test_expect_success 'A single-liner subject with a token plus colon is not a footer' '

	but reset --hard &&
	but cummit -s -m "hello: kitty" --allow-empty &&
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_line_count = 3 actual

'

test_expect_success 'cummit -s places sob on third line after two empty lines' '
	but cummit -s --allow-empty --allow-empty-message &&
	cat <<-EOF >expect &&


	Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>

	EOF
	sed -e "/^#/d" -e "s/^:.*//" .but/CUMMIT_EDITMSG >actual &&
	test_cmp expect actual
'

write_script .but/FAKE_EDITOR <<\EOF
mv "$1" "$1.orig"
(
	echo message
	cat "$1.orig"
) >"$1"
EOF

echo '## Custom template' >template

try_cummit () {
	but reset --hard &&
	echo >>negative &&
	GIT_EDITOR=.but/FAKE_EDITOR but cummit -a $* $use_template &&
	case "$use_template" in
	'')
		test_i18ngrep ! "^## Custom template" .but/CUMMIT_EDITMSG ;;
	*)
		test_i18ngrep "^## Custom template" .but/CUMMIT_EDITMSG ;;
	esac
}

try_cummit_status_combo () {

	test_expect_success 'cummit' '
		try_cummit "" &&
		test_i18ngrep "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit --status' '
		try_cummit --status &&
		test_i18ngrep "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit --no-status' '
		try_cummit --no-status &&
		test_i18ngrep ! "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit with cummit.status = yes' '
		test_config cummit.status yes &&
		try_cummit "" &&
		test_i18ngrep "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit with cummit.status = no' '
		test_config cummit.status no &&
		try_cummit "" &&
		test_i18ngrep ! "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit --status with cummit.status = yes' '
		test_config cummit.status yes &&
		try_cummit --status &&
		test_i18ngrep "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit --no-status with cummit.status = yes' '
		test_config cummit.status yes &&
		try_cummit --no-status &&
		test_i18ngrep ! "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit --status with cummit.status = no' '
		test_config cummit.status no &&
		try_cummit --status &&
		test_i18ngrep "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

	test_expect_success 'cummit --no-status with cummit.status = no' '
		test_config cummit.status no &&
		try_cummit --no-status &&
		test_i18ngrep ! "^# Changes to be cummitted:" .but/CUMMIT_EDITMSG
	'

}

try_cummit_status_combo

use_template="-t template"

try_cummit_status_combo

test_expect_success 'cummit --status with custom comment character' '
	test_config core.commentchar ";" &&
	try_cummit --status &&
	test_i18ngrep "^; Changes to be cummitted:" .but/CUMMIT_EDITMSG
'

test_expect_success 'switch core.commentchar' '
	test_cummit "#foo" foo &&
	GIT_EDITOR=.but/FAKE_EDITOR but -c core.commentChar=auto cummit --amend &&
	test_i18ngrep "^; Changes to be cummitted:" .but/CUMMIT_EDITMSG
'

test_expect_success 'switch core.commentchar but out of options' '
	cat >text <<\EOF &&
# 1
; 2
@ 3
! 4
$ 5
% 6
^ 7
& 8
| 9
: 10
EOF
	but cummit --amend -F text &&
	(
		test_set_editor .but/FAKE_EDITOR &&
		test_must_fail but -c core.commentChar=auto cummit --amend
	)
'

test_done
