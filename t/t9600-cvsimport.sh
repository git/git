#!/bin/sh

test_description='git-cvsimport basic tests'
. ./test-lib.sh

CVSROOT=$(pwd)/cvsroot
export CVSROOT
# for clean cvsps cache
HOME=$(pwd)
export HOME

if ! type cvs >/dev/null 2>&1
then
	say 'skipping cvsimport tests, cvs not found'
	test_done
	exit
fi

cvsps_version=`cvsps -h 2>&1 | sed -ne 's/cvsps version //p'`
case "$cvsps_version" in
2.1)
	;;
'')
	say 'skipping cvsimport tests, cvsps not found'
	test_done
	exit
	;;
*)
	say 'skipping cvsimport tests, cvsps too old'
	test_done
	exit
	;;
esac

test_expect_success 'setup cvsroot' 'cvs init'

test_expect_success 'setup a cvs module' '

	mkdir $CVSROOT/module &&
	cvs co -d module-cvs module &&
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
	cvs add o_fortuna &&
	cat <<EOF >message &&
add "O Fortuna" lyrics

These public domain lyrics make an excellent sample text.
EOF
	cvs commit -F message &&
	cd ..
'

test_expect_success 'import a trivial module' '

	git cvsimport -a -z 0 -C module-git module &&
	git diff module-cvs/o_fortuna module-git/o_fortuna

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
	cvs commit -F message &&
	cd ..
'

test_expect_success 'update git module' '

	cd module-git &&
	git cvsimport -a -z 0 module &&
	git merge origin &&
	cd .. &&
	git diff module-cvs/o_fortuna module-git/o_fortuna

'

test_expect_success 'update cvs module' '

	cd module-cvs &&
		echo 1 >tick &&
		cvs add tick &&
		cvs commit -m 1
	cd ..

'

test_expect_success 'cvsimport.module config works' '

	cd module-git &&
		git config cvsimport.module module &&
		git cvsimport -a -z0 &&
		git merge origin &&
	cd .. &&
	git diff module-cvs/tick module-git/tick

'

test_expect_success 'import from a CVS working tree' '

	cvs co -d import-from-wt module &&
	cd import-from-wt &&
		git cvsimport -a -z0 &&
		echo 1 >expect &&
		git log -1 --pretty=format:%s%n >actual &&
		git diff actual expect &&
	cd ..

'

test_done
