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

test_expect_success 'push options and submodules' '
	test_when_finished "rm -rf parent" &&
	test_when_finished "rm -rf parent_upstream" &&
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	cp -r upstream parent_upstream &&
	test_commit -C upstream one &&

	test_create_repo parent &&
	git -C parent remote add up ../parent_upstream &&
	test_commit -C parent one &&
	git -C parent push --mirror up &&

	git -C parent submodule add ../upstream workbench &&
	git -C parent/workbench remote add up ../../upstream &&
	git -C parent commit -m "add submoule" &&

	test_commit -C parent/workbench two &&
	git -C parent add workbench &&
	git -C parent commit -m "update workbench" &&

	git -C parent push \
		--push-option=asdf --push-option="more structured text" \
		--recurse-submodules=on-demand up master &&

	git -C upstream rev-parse --verify master >expect &&
	git -C parent/workbench rev-parse --verify master >actual &&
	test_cmp expect actual &&

	git -C parent_upstream rev-parse --verify master >expect &&
	git -C parent rev-parse --verify master >actual &&
	test_cmp expect actual &&

	printf "asdf\nmore structured text\n" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options &&
	test_cmp expect parent_upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect parent_upstream/.git/hooks/post-receive.push_options
'

test_expect_success 'default push option' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git -c push.pushOption=default push up master
	) &&
	test_refs master master &&
	echo "default" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options
'

test_expect_success 'two default push options' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git -c push.pushOption=default1 -c push.pushOption=default2 push up master
	) &&
	test_refs master master &&
	printf "default1\ndefault2\n" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options
'

test_expect_success 'push option from command line overrides from-config push option' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git -c push.pushOption=default push --push-option=manual up master
	) &&
	test_refs master master &&
	echo "manual" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options
'

test_expect_success 'empty value of push.pushOption in config clears the list' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git -c push.pushOption=default1 -c push.pushOption= -c push.pushOption=default2 push up master
	) &&
	test_refs master master &&
	echo "default2" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.git/hooks/post-receive.push_options
'

test_expect_success 'invalid push option in config' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		test_must_fail git -c push.pushOption push up master
	) &&
	test_refs master HEAD@{1}
'

test_expect_success 'push options keep quoted characters intact (direct)' '
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions true &&
	test_commit -C workbench one &&
	git -C workbench push --push-option="\"embedded quotes\"" up master &&
	echo "\"embedded quotes\"" >expect &&
	test_cmp expect upstream/.git/hooks/pre-receive.push_options
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

# set up http repository for fetching/pushing, with push options config
# bool set to $1
mk_http_pair () {
	test_when_finished "rm -rf test_http_clone" &&
	test_when_finished 'rm -rf "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git' &&
	mk_repo_pair &&
	git -C upstream config receive.advertisePushOptions "$1" &&
	git -C upstream config http.receivepack true &&
	cp -R upstream/.git "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git &&
	git clone "$HTTPD_URL"/smart/upstream test_http_clone
}

test_expect_success 'push option denied properly by http server' '
	mk_http_pair false &&
	test_commit -C test_http_clone one &&
	test_must_fail git -C test_http_clone push --push-option=asdf origin master 2>actual &&
	test_i18ngrep "the receiving end does not support push options" actual &&
	git -C test_http_clone push origin master
'

test_expect_success 'push options work properly across http' '
	mk_http_pair true &&

	test_commit -C test_http_clone one &&
	git -C test_http_clone push origin master &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git rev-parse --verify master >expect &&
	git -C test_http_clone rev-parse --verify master >actual &&
	test_cmp expect actual &&

	test_commit -C test_http_clone two &&
	git -C test_http_clone push --push-option=asdf --push-option="more structured text" origin master &&
	printf "asdf\nmore structured text\n" >expect &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git/hooks/pre-receive.push_options &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git/hooks/post-receive.push_options &&

	git -C "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git rev-parse --verify master >expect &&
	git -C test_http_clone rev-parse --verify master >actual &&
	test_cmp expect actual
'

test_expect_success 'push options keep quoted characters intact (http)' '
	mk_http_pair true &&

	test_commit -C test_http_clone one &&
	git -C test_http_clone push --push-option="\"embedded quotes\"" origin master &&
	echo "\"embedded quotes\"" >expect &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.git/hooks/pre-receive.push_options
'

test_done
