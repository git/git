#!/bin/sh
#
# Copyright (c) 2008 Johannes Schindelin
#

test_description='Test rebasing, stashing, etc. with submodules'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	echo file > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git clone . submodule &&
	git add submodule &&
	test_tick &&
	git commit -m submodule &&
	echo second line >> file &&
	(cd submodule && git pull) &&
	test_tick &&
	git commit -m file-and-submodule -a &&
	git branch added-submodule

'

test_expect_success 'rebase with a dirty submodule' '

	(cd submodule &&
	 echo 3rd line >> file &&
	 test_tick &&
	 git commit -m fork -a) &&
	echo unrelated >> file2 &&
	git add file2 &&
	test_tick &&
	git commit -m unrelated file2 &&
	echo other line >> file &&
	test_tick &&
	git commit -m update file &&
	CURRENT=$(cd submodule && git rev-parse HEAD) &&
	EXPECTED=$(git rev-parse HEAD~2:submodule) &&
	GIT_TRACE=1 git rebase --onto HEAD~2 HEAD^ &&
	STORED=$(git rev-parse HEAD:submodule) &&
	test $EXPECTED = $STORED &&
	test $CURRENT = $(cd submodule && git rev-parse HEAD)

'

cat > fake-editor.sh << \EOF
#!/bin/sh
echo $EDITOR_TEXT
EOF
chmod a+x fake-editor.sh

test_expect_success 'interactive rebase with a dirty submodule' '

	echo submodule >expect &&
	git diff --name-only >actual &&
	test_cmp expect actual &&
	HEAD=$(git rev-parse HEAD) &&
	GIT_EDITOR="\"$(pwd)/fake-editor.sh\"" EDITOR_TEXT="pick $HEAD" \
		git rebase -i HEAD^ &&
	echo submodule >expect &&
	git diff --name-only >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase with dirty file and submodule fails' '

	echo yet another line >> file &&
	test_tick &&
	git commit -m next file &&
	echo rewrite > file &&
	test_tick &&
	git commit -m rewrite file &&
	echo dirty > file &&
	test_must_fail git rebase --onto HEAD~2 HEAD^

'

test_expect_success 'stash with a dirty submodule' '

	echo new > file &&
	CURRENT=$(cd submodule && git rev-parse HEAD) &&
	git stash &&
	test new != $(cat file) &&
	echo submodule >expect &&
	git diff --name-only >actual &&
	test_cmp expect actual &&

	echo "$CURRENT" >expect &&
	git -C submodule rev-parse HEAD >actual &&
	test_cmp expect actual &&

	git stash apply &&
	test new = $(cat file) &&
	echo "$CURRENT" >expect &&
	git -C submodule rev-parse HEAD >actual &&
	test_cmp expect actual

'

test_expect_success 'rebasing submodule that should conflict' '
	git reset --hard &&
	git checkout added-submodule &&
	git add submodule &&
	test_tick &&
	git commit -m third &&
	(
		cd submodule &&
		git commit --allow-empty -m extra
	) &&
	git add submodule &&
	test_tick &&
	git commit -m fourth &&

	test_must_fail git rebase --onto HEAD^^ HEAD^ HEAD^0 >actual_output &&
	git ls-files -s submodule >actual &&
	(
		cd submodule &&
		echo "160000 $(git rev-parse HEAD^) 1	submodule" &&
		echo "160000 $(git rev-parse HEAD^^) 2	submodule" &&
		echo "160000 $(git rev-parse HEAD) 3	submodule"
	) >expect &&
	test_cmp expect actual &&
	if test "$GIT_TEST_MERGE_ALGORITHM" = ort
    then
		sub_expect="go to submodule (submodule), and either merge commit $(git -C submodule rev-parse --short HEAD^0)" &&
		grep "$sub_expect" actual_output
	fi
'

test_done
