#!/bin/sh

test_description='but cvsimport basic tests'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-cvs.sh

if ! test_have_prereq NOT_ROOT; then
	skip_all='When cvs is compiled with CVS_BADROOT cummits as root fail'
	test_done
fi

test_expect_success PERL 'setup cvsroot environment' '
	CVSROOT=$(pwd)/cvsroot &&
	export CVSROOT
'

test_expect_success PERL 'setup cvsroot' '$CVS init'

test_expect_success PERL 'setup a cvs module' '

	mkdir "$CVSROOT/module" &&
	$CVS co -d module-cvs module &&
	(cd module-cvs &&
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
	$CVS cummit -F message
	)
'

test_expect_success PERL 'import a trivial module' '

	but cvsimport -a -R -z 0 -C module-but module &&
	test_cmp module-cvs/o_fortuna module-but/o_fortuna

'

test_expect_success PERL 'pack refs' '(cd module-but && but gc)'

test_expect_success PERL 'initial import has correct .but/cvs-revisions' '

	(cd module-but &&
	 but log --format="o_fortuna 1.1 %H" -1) > expected &&
	test_cmp expected module-but/.but/cvs-revisions
'

test_expect_success PERL 'update cvs module' '
	(cd module-cvs &&
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
	$CVS cummit -F message
	)
'

test_expect_success PERL 'update but module' '

	(cd module-but &&
	but config cvsimport.trackRevisions true &&
	but cvsimport -a -z 0 module &&
	but merge origin
	) &&
	test_cmp module-cvs/o_fortuna module-but/o_fortuna

'

test_expect_success PERL 'update has correct .but/cvs-revisions' '

	(cd module-but &&
	 but log --format="o_fortuna 1.1 %H" -1 HEAD^ &&
	 but log --format="o_fortuna 1.2 %H" -1 HEAD) > expected &&
	test_cmp expected module-but/.but/cvs-revisions
'

test_expect_success PERL 'update cvs module' '

	(cd module-cvs &&
		echo 1 >tick &&
		$CVS add tick &&
		$CVS cummit -m 1
	)
'

test_expect_success PERL 'cvsimport.module config works' '

	(cd module-but &&
		but config cvsimport.module module &&
		but config cvsimport.trackRevisions true &&
		but cvsimport -a -z0 &&
		but merge origin
	) &&
	test_cmp module-cvs/tick module-but/tick

'

test_expect_success PERL 'second update has correct .but/cvs-revisions' '

	(cd module-but &&
	 but log --format="o_fortuna 1.1 %H" -1 HEAD^^ &&
	 but log --format="o_fortuna 1.2 %H" -1 HEAD^ &&
	 but log --format="tick 1.1 %H" -1 HEAD) > expected &&
	test_cmp expected module-but/.but/cvs-revisions
'

test_expect_success PERL 'import from a CVS working tree' '

	$CVS co -d import-from-wt module &&
	(cd import-from-wt &&
		but config cvsimport.trackRevisions false &&
		but cvsimport -a -z0 &&
		echo 1 >expect &&
		but log -1 --pretty=format:%s%n >actual &&
		test_cmp expect actual
	)

'

test_expect_success PERL 'no .but/cvs-revisions created by default' '

	! test -e import-from-wt/.but/cvs-revisions

'

test_expect_success PERL 'test entire HEAD' 'test_cmp_branch_tree main'

test_done
