#!/bin/sh

test_description='git-cvsimport basic tests'
. ./test-lib.sh

if ! ( type cvs && type cvsps ) >/dev/null 2>&1
then
	test_expect_success 'skipping cvsimport tests, cvs/cvsps not found' ''
	test_done
	exit
fi

CVSROOT=$(pwd)/cvsroot
export CVSROOT
# for clean cvsps cache
HOME=$(pwd)
export HOME

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

test_done
