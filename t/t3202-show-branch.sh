#!/bin/sh

test_description='test show-branch'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	for i in $(test_seq 1 10)
	do
		git checkout -b branch$i initial &&
		test_commit --no-tag branch$i
	done &&
	git for-each-ref \
		--sort=version:refname \
		--format="%(refname:strip=2)" \
		"refs/heads/branch*" >branches.sorted &&
	sed "s/^> //" >expect <<-\EOF
	> ! [branch1] branch1
	>  ! [branch2] branch2
	>   ! [branch3] branch3
	>    ! [branch4] branch4
	>     ! [branch5] branch5
	>      ! [branch6] branch6
	>       ! [branch7] branch7
	>        ! [branch8] branch8
	>         ! [branch9] branch9
	>          * [branch10] branch10
	> ----------
	>          * [branch10] branch10
	>         +  [branch9] branch9
	>        +   [branch8] branch8
	>       +    [branch7] branch7
	>      +     [branch6] branch6
	>     +      [branch5] branch5
	>    +       [branch4] branch4
	>   +        [branch3] branch3
	>  +         [branch2] branch2
	> +          [branch1] branch1
	> +++++++++* [branch10^] initial
	EOF
'

test_expect_success 'show-branch with more than 8 branches' '
	git show-branch $(cat branches.sorted) >actual &&
	test_cmp expect actual
'

test_expect_success 'show-branch with showbranch.default' '
	for branch in $(cat branches.sorted)
	do
		test_config showbranch.default $branch --add
	done &&
	git show-branch >actual &&
	test_cmp expect actual
'

test_done
