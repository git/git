#!/bin/sh
#
# Copyright (c) 2008 Johannes Schindelin
#

test_description='Test rebasing, stashing, etc. with submodules'

. ./test-lib.sh

test_expect_success setup '

	echo file > file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but clone . submodule &&
	but add submodule &&
	test_tick &&
	but cummit -m submodule &&
	echo second line >> file &&
	(cd submodule && but pull) &&
	test_tick &&
	but cummit -m file-and-submodule -a &&
	but branch added-submodule

'

test_expect_success 'rebase with a dirty submodule' '

	(cd submodule &&
	 echo 3rd line >> file &&
	 test_tick &&
	 but cummit -m fork -a) &&
	echo unrelated >> file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m unrelated file2 &&
	echo other line >> file &&
	test_tick &&
	but cummit -m update file &&
	CURRENT=$(cd submodule && but rev-parse HEAD) &&
	EXPECTED=$(but rev-parse HEAD~2:submodule) &&
	BUT_TRACE=1 but rebase --onto HEAD~2 HEAD^ &&
	STORED=$(but rev-parse HEAD:submodule) &&
	test $EXPECTED = $STORED &&
	test $CURRENT = $(cd submodule && but rev-parse HEAD)

'

cat > fake-editor.sh << \EOF
#!/bin/sh
echo $EDITOR_TEXT
EOF
chmod a+x fake-editor.sh

test_expect_success 'interactive rebase with a dirty submodule' '

	test submodule = $(but diff --name-only) &&
	HEAD=$(but rev-parse HEAD) &&
	BUT_EDITOR="\"$(pwd)/fake-editor.sh\"" EDITOR_TEXT="pick $HEAD" \
		but rebase -i HEAD^ &&
	test submodule = $(but diff --name-only)

'

test_expect_success 'rebase with dirty file and submodule fails' '

	echo yet another line >> file &&
	test_tick &&
	but cummit -m next file &&
	echo rewrite > file &&
	test_tick &&
	but cummit -m rewrite file &&
	echo dirty > file &&
	test_must_fail but rebase --onto HEAD~2 HEAD^

'

test_expect_success 'stash with a dirty submodule' '

	echo new > file &&
	CURRENT=$(cd submodule && but rev-parse HEAD) &&
	but stash &&
	test new != $(cat file) &&
	test submodule = $(but diff --name-only) &&
	test $CURRENT = $(cd submodule && but rev-parse HEAD) &&
	but stash apply &&
	test new = $(cat file) &&
	test $CURRENT = $(cd submodule && but rev-parse HEAD)

'

test_expect_success 'rebasing submodule that should conflict' '
	but reset --hard &&
	but checkout added-submodule &&
	but add submodule &&
	test_tick &&
	but cummit -m third &&
	(
		cd submodule &&
		but cummit --allow-empty -m extra
	) &&
	but add submodule &&
	test_tick &&
	but cummit -m fourth &&

	test_must_fail but rebase --onto HEAD^^ HEAD^ HEAD^0 &&
	but ls-files -s submodule >actual &&
	(
		cd submodule &&
		echo "160000 $(but rev-parse HEAD^) 1	submodule" &&
		echo "160000 $(but rev-parse HEAD^^) 2	submodule" &&
		echo "160000 $(but rev-parse HEAD) 3	submodule"
	) >expect &&
	test_cmp expect actual
'

test_done
