#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='See why rewinding head breaks send-pack

'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cnt=64
test_expect_success setup '
	test_tick &&
	mkdir mozart mozart/is &&
	echo "cummit #0" >mozart/is/pink &&
	but update-index --add mozart/is/pink &&
	tree=$(but write-tree) &&
	cummit=$(echo "cummit #0" | but cummit-tree $tree) &&
	zero=$cummit &&
	parent=$zero &&
	i=0 &&
	while test $i -le $cnt
	do
		i=$(($i+1)) &&
		test_tick &&
		echo "cummit #$i" >mozart/is/pink &&
		but update-index --add mozart/is/pink &&
		tree=$(but write-tree) &&
		cummit=$(echo "cummit #$i" |
			 but cummit-tree $tree -p $parent) &&
		but update-ref refs/tags/cummit$i $cummit &&
		parent=$cummit || return 1
	done &&
	but update-ref HEAD "$cummit" &&
	but clone ./. victim &&
	( cd victim && but config receive.denyCurrentBranch warn && but log ) &&
	but update-ref HEAD "$zero" &&
	parent=$zero &&
	i=0 &&
	while test $i -le $cnt
	do
		i=$(($i+1)) &&
		test_tick &&
		echo "Rebase #$i" >mozart/is/pink &&
		but update-index --add mozart/is/pink &&
		tree=$(but write-tree) &&
		cummit=$(echo "Rebase #$i" | but cummit-tree $tree -p $parent) &&
		but update-ref refs/tags/rebase$i $cummit &&
		parent=$cummit || return 1
	done &&
	but update-ref HEAD "$cummit" &&
	echo Rebase &&
	but log'

test_expect_success 'pack the source repository' '
	but repack -a -d &&
	but prune
'

test_expect_success 'pack the destination repository' '
	(
		cd victim &&
		but repack -a -d &&
		but prune
	)
'

test_expect_success 'refuse pushing rewound head without --force' '
	pushed_head=$(but rev-parse --verify main) &&
	victim_orig=$(cd victim && but rev-parse --verify main) &&
	test_must_fail but send-pack ./victim main &&
	victim_head=$(cd victim && but rev-parse --verify main) &&
	test "$victim_head" = "$victim_orig" &&
	# this should update
	but send-pack --force ./victim main &&
	victim_head=$(cd victim && but rev-parse --verify main) &&
	test "$victim_head" = "$pushed_head"
'

test_expect_success 'push can be used to delete a ref' '
	( cd victim && but branch extra main ) &&
	but send-pack ./victim :extra main &&
	( cd victim &&
	  test_must_fail but rev-parse --verify extra )
'

test_expect_success 'refuse deleting push with denyDeletes' '
	(
		cd victim &&
		test_might_fail but branch -D extra &&
		but config receive.denyDeletes true &&
		but branch extra main
	) &&
	test_must_fail but send-pack ./victim :extra main
'

test_expect_success 'cannot override denyDeletes with but -c send-pack' '
	(
		cd victim &&
		test_might_fail but branch -D extra &&
		but config receive.denyDeletes true &&
		but branch extra main
	) &&
	test_must_fail but -c receive.denyDeletes=false \
					send-pack ./victim :extra main
'

test_expect_success 'override denyDeletes with but -c receive-pack' '
	(
		cd victim &&
		test_might_fail but branch -D extra &&
		but config receive.denyDeletes true &&
		but branch extra main
	) &&
	but send-pack \
		--receive-pack="but -c receive.denyDeletes=false receive-pack" \
		./victim :extra main
'

test_expect_success 'denyNonFastforwards trumps --force' '
	(
		cd victim &&
		test_might_fail but branch -D extra &&
		but config receive.denyNonFastforwards true
	) &&
	victim_orig=$(cd victim && but rev-parse --verify main) &&
	test_must_fail but send-pack --force ./victim main^:main &&
	victim_head=$(cd victim && but rev-parse --verify main) &&
	test "$victim_orig" = "$victim_head"
'

test_expect_success 'send-pack --all sends all branches' '
	# make sure we have at least 2 branches with different
	# values, just to be thorough
	but branch other-branch HEAD^ &&

	but init --bare all.but &&
	but send-pack --all all.but &&
	but for-each-ref refs/heads >expect &&
	but -C all.but for-each-ref refs/heads >actual &&
	test_cmp expect actual
'

test_expect_success 'push --all excludes remote-tracking hierarchy' '
	mkdir parent &&
	(
		cd parent &&
		but init && : >file && but add file && but cummit -m add
	) &&
	but clone parent child &&
	(
		cd child && but push --all
	) &&
	(
		cd parent &&
		test -z "$(but for-each-ref refs/remotes/origin)"
	)
'

test_expect_success 'receive-pack runs auto-gc in remote repo' '
	rm -rf parent child &&
	but init parent &&
	(
		# Setup a repo with 2 packs
		cd parent &&
		echo "Some text" >file.txt &&
		but add . &&
		but cummit -m "Initial cummit" &&
		but repack -adl &&
		echo "Some more text" >>file.txt &&
		but cummit -a -m "Second cummit" &&
		but repack
	) &&
	cp -R parent child &&
	(
		# Set the child to auto-pack if more than one pack exists
		cd child &&
		but config gc.autopacklimit 1 &&
		but config gc.autodetach false &&
		but branch test_auto_gc &&
		# And create a file that follows the temporary object naming
		# convention for the auto-gc to remove
		: >.but/objects/tmp_test_object &&
		test-tool chmtime =-1209601 .but/objects/tmp_test_object
	) &&
	(
		cd parent &&
		echo "Even more text" >>file.txt &&
		but cummit -a -m "Third cummit" &&
		but send-pack ../child HEAD:refs/heads/test_auto_gc
	) &&
	test ! -e child/.but/objects/tmp_test_object
'

rewound_push_setup() {
	rm -rf parent child &&
	mkdir parent &&
	(
		cd parent &&
		but init &&
		echo one >file && but add file && but cummit -m one &&
		but config receive.denyCurrentBranch warn &&
		echo two >file && but cummit -a -m two
	) &&
	but clone parent child &&
	(
		cd child && but reset --hard HEAD^
	)
}

test_expect_success 'pushing explicit refspecs respects forcing' '
	rewound_push_setup &&
	parent_orig=$(cd parent && but rev-parse --verify main) &&
	(
		cd child &&
		test_must_fail but send-pack ../parent \
			refs/heads/main:refs/heads/main
	) &&
	parent_head=$(cd parent && but rev-parse --verify main) &&
	test "$parent_orig" = "$parent_head" &&
	(
		cd child &&
		but send-pack ../parent \
			+refs/heads/main:refs/heads/main
	) &&
	parent_head=$(cd parent && but rev-parse --verify main) &&
	child_head=$(cd child && but rev-parse --verify main) &&
	test "$parent_head" = "$child_head"
'

test_expect_success 'pushing wildcard refspecs respects forcing' '
	rewound_push_setup &&
	parent_orig=$(cd parent && but rev-parse --verify main) &&
	(
		cd child &&
		test_must_fail but send-pack ../parent \
			"refs/heads/*:refs/heads/*"
	) &&
	parent_head=$(cd parent && but rev-parse --verify main) &&
	test "$parent_orig" = "$parent_head" &&
	(
		cd child &&
		but send-pack ../parent \
			"+refs/heads/*:refs/heads/*"
	) &&
	parent_head=$(cd parent && but rev-parse --verify main) &&
	child_head=$(cd child && but rev-parse --verify main) &&
	test "$parent_head" = "$child_head"
'

test_expect_success 'deny pushing to delete current branch' '
	rewound_push_setup &&
	(
		cd child &&
		test_must_fail but send-pack ../parent :refs/heads/main 2>errs
	)
'

extract_ref_advertisement () {
	perl -lne '
		# \\ is there to skip capabilities after \0
		/push< ([^\\]+)/ or next;
		exit 0 if $1 eq "0000";
		print $1;
	'
}

test_expect_success 'receive-pack de-dupes .have lines' '
	but init shared &&
	but -C shared cummit --allow-empty -m both &&
	but clone -s shared fork &&
	(
		cd shared &&
		but checkout -b only-shared &&
		but cummit --allow-empty -m only-shared &&
		but update-ref refs/heads/foo HEAD
	) &&

	# Notable things in this expectation:
	#  - local refs are not de-duped
	#  - .have does not duplicate locals
	#  - .have does not duplicate itself
	local=$(but -C fork rev-parse HEAD) &&
	shared=$(but -C shared rev-parse only-shared) &&
	cat >expect <<-EOF &&
	$local refs/heads/main
	$local refs/remotes/origin/HEAD
	$local refs/remotes/origin/main
	$shared .have
	EOF

	BUT_TRACE_PACKET=$(pwd)/trace BUT_TEST_PROTOCOL_VERSION=0 \
	but push \
		--receive-pack="unset BUT_TRACE_PACKET; but-receive-pack" \
		fork HEAD:foo &&
	extract_ref_advertisement <trace >refs &&
	test_cmp expect refs
'

test_done
