#!/bin/sh
#
# Copyright (c) 2023 Teng Long
#

test_description='Test commit notes with stripspace behavior'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

MULTI_LF="$LF$LF$LF"
write_script fake_editor <<\EOF
echo "$MSG" >"$1"
echo "$MSG" >&2
EOF
GIT_EDITOR=./fake_editor
export GIT_EDITOR

test_expect_success 'setup the commit' '
	test_commit 1st
'

test_expect_success 'add note by editor' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes add  &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying single "-m", "--stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	git notes add -m "${LF}first-line${MULTI_LF}second-line${LF}" &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	git notes add --stripspace -m "${LF}first-line${MULTI_LF}second-line${LF}" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying single "-m" and "--no-stripspace" ' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}first-line${MULTI_LF}second-line
	EOF

	git notes add --no-stripspace \
		      -m "${LF}first-line${MULTI_LF}second-line${LF}" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying multiple "-m", "--stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	git notes add -m "${LF}" \
		      -m "first-line" \
		      -m "${MULTI_LF}" \
		      -m "second-line" \
		      -m "${LF}" &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	git notes add --stripspace -m "${LF}" \
		      -m "first-line" \
		      -m "${MULTI_LF}" \
		      -m "second-line" \
		      -m "${LF}" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add notes by specifying multiple "-m" and "--no-stripspace"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line${LF}
	EOF

	git notes add --no-stripspace \
		      -m "${LF}" \
		      -m "first-line" \
		      -m "${MULTI_LF}" \
		      -m "second-line" \
		      -m "${LF}" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying single "-F", "--stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	cat >note-file <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	git notes add -F note-file &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	git notes add --stripspace -F note-file &&
	git notes show >actual
'

test_expect_success 'add note by specifying single "-F" and "--no-stripspace"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	cat >note-file <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	git notes add --no-stripspace -F note-file &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying multiple "-F", "--stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	file-1-first-line

	file-1-second-line

	file-2-first-line

	file-2-second-line
	EOF

	cat >note-file-1 <<-EOF &&
	${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}
	EOF

	cat >note-file-2 <<-EOF &&
	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	git notes add -F note-file-1 -F note-file-2 &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	git notes add --stripspace -F note-file-1 -F note-file-2 &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying multiple "-F" with "--no-stripspace"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}

	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	cat >note-file-1 <<-EOF &&
	${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}
	EOF

	cat >note-file-2 <<-EOF &&
	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	git notes add --no-stripspace -F note-file-1 -F note-file-2 &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'append note by editor' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	git notes add -m "first-line" &&
	MSG="${MULTI_LF}second-line${LF}" git notes append  &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'append note by specifying single "-m"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	git notes add -m "${LF}first-line" &&
	git notes append -m "${MULTI_LF}second-line${LF}" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'append note by specifying multiple "-m"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	git notes add -m "${LF}first-line" &&
	git notes append -m "${MULTI_LF}" \
		      -m "second-line" \
		      -m "${LF}" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying single "-F"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	cat >note-file <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	git notes add -F note-file &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add notes by specifying multiple "-F"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	file-1-first-line

	file-1-second-line

	file-2-first-line

	file-2-second-line
	EOF

	cat >note-file-1 <<-EOF &&
	${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}
	EOF

	cat >note-file-2 <<-EOF &&
	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	git notes add -F note-file-1 -F note-file-2 &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'append note by specifying single "-F"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	initial-line

	first-line

	second-line
	EOF

	cat >note-file <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	git notes add -m "initial-line" &&
	git notes append -F note-file &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'append notes by specifying multiple "-F"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	initial-line

	file-1-first-line

	file-1-second-line

	file-2-first-line

	file-2-second-line
	EOF

	cat >note-file-1 <<-EOF &&
	${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}
	EOF

	cat >note-file-2 <<-EOF &&
	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	git notes add -m "initial-line" &&
	git notes append -F note-file-1 -F note-file-2 &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'append note by specifying multiple "-F" with "--no-stripspace"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	initial-line
	${LF}${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}

	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	cat >note-file-1 <<-EOF &&
	${LF}
	file-1-first-line
	${MULTI_LF}
	file-1-second-line
	${LF}
	EOF

	cat >note-file-2 <<-EOF &&
	${LF}
	file-2-first-line
	${MULTI_LF}
	file-2-second-line
	${LF}
	EOF

	git notes add -m "initial-line" &&
	git notes append --no-stripspace -F note-file-1 -F note-file-2 &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add notes with empty messages' '
	rev=$(git rev-parse HEAD) &&
	git notes add -m "${LF}" \
		      -m "${MULTI_LF}" \
		      -m "${LF}" >actual 2>&1 &&
	test_grep "Removing note for object" actual
'

test_expect_success 'add note by specifying "-C", "--no-stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	cat expect | git hash-object -w --stdin >blob &&
	git notes add -C $(cat blob) &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	git notes add --no-stripspace -C $(cat blob) &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'reuse note by specifying "-C" and "--stripspace"' '
	test_when_finished "git notes remove" &&
	cat >data <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	cat data | git hash-object -w --stdin >blob &&
	git notes add --stripspace -C $(cat blob) &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'reuse with "-C" and add note with "-m", "-m" will stripspace all together' '
	test_when_finished "git notes remove" &&
	cat >data <<-EOF &&
	${LF}
	first-line
	${MULTI_LF}
	second-line
	${LF}
	EOF

	cat >expect <<-EOF &&
	first-line

	second-line

	third-line
	EOF

	cat data | git hash-object -w --stdin >blob &&
	git notes add -C $(cat blob) -m "third-line" &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note with "-m" and reuse note with "-C", "-C" will not stripspace all together' '
	test_when_finished "git notes remove" &&
	cat >data <<-EOF &&

	second-line
	EOF

	cat >expect <<-EOF &&
	first-line
	${LF}
	second-line
	EOF

	cat data | git hash-object -w --stdin >blob &&
	git notes add -m "first-line" -C $(cat blob)  &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying "-c", "--stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	echo "initial-line" | git hash-object -w --stdin >blob &&
	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes add -c $(cat blob) &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes add --stripspace -c $(cat blob) &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'add note by specifying "-c" with "--no-stripspace"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}first-line${MULTI_LF}second-line${LF}
	EOF

	echo "initial-line" | git hash-object -w --stdin >blob &&
	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes add --no-stripspace -c $(cat blob) &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'edit note by specifying "-c", "--stripspace" is the default behavior' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	first-line

	second-line
	EOF

	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes edit &&
	git notes show >actual &&
	test_cmp expect actual &&
	git notes remove &&
	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes edit --stripspace &&
	git notes show >actual &&
	test_cmp expect actual
'

test_expect_success 'edit note by specifying "-c" with "--no-stripspace"' '
	test_when_finished "git notes remove" &&
	cat >expect <<-EOF &&
	${LF}first-line${MULTI_LF}second-line${LF}
	EOF

	MSG="${LF}first-line${MULTI_LF}second-line${LF}" git notes add --no-stripspace &&
	git notes show >actual &&
	test_cmp expect actual
'

test_done
