#!/bin/sh

test_description='git mktree'

. ./test-lib.sh

test_expect_success setup '
	for d in a a. a0
	do
		mkdir "$d" && echo "$d/one" >"$d/one" &&
		git add "$d"
	done &&
	echo zero >one &&
	git update-index --add --info-only one &&
	git write-tree --missing-ok >tree.missing &&
	git ls-tree $(cat tree.missing) >top.missing &&
	git ls-tree -r $(cat tree.missing) >all.missing &&
	echo one >one &&
	git add one &&
	git write-tree >tree &&
	git ls-tree $(cat tree) >top &&
	git ls-tree -r $(cat tree) >all &&
	test_tick &&
	git commit -q -m one &&
	H=$(git rev-parse HEAD) &&
	git update-index --add --cacheinfo 160000 $H sub &&
	test_tick &&
	git commit -q -m two &&
	git rev-parse HEAD^{tree} >tree.withsub &&
	git ls-tree HEAD >top.withsub &&
	git ls-tree -r HEAD >all.withsub
'

test_expect_success 'ls-tree piped to mktree (1)' '
	git mktree <top >actual &&
	test_cmp tree actual
'

test_expect_success 'ls-tree piped to mktree (2)' '
	git mktree <top.withsub >actual &&
	test_cmp tree.withsub actual
'

test_expect_success 'ls-tree output in wrong order given to mktree (1)' '
	perl -e "print reverse <>" <top |
	git mktree >actual &&
	test_cmp tree actual
'

test_expect_success 'ls-tree output in wrong order given to mktree (2)' '
	perl -e "print reverse <>" <top.withsub |
	git mktree >actual &&
	test_cmp tree.withsub actual
'

test_expect_success 'allow missing object with --missing' '
	git mktree --missing <top.missing >actual &&
	test_cmp tree.missing actual
'

test_expect_success 'mktree refuses to read ls-tree -r output (1)' '
	test_must_fail git mktree <all >actual
'

test_expect_success 'mktree refuses to read ls-tree -r output (2)' '
	test_must_fail git mktree <all.withsub >actual
'

test_done
