#!/bin/sh

test_description='git cvsimport basic tests'
. ./lib-cvs.sh

if ! test_have_prereq PERL; then
	say 'skipping git cvsimport tests, perl not available'
	test_done
fi

CVSROOT=$(pwd)/cvsroot
export CVSROOT

test_expect_success 'setup cvsroot' '$CVS init'

test_expect_success 'setup a cvs module' '

	mkdir "$CVSROOT/module" &&
	$CVS co -d module-cvs module &&
	cd module-cvs &&
	cat <<EOF >o_fortuna &&
O Fortuna
velut luna
statu variabilis,

semper crescis
aut decrescis;
vita detestabilis

nunc obdurat
et tunc curat
ludo mentis aciem,

egestatem,
potestatem
dissolvit ut glaciem.
EOF
	$CVS add o_fortuna &&
	cat <<EOF >message &&
add "O Fortuna" lyrics

These public domain lyrics make an excellent sample text.
EOF
	$CVS commit -F message &&
	cd ..
'

test_expect_success 'import a trivial module' '

	git cvsimport -a -z 0 -C module-git module &&
	test_cmp module-cvs/o_fortuna module-git/o_fortuna

'

test_expect_success 'pack refs' 'cd module-git && git gc && cd ..'

test_expect_success 'update cvs module' '

	cd module-cvs &&
	cat <<EOF >o_fortuna &&
O Fortune,
like the moon
you are changeable,

ever waxing
and waning;
hateful life

first oppresses
and then soothes
as fancy takes it;

poverty
and power
it melts them like ice.
EOF
	cat <<EOF >message &&
translate to English

My Latin is terrible.
EOF
	$CVS commit -F message &&
	cd ..
'

test_expect_success 'update git module' '

	cd module-git &&
	git cvsimport -a -z 0 module &&
	git merge origin &&
	cd .. &&
	test_cmp module-cvs/o_fortuna module-git/o_fortuna

'

test_expect_success 'update cvs module' '

	cd module-cvs &&
		echo 1 >tick &&
		$CVS add tick &&
		$CVS commit -m 1
	cd ..

'

test_expect_success 'cvsimport.module config works' '

	cd module-git &&
		git config cvsimport.module module &&
		git cvsimport -a -z0 &&
		git merge origin &&
	cd .. &&
	test_cmp module-cvs/tick module-git/tick

'

test_expect_success 'import from a CVS working tree' '

	$CVS co -d import-from-wt module &&
	cd import-from-wt &&
		git cvsimport -a -z0 &&
		echo 1 >expect &&
		git log -1 --pretty=format:%s%n >actual &&
		test_cmp actual expect &&
	cd ..

'

test_expect_success 'test entire HEAD' 'test_cmp_branch_tree master'

test_done
