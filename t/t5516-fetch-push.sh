#!/bin/sh

test_description='Basic fetch/push functionality.

This test checks the following functionality:

* command-line syntax
* refspecs
* fast-forward detection, and overriding it
* configuration
* hooks
* --porcelain output format
* hiderefs
* reflogs
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

D=$(pwd)

mk_empty () {
	repo_name="$1"
	test_when_finished "rm -rf \"$repo_name\"" &&
	test_path_is_missing "$repo_name" &&
	but init "$repo_name" &&
	but -C "$repo_name" config receive.denyCurrentBranch warn
}

mk_test () {
	repo_name="$1"
	shift

	mk_empty "$repo_name" &&
	(
		for ref in "$@"
		do
			but push "$repo_name" $the_first_cummit:refs/$ref ||
			exit
		done &&
		cd "$repo_name" &&
		for ref in "$@"
		do
			echo "$the_first_cummit" >expect &&
			but show-ref -s --verify refs/$ref >actual &&
			test_cmp expect actual ||
			exit
		done &&
		but fsck --full
	)
}

mk_test_with_hooks() {
	repo_name=$1
	mk_test "$@" &&
	test_hook -C "$repo_name" pre-receive <<-'EOF' &&
	cat - >>pre-receive.actual
	EOF

	test_hook -C "$repo_name" update <<-'EOF' &&
	printf "%s %s %s\n" "$@" >>update.actual
	EOF

	test_hook -C "$repo_name" post-receive <<-'EOF' &&
	cat - >>post-receive.actual
	EOF

	test_hook -C "$repo_name" post-update <<-'EOF'
	for ref in "$@"
	do
		printf "%s\n" "$ref" >>post-update.actual
	done
	EOF
}

mk_child() {
	test_when_finished "rm -rf \"$2\"" &&
	but clone "$1" "$2"
}

check_push_result () {
	test $# -ge 3 ||
	BUG "check_push_result requires at least 3 parameters"

	repo_name="$1"
	shift

	(
		cd "$repo_name" &&
		echo "$1" >expect &&
		shift &&
		for ref in "$@"
		do
			but show-ref -s --verify refs/$ref >actual &&
			test_cmp expect actual ||
			exit
		done &&
		but fsck --full
	)
}

test_expect_success setup '

	>path1 &&
	but add path1 &&
	test_tick &&
	but cummit -a -m repo &&
	the_first_cummit=$(but show-ref -s --verify refs/heads/main) &&

	>path2 &&
	but add path2 &&
	test_tick &&
	but cummit -a -m second &&
	the_cummit=$(but show-ref -s --verify refs/heads/main)

'

test_expect_success 'fetch without wildcard' '
	mk_empty testrepo &&
	(
		cd testrepo &&
		but fetch .. refs/heads/main:refs/remotes/origin/main &&

		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch with wildcard' '
	mk_empty testrepo &&
	(
		cd testrepo &&
		but config remote.up.url .. &&
		but config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		but fetch up &&

		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch with insteadOf' '
	mk_empty testrepo &&
	(
		TRASH=$(pwd)/ &&
		cd testrepo &&
		but config "url.$TRASH.insteadOf" trash/ &&
		but config remote.up.url trash/. &&
		but config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		but fetch up &&

		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch with pushInsteadOf (should not rewrite)' '
	mk_empty testrepo &&
	(
		TRASH=$(pwd)/ &&
		cd testrepo &&
		but config "url.trash/.pushInsteadOf" "$TRASH" &&
		but config remote.up.url "$TRASH." &&
		but config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		but fetch up &&

		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

grep_wrote () {
	object_count=$1
	file_name=$2
	grep 'write_pack_file/wrote.*"value":"'$1'"' $2
}

test_expect_success 'push without negotiation' '
	mk_empty testrepo &&
	but push testrepo $the_first_cummit:refs/remotes/origin/first_cummit &&
	test_cummit -C testrepo unrelated_cummit &&
	but -C testrepo config receive.hideRefs refs/remotes/origin/first_cummit &&
	test_when_finished "rm event" &&
	BUT_TRACE2_EVENT="$(pwd)/event" but -c protocol.version=2 push testrepo refs/heads/main:refs/remotes/origin/main &&
	grep_wrote 5 event # 2 cummits, 2 trees, 1 blob
'

test_expect_success 'push with negotiation' '
	mk_empty testrepo &&
	but push testrepo $the_first_cummit:refs/remotes/origin/first_cummit &&
	test_cummit -C testrepo unrelated_cummit &&
	but -C testrepo config receive.hideRefs refs/remotes/origin/first_cummit &&
	test_when_finished "rm event" &&
	BUT_TRACE2_EVENT="$(pwd)/event" but -c protocol.version=2 -c push.negotiate=1 push testrepo refs/heads/main:refs/remotes/origin/main &&
	grep_wrote 2 event # 1 cummit, 1 tree
'

test_expect_success 'push with negotiation proceeds anyway even if negotiation fails' '
	mk_empty testrepo &&
	but push testrepo $the_first_cummit:refs/remotes/origin/first_cummit &&
	test_cummit -C testrepo unrelated_cummit &&
	but -C testrepo config receive.hideRefs refs/remotes/origin/first_cummit &&
	test_when_finished "rm event" &&
	BUT_TEST_PROTOCOL_VERSION=0 BUT_TRACE2_EVENT="$(pwd)/event" \
		but -c push.negotiate=1 push testrepo refs/heads/main:refs/remotes/origin/main 2>err &&
	grep_wrote 5 event && # 2 cummits, 2 trees, 1 blob
	test_i18ngrep "push negotiation failed" err
'

test_expect_success 'push with negotiation does not attempt to fetch submodules' '
	mk_empty submodule_upstream &&
	test_cummit -C submodule_upstream submodule_cummit &&
	but submodule add ./submodule_upstream submodule &&
	mk_empty testrepo &&
	but push testrepo $the_first_cummit:refs/remotes/origin/first_cummit &&
	test_cummit -C testrepo unrelated_cummit &&
	but -C testrepo config receive.hideRefs refs/remotes/origin/first_cummit &&
	but -c submodule.recurse=true -c protocol.version=2 -c push.negotiate=1 push testrepo refs/heads/main:refs/remotes/origin/main 2>err &&
	! grep "Fetching submodule" err
'

test_expect_success 'push without wildcard' '
	mk_empty testrepo &&

	but push testrepo refs/heads/main:refs/remotes/origin/main &&
	(
		cd testrepo &&
		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with wildcard' '
	mk_empty testrepo &&

	but push testrepo "refs/heads/*:refs/remotes/origin/*" &&
	(
		cd testrepo &&
		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with insteadOf' '
	mk_empty testrepo &&
	TRASH="$(pwd)/" &&
	test_config "url.$TRASH.insteadOf" trash/ &&
	but push trash/testrepo refs/heads/main:refs/remotes/origin/main &&
	(
		cd testrepo &&
		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with pushInsteadOf' '
	mk_empty testrepo &&
	TRASH="$(pwd)/" &&
	test_config "url.$TRASH.pushInsteadOf" trash/ &&
	but push trash/testrepo refs/heads/main:refs/remotes/origin/main &&
	(
		cd testrepo &&
		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with pushInsteadOf and explicit pushurl (pushInsteadOf should not rewrite)' '
	mk_empty testrepo &&
	test_config "url.trash2/.pushInsteadOf" testrepo/ &&
	test_config "url.trash3/.pushInsteadOf" trash/wrong &&
	test_config remote.r.url trash/wrong &&
	test_config remote.r.pushurl "testrepo/" &&
	but push r refs/heads/main:refs/remotes/origin/main &&
	(
		cd testrepo &&
		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with matching heads' '

	mk_test testrepo heads/main &&
	but push testrepo : &&
	check_push_result testrepo $the_commit heads/main

'

test_expect_success 'push with matching heads on the command line' '

	mk_test testrepo heads/main &&
	but push testrepo : &&
	check_push_result testrepo $the_commit heads/main

'

test_expect_success 'failed (non-fast-forward) push with matching heads' '

	mk_test testrepo heads/main &&
	but push testrepo : &&
	but cummit --amend -massaged &&
	test_must_fail but push testrepo &&
	check_push_result testrepo $the_commit heads/main &&
	but reset --hard $the_cummit

'

test_expect_success 'push --force with matching heads' '

	mk_test testrepo heads/main &&
	but push testrepo : &&
	but cummit --amend -massaged &&
	but push --force testrepo : &&
	! check_push_result testrepo $the_commit heads/main &&
	but reset --hard $the_cummit

'

test_expect_success 'push with matching heads and forced update' '

	mk_test testrepo heads/main &&
	but push testrepo : &&
	but cummit --amend -massaged &&
	but push testrepo +: &&
	! check_push_result testrepo $the_commit heads/main &&
	but reset --hard $the_cummit

'

test_expect_success 'push with no ambiguity (1)' '

	mk_test testrepo heads/main &&
	but push testrepo main:main &&
	check_push_result testrepo $the_commit heads/main

'

test_expect_success 'push with no ambiguity (2)' '

	mk_test testrepo remotes/origin/main &&
	but push testrepo main:origin/main &&
	check_push_result testrepo $the_cummit remotes/origin/main

'

test_expect_success 'push with colon-less refspec, no ambiguity' '

	mk_test testrepo heads/main heads/t/main &&
	but branch -f t/main main &&
	but push testrepo main &&
	check_push_result testrepo $the_commit heads/main &&
	check_push_result testrepo $the_first_commit heads/t/main

'

test_expect_success 'push with weak ambiguity (1)' '

	mk_test testrepo heads/main remotes/origin/main &&
	but push testrepo main:main &&
	check_push_result testrepo $the_commit heads/main &&
	check_push_result testrepo $the_first_cummit remotes/origin/main

'

test_expect_success 'push with weak ambiguity (2)' '

	mk_test testrepo heads/main remotes/origin/main remotes/another/main &&
	but push testrepo main:main &&
	check_push_result testrepo $the_commit heads/main &&
	check_push_result testrepo $the_first_cummit remotes/origin/main remotes/another/main

'

test_expect_success 'push with ambiguity' '

	mk_test testrepo heads/frotz tags/frotz &&
	test_must_fail but push testrepo main:frotz &&
	check_push_result testrepo $the_first_commit heads/frotz tags/frotz

'

test_expect_success 'push with colon-less refspec (1)' '

	mk_test testrepo heads/frotz tags/frotz &&
	but branch -f frotz main &&
	but push testrepo frotz &&
	check_push_result testrepo $the_commit heads/frotz &&
	check_push_result testrepo $the_first_cummit tags/frotz

'

test_expect_success 'push with colon-less refspec (2)' '

	mk_test testrepo heads/frotz tags/frotz &&
	if but show-ref --verify -q refs/heads/frotz
	then
		but branch -D frotz
	fi &&
	but tag -f frotz &&
	but push -f testrepo frotz &&
	check_push_result testrepo $the_cummit tags/frotz &&
	check_push_result testrepo $the_first_commit heads/frotz

'

test_expect_success 'push with colon-less refspec (3)' '

	mk_test testrepo &&
	if but show-ref --verify -q refs/tags/frotz
	then
		but tag -d frotz
	fi &&
	but branch -f frotz main &&
	but push testrepo frotz &&
	check_push_result testrepo $the_commit heads/frotz &&
	test 1 = $( cd testrepo && but show-ref | wc -l )
'

test_expect_success 'push with colon-less refspec (4)' '

	mk_test testrepo &&
	if but show-ref --verify -q refs/heads/frotz
	then
		but branch -D frotz
	fi &&
	but tag -f frotz &&
	but push testrepo frotz &&
	check_push_result testrepo $the_cummit tags/frotz &&
	test 1 = $( cd testrepo && but show-ref | wc -l )

'

test_expect_success 'push head with non-existent, incomplete dest' '

	mk_test testrepo &&
	but push testrepo main:branch &&
	check_push_result testrepo $the_commit heads/branch

'

test_expect_success 'push tag with non-existent, incomplete dest' '

	mk_test testrepo &&
	but tag -f v1.0 &&
	but push testrepo v1.0:tag &&
	check_push_result testrepo $the_cummit tags/tag

'

test_expect_success 'push sha1 with non-existent, incomplete dest' '

	mk_test testrepo &&
	test_must_fail but push testrepo $(but rev-parse main):foo

'

test_expect_success 'push ref expression with non-existent, incomplete dest' '

	mk_test testrepo &&
	test_must_fail but push testrepo main^:branch

'

for head in HEAD @
do

	test_expect_success "push with $head" '
		mk_test testrepo heads/main &&
		but checkout main &&
		but push testrepo $head &&
		check_push_result testrepo $the_commit heads/main
	'

	test_expect_success "push with $head nonexisting at remote" '
		mk_test testrepo heads/main &&
		but checkout -b local main &&
		test_when_finished "but checkout main; but branch -D local" &&
		but push testrepo $head &&
		check_push_result testrepo $the_commit heads/local
	'

	test_expect_success "push with +$head" '
		mk_test testrepo heads/main &&
		but checkout -b local main &&
		test_when_finished "but checkout main; but branch -D local" &&
		but push testrepo main local &&
		check_push_result testrepo $the_commit heads/main &&
		check_push_result testrepo $the_commit heads/local &&

		# Without force rewinding should fail
		but reset --hard $head^ &&
		test_must_fail but push testrepo $head &&
		check_push_result testrepo $the_commit heads/local &&

		# With force rewinding should succeed
		but push testrepo +$head &&
		check_push_result testrepo $the_first_commit heads/local
	'

	test_expect_success "push $head with non-existent, incomplete dest" '
		mk_test testrepo &&
		but checkout main &&
		but push testrepo $head:branch &&
		check_push_result testrepo $the_commit heads/branch

	'

	test_expect_success "push with config remote.*.push = $head" '
		mk_test testrepo heads/local &&
		but checkout main &&
		but branch -f local $the_cummit &&
		test_when_finished "but branch -D local" &&
		(
			cd testrepo &&
			but checkout local &&
			but reset --hard $the_first_cummit
		) &&
		test_config remote.there.url testrepo &&
		test_config remote.there.push $head &&
		test_config branch.main.remote there &&
		but push &&
		check_push_result testrepo $the_commit heads/main &&
		check_push_result testrepo $the_first_commit heads/local
	'

done

test_expect_success "push to remote with no explicit refspec and config remote.*.push = src:dest" '
	mk_test testrepo heads/main &&
	but checkout $the_first_cummit &&
	test_config remote.there.url testrepo &&
	test_config remote.there.push refs/heads/main:refs/heads/main &&
	but push there &&
	check_push_result testrepo $the_commit heads/main
'

test_expect_success 'push with remote.pushdefault' '
	mk_test up_repo heads/main &&
	mk_test down_repo heads/main &&
	test_config remote.up.url up_repo &&
	test_config remote.down.url down_repo &&
	test_config branch.main.remote up &&
	test_config remote.pushdefault down &&
	test_config push.default matching &&
	but push &&
	check_push_result up_repo $the_first_commit heads/main &&
	check_push_result down_repo $the_commit heads/main
'

test_expect_success 'push with config remote.*.pushurl' '

	mk_test testrepo heads/main &&
	but checkout main &&
	test_config remote.there.url test2repo &&
	test_config remote.there.pushurl testrepo &&
	but push there : &&
	check_push_result testrepo $the_commit heads/main
'

test_expect_success 'push with config branch.*.pushremote' '
	mk_test up_repo heads/main &&
	mk_test side_repo heads/main &&
	mk_test down_repo heads/main &&
	test_config remote.up.url up_repo &&
	test_config remote.pushdefault side_repo &&
	test_config remote.down.url down_repo &&
	test_config branch.main.remote up &&
	test_config branch.main.pushremote down &&
	test_config push.default matching &&
	but push &&
	check_push_result up_repo $the_first_commit heads/main &&
	check_push_result side_repo $the_first_commit heads/main &&
	check_push_result down_repo $the_commit heads/main
'

test_expect_success 'branch.*.pushremote config order is irrelevant' '
	mk_test one_repo heads/main &&
	mk_test two_repo heads/main &&
	test_config remote.one.url one_repo &&
	test_config remote.two.url two_repo &&
	test_config branch.main.pushremote two_repo &&
	test_config remote.pushdefault one_repo &&
	test_config push.default matching &&
	but push &&
	check_push_result one_repo $the_first_commit heads/main &&
	check_push_result two_repo $the_commit heads/main
'

test_expect_success 'push with dry-run' '

	mk_test testrepo heads/main &&
	old_cummit=$(but -C testrepo show-ref -s --verify refs/heads/main) &&
	but push --dry-run testrepo : &&
	check_push_result testrepo $old_commit heads/main
'

test_expect_success 'push updates local refs' '

	mk_test testrepo heads/main &&
	mk_child testrepo child &&
	(
		cd child &&
		but pull .. main &&
		but push &&
		test $(but rev-parse main) = \
			$(but rev-parse remotes/origin/main)
	)

'

test_expect_success 'push updates up-to-date local refs' '

	mk_test testrepo heads/main &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(cd child1 && but pull .. main && but push) &&
	(
		cd child2 &&
		but pull ../child1 main &&
		but push &&
		test $(but rev-parse main) = \
			$(but rev-parse remotes/origin/main)
	)

'

test_expect_success 'push preserves up-to-date packed refs' '

	mk_test testrepo heads/main &&
	mk_child testrepo child &&
	(
		cd child &&
		but push &&
		! test -f .but/refs/remotes/origin/main
	)

'

test_expect_success 'push does not update local refs on failure' '

	mk_test testrepo heads/main &&
	mk_child testrepo child &&
	echo "#!/no/frobnication/today" >testrepo/.but/hooks/pre-receive &&
	chmod +x testrepo/.but/hooks/pre-receive &&
	(
		cd child &&
		but pull .. main &&
		test_must_fail but push &&
		test $(but rev-parse main) != \
			$(but rev-parse remotes/origin/main)
	)

'

test_expect_success 'allow deleting an invalid remote ref' '

	mk_test testrepo heads/branch &&
	rm -f testrepo/.but/objects/??/* &&
	but push testrepo :refs/heads/branch &&
	(cd testrepo && test_must_fail but rev-parse --verify refs/heads/branch)

'

test_expect_success 'pushing valid refs triggers post-receive and post-update hooks' '
	mk_test_with_hooks testrepo heads/main heads/next &&
	orgmain=$(cd testrepo && but show-ref -s --verify refs/heads/main) &&
	newmain=$(but show-ref -s --verify refs/heads/main) &&
	orgnext=$(cd testrepo && but show-ref -s --verify refs/heads/next) &&
	newnext=$ZERO_OID &&
	but push testrepo refs/heads/main:refs/heads/main :refs/heads/next &&
	(
		cd testrepo/.but &&
		cat >pre-receive.expect <<-EOF &&
		$orgmain $newmain refs/heads/main
		$orgnext $newnext refs/heads/next
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/main $orgmain $newmain
		refs/heads/next $orgnext $newnext
		EOF

		cat >post-receive.expect <<-EOF &&
		$orgmain $newmain refs/heads/main
		$orgnext $newnext refs/heads/next
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/main
		refs/heads/next
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'deleting dangling ref triggers hooks with correct args' '
	mk_test_with_hooks testrepo heads/branch &&
	orig=$(but -C testrepo rev-parse refs/heads/branch) &&
	rm -f testrepo/.but/objects/??/* &&
	but push testrepo :refs/heads/branch &&
	(
		cd testrepo/.but &&
		cat >pre-receive.expect <<-EOF &&
		$orig $ZERO_OID refs/heads/branch
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/branch $orig $ZERO_OID
		EOF

		cat >post-receive.expect <<-EOF &&
		$orig $ZERO_OID refs/heads/branch
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/branch
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'deletion of a non-existent ref is not fed to post-receive and post-update hooks' '
	mk_test_with_hooks testrepo heads/main &&
	orgmain=$(cd testrepo && but show-ref -s --verify refs/heads/main) &&
	newmain=$(but show-ref -s --verify refs/heads/main) &&
	but push testrepo main :refs/heads/nonexistent &&
	(
		cd testrepo/.but &&
		cat >pre-receive.expect <<-EOF &&
		$orgmain $newmain refs/heads/main
		$ZERO_OID $ZERO_OID refs/heads/nonexistent
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/main $orgmain $newmain
		refs/heads/nonexistent $ZERO_OID $ZERO_OID
		EOF

		cat >post-receive.expect <<-EOF &&
		$orgmain $newmain refs/heads/main
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/main
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'deletion of a non-existent ref alone does trigger post-receive and post-update hooks' '
	mk_test_with_hooks testrepo heads/main &&
	but push testrepo :refs/heads/nonexistent &&
	(
		cd testrepo/.but &&
		cat >pre-receive.expect <<-EOF &&
		$ZERO_OID $ZERO_OID refs/heads/nonexistent
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/nonexistent $ZERO_OID $ZERO_OID
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_path_is_missing post-receive.actual &&
		test_path_is_missing post-update.actual
	)
'

test_expect_success 'mixed ref updates, deletes, invalid deletes trigger hooks with correct input' '
	mk_test_with_hooks testrepo heads/main heads/next heads/seen &&
	orgmain=$(cd testrepo && but show-ref -s --verify refs/heads/main) &&
	newmain=$(but show-ref -s --verify refs/heads/main) &&
	orgnext=$(cd testrepo && but show-ref -s --verify refs/heads/next) &&
	newnext=$ZERO_OID &&
	orgseen=$(cd testrepo && but show-ref -s --verify refs/heads/seen) &&
	newseen=$(but show-ref -s --verify refs/heads/main) &&
	but push testrepo refs/heads/main:refs/heads/main \
	    refs/heads/main:refs/heads/seen :refs/heads/next \
	    :refs/heads/nonexistent &&
	(
		cd testrepo/.but &&
		cat >pre-receive.expect <<-EOF &&
		$orgmain $newmain refs/heads/main
		$orgnext $newnext refs/heads/next
		$orgseen $newseen refs/heads/seen
		$ZERO_OID $ZERO_OID refs/heads/nonexistent
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/main $orgmain $newmain
		refs/heads/next $orgnext $newnext
		refs/heads/seen $orgseen $newseen
		refs/heads/nonexistent $ZERO_OID $ZERO_OID
		EOF

		cat >post-receive.expect <<-EOF &&
		$orgmain $newmain refs/heads/main
		$orgnext $newnext refs/heads/next
		$orgseen $newseen refs/heads/seen
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/main
		refs/heads/next
		refs/heads/seen
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'allow deleting a ref using --delete' '
	mk_test testrepo heads/main &&
	(cd testrepo && but config receive.denyDeleteCurrent warn) &&
	but push testrepo --delete main &&
	(cd testrepo && test_must_fail but rev-parse --verify refs/heads/main)
'

test_expect_success 'allow deleting a tag using --delete' '
	mk_test testrepo heads/main &&
	but tag -a -m dummy_message deltag heads/main &&
	but push testrepo --tags &&
	(cd testrepo && but rev-parse --verify -q refs/tags/deltag) &&
	but push testrepo --delete tag deltag &&
	(cd testrepo && test_must_fail but rev-parse --verify refs/tags/deltag)
'

test_expect_success 'push --delete without args aborts' '
	mk_test testrepo heads/main &&
	test_must_fail but push testrepo --delete
'

test_expect_success 'push --delete refuses src:dest refspecs' '
	mk_test testrepo heads/main &&
	test_must_fail but push testrepo --delete main:foo
'

test_expect_success 'push --delete refuses empty string' '
	mk_test testrepo heads/master &&
	test_must_fail but push testrepo --delete ""
'

test_expect_success 'warn on push to HEAD of non-bare repository' '
	mk_test testrepo heads/main &&
	(
		cd testrepo &&
		but checkout main &&
		but config receive.denyCurrentBranch warn
	) &&
	but push testrepo main 2>stderr &&
	grep "warning: updating the current branch" stderr
'

test_expect_success 'deny push to HEAD of non-bare repository' '
	mk_test testrepo heads/main &&
	(
		cd testrepo &&
		but checkout main &&
		but config receive.denyCurrentBranch true
	) &&
	test_must_fail but push testrepo main
'

test_expect_success 'allow push to HEAD of bare repository (bare)' '
	mk_test testrepo heads/main &&
	(
		cd testrepo &&
		but checkout main &&
		but config receive.denyCurrentBranch true &&
		but config core.bare true
	) &&
	but push testrepo main 2>stderr &&
	! grep "warning: updating the current branch" stderr
'

test_expect_success 'allow push to HEAD of non-bare repository (config)' '
	mk_test testrepo heads/main &&
	(
		cd testrepo &&
		but checkout main &&
		but config receive.denyCurrentBranch false
	) &&
	but push testrepo main 2>stderr &&
	! grep "warning: updating the current branch" stderr
'

test_expect_success 'fetch with branches' '
	mk_empty testrepo &&
	but branch second $the_first_cummit &&
	but checkout second &&
	echo ".." > testrepo/.but/branches/branch1 &&
	(
		cd testrepo &&
		but fetch branch1 &&
		echo "$the_cummit cummit	refs/heads/branch1" >expect &&
		but for-each-ref refs/heads >actual &&
		test_cmp expect actual
	) &&
	but checkout main
'

test_expect_success 'fetch with branches containing #' '
	mk_empty testrepo &&
	echo "..#second" > testrepo/.but/branches/branch2 &&
	(
		cd testrepo &&
		but fetch branch2 &&
		echo "$the_first_cummit cummit	refs/heads/branch2" >expect &&
		but for-each-ref refs/heads >actual &&
		test_cmp expect actual
	) &&
	but checkout main
'

test_expect_success 'push with branches' '
	mk_empty testrepo &&
	but checkout second &&
	echo "testrepo" > .but/branches/branch1 &&
	but push branch1 &&
	(
		cd testrepo &&
		echo "$the_first_cummit cummit	refs/heads/main" >expect &&
		but for-each-ref refs/heads >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with branches containing #' '
	mk_empty testrepo &&
	echo "testrepo#branch3" > .but/branches/branch2 &&
	but push branch2 &&
	(
		cd testrepo &&
		echo "$the_first_cummit cummit	refs/heads/branch3" >expect &&
		but for-each-ref refs/heads >actual &&
		test_cmp expect actual
	) &&
	but checkout main
'

test_expect_success 'push into aliased refs (consistent)' '
	mk_test testrepo heads/main &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(
		cd child1 &&
		but branch foo &&
		but symbolic-ref refs/heads/bar refs/heads/foo &&
		but config receive.denyCurrentBranch false
	) &&
	(
		cd child2 &&
		>path2 &&
		but add path2 &&
		test_tick &&
		but cummit -a -m child2 &&
		but branch foo &&
		but branch bar &&
		but push ../child1 foo bar
	)
'

test_expect_success 'push into aliased refs (inconsistent)' '
	mk_test testrepo heads/main &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(
		cd child1 &&
		but branch foo &&
		but symbolic-ref refs/heads/bar refs/heads/foo &&
		but config receive.denyCurrentBranch false
	) &&
	(
		cd child2 &&
		>path2 &&
		but add path2 &&
		test_tick &&
		but cummit -a -m child2 &&
		but branch foo &&
		>path3 &&
		but add path3 &&
		test_tick &&
		but cummit -a -m child2 &&
		but branch bar &&
		test_must_fail but push ../child1 foo bar 2>stderr &&
		grep "refusing inconsistent update" stderr
	)
'

test_force_push_tag () {
	tag_type_description=$1
	tag_args=$2

	test_expect_success "force pushing required to update $tag_type_description" "
		mk_test testrepo heads/main &&
		mk_child testrepo child1 &&
		mk_child testrepo child2 &&
		(
			cd child1 &&
			but tag testTag &&
			but push ../child2 testTag &&
			>file1 &&
			but add file1 &&
			but cummit -m 'file1' &&
			but tag $tag_args testTag &&
			test_must_fail but push ../child2 testTag &&
			but push --force ../child2 testTag &&
			but tag $tag_args testTag HEAD~ &&
			test_must_fail but push ../child2 testTag &&
			but push --force ../child2 testTag &&

			# Clobbering without + in refspec needs --force
			but tag -f testTag &&
			test_must_fail but push ../child2 'refs/tags/*:refs/tags/*' &&
			but push --force ../child2 'refs/tags/*:refs/tags/*' &&

			# Clobbering with + in refspec does not need --force
			but tag -f testTag HEAD~ &&
			but push ../child2 '+refs/tags/*:refs/tags/*' &&

			# Clobbering with --no-force still obeys + in refspec
			but tag -f testTag &&
			but push --no-force ../child2 '+refs/tags/*:refs/tags/*' &&

			# Clobbering with/without --force and 'tag <name>' format
			but tag -f testTag HEAD~ &&
			test_must_fail but push ../child2 tag testTag &&
			but push --force ../child2 tag testTag
		)
	"
}

test_force_push_tag "lightweight tag" "-f"
test_force_push_tag "annotated tag" "-f -a -m'tag message'"

test_force_fetch_tag () {
	tag_type_description=$1
	tag_args=$2

	test_expect_success "fetch will not clobber an existing $tag_type_description without --force" "
		mk_test testrepo heads/main &&
		mk_child testrepo child1 &&
		mk_child testrepo child2 &&
		(
			cd testrepo &&
			but tag testTag &&
			but -C ../child1 fetch origin tag testTag &&
			>file1 &&
			but add file1 &&
			but cummit -m 'file1' &&
			but tag $tag_args testTag &&
			test_must_fail but -C ../child1 fetch origin tag testTag &&
			but -C ../child1 fetch origin '+refs/tags/*:refs/tags/*'
		)
	"
}

test_force_fetch_tag "lightweight tag" "-f"
test_force_fetch_tag "annotated tag" "-f -a -m'tag message'"

test_expect_success 'push --porcelain' '
	mk_empty testrepo &&
	echo >.but/foo  "To testrepo" &&
	echo >>.but/foo "*	refs/heads/main:refs/remotes/origin/main	[new reference]"  &&
	echo >>.but/foo "Done" &&
	but push >.but/bar --porcelain  testrepo refs/heads/main:refs/remotes/origin/main &&
	(
		cd testrepo &&
		echo "$the_cummit cummit	refs/remotes/origin/main" >expect &&
		but for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	) &&
	test_cmp .but/foo .but/bar
'

test_expect_success 'push --porcelain bad url' '
	mk_empty testrepo &&
	test_must_fail but push >.but/bar --porcelain asdfasdfasd refs/heads/main:refs/remotes/origin/main &&
	! grep -q Done .but/bar
'

test_expect_success 'push --porcelain rejected' '
	mk_empty testrepo &&
	but push testrepo refs/heads/main:refs/remotes/origin/main &&
	(cd testrepo &&
		but reset --hard origin/main^ &&
		but config receive.denyCurrentBranch true) &&

	echo >.but/foo  "To testrepo"  &&
	echo >>.but/foo "!	refs/heads/main:refs/heads/main	[remote rejected] (branch is currently checked out)" &&
	echo >>.but/foo "Done" &&

	test_must_fail but push >.but/bar --porcelain  testrepo refs/heads/main:refs/heads/main &&
	test_cmp .but/foo .but/bar
'

test_expect_success 'push --porcelain --dry-run rejected' '
	mk_empty testrepo &&
	but push testrepo refs/heads/main:refs/remotes/origin/main &&
	(cd testrepo &&
		but reset --hard origin/main &&
		but config receive.denyCurrentBranch true) &&

	echo >.but/foo  "To testrepo"  &&
	echo >>.but/foo "!	refs/heads/main^:refs/heads/main	[rejected] (non-fast-forward)" &&
	echo >>.but/foo "Done" &&

	test_must_fail but push >.but/bar --porcelain  --dry-run testrepo refs/heads/main^:refs/heads/main &&
	test_cmp .but/foo .but/bar
'

test_expect_success 'push --prune' '
	mk_test testrepo heads/main heads/second heads/foo heads/bar &&
	but push --prune testrepo : &&
	check_push_result testrepo $the_commit heads/main &&
	check_push_result testrepo $the_first_commit heads/second &&
	! check_push_result testrepo $the_first_commit heads/foo heads/bar
'

test_expect_success 'push --prune refspec' '
	mk_test testrepo tmp/main tmp/second tmp/foo tmp/bar &&
	but push --prune testrepo "refs/heads/*:refs/tmp/*" &&
	check_push_result testrepo $the_cummit tmp/main &&
	check_push_result testrepo $the_first_cummit tmp/second &&
	! check_push_result testrepo $the_first_cummit tmp/foo tmp/bar
'

for configsection in transfer receive
do
	test_expect_success "push to update a ref hidden by $configsection.hiderefs" '
		mk_test testrepo heads/main hidden/one hidden/two hidden/three &&
		(
			cd testrepo &&
			but config $configsection.hiderefs refs/hidden
		) &&

		# push to unhidden ref succeeds normally
		but push testrepo main:refs/heads/main &&
		check_push_result testrepo $the_commit heads/main &&

		# push to update a hidden ref should fail
		test_must_fail but push testrepo main:refs/hidden/one &&
		check_push_result testrepo $the_first_commit hidden/one &&

		# push to delete a hidden ref should fail
		test_must_fail but push testrepo :refs/hidden/two &&
		check_push_result testrepo $the_first_commit hidden/two &&

		# idempotent push to update a hidden ref should fail
		test_must_fail but push testrepo $the_first_cummit:refs/hidden/three &&
		check_push_result testrepo $the_first_commit hidden/three
	'
done

test_expect_success 'fetch exact SHA1' '
	mk_test testrepo heads/main hidden/one &&
	but push testrepo main:refs/hidden/one &&
	(
		cd testrepo &&
		but config transfer.hiderefs refs/hidden
	) &&
	check_push_result testrepo $the_commit hidden/one &&

	mk_child testrepo child &&
	(
		cd child &&

		# make sure $the_cummit does not exist here
		but repack -a -d &&
		but prune &&
		test_must_fail but cat-file -t $the_cummit &&

		# Some protocol versions (e.g. 2) support fetching
		# unadvertised objects, so restrict this test to v0.

		# fetching the hidden object should fail by default
		test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
			but fetch -v ../testrepo $the_cummit:refs/heads/copy 2>err &&
		test_i18ngrep "Server does not allow request for unadvertised object" err &&
		test_must_fail but rev-parse --verify refs/heads/copy &&

		# the server side can allow it to succeed
		(
			cd ../testrepo &&
			but config uploadpack.allowtipsha1inwant true
		) &&

		but fetch -v ../testrepo $the_cummit:refs/heads/copy main:refs/heads/extra &&
		cat >expect <<-EOF &&
		$the_cummit
		$the_first_cummit
		EOF
		{
			but rev-parse --verify refs/heads/copy &&
			but rev-parse --verify refs/heads/extra
		} >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch exact SHA1 in protocol v2' '
	mk_test testrepo heads/main hidden/one &&
	but push testrepo main:refs/hidden/one &&
	but -C testrepo config transfer.hiderefs refs/hidden &&
	check_push_result testrepo $the_commit hidden/one &&

	mk_child testrepo child &&
	but -C child config protocol.version 2 &&

	# make sure $the_cummit does not exist here
	but -C child repack -a -d &&
	but -C child prune &&
	test_must_fail but -C child cat-file -t $the_cummit &&

	# fetching the hidden object succeeds by default
	# NEEDSWORK: should this match the v0 behavior instead?
	but -C child fetch -v ../testrepo $the_cummit:refs/heads/copy
'

for configallowtipsha1inwant in true false
do
	test_expect_success "shallow fetch reachable SHA1 (but not a ref), allowtipsha1inwant=$configallowtipsha1inwant" '
		mk_empty testrepo &&
		(
			cd testrepo &&
			but config uploadpack.allowtipsha1inwant $configallowtipsha1inwant &&
			but cummit --allow-empty -m foo &&
			but cummit --allow-empty -m bar
		) &&
		SHA1=$(but --but-dir=testrepo/.but rev-parse HEAD^) &&
		mk_empty shallow &&
		(
			cd shallow &&
			# Some protocol versions (e.g. 2) support fetching
			# unadvertised objects, so restrict this test to v0.
			test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
				but fetch --depth=1 ../testrepo/.but $SHA1 &&
			but --but-dir=../testrepo/.but config uploadpack.allowreachablesha1inwant true &&
			but fetch --depth=1 ../testrepo/.but $SHA1 &&
			but cat-file cummit $SHA1
		)
	'

	test_expect_success "deny fetch unreachable SHA1, allowtipsha1inwant=$configallowtipsha1inwant" '
		mk_empty testrepo &&
		(
			cd testrepo &&
			but config uploadpack.allowtipsha1inwant $configallowtipsha1inwant &&
			but cummit --allow-empty -m foo &&
			but cummit --allow-empty -m bar &&
			but cummit --allow-empty -m xyz
		) &&
		SHA1_1=$(but --but-dir=testrepo/.but rev-parse HEAD^^) &&
		SHA1_2=$(but --but-dir=testrepo/.but rev-parse HEAD^) &&
		SHA1_3=$(but --but-dir=testrepo/.but rev-parse HEAD) &&
		(
			cd testrepo &&
			but reset --hard $SHA1_2 &&
			but cat-file cummit $SHA1_1 &&
			but cat-file cummit $SHA1_3
		) &&
		mk_empty shallow &&
		(
			cd shallow &&
			# Some protocol versions (e.g. 2) support fetching
			# unadvertised objects, so restrict this test to v0.
			test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
				but fetch ../testrepo/.but $SHA1_3 &&
			test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
				but fetch ../testrepo/.but $SHA1_1 &&
			but --but-dir=../testrepo/.but config uploadpack.allowreachablesha1inwant true &&
			but fetch ../testrepo/.but $SHA1_1 &&
			but cat-file cummit $SHA1_1 &&
			test_must_fail but cat-file cummit $SHA1_2 &&
			but fetch ../testrepo/.but $SHA1_2 &&
			but cat-file cummit $SHA1_2 &&
			test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
				but fetch ../testrepo/.but $SHA1_3 2>err &&
			# ideally we would insist this be on a "remote error:"
			# line, but it is racy; see the cummit message
			test_i18ngrep "not our ref.*$SHA1_3\$" err
		)
	'
done

test_expect_success 'fetch follows tags by default' '
	mk_test testrepo heads/main &&
	test_when_finished "rm -rf src" &&
	but init src &&
	(
		cd src &&
		but pull ../testrepo main &&
		but tag -m "annotated" tag &&
		but for-each-ref >tmp1 &&
		sed -n "p; s|refs/heads/main$|refs/remotes/origin/main|p" tmp1 |
		sort -k 3 >../expect
	) &&
	test_when_finished "rm -rf dst" &&
	but init dst &&
	(
		cd dst &&
		but remote add origin ../src &&
		but config branch.main.remote origin &&
		but config branch.main.merge refs/heads/main &&
		but pull &&
		but for-each-ref >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'peeled advertisements are not considered ref tips' '
	mk_empty testrepo &&
	but -C testrepo cummit --allow-empty -m one &&
	but -C testrepo cummit --allow-empty -m two &&
	but -C testrepo tag -m foo mytag HEAD^ &&
	oid=$(but -C testrepo rev-parse mytag^{cummit}) &&
	test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 \
		but fetch testrepo $oid 2>err &&
	test_i18ngrep "Server does not allow request for unadvertised object" err
'

test_expect_success 'pushing a specific ref applies remote.$name.push as refmap' '
	mk_test testrepo heads/main &&
	test_when_finished "rm -rf src" &&
	but init src &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	(
		cd src &&
		but pull ../testrepo main &&
		but branch next &&
		but config remote.dst.url ../dst &&
		but config remote.dst.push "+refs/heads/*:refs/remotes/src/*" &&
		but push dst main &&
		but show-ref refs/heads/main |
		sed -e "s|refs/heads/|refs/remotes/src/|" >../dst/expect
	) &&
	(
		cd dst &&
		test_must_fail but show-ref refs/heads/next &&
		test_must_fail but show-ref refs/heads/main &&
		but show-ref refs/remotes/src/main >actual
	) &&
	test_cmp dst/expect dst/actual
'

test_expect_success 'with no remote.$name.push, it is not used as refmap' '
	mk_test testrepo heads/main &&
	test_when_finished "rm -rf src" &&
	but init src &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	(
		cd src &&
		but pull ../testrepo main &&
		but branch next &&
		but config remote.dst.url ../dst &&
		but config push.default matching &&
		but push dst main &&
		but show-ref refs/heads/main >../dst/expect
	) &&
	(
		cd dst &&
		test_must_fail but show-ref refs/heads/next &&
		but show-ref refs/heads/main >actual
	) &&
	test_cmp dst/expect dst/actual
'

test_expect_success 'with no remote.$name.push, upstream mapping is used' '
	mk_test testrepo heads/main &&
	test_when_finished "rm -rf src" &&
	but init src &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	(
		cd src &&
		but pull ../testrepo main &&
		but branch next &&
		but config remote.dst.url ../dst &&
		but config remote.dst.fetch "+refs/heads/*:refs/remotes/dst/*" &&
		but config push.default upstream &&

		but config branch.main.merge refs/heads/trunk &&
		but config branch.main.remote dst &&

		but push dst main &&
		but show-ref refs/heads/main |
		sed -e "s|refs/heads/main|refs/heads/trunk|" >../dst/expect
	) &&
	(
		cd dst &&
		test_must_fail but show-ref refs/heads/main &&
		test_must_fail but show-ref refs/heads/next &&
		but show-ref refs/heads/trunk >actual
	) &&
	test_cmp dst/expect dst/actual
'

test_expect_success 'push does not follow tags by default' '
	mk_test testrepo heads/main &&
	test_when_finished "rm -rf src" &&
	but init src &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	(
		cd src &&
		but pull ../testrepo main &&
		but tag -m "annotated" tag &&
		but checkout -b another &&
		but cummit --allow-empty -m "future cummit" &&
		but tag -m "future" future &&
		but checkout main &&
		but for-each-ref refs/heads/main >../expect &&
		but push ../dst main
	) &&
	(
		cd dst &&
		but for-each-ref >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'push --follow-tags only pushes relevant tags' '
	mk_test testrepo heads/main &&
	test_when_finished "rm -rf src" &&
	but init src &&
	test_when_finished "rm -rf dst" &&
	but init --bare dst &&
	(
		cd src &&
		but pull ../testrepo main &&
		but tag -m "annotated" tag &&
		but checkout -b another &&
		but cummit --allow-empty -m "future cummit" &&
		but tag -m "future" future &&
		but checkout main &&
		but for-each-ref refs/heads/main refs/tags/tag >../expect &&
		but push --follow-tags ../dst main
	) &&
	(
		cd dst &&
		but for-each-ref >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'push --no-thin must produce non-thin pack' '
	cat >>path1 <<\EOF &&
keep base version of path1 big enough, compared to the new changes
later, in order to pass size heuristics in
builtin/pack-objects.c:try_delta()
EOF
	but cummit -am initial &&
	but init no-thin &&
	but --but-dir=no-thin/.but config receive.unpacklimit 0 &&
	but push no-thin/.but refs/heads/main:refs/heads/foo &&
	echo modified >> path1 &&
	but cummit -am modified &&
	but repack -adf &&
	rcvpck="but receive-pack --reject-thin-pack-for-testing" &&
	but push --no-thin --receive-pack="$rcvpck" no-thin/.but refs/heads/main:refs/heads/foo
'

test_expect_success 'pushing a tag pushes the tagged object' '
	blob=$(echo unreferenced | but hash-object -w --stdin) &&
	but tag -m foo tag-of-blob $blob &&
	test_when_finished "rm -rf dst.but" &&
	but init --bare dst.but &&
	but push dst.but tag-of-blob &&
	# the receiving index-pack should have noticed
	# any problems, but we double check
	echo unreferenced >expect &&
	but --but-dir=dst.but cat-file blob tag-of-blob >actual &&
	test_cmp expect actual
'

test_expect_success 'push into bare respects core.logallrefupdates' '
	test_when_finished "rm -rf dst.but" &&
	but init --bare dst.but &&
	but -C dst.but config core.logallrefupdates true &&

	# double push to test both with and without
	# the actual pack transfer
	but push dst.but main:one &&
	echo "one@{0} push" >expect &&
	but -C dst.but log -g --format="%gd %gs" one >actual &&
	test_cmp expect actual &&

	but push dst.but main:two &&
	echo "two@{0} push" >expect &&
	but -C dst.but log -g --format="%gd %gs" two >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch into bare respects core.logallrefupdates' '
	test_when_finished "rm -rf dst.but" &&
	but init --bare dst.but &&
	(
		cd dst.but &&
		but config core.logallrefupdates true &&

		# as above, we double-fetch to test both
		# with and without pack transfer
		but fetch .. main:one &&
		echo "one@{0} fetch .. main:one: storing head" >expect &&
		but log -g --format="%gd %gs" one >actual &&
		test_cmp expect actual &&

		but fetch .. main:two &&
		echo "two@{0} fetch .. main:two: storing head" >expect &&
		but log -g --format="%gd %gs" two >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'receive.denyCurrentBranch = updateInstead' '
	mk_empty testrepo &&
	but push testrepo main &&
	(
		cd testrepo &&
		but reset --hard &&
		but config receive.denyCurrentBranch updateInstead
	) &&
	test_cummit third path2 &&

	# Try pushing into a repository with pristine working tree
	but push testrepo main &&
	(
		cd testrepo &&
		but update-index -q --refresh &&
		but diff-files --quiet -- &&
		but diff-index --quiet --cached HEAD -- &&
		test third = "$(cat path2)" &&
		test $(but -C .. rev-parse HEAD) = $(but rev-parse HEAD)
	) &&

	# Try pushing into a repository with working tree needing a refresh
	(
		cd testrepo &&
		but reset --hard HEAD^ &&
		test $(but -C .. rev-parse HEAD^) = $(but rev-parse HEAD) &&
		test-tool chmtime +100 path1
	) &&
	but push testrepo main &&
	(
		cd testrepo &&
		but update-index -q --refresh &&
		but diff-files --quiet -- &&
		but diff-index --quiet --cached HEAD -- &&
		test_cmp ../path1 path1 &&
		test third = "$(cat path2)" &&
		test $(but -C .. rev-parse HEAD) = $(but rev-parse HEAD)
	) &&

	# Update what is to be pushed
	test_cummit fourth path2 &&

	# Try pushing into a repository with a dirty working tree
	# (1) the working tree updated
	(
		cd testrepo &&
		echo changed >path1
	) &&
	test_must_fail but push testrepo main &&
	(
		cd testrepo &&
		test $(but -C .. rev-parse HEAD^) = $(but rev-parse HEAD) &&
		but diff --quiet --cached &&
		test changed = "$(cat path1)"
	) &&

	# (2) the index updated
	(
		cd testrepo &&
		echo changed >path1 &&
		but add path1
	) &&
	test_must_fail but push testrepo main &&
	(
		cd testrepo &&
		test $(but -C .. rev-parse HEAD^) = $(but rev-parse HEAD) &&
		but diff --quiet &&
		test changed = "$(cat path1)"
	) &&

	# Introduce a new file in the update
	test_cummit fifth path3 &&

	# (3) the working tree has an untracked file that would interfere
	(
		cd testrepo &&
		but reset --hard &&
		echo changed >path3
	) &&
	test_must_fail but push testrepo main &&
	(
		cd testrepo &&
		test $(but -C .. rev-parse HEAD^^) = $(but rev-parse HEAD) &&
		but diff --quiet &&
		but diff --quiet --cached &&
		test changed = "$(cat path3)"
	) &&

	# (4) the target changes to what gets pushed but it still is a change
	(
		cd testrepo &&
		but reset --hard &&
		echo fifth >path3 &&
		but add path3
	) &&
	test_must_fail but push testrepo main &&
	(
		cd testrepo &&
		test $(but -C .. rev-parse HEAD^^) = $(but rev-parse HEAD) &&
		but diff --quiet &&
		test fifth = "$(cat path3)"
	) &&

	# (5) push into void
	test_when_finished "rm -rf void" &&
	but init void &&
	(
		cd void &&
		but config receive.denyCurrentBranch updateInstead
	) &&
	but push void main &&
	(
		cd void &&
		test $(but -C .. rev-parse main) = $(but rev-parse HEAD) &&
		but diff --quiet &&
		but diff --cached --quiet
	) &&

	# (6) updateInstead intervened by fast-forward check
	test_must_fail but push void main^:main &&
	test $(but -C void rev-parse HEAD) = $(but rev-parse main) &&
	but -C void diff --quiet &&
	but -C void diff --cached --quiet
'

test_expect_success 'updateInstead with push-to-checkout hook' '
	test_when_finished "rm -rf testrepo" &&
	but init testrepo &&
	but -C testrepo pull .. main &&
	but -C testrepo reset --hard HEAD^^ &&
	but -C testrepo tag initial &&
	but -C testrepo config receive.denyCurrentBranch updateInstead &&
	test_hook -C testrepo push-to-checkout <<-\EOF &&
	echo >&2 updating from $(but rev-parse HEAD)
	echo >&2 updating to "$1"

	but update-index -q --refresh &&
	but read-tree -u -m HEAD "$1" || {
		status=$?
		echo >&2 read-tree failed
		exit $status
	}
	EOF

	# Try pushing into a pristine
	but push testrepo main &&
	(
		cd testrepo &&
		but diff --quiet &&
		but diff HEAD --quiet &&
		test $(but -C .. rev-parse HEAD) = $(but rev-parse HEAD)
	) &&

	# Try pushing into a repository with conflicting change
	(
		cd testrepo &&
		but reset --hard initial &&
		echo conflicting >path2
	) &&
	test_must_fail but push testrepo main &&
	(
		cd testrepo &&
		test $(but rev-parse initial) = $(but rev-parse HEAD) &&
		test conflicting = "$(cat path2)" &&
		but diff-index --quiet --cached HEAD
	) &&

	# Try pushing into a repository with unrelated change
	(
		cd testrepo &&
		but reset --hard initial &&
		echo unrelated >path1 &&
		echo irrelevant >path5 &&
		but add path5
	) &&
	but push testrepo main &&
	(
		cd testrepo &&
		test "$(cat path1)" = unrelated &&
		test "$(cat path5)" = irrelevant &&
		test "$(but diff --name-only --cached HEAD)" = path5 &&
		test $(but -C .. rev-parse HEAD) = $(but rev-parse HEAD)
	) &&

	# push into void
	test_when_finished "rm -rf void" &&
	but init void &&
	but -C void config receive.denyCurrentBranch updateInstead &&
	test_hook -C void push-to-checkout <<-\EOF &&
	if but rev-parse --quiet --verify HEAD
	then
		has_head=yes
		echo >&2 updating from $(but rev-parse HEAD)
	else
		has_head=no
		echo >&2 pushing into void
	fi
	echo >&2 updating to "$1"

	but update-index -q --refresh &&
	case "$has_head" in
	yes)
		but read-tree -u -m HEAD "$1" ;;
	no)
		but read-tree -u -m "$1" ;;
	esac || {
		status=$?
		echo >&2 read-tree failed
		exit $status
	}
	EOF

	but push void main &&
	(
		cd void &&
		but diff --quiet &&
		but diff --cached --quiet &&
		test $(but -C .. rev-parse HEAD) = $(but rev-parse HEAD)
	)
'

test_expect_success 'denyCurrentBranch and worktrees' '
	but worktree add new-wt &&
	but clone . cloned &&
	test_cummit -C cloned first &&
	test_config receive.denyCurrentBranch refuse &&
	test_must_fail but -C cloned push origin HEAD:new-wt &&
	test_config receive.denyCurrentBranch updateInstead &&
	but -C cloned push origin HEAD:new-wt &&
	test_path_exists new-wt/first.t &&
	test_must_fail but -C cloned push --delete origin new-wt
'

test_expect_success 'denyCurrentBranch and bare repository worktrees' '
	test_when_finished "rm -fr bare.but" &&
	but clone --bare . bare.but &&
	but -C bare.but worktree add wt &&
	test_cummit grape &&
	but -C bare.but config receive.denyCurrentBranch refuse &&
	test_must_fail but push bare.but HEAD:wt &&
	but -C bare.but config receive.denyCurrentBranch updateInstead &&
	but push bare.but HEAD:wt &&
	test_path_exists bare.but/wt/grape.t &&
	test_must_fail but push --delete bare.but wt
'

test_expect_success 'refuse fetch to current branch of worktree' '
	test_when_finished "but worktree remove --force wt && but branch -D wt" &&
	but worktree add wt &&
	test_cummit apple &&
	test_must_fail but fetch . HEAD:wt &&
	but fetch -u . HEAD:wt
'

test_expect_success 'refuse fetch to current branch of bare repository worktree' '
	test_when_finished "rm -fr bare.but" &&
	but clone --bare . bare.but &&
	but -C bare.but worktree add wt &&
	test_cummit banana &&
	test_must_fail but -C bare.but fetch .. HEAD:wt &&
	but -C bare.but fetch -u .. HEAD:wt
'

test_expect_success 'refuse to push a hidden ref, and make sure do not pollute the repository' '
	mk_empty testrepo &&
	but -C testrepo config receive.hiderefs refs/hidden &&
	but -C testrepo config receive.unpackLimit 1 &&
	test_must_fail but push testrepo HEAD:refs/hidden/foo &&
	test_dir_is_empty testrepo/.but/objects/pack
'

test_done
