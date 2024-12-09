#!/bin/sh

test_description='test for-each-refs usage of ref-filter APIs'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

test_expect_success 'setup some history and refs' '
	test_commit one &&
	git branch -M main &&
	test_commit two &&
	test_commit three &&
	git checkout -b side &&
	test_commit four &&
	git tag -m "An annotated tag" annotated-tag &&
	git tag -m "Annotated doubly" doubly-annotated-tag annotated-tag &&

	# Note that these "signed" tags might not actually be signed.
	# Tests which care about the distinction should be marked
	# with the GPG prereq.
	if test_have_prereq GPG
	then
		sign=-s
	else
		sign=
	fi &&
	git tag $sign -m "A signed tag" signed-tag &&
	git tag $sign -m "Signed doubly" doubly-signed-tag signed-tag &&

	git checkout main &&
	git update-ref refs/odd/spot main
'

test_expect_success '--include-root-refs pattern prints pseudorefs' '
	cat >expect <<-\EOF &&
	HEAD
	ORIG_HEAD
	refs/heads/main
	refs/heads/side
	refs/odd/spot
	refs/tags/annotated-tag
	refs/tags/doubly-annotated-tag
	refs/tags/doubly-signed-tag
	refs/tags/four
	refs/tags/one
	refs/tags/signed-tag
	refs/tags/three
	refs/tags/two
	EOF
	git update-ref ORIG_HEAD main &&
	git for-each-ref --format="%(refname)" --include-root-refs >actual &&
	test_cmp expect actual
'

test_expect_success '--include-root-refs pattern does not print special refs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		git rev-parse HEAD >.git/MERGE_HEAD &&
		git for-each-ref --format="%(refname)" --include-root-refs >actual &&
		cat >expect <<-EOF &&
		HEAD
		$(git symbolic-ref HEAD)
		refs/tags/initial
		EOF
		test_cmp expect actual
	)
'

test_expect_success '--include-root-refs with other patterns' '
	cat >expect <<-\EOF &&
	HEAD
	ORIG_HEAD
	EOF
	git update-ref ORIG_HEAD main &&
	git for-each-ref --format="%(refname)" --include-root-refs "*HEAD" >actual &&
	test_cmp expect actual
'

test_expect_success '--include-root-refs omits dangling symrefs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		git symbolic-ref DANGLING_HEAD refs/heads/missing &&
		cat >expect <<-EOF &&
		HEAD
		$(git symbolic-ref HEAD)
		refs/tags/initial
		EOF
		git for-each-ref --format="%(refname)" --include-root-refs >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'filtering with --points-at' '
	cat >expect <<-\EOF &&
	refs/heads/main
	refs/odd/spot
	refs/tags/three
	EOF
	git for-each-ref --format="%(refname)" --points-at=main >actual &&
	test_cmp expect actual
'

test_expect_success 'check signed tags with --points-at' '
	sed -e "s/Z$//" >expect <<-\EOF &&
	refs/heads/side Z
	refs/tags/annotated-tag four
	refs/tags/doubly-annotated-tag four
	refs/tags/doubly-signed-tag four
	refs/tags/four Z
	refs/tags/signed-tag four
	EOF
	git for-each-ref --format="%(refname) %(*subject)" --points-at=side >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --merged' '
	cat >expect <<-\EOF &&
	refs/heads/main
	refs/odd/spot
	refs/tags/one
	refs/tags/three
	refs/tags/two
	EOF
	git for-each-ref --format="%(refname)" --merged=main >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --no-merged' '
	cat >expect <<-\EOF &&
	refs/heads/side
	refs/tags/annotated-tag
	refs/tags/doubly-annotated-tag
	refs/tags/doubly-signed-tag
	refs/tags/four
	refs/tags/signed-tag
	EOF
	git for-each-ref --format="%(refname)" --no-merged=main >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --contains' '
	cat >expect <<-\EOF &&
	refs/heads/main
	refs/heads/side
	refs/odd/spot
	refs/tags/annotated-tag
	refs/tags/doubly-annotated-tag
	refs/tags/doubly-signed-tag
	refs/tags/four
	refs/tags/signed-tag
	refs/tags/three
	refs/tags/two
	EOF
	git for-each-ref --format="%(refname)" --contains=two >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --no-contains' '
	cat >expect <<-\EOF &&
	refs/tags/one
	EOF
	git for-each-ref --format="%(refname)" --no-contains=two >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --contains and --no-contains' '
	cat >expect <<-\EOF &&
	refs/tags/two
	EOF
	git for-each-ref --format="%(refname)" --contains=two --no-contains=three >actual &&
	test_cmp expect actual
'

test_expect_success '%(color) must fail' '
	test_must_fail git for-each-ref --format="%(color)%(refname)"
'

test_expect_success '%(color:#aa22ac) must succeed' '
	test_when_finished rm -rf test &&
	git init test &&
	(
		cd test &&
		test_commit initial &&
		git branch -M main &&
		cat >expect <<-\EOF &&
		refs/heads/main
		refs/tags/initial
		EOF
		git remote add origin nowhere &&
		git config branch.main.remote origin &&
		git config branch.main.merge refs/heads/main &&
		git for-each-ref --format="%(color:#aa22ac)%(refname)" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'left alignment is default' '
	cat >expect <<-\EOF &&
	refname is refs/heads/main    |refs/heads/main
	refname is refs/heads/side    |refs/heads/side
	refname is refs/odd/spot      |refs/odd/spot
	refname is refs/tags/annotated-tag|refs/tags/annotated-tag
	refname is refs/tags/doubly-annotated-tag|refs/tags/doubly-annotated-tag
	refname is refs/tags/doubly-signed-tag|refs/tags/doubly-signed-tag
	refname is refs/tags/four     |refs/tags/four
	refname is refs/tags/one      |refs/tags/one
	refname is refs/tags/signed-tag|refs/tags/signed-tag
	refname is refs/tags/three    |refs/tags/three
	refname is refs/tags/two      |refs/tags/two
	EOF
	git for-each-ref --format="%(align:30)refname is %(refname)%(end)|%(refname)" >actual &&
	test_cmp expect actual
'

test_expect_success 'middle alignment' '
	cat >expect <<-\EOF &&
	|  refname is refs/heads/main  |refs/heads/main
	|  refname is refs/heads/side  |refs/heads/side
	|   refname is refs/odd/spot   |refs/odd/spot
	|refname is refs/tags/annotated-tag|refs/tags/annotated-tag
	|refname is refs/tags/doubly-annotated-tag|refs/tags/doubly-annotated-tag
	|refname is refs/tags/doubly-signed-tag|refs/tags/doubly-signed-tag
	|  refname is refs/tags/four   |refs/tags/four
	|   refname is refs/tags/one   |refs/tags/one
	|refname is refs/tags/signed-tag|refs/tags/signed-tag
	|  refname is refs/tags/three  |refs/tags/three
	|   refname is refs/tags/two   |refs/tags/two
	EOF
	git for-each-ref --format="|%(align:middle,30)refname is %(refname)%(end)|%(refname)" >actual &&
	test_cmp expect actual
'

test_expect_success 'right alignment' '
	cat >expect <<-\EOF &&
	|    refname is refs/heads/main|refs/heads/main
	|    refname is refs/heads/side|refs/heads/side
	|      refname is refs/odd/spot|refs/odd/spot
	|refname is refs/tags/annotated-tag|refs/tags/annotated-tag
	|refname is refs/tags/doubly-annotated-tag|refs/tags/doubly-annotated-tag
	|refname is refs/tags/doubly-signed-tag|refs/tags/doubly-signed-tag
	|     refname is refs/tags/four|refs/tags/four
	|      refname is refs/tags/one|refs/tags/one
	|refname is refs/tags/signed-tag|refs/tags/signed-tag
	|    refname is refs/tags/three|refs/tags/three
	|      refname is refs/tags/two|refs/tags/two
	EOF
	git for-each-ref --format="|%(align:30,right)refname is %(refname)%(end)|%(refname)" >actual &&
	test_cmp expect actual
'

cat >expect <<-\EOF
|        refname is refs/heads/main        |refs/heads/main
|        refname is refs/heads/side        |refs/heads/side
|         refname is refs/odd/spot         |refs/odd/spot
|    refname is refs/tags/annotated-tag    |refs/tags/annotated-tag
|refname is refs/tags/doubly-annotated-tag |refs/tags/doubly-annotated-tag
|  refname is refs/tags/doubly-signed-tag  |refs/tags/doubly-signed-tag
|        refname is refs/tags/four         |refs/tags/four
|         refname is refs/tags/one         |refs/tags/one
|     refname is refs/tags/signed-tag      |refs/tags/signed-tag
|        refname is refs/tags/three        |refs/tags/three
|         refname is refs/tags/two         |refs/tags/two
EOF

test_align_permutations() {
	while read -r option
	do
		test_expect_success "align:$option" '
			git for-each-ref --format="|%(align:$option)refname is %(refname)%(end)|%(refname)" >actual &&
			test_cmp expect actual
		'
	done
}

test_align_permutations <<-\EOF
	middle,42
	42,middle
	position=middle,42
	42,position=middle
	middle,width=42
	width=42,middle
	position=middle,width=42
	width=42,position=middle
EOF

# Last one wins (silently) when multiple arguments of the same type are given

test_align_permutations <<-\EOF
	32,width=42,middle
	width=30,42,middle
	width=42,position=right,middle
	42,right,position=middle
EOF

# Individual atoms inside %(align:...) and %(end) must not be quoted.

test_expect_success 'alignment with format quote' "
	cat >expect <<-\EOF &&
	|'       '\''main| A U Thor'\''       '|
	|'       '\''side| A U Thor'\''       '|
	|'     '\''odd/spot| A U Thor'\''     '|
	|'      '\''annotated-tag| '\''       '|
	|'   '\''doubly-annotated-tag| '\''   '|
	|'    '\''doubly-signed-tag| '\''     '|
	|'       '\''four| A U Thor'\''       '|
	|'       '\''one| A U Thor'\''        '|
	|'        '\''signed-tag| '\''        '|
	|'      '\''three| A U Thor'\''       '|
	|'       '\''two| A U Thor'\''        '|
	EOF
	git for-each-ref --shell --format=\"|%(align:30,middle)'%(refname:short)| %(authorname)'%(end)|\" >actual &&
	test_cmp expect actual
"

test_expect_success 'nested alignment with quote formatting' "
	cat >expect <<-\EOF &&
	|'           main               '|
	|'           side               '|
	|'       odd/spot               '|
	|'  annotated-tag               '|
	|'doubly-annotated-tag          '|
	|'doubly-signed-tag             '|
	|'           four               '|
	|'            one               '|
	|'     signed-tag               '|
	|'          three               '|
	|'            two               '|
	EOF
	git for-each-ref --shell --format='|%(align:30,left)%(align:15,right)%(refname:short)%(end)%(end)|' >actual &&
	test_cmp expect actual
"

test_expect_success 'check `%(contents:lines=1)`' '
	cat >expect <<-\EOF &&
	main |three
	side |four
	odd/spot |three
	annotated-tag |An annotated tag
	doubly-annotated-tag |Annotated doubly
	doubly-signed-tag |Signed doubly
	four |four
	one |one
	signed-tag |A signed tag
	three |three
	two |two
	EOF
	git for-each-ref --format="%(refname:short) |%(contents:lines=1)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check `%(contents:lines=0)`' '
	cat >expect <<-\EOF &&
	main |
	side |
	odd/spot |
	annotated-tag |
	doubly-annotated-tag |
	doubly-signed-tag |
	four |
	one |
	signed-tag |
	three |
	two |
	EOF
	git for-each-ref --format="%(refname:short) |%(contents:lines=0)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check `%(contents:lines=99999)`' '
	cat >expect <<-\EOF &&
	main |three
	side |four
	odd/spot |three
	annotated-tag |An annotated tag
	doubly-annotated-tag |Annotated doubly
	doubly-signed-tag |Signed doubly
	four |four
	one |one
	signed-tag |A signed tag
	three |three
	two |two
	EOF
	git for-each-ref --format="%(refname:short) |%(contents:lines=99999)" >actual &&
	test_cmp expect actual
'

test_expect_success '`%(contents:lines=-1)` should fail' '
	test_must_fail git for-each-ref --format="%(refname:short) |%(contents:lines=-1)"
'

test_expect_success 'setup for version sort' '
	test_commit foo1.3 &&
	test_commit foo1.6 &&
	test_commit foo1.10
'

test_expect_success 'version sort' '
	git for-each-ref --sort=version:refname --format="%(refname:short)" refs/tags/ | grep "foo" >actual &&
	cat >expect <<-\EOF &&
	foo1.3
	foo1.6
	foo1.10
	EOF
	test_cmp expect actual
'

test_expect_success 'version sort (shortened)' '
	git for-each-ref --sort=v:refname --format="%(refname:short)" refs/tags/ | grep "foo" >actual &&
	cat >expect <<-\EOF &&
	foo1.3
	foo1.6
	foo1.10
	EOF
	test_cmp expect actual
'

test_expect_success 'reverse version sort' '
	git for-each-ref --sort=-version:refname --format="%(refname:short)" refs/tags/ | grep "foo" >actual &&
	cat >expect <<-\EOF &&
	foo1.10
	foo1.6
	foo1.3
	EOF
	test_cmp expect actual
'

test_expect_success 'improper usage of %(if), %(then), %(else) and %(end) atoms' '
	test_must_fail git for-each-ref --format="%(if)" &&
	test_must_fail git for-each-ref --format="%(then) %(end)" &&
	test_must_fail git for-each-ref --format="%(else) %(end)" &&
	test_must_fail git for-each-ref --format="%(if) %(else) %(end)" &&
	test_must_fail git for-each-ref --format="%(if) %(then) %(then) %(end)" &&
	test_must_fail git for-each-ref --format="%(then) %(else) %(end)" &&
	test_must_fail git for-each-ref --format="%(if) %(else) %(end)" &&
	test_must_fail git for-each-ref --format="%(if) %(then) %(else)" &&
	test_must_fail git for-each-ref --format="%(if) %(else) %(then) %(end)" &&
	test_must_fail git for-each-ref --format="%(if) %(then) %(else) %(else) %(end)" &&
	test_must_fail git for-each-ref --format="%(if) %(end)"
'

test_expect_success 'check %(if)...%(then)...%(end) atoms' '
	git for-each-ref --format="%(refname)%(if)%(authorname)%(then) Author: %(authorname)%(end)" >actual &&
	cat >expect <<-\EOF &&
	refs/heads/main Author: A U Thor
	refs/heads/side Author: A U Thor
	refs/odd/spot Author: A U Thor
	refs/tags/annotated-tag
	refs/tags/doubly-annotated-tag
	refs/tags/doubly-signed-tag
	refs/tags/foo1.10 Author: A U Thor
	refs/tags/foo1.3 Author: A U Thor
	refs/tags/foo1.6 Author: A U Thor
	refs/tags/four Author: A U Thor
	refs/tags/one Author: A U Thor
	refs/tags/signed-tag
	refs/tags/three Author: A U Thor
	refs/tags/two Author: A U Thor
	EOF
	test_cmp expect actual
'

test_expect_success 'check %(if)...%(then)...%(else)...%(end) atoms' '
	git for-each-ref --format="%(if)%(authorname)%(then)%(authorname)%(else)No author%(end): %(refname)" >actual &&
	cat >expect <<-\EOF &&
	A U Thor: refs/heads/main
	A U Thor: refs/heads/side
	A U Thor: refs/odd/spot
	No author: refs/tags/annotated-tag
	No author: refs/tags/doubly-annotated-tag
	No author: refs/tags/doubly-signed-tag
	A U Thor: refs/tags/foo1.10
	A U Thor: refs/tags/foo1.3
	A U Thor: refs/tags/foo1.6
	A U Thor: refs/tags/four
	A U Thor: refs/tags/one
	No author: refs/tags/signed-tag
	A U Thor: refs/tags/three
	A U Thor: refs/tags/two
	EOF
	test_cmp expect actual
'
test_expect_success 'ignore spaces in %(if) atom usage' '
	git for-each-ref --format="%(refname:short): %(if)%(HEAD)%(then)Head ref%(else)Not Head ref%(end)" >actual &&
	cat >expect <<-\EOF &&
	main: Head ref
	side: Not Head ref
	odd/spot: Not Head ref
	annotated-tag: Not Head ref
	doubly-annotated-tag: Not Head ref
	doubly-signed-tag: Not Head ref
	foo1.10: Not Head ref
	foo1.3: Not Head ref
	foo1.6: Not Head ref
	four: Not Head ref
	one: Not Head ref
	signed-tag: Not Head ref
	three: Not Head ref
	two: Not Head ref
	EOF
	test_cmp expect actual
'

test_expect_success 'check %(if:equals=<string>)' '
	git for-each-ref --format="%(if:equals=main)%(refname:short)%(then)Found main%(else)Not main%(end)" refs/heads/ >actual &&
	cat >expect <<-\EOF &&
	Found main
	Not main
	EOF
	test_cmp expect actual
'

test_expect_success 'check %(if:notequals=<string>)' '
	git for-each-ref --format="%(if:notequals=main)%(refname:short)%(then)Not main%(else)Found main%(end)" refs/heads/ >actual &&
	cat >expect <<-\EOF &&
	Found main
	Not main
	EOF
	test_cmp expect actual
'

test_expect_success '--merged is compatible with --no-merged' '
	git for-each-ref --merged HEAD --no-merged HEAD
'

test_expect_success 'validate worktree atom' '
	cat >expect <<-EOF &&
	main: $(pwd)
	main_worktree: $(pwd)/worktree_dir
	side: not checked out
	EOF
	git worktree add -b main_worktree worktree_dir main &&
	git for-each-ref --format="%(refname:short): %(if)%(worktreepath)%(then)%(worktreepath)%(else)not checked out%(end)" refs/heads/ >actual &&
	rm -r worktree_dir &&
	git worktree prune &&
	test_cmp expect actual
'

test_done
