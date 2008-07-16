#!/bin/sh

test_description='remote tracking stats'

. ./test-lib.sh

advance () {
	echo "$1" >"$1" &&
	git add "$1" &&
	test_tick &&
	git commit -m "$1"
}

test_expect_success setup '
	for i in a b c;
	do
		advance $i || break
	done &&
	git clone . test &&
	(
		cd test &&
		git checkout -b b1 origin &&
		git reset --hard HEAD^ &&
		advance d &&
		git checkout -b b2 origin &&
		git reset --hard b1 &&
		git checkout -b b3 origin &&
		git reset --hard HEAD^ &&
		git checkout -b b4 origin &&
		advance e &&
		advance f
	)
'

script='s/^..\(b.\)[	 0-9a-f]*\[\([^]]*\)\].*/\1 \2/p'
cat >expect <<\EOF
b1 ahead 1, behind 1
b2 ahead 1, behind 1
b3 behind 1
b4 ahead 2
EOF

test_expect_success 'branch -v' '
	(
		cd test &&
		git branch -v
	) |
	sed -n -e "$script" >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout' '
	(
		cd test && git checkout b1
	) >actual &&
	grep -e "have 1 and 1 different" actual
'

test_expect_success 'status' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		# reports nothing to commit
		test_must_fail git status
	) >actual &&
	grep -e "have 1 and 1 different" actual
'


test_done
