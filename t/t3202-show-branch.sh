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

test_expect_success 'show-branch --color output' '
	sed "s/^> //" >expect <<-\EOF &&
	> <RED>!<RESET> [branch1] branch1
	>  <GREEN>!<RESET> [branch2] branch2
	>   <YELLOW>!<RESET> [branch3] branch3
	>    <BLUE>!<RESET> [branch4] branch4
	>     <MAGENTA>!<RESET> [branch5] branch5
	>      <CYAN>!<RESET> [branch6] branch6
	>       <BOLD;RED>!<RESET> [branch7] branch7
	>        <BOLD;GREEN>!<RESET> [branch8] branch8
	>         <BOLD;YELLOW>!<RESET> [branch9] branch9
	>          <BOLD;BLUE>*<RESET> [branch10] branch10
	> ----------
	>          <BOLD;BLUE>*<RESET> [branch10] branch10
	>         <BOLD;YELLOW>+<RESET>  [branch9] branch9
	>        <BOLD;GREEN>+<RESET>   [branch8] branch8
	>       <BOLD;RED>+<RESET>    [branch7] branch7
	>      <CYAN>+<RESET>     [branch6] branch6
	>     <MAGENTA>+<RESET>      [branch5] branch5
	>    <BLUE>+<RESET>       [branch4] branch4
	>   <YELLOW>+<RESET>        [branch3] branch3
	>  <GREEN>+<RESET>         [branch2] branch2
	> <RED>+<RESET>          [branch1] branch1
	> <RED>+<RESET><GREEN>+<RESET><YELLOW>+<RESET><BLUE>+<RESET><MAGENTA>+<RESET><CYAN>+<RESET><BOLD;RED>+<RESET><BOLD;GREEN>+<RESET><BOLD;YELLOW>+<RESET><BOLD;BLUE>*<RESET> [branch10^] initial
	EOF
	git show-branch --color=always $(cat branches.sorted) >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_done
