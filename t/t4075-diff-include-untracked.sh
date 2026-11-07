#!/bin/sh

test_description='git diff --include-untracked'

. ./test-lib.sh

test_expect_success 'setup' '
	test_write_lines ignored actual expect err "index.*" >.gitignore &&
	echo one >tracked &&
	git add .gitignore tracked &&
	git commit -m initial &&
	echo two >>tracked &&
	echo staged >staged &&
	git add staged &&
	mkdir dir &&
	echo untracked >dir/untracked &&
	echo untracked >untracked &&
	echo ignored >ignored
'

test_expect_success 'untracked files show up as new files against the index' '
	git diff --include-untracked --name-status >actual &&
	cat >expect <<-\EOF &&
	A	dir/untracked
	M	tracked
	A	untracked
	EOF
	test_cmp expect actual
'

test_expect_success 'untracked files show up as new files against a commit' '
	git diff --include-untracked --name-status HEAD >actual &&
	cat >expect <<-\EOF &&
	A	dir/untracked
	A	staged
	M	tracked
	A	untracked
	EOF
	test_cmp expect actual
'

test_expect_success 'output matches marking untracked files with intent-to-add' '
	cp .git/index index.ita &&
	GIT_INDEX_FILE=index.ita git add -N . &&
	GIT_INDEX_FILE=index.ita git diff HEAD >expect &&
	rm index.ita &&
	git diff --include-untracked HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'pathspec limits the untracked files shown' '
	git diff --include-untracked --name-only -- dir >actual &&
	echo dir/untracked >expect &&
	test_cmp expect actual
'

test_expect_success 'index is left untouched' '
	cp .git/index index.before &&
	git diff --include-untracked HEAD >/dev/null &&
	test_cmp_bin index.before .git/index &&
	git diff --name-only HEAD >actual &&
	test_grep ! untracked actual
'

test_expect_success '--exit-code notices untracked files' '
	test_expect_code 1 git diff --include-untracked --exit-code -- dir
'

test_expect_success '--include-untracked is incompatible with --cached' '
	test_must_fail git diff --cached --include-untracked 2>err &&
	test_grep "cannot be used together" err
'

test_done
