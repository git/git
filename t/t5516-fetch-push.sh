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
'

. ./test-lib.sh

D=`pwd`

mk_empty () {
	repo_name="$1"
	rm -fr "$repo_name" &&
	mkdir "$repo_name" &&
	(
		cd "$repo_name" &&
		git init &&
		git config receive.denyCurrentBranch warn &&
		mv .git/hooks .git/hooks-disabled
	)
}

mk_test () {
	repo_name="$1"
	shift

	mk_empty "$repo_name" &&
	(
		for ref in "$@"
		do
			git push "$repo_name" $the_first_commit:refs/$ref ||
			exit
		done &&
		cd "$repo_name" &&
		for ref in "$@"
		do
			echo "$the_first_commit" >expect &&
			git show-ref -s --verify refs/$ref >actual &&
			test_cmp expect actual ||
			exit
		done &&
		git fsck --full
	)
}

mk_test_with_hooks() {
	repo_name=$1
	mk_test "$@" &&
	(
		cd "$repo_name" &&
		mkdir .git/hooks &&
		cd .git/hooks &&

		cat >pre-receive <<-'EOF' &&
		#!/bin/sh
		cat - >>pre-receive.actual
		EOF

		cat >update <<-'EOF' &&
		#!/bin/sh
		printf "%s %s %s\n" "$@" >>update.actual
		EOF

		cat >post-receive <<-'EOF' &&
		#!/bin/sh
		cat - >>post-receive.actual
		EOF

		cat >post-update <<-'EOF' &&
		#!/bin/sh
		for ref in "$@"
		do
			printf "%s\n" "$ref" >>post-update.actual
		done
		EOF

		chmod +x pre-receive update post-receive post-update
	)
}

mk_child() {
	rm -rf "$2" &&
	git clone "$1" "$2"
}

check_push_result () {
	repo_name="$1"
	shift

	(
		cd "$repo_name" &&
		echo "$1" >expect &&
		shift &&
		for ref in "$@"
		do
			git show-ref -s --verify refs/$ref >actual &&
			test_cmp expect actual ||
			exit
		done &&
		git fsck --full
	)
}

test_expect_success setup '

	>path1 &&
	git add path1 &&
	test_tick &&
	git commit -a -m repo &&
	the_first_commit=$(git show-ref -s --verify refs/heads/master) &&

	>path2 &&
	git add path2 &&
	test_tick &&
	git commit -a -m second &&
	the_commit=$(git show-ref -s --verify refs/heads/master)

'

test_expect_success 'fetch without wildcard' '
	mk_empty testrepo &&
	(
		cd testrepo &&
		git fetch .. refs/heads/master:refs/remotes/origin/master &&

		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch with wildcard' '
	mk_empty testrepo &&
	(
		cd testrepo &&
		git config remote.up.url .. &&
		git config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		git fetch up &&

		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch with insteadOf' '
	mk_empty testrepo &&
	(
		TRASH=$(pwd)/ &&
		cd testrepo &&
		git config "url.$TRASH.insteadOf" trash/ &&
		git config remote.up.url trash/. &&
		git config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		git fetch up &&

		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'fetch with pushInsteadOf (should not rewrite)' '
	mk_empty testrepo &&
	(
		TRASH=$(pwd)/ &&
		cd testrepo &&
		git config "url.trash/.pushInsteadOf" "$TRASH" &&
		git config remote.up.url "$TRASH." &&
		git config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		git fetch up &&

		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push without wildcard' '
	mk_empty testrepo &&

	git push testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with wildcard' '
	mk_empty testrepo &&

	git push testrepo "refs/heads/*:refs/remotes/origin/*" &&
	(
		cd testrepo &&
		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with insteadOf' '
	mk_empty testrepo &&
	TRASH="$(pwd)/" &&
	test_config "url.$TRASH.insteadOf" trash/ &&
	git push trash/testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with pushInsteadOf' '
	mk_empty testrepo &&
	TRASH="$(pwd)/" &&
	test_config "url.$TRASH.pushInsteadOf" trash/ &&
	git push trash/testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with pushInsteadOf and explicit pushurl (pushInsteadOf should not rewrite)' '
	mk_empty testrepo &&
	test_config "url.trash2/.pushInsteadOf" testrepo/ &&
	test_config "url.trash3/.pusnInsteadOf" trash/wrong &&
	test_config remote.r.url trash/wrong &&
	test_config remote.r.pushurl "testrepo/" &&
	git push r refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with matching heads' '

	mk_test testrepo heads/master &&
	git push testrepo : &&
	check_push_result testrepo $the_commit heads/master

'

test_expect_success 'push with matching heads on the command line' '

	mk_test testrepo heads/master &&
	git push testrepo : &&
	check_push_result testrepo $the_commit heads/master

'

test_expect_success 'failed (non-fast-forward) push with matching heads' '

	mk_test testrepo heads/master &&
	git push testrepo : &&
	git commit --amend -massaged &&
	test_must_fail git push testrepo &&
	check_push_result testrepo $the_commit heads/master &&
	git reset --hard $the_commit

'

test_expect_success 'push --force with matching heads' '

	mk_test testrepo heads/master &&
	git push testrepo : &&
	git commit --amend -massaged &&
	git push --force testrepo : &&
	! check_push_result testrepo $the_commit heads/master &&
	git reset --hard $the_commit

'

test_expect_success 'push with matching heads and forced update' '

	mk_test testrepo heads/master &&
	git push testrepo : &&
	git commit --amend -massaged &&
	git push testrepo +: &&
	! check_push_result testrepo $the_commit heads/master &&
	git reset --hard $the_commit

'

test_expect_success 'push with no ambiguity (1)' '

	mk_test testrepo heads/master &&
	git push testrepo master:master &&
	check_push_result testrepo $the_commit heads/master

'

test_expect_success 'push with no ambiguity (2)' '

	mk_test testrepo remotes/origin/master &&
	git push testrepo master:origin/master &&
	check_push_result testrepo $the_commit remotes/origin/master

'

test_expect_success 'push with colon-less refspec, no ambiguity' '

	mk_test testrepo heads/master heads/t/master &&
	git branch -f t/master master &&
	git push testrepo master &&
	check_push_result testrepo $the_commit heads/master &&
	check_push_result testrepo $the_first_commit heads/t/master

'

test_expect_success 'push with weak ambiguity (1)' '

	mk_test testrepo heads/master remotes/origin/master &&
	git push testrepo master:master &&
	check_push_result testrepo $the_commit heads/master &&
	check_push_result testrepo $the_first_commit remotes/origin/master

'

test_expect_success 'push with weak ambiguity (2)' '

	mk_test testrepo heads/master remotes/origin/master remotes/another/master &&
	git push testrepo master:master &&
	check_push_result testrepo $the_commit heads/master &&
	check_push_result testrepo $the_first_commit remotes/origin/master remotes/another/master

'

test_expect_success 'push with ambiguity' '

	mk_test testrepo heads/frotz tags/frotz &&
	test_must_fail git push testrepo master:frotz &&
	check_push_result testrepo $the_first_commit heads/frotz tags/frotz

'

test_expect_success 'push with colon-less refspec (1)' '

	mk_test testrepo heads/frotz tags/frotz &&
	git branch -f frotz master &&
	git push testrepo frotz &&
	check_push_result testrepo $the_commit heads/frotz &&
	check_push_result testrepo $the_first_commit tags/frotz

'

test_expect_success 'push with colon-less refspec (2)' '

	mk_test testrepo heads/frotz tags/frotz &&
	if git show-ref --verify -q refs/heads/frotz
	then
		git branch -D frotz
	fi &&
	git tag -f frotz &&
	git push -f testrepo frotz &&
	check_push_result testrepo $the_commit tags/frotz &&
	check_push_result testrepo $the_first_commit heads/frotz

'

test_expect_success 'push with colon-less refspec (3)' '

	mk_test testrepo &&
	if git show-ref --verify -q refs/tags/frotz
	then
		git tag -d frotz
	fi &&
	git branch -f frotz master &&
	git push testrepo frotz &&
	check_push_result testrepo $the_commit heads/frotz &&
	test 1 = $( cd testrepo && git show-ref | wc -l )
'

test_expect_success 'push with colon-less refspec (4)' '

	mk_test testrepo &&
	if git show-ref --verify -q refs/heads/frotz
	then
		git branch -D frotz
	fi &&
	git tag -f frotz &&
	git push testrepo frotz &&
	check_push_result testrepo $the_commit tags/frotz &&
	test 1 = $( cd testrepo && git show-ref | wc -l )

'

test_expect_success 'push head with non-existent, incomplete dest' '

	mk_test testrepo &&
	git push testrepo master:branch &&
	check_push_result testrepo $the_commit heads/branch

'

test_expect_success 'push tag with non-existent, incomplete dest' '

	mk_test testrepo &&
	git tag -f v1.0 &&
	git push testrepo v1.0:tag &&
	check_push_result testrepo $the_commit tags/tag

'

test_expect_success 'push sha1 with non-existent, incomplete dest' '

	mk_test testrepo &&
	test_must_fail git push testrepo `git rev-parse master`:foo

'

test_expect_success 'push ref expression with non-existent, incomplete dest' '

	mk_test testrepo &&
	test_must_fail git push testrepo master^:branch

'

test_expect_success 'push with HEAD' '

	mk_test testrepo heads/master &&
	git checkout master &&
	git push testrepo HEAD &&
	check_push_result testrepo $the_commit heads/master

'

test_expect_success 'push with HEAD nonexisting at remote' '

	mk_test testrepo heads/master &&
	git checkout -b local master &&
	git push testrepo HEAD &&
	check_push_result testrepo $the_commit heads/local
'

test_expect_success 'push with +HEAD' '

	mk_test testrepo heads/master &&
	git checkout master &&
	git branch -D local &&
	git checkout -b local &&
	git push testrepo master local &&
	check_push_result testrepo $the_commit heads/master &&
	check_push_result testrepo $the_commit heads/local &&

	# Without force rewinding should fail
	git reset --hard HEAD^ &&
	test_must_fail git push testrepo HEAD &&
	check_push_result testrepo $the_commit heads/local &&

	# With force rewinding should succeed
	git push testrepo +HEAD &&
	check_push_result testrepo $the_first_commit heads/local

'

test_expect_success 'push HEAD with non-existent, incomplete dest' '

	mk_test testrepo &&
	git checkout master &&
	git push testrepo HEAD:branch &&
	check_push_result testrepo $the_commit heads/branch

'

test_expect_success 'push with config remote.*.push = HEAD' '

	mk_test testrepo heads/local &&
	git checkout master &&
	git branch -f local $the_commit &&
	(
		cd testrepo &&
		git checkout local &&
		git reset --hard $the_first_commit
	) &&
	test_config remote.there.url testrepo &&
	test_config remote.there.push HEAD &&
	test_config branch.master.remote there &&
	git push &&
	check_push_result testrepo $the_commit heads/master &&
	check_push_result testrepo $the_first_commit heads/local
'

test_expect_success 'push with remote.pushdefault' '
	mk_test up_repo heads/master &&
	mk_test down_repo heads/master &&
	test_config remote.up.url up_repo &&
	test_config remote.down.url down_repo &&
	test_config branch.master.remote up &&
	test_config remote.pushdefault down &&
	test_config push.default matching &&
	git push &&
	check_push_result up_repo $the_first_commit heads/master &&
	check_push_result down_repo $the_commit heads/master
'

test_expect_success 'push with config remote.*.pushurl' '

	mk_test testrepo heads/master &&
	git checkout master &&
	test_config remote.there.url test2repo &&
	test_config remote.there.pushurl testrepo &&
	git push there : &&
	check_push_result testrepo $the_commit heads/master
'

test_expect_success 'push with config branch.*.pushremote' '
	mk_test up_repo heads/master &&
	mk_test side_repo heads/master &&
	mk_test down_repo heads/master &&
	test_config remote.up.url up_repo &&
	test_config remote.pushdefault side_repo &&
	test_config remote.down.url down_repo &&
	test_config branch.master.remote up &&
	test_config branch.master.pushremote down &&
	test_config push.default matching &&
	git push &&
	check_push_result up_repo $the_first_commit heads/master &&
	check_push_result side_repo $the_first_commit heads/master &&
	check_push_result down_repo $the_commit heads/master
'

test_expect_success 'branch.*.pushremote config order is irrelevant' '
	mk_test one_repo heads/master &&
	mk_test two_repo heads/master &&
	test_config remote.one.url one_repo &&
	test_config remote.two.url two_repo &&
	test_config branch.master.pushremote two_repo &&
	test_config remote.pushdefault one_repo &&
	test_config push.default matching &&
	git push &&
	check_push_result one_repo $the_first_commit heads/master &&
	check_push_result two_repo $the_commit heads/master
'

test_expect_success 'push with dry-run' '

	mk_test testrepo heads/master &&
	(
		cd testrepo &&
		old_commit=$(git show-ref -s --verify refs/heads/master)
	) &&
	git push --dry-run testrepo : &&
	check_push_result testrepo $old_commit heads/master
'

test_expect_success 'push updates local refs' '

	mk_test testrepo heads/master &&
	mk_child testrepo child &&
	(
		cd child &&
		git pull .. master &&
		git push &&
		test $(git rev-parse master) = \
			$(git rev-parse remotes/origin/master)
	)

'

test_expect_success 'push updates up-to-date local refs' '

	mk_test testrepo heads/master &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(cd child1 && git pull .. master && git push) &&
	(
		cd child2 &&
		git pull ../child1 master &&
		git push &&
		test $(git rev-parse master) = \
			$(git rev-parse remotes/origin/master)
	)

'

test_expect_success 'push preserves up-to-date packed refs' '

	mk_test testrepo heads/master &&
	mk_child testrepo child &&
	(
		cd child &&
		git push &&
		! test -f .git/refs/remotes/origin/master
	)

'

test_expect_success 'push does not update local refs on failure' '

	mk_test testrepo heads/master &&
	mk_child testrepo child &&
	mkdir testrepo/.git/hooks &&
	echo "#!/no/frobnication/today" >testrepo/.git/hooks/pre-receive &&
	chmod +x testrepo/.git/hooks/pre-receive &&
	(
		cd child &&
		git pull .. master
		test_must_fail git push &&
		test $(git rev-parse master) != \
			$(git rev-parse remotes/origin/master)
	)

'

test_expect_success 'allow deleting an invalid remote ref' '

	mk_test testrepo heads/master &&
	rm -f testrepo/.git/objects/??/* &&
	git push testrepo :refs/heads/master &&
	(cd testrepo && test_must_fail git rev-parse --verify refs/heads/master)

'

test_expect_success 'pushing valid refs triggers post-receive and post-update hooks' '
	mk_test_with_hooks testrepo heads/master heads/next &&
	orgmaster=$(cd testrepo && git show-ref -s --verify refs/heads/master) &&
	newmaster=$(git show-ref -s --verify refs/heads/master) &&
	orgnext=$(cd testrepo && git show-ref -s --verify refs/heads/next) &&
	newnext=$_z40 &&
	git push testrepo refs/heads/master:refs/heads/master :refs/heads/next &&
	(
		cd testrepo/.git &&
		cat >pre-receive.expect <<-EOF &&
		$orgmaster $newmaster refs/heads/master
		$orgnext $newnext refs/heads/next
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/master $orgmaster $newmaster
		refs/heads/next $orgnext $newnext
		EOF

		cat >post-receive.expect <<-EOF &&
		$orgmaster $newmaster refs/heads/master
		$orgnext $newnext refs/heads/next
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/master
		refs/heads/next
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'deleting dangling ref triggers hooks with correct args' '
	mk_test_with_hooks testrepo heads/master &&
	rm -f testrepo/.git/objects/??/* &&
	git push testrepo :refs/heads/master &&
	(
		cd testrepo/.git &&
		cat >pre-receive.expect <<-EOF &&
		$_z40 $_z40 refs/heads/master
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/master $_z40 $_z40
		EOF

		cat >post-receive.expect <<-EOF &&
		$_z40 $_z40 refs/heads/master
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/master
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'deletion of a non-existent ref is not fed to post-receive and post-update hooks' '
	mk_test_with_hooks testrepo heads/master &&
	orgmaster=$(cd testrepo && git show-ref -s --verify refs/heads/master) &&
	newmaster=$(git show-ref -s --verify refs/heads/master) &&
	git push testrepo master :refs/heads/nonexistent &&
	(
		cd testrepo/.git &&
		cat >pre-receive.expect <<-EOF &&
		$orgmaster $newmaster refs/heads/master
		$_z40 $_z40 refs/heads/nonexistent
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/master $orgmaster $newmaster
		refs/heads/nonexistent $_z40 $_z40
		EOF

		cat >post-receive.expect <<-EOF &&
		$orgmaster $newmaster refs/heads/master
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/master
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'deletion of a non-existent ref alone does trigger post-receive and post-update hooks' '
	mk_test_with_hooks testrepo heads/master &&
	git push testrepo :refs/heads/nonexistent &&
	(
		cd testrepo/.git &&
		cat >pre-receive.expect <<-EOF &&
		$_z40 $_z40 refs/heads/nonexistent
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/nonexistent $_z40 $_z40
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_path_is_missing post-receive.actual &&
		test_path_is_missing post-update.actual
	)
'

test_expect_success 'mixed ref updates, deletes, invalid deletes trigger hooks with correct input' '
	mk_test_with_hooks testrepo heads/master heads/next heads/pu &&
	orgmaster=$(cd testrepo && git show-ref -s --verify refs/heads/master) &&
	newmaster=$(git show-ref -s --verify refs/heads/master) &&
	orgnext=$(cd testrepo && git show-ref -s --verify refs/heads/next) &&
	newnext=$_z40 &&
	orgpu=$(cd testrepo && git show-ref -s --verify refs/heads/pu) &&
	newpu=$(git show-ref -s --verify refs/heads/master) &&
	git push testrepo refs/heads/master:refs/heads/master \
	    refs/heads/master:refs/heads/pu :refs/heads/next \
	    :refs/heads/nonexistent &&
	(
		cd testrepo/.git &&
		cat >pre-receive.expect <<-EOF &&
		$orgmaster $newmaster refs/heads/master
		$orgnext $newnext refs/heads/next
		$orgpu $newpu refs/heads/pu
		$_z40 $_z40 refs/heads/nonexistent
		EOF

		cat >update.expect <<-EOF &&
		refs/heads/master $orgmaster $newmaster
		refs/heads/next $orgnext $newnext
		refs/heads/pu $orgpu $newpu
		refs/heads/nonexistent $_z40 $_z40
		EOF

		cat >post-receive.expect <<-EOF &&
		$orgmaster $newmaster refs/heads/master
		$orgnext $newnext refs/heads/next
		$orgpu $newpu refs/heads/pu
		EOF

		cat >post-update.expect <<-EOF &&
		refs/heads/master
		refs/heads/next
		refs/heads/pu
		EOF

		test_cmp pre-receive.expect pre-receive.actual &&
		test_cmp update.expect update.actual &&
		test_cmp post-receive.expect post-receive.actual &&
		test_cmp post-update.expect post-update.actual
	)
'

test_expect_success 'allow deleting a ref using --delete' '
	mk_test testrepo heads/master &&
	(cd testrepo && git config receive.denyDeleteCurrent warn) &&
	git push testrepo --delete master &&
	(cd testrepo && test_must_fail git rev-parse --verify refs/heads/master)
'

test_expect_success 'allow deleting a tag using --delete' '
	mk_test testrepo heads/master &&
	git tag -a -m dummy_message deltag heads/master &&
	git push testrepo --tags &&
	(cd testrepo && git rev-parse --verify -q refs/tags/deltag) &&
	git push testrepo --delete tag deltag &&
	(cd testrepo && test_must_fail git rev-parse --verify refs/tags/deltag)
'

test_expect_success 'push --delete without args aborts' '
	mk_test testrepo heads/master &&
	test_must_fail git push testrepo --delete
'

test_expect_success 'push --delete refuses src:dest refspecs' '
	mk_test testrepo heads/master &&
	test_must_fail git push testrepo --delete master:foo
'

test_expect_success 'warn on push to HEAD of non-bare repository' '
	mk_test testrepo heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch warn
	) &&
	git push testrepo master 2>stderr &&
	grep "warning: updating the current branch" stderr
'

test_expect_success 'deny push to HEAD of non-bare repository' '
	mk_test testrepo heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch true
	) &&
	test_must_fail git push testrepo master
'

test_expect_success 'allow push to HEAD of bare repository (bare)' '
	mk_test testrepo heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch true &&
		git config core.bare true
	) &&
	git push testrepo master 2>stderr &&
	! grep "warning: updating the current branch" stderr
'

test_expect_success 'allow push to HEAD of non-bare repository (config)' '
	mk_test testrepo heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch false
	) &&
	git push testrepo master 2>stderr &&
	! grep "warning: updating the current branch" stderr
'

test_expect_success 'fetch with branches' '
	mk_empty testrepo &&
	git branch second $the_first_commit &&
	git checkout second &&
	echo ".." > testrepo/.git/branches/branch1 &&
	(
		cd testrepo &&
		git fetch branch1 &&
		echo "$the_commit commit	refs/heads/branch1" >expect &&
		git for-each-ref refs/heads >actual &&
		test_cmp expect actual
	) &&
	git checkout master
'

test_expect_success 'fetch with branches containing #' '
	mk_empty testrepo &&
	echo "..#second" > testrepo/.git/branches/branch2 &&
	(
		cd testrepo &&
		git fetch branch2 &&
		echo "$the_first_commit commit	refs/heads/branch2" >expect &&
		git for-each-ref refs/heads >actual &&
		test_cmp expect actual
	) &&
	git checkout master
'

test_expect_success 'push with branches' '
	mk_empty testrepo &&
	git checkout second &&
	echo "testrepo" > .git/branches/branch1 &&
	git push branch1 &&
	(
		cd testrepo &&
		echo "$the_first_commit commit	refs/heads/master" >expect &&
		git for-each-ref refs/heads >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'push with branches containing #' '
	mk_empty testrepo &&
	echo "testrepo#branch3" > .git/branches/branch2 &&
	git push branch2 &&
	(
		cd testrepo &&
		echo "$the_first_commit commit	refs/heads/branch3" >expect &&
		git for-each-ref refs/heads >actual &&
		test_cmp expect actual
	) &&
	git checkout master
'

test_expect_success 'push into aliased refs (consistent)' '
	mk_test testrepo heads/master &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(
		cd child1 &&
		git branch foo &&
		git symbolic-ref refs/heads/bar refs/heads/foo
		git config receive.denyCurrentBranch false
	) &&
	(
		cd child2 &&
		>path2 &&
		git add path2 &&
		test_tick &&
		git commit -a -m child2 &&
		git branch foo &&
		git branch bar &&
		git push ../child1 foo bar
	)
'

test_expect_success 'push into aliased refs (inconsistent)' '
	mk_test testrepo heads/master &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(
		cd child1 &&
		git branch foo &&
		git symbolic-ref refs/heads/bar refs/heads/foo
		git config receive.denyCurrentBranch false
	) &&
	(
		cd child2 &&
		>path2 &&
		git add path2 &&
		test_tick &&
		git commit -a -m child2 &&
		git branch foo &&
		>path3 &&
		git add path3 &&
		test_tick &&
		git commit -a -m child2 &&
		git branch bar &&
		test_must_fail git push ../child1 foo bar 2>stderr &&
		grep "refusing inconsistent update" stderr
	)
'

test_expect_success 'push requires --force to update lightweight tag' '
	mk_test testrepo heads/master &&
	mk_child testrepo child1 &&
	mk_child testrepo child2 &&
	(
		cd child1 &&
		git tag Tag &&
		git push ../child2 Tag &&
		git push ../child2 Tag &&
		>file1 &&
		git add file1 &&
		git commit -m "file1" &&
		git tag -f Tag &&
		test_must_fail git push ../child2 Tag &&
		git push --force ../child2 Tag &&
		git tag -f Tag &&
		test_must_fail git push ../child2 Tag HEAD~ &&
		git push --force ../child2 Tag
	)
'

test_expect_success 'push --porcelain' '
	mk_empty testrepo &&
	echo >.git/foo  "To testrepo" &&
	echo >>.git/foo "*	refs/heads/master:refs/remotes/origin/master	[new branch]"  &&
	echo >>.git/foo "Done" &&
	git push >.git/bar --porcelain  testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		echo "$the_commit commit	refs/remotes/origin/master" >expect &&
		git for-each-ref refs/remotes/origin >actual &&
		test_cmp expect actual
	) &&
	test_cmp .git/foo .git/bar
'

test_expect_success 'push --porcelain bad url' '
	mk_empty testrepo &&
	test_must_fail git push >.git/bar --porcelain asdfasdfasd refs/heads/master:refs/remotes/origin/master &&
	test_must_fail grep -q Done .git/bar
'

test_expect_success 'push --porcelain rejected' '
	mk_empty testrepo &&
	git push testrepo refs/heads/master:refs/remotes/origin/master &&
	(cd testrepo &&
		git reset --hard origin/master^
		git config receive.denyCurrentBranch true) &&

	echo >.git/foo  "To testrepo"  &&
	echo >>.git/foo "!	refs/heads/master:refs/heads/master	[remote rejected] (branch is currently checked out)" &&

	test_must_fail git push >.git/bar --porcelain  testrepo refs/heads/master:refs/heads/master &&
	test_cmp .git/foo .git/bar
'

test_expect_success 'push --porcelain --dry-run rejected' '
	mk_empty testrepo &&
	git push testrepo refs/heads/master:refs/remotes/origin/master &&
	(cd testrepo &&
		git reset --hard origin/master
		git config receive.denyCurrentBranch true) &&

	echo >.git/foo  "To testrepo"  &&
	echo >>.git/foo "!	refs/heads/master^:refs/heads/master	[rejected] (non-fast-forward)" &&
	echo >>.git/foo "Done" &&

	test_must_fail git push >.git/bar --porcelain  --dry-run testrepo refs/heads/master^:refs/heads/master &&
	test_cmp .git/foo .git/bar
'

test_expect_success 'push --prune' '
	mk_test testrepo heads/master heads/second heads/foo heads/bar &&
	git push --prune testrepo : &&
	check_push_result testrepo $the_commit heads/master &&
	check_push_result testrepo $the_first_commit heads/second &&
	! check_push_result testrepo $the_first_commit heads/foo heads/bar
'

test_expect_success 'push --prune refspec' '
	mk_test testrepo tmp/master tmp/second tmp/foo tmp/bar &&
	git push --prune testrepo "refs/heads/*:refs/tmp/*" &&
	check_push_result testrepo $the_commit tmp/master &&
	check_push_result testrepo $the_first_commit tmp/second &&
	! check_push_result testrepo $the_first_commit tmp/foo tmp/bar
'

for configsection in transfer receive
do
	test_expect_success "push to update a ref hidden by $configsection.hiderefs" '
		mk_test testrepo heads/master hidden/one hidden/two hidden/three &&
		(
			cd testrepo &&
			git config $configsection.hiderefs refs/hidden
		) &&

		# push to unhidden ref succeeds normally
		git push testrepo master:refs/heads/master &&
		check_push_result testrepo $the_commit heads/master &&

		# push to update a hidden ref should fail
		test_must_fail git push testrepo master:refs/hidden/one &&
		check_push_result testrepo $the_first_commit hidden/one &&

		# push to delete a hidden ref should fail
		test_must_fail git push testrepo :refs/hidden/two &&
		check_push_result testrepo $the_first_commit hidden/two &&

		# idempotent push to update a hidden ref should fail
		test_must_fail git push testrepo $the_first_commit:refs/hidden/three &&
		check_push_result testrepo $the_first_commit hidden/three
	'
done

test_expect_success 'fetch exact SHA1' '
	mk_test testrepo heads/master hidden/one &&
	git push testrepo master:refs/hidden/one &&
	(
		cd testrepo &&
		git config transfer.hiderefs refs/hidden
	) &&
	check_push_result testrepo $the_commit hidden/one &&

	mk_child testrepo child &&
	(
		cd child &&

		# make sure $the_commit does not exist here
		git repack -a -d &&
		git prune &&
		test_must_fail git cat-file -t $the_commit &&

		# fetching the hidden object should fail by default
		test_must_fail git fetch -v ../testrepo $the_commit:refs/heads/copy &&
		test_must_fail git rev-parse --verify refs/heads/copy &&

		# the server side can allow it to succeed
		(
			cd ../testrepo &&
			git config uploadpack.allowtipsha1inwant true
		) &&

		git fetch -v ../testrepo $the_commit:refs/heads/copy &&
		result=$(git rev-parse --verify refs/heads/copy) &&
		test "$the_commit" = "$result"
	)
'

test_expect_success 'fetch follows tags by default' '
	mk_test testrepo heads/master &&
	rm -fr src dst &&
	git init src &&
	(
		cd src &&
		git pull ../testrepo master &&
		git tag -m "annotated" tag &&
		git for-each-ref >tmp1 &&
		(
			cat tmp1
			sed -n "s|refs/heads/master$|refs/remotes/origin/master|p" tmp1
		) |
		sort -k 3 >../expect
	) &&
	git init dst &&
	(
		cd dst &&
		git remote add origin ../src &&
		git config branch.master.remote origin &&
		git config branch.master.merge refs/heads/master &&
		git pull &&
		git for-each-ref >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'pushing a specific ref applies remote.$name.push as refmap' '
	mk_test testrepo heads/master &&
	rm -fr src dst &&
	git init src &&
	git init --bare dst &&
	(
		cd src &&
		git pull ../testrepo master &&
		git branch next &&
		git config remote.dst.url ../dst &&
		git config remote.dst.push "+refs/heads/*:refs/remotes/src/*" &&
		git push dst master &&
		git show-ref refs/heads/master |
		sed -e "s|refs/heads/|refs/remotes/src/|" >../dst/expect
	) &&
	(
		cd dst &&
		test_must_fail git show-ref refs/heads/next &&
		test_must_fail git show-ref refs/heads/master &&
		git show-ref refs/remotes/src/master >actual
	) &&
	test_cmp dst/expect dst/actual
'

test_expect_success 'with no remote.$name.push, it is not used as refmap' '
	mk_test testrepo heads/master &&
	rm -fr src dst &&
	git init src &&
	git init --bare dst &&
	(
		cd src &&
		git pull ../testrepo master &&
		git branch next &&
		git config remote.dst.url ../dst &&
		git config push.default matching &&
		git push dst master &&
		git show-ref refs/heads/master >../dst/expect
	) &&
	(
		cd dst &&
		test_must_fail git show-ref refs/heads/next &&
		git show-ref refs/heads/master >actual
	) &&
	test_cmp dst/expect dst/actual
'

test_expect_success 'with no remote.$name.push, upstream mapping is used' '
	mk_test testrepo heads/master &&
	rm -fr src dst &&
	git init src &&
	git init --bare dst &&
	(
		cd src &&
		git pull ../testrepo master &&
		git branch next &&
		git config remote.dst.url ../dst &&
		git config remote.dst.fetch "+refs/heads/*:refs/remotes/dst/*" &&
		git config push.default upstream &&

		git config branch.master.merge refs/heads/trunk &&
		git config branch.master.remote dst &&

		git push dst master &&
		git show-ref refs/heads/master |
		sed -e "s|refs/heads/master|refs/heads/trunk|" >../dst/expect
	) &&
	(
		cd dst &&
		test_must_fail git show-ref refs/heads/master &&
		test_must_fail git show-ref refs/heads/next &&
		git show-ref refs/heads/trunk >actual
	) &&
	test_cmp dst/expect dst/actual
'

test_expect_success 'push does not follow tags by default' '
	mk_test testrepo heads/master &&
	rm -fr src dst &&
	git init src &&
	git init --bare dst &&
	(
		cd src &&
		git pull ../testrepo master &&
		git tag -m "annotated" tag &&
		git checkout -b another &&
		git commit --allow-empty -m "future commit" &&
		git tag -m "future" future &&
		git checkout master &&
		git for-each-ref refs/heads/master >../expect &&
		git push ../dst master
	) &&
	(
		cd dst &&
		git for-each-ref >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'push --follow-tag only pushes relevant tags' '
	mk_test testrepo heads/master &&
	rm -fr src dst &&
	git init src &&
	git init --bare dst &&
	(
		cd src &&
		git pull ../testrepo master &&
		git tag -m "annotated" tag &&
		git checkout -b another &&
		git commit --allow-empty -m "future commit" &&
		git tag -m "future" future &&
		git checkout master &&
		git for-each-ref refs/heads/master refs/tags/tag >../expect
		git push --follow-tag ../dst master
	) &&
	(
		cd dst &&
		git for-each-ref >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'push --no-thin must produce non-thin pack' '
	cat >>path1 <<\EOF &&
keep base version of path1 big enough, compared to the new changes
later, in order to pass size heuristics in
builtin/pack-objects.c:try_delta()
EOF
	git commit -am initial &&
	git init no-thin &&
	git --git-dir=no-thin/.git config receive.unpacklimit 0 &&
	git push no-thin/.git refs/heads/master:refs/heads/foo &&
	echo modified >> path1 &&
	git commit -am modified &&
	git repack -adf &&
	rcvpck="git receive-pack --reject-thin-pack-for-testing" &&
	git push --no-thin --receive-pack="$rcvpck" no-thin/.git refs/heads/master:refs/heads/foo
'

test_done
