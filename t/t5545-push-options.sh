#!/bin/sh

test_description='pushing to a repository using push options'

. ./test-lib.sh

mk_repo_pair () {
	rm -rf workbench upstream &&
	test_create_repo upstream &&
	test_create_repo workbench &&
	(
		cd upstream &&
		git config receive.denyCurrentBranch warn &&
		mkdir -p .git/hooks &&
		cat >.git/hooks/pre-receive <<-'EOF' &&
		#!/bin/sh
		if test -n "$GIT_PUSH_OPTION_COUNT"; then
			i=0
			>hooks/pre-receive.push_options
			while test "$i" -lt "$GIT_PUSH_OPTION_COUNT"; do
				eval "value=\$GIT_PUSH_OPTION_$i"
				echo $value >>hooks/pre-receive.push_options
				i=$((i + 1))
			done
		fi
		EOF
		chmod u+x .git/hooks/pre-receive

		cat >.git/hooks/post-receive <<-'EOF' &&
		#!/bin/sh
		if test -n "$GIT_PUSH_OPTION_COUNT"; then
			i=0
			>hooks/post-receive.push_options
			while test "$i" -lt "$GIT_PUSH_OPTION_COUNT"; do
				eval "value=\$GIT_PUSH_OPTION_$i"
				echo $value >>hooks/post-receive.push_options
				i=$((i + 1))
			done
		fi
		EOF
		chmod u+x .git/hooks/post-receive
	) &&
	(
		cd workbench &&
		git remote add up ../upstream
	)
}

# Compare the ref ($1) in upstream with a ref value from workbench ($2)
# i.e. test_refs second HEAD@{2}
test_refs () {
	test $# = 2 &&
	git -C upstream rev-parse --verify "$1" >expect &&
	git -C workbench rev-parse --verify "$2" >actual &&
	test_cmp expect actual
}

test_expect_success 'one push option works for a single branch' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git push --push-option=asdf up master
	) &&
	test_refs master master &&
	echo "asdf" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options
'

test_expect_success 'push option denied by remote' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions false &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		test_must_fail git push --push-option=asdf up master
	) &&
	test_refs master HEAD@{1}
'

test_expect_success 'two push options work' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git push --push-option=asdf --push-option="more structured text" up master
	) &&
	test_refs master master &&
	printf "asdf\nmore structured text\n" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options
'

test_done
