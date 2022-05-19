#!/bin/sh

test_description='pushing to a repository using push options'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

BUT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export BUT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh

mk_repo_pair () {
	rm -rf workbench upstream &&
	test_create_repo upstream &&
	test_create_repo workbench &&
	(
		cd upstream &&
		but config receive.denyCurrentBranch warn &&
		mkdir -p .but/hooks &&
		cat >.but/hooks/pre-receive <<-'EOF' &&
		#!/bin/sh
		if test -n "$BUT_PUSH_OPTION_COUNT"; then
			i=0
			>hooks/pre-receive.push_options
			while test "$i" -lt "$BUT_PUSH_OPTION_COUNT"; do
				eval "value=\$BUT_PUSH_OPTION_$i"
				echo $value >>hooks/pre-receive.push_options
				i=$((i + 1))
			done
		fi
		EOF
		chmod u+x .but/hooks/pre-receive

		cat >.but/hooks/post-receive <<-'EOF' &&
		#!/bin/sh
		if test -n "$BUT_PUSH_OPTION_COUNT"; then
			i=0
			>hooks/post-receive.push_options
			while test "$i" -lt "$BUT_PUSH_OPTION_COUNT"; do
				eval "value=\$BUT_PUSH_OPTION_$i"
				echo $value >>hooks/post-receive.push_options
				i=$((i + 1))
			done
		fi
		EOF
		chmod u+x .but/hooks/post-receive
	) &&
	(
		cd workbench &&
		but remote add up ../upstream
	)
}

# Compare the ref ($1) in upstream with a ref value from workbench ($2)
# i.e. test_refs second HEAD@{2}
test_refs () {
	test $# = 2 &&
	but -C upstream rev-parse --verify "$1" >expect &&
	but -C workbench rev-parse --verify "$2" >actual &&
	test_cmp expect actual
}

test_expect_success 'one push option works for a single branch' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but push --push-option=asdf up main
	) &&
	test_refs main main &&
	echo "asdf" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'push option denied by remote' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions false &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		test_must_fail but push --push-option=asdf up main
	) &&
	test_refs main HEAD@{1}
'

test_expect_success 'two push options work' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but push --push-option=asdf --push-option="more structured text" up main
	) &&
	test_refs main main &&
	printf "asdf\nmore structured text\n" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'push options and submodules' '
	test_when_finished "rm -rf parent" &&
	test_when_finished "rm -rf parent_upstream" &&
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	cp -r upstream parent_upstream &&
	test_cummit -C upstream one &&

	test_create_repo parent &&
	but -C parent remote add up ../parent_upstream &&
	test_cummit -C parent one &&
	but -C parent push --mirror up &&

	but -C parent submodule add ../upstream workbench &&
	but -C parent/workbench remote add up ../../upstream &&
	but -C parent cummit -m "add submodule" &&

	test_cummit -C parent/workbench two &&
	but -C parent add workbench &&
	but -C parent cummit -m "update workbench" &&

	but -C parent push \
		--push-option=asdf --push-option="more structured text" \
		--recurse-submodules=on-demand up main &&

	but -C upstream rev-parse --verify main >expect &&
	but -C parent/workbench rev-parse --verify main >actual &&
	test_cmp expect actual &&

	but -C parent_upstream rev-parse --verify main >expect &&
	but -C parent rev-parse --verify main >actual &&
	test_cmp expect actual &&

	printf "asdf\nmore structured text\n" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options &&
	test_cmp expect parent_upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect parent_upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'default push option' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but -c push.pushOption=default push up main
	) &&
	test_refs main main &&
	echo "default" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'two default push options' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but -c push.pushOption=default1 -c push.pushOption=default2 push up main
	) &&
	test_refs main main &&
	printf "default1\ndefault2\n" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'push option from command line overrides from-config push option' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but -c push.pushOption=default push --push-option=manual up main
	) &&
	test_refs main main &&
	echo "manual" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'empty value of push.pushOption in config clears the list' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but -c push.pushOption=default1 -c push.pushOption= -c push.pushOption=default2 push up main
	) &&
	test_refs main main &&
	echo "default2" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options &&
	test_cmp expect upstream/.but/hooks/post-receive.push_options
'

test_expect_success 'invalid push option in config' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		test_must_fail but -c push.pushOption push up main
	) &&
	test_refs main HEAD@{1}
'

test_expect_success 'push options keep quoted characters intact (direct)' '
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions true &&
	test_cummit -C workbench one &&
	but -C workbench push --push-option="\"embedded quotes\"" up main &&
	echo "\"embedded quotes\"" >expect &&
	test_cmp expect upstream/.but/hooks/pre-receive.push_options
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

# set up http repository for fetching/pushing, with push options config
# bool set to $1
mk_http_pair () {
	test_when_finished "rm -rf test_http_clone" &&
	test_when_finished 'rm -rf "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but' &&
	mk_repo_pair &&
	but -C upstream config receive.advertisePushOptions "$1" &&
	but -C upstream config http.receivepack true &&
	cp -R upstream/.but "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but &&
	but clone "$HTTPD_URL"/smart/upstream test_http_clone
}

test_expect_success 'push option denied properly by http server' '
	mk_http_pair false &&
	test_cummit -C test_http_clone one &&
	test_must_fail but -C test_http_clone push --push-option=asdf origin main 2>actual &&
	test_i18ngrep "the receiving end does not support push options" actual &&
	but -C test_http_clone push origin main
'

test_expect_success 'push options work properly across http' '
	mk_http_pair true &&

	test_cummit -C test_http_clone one &&
	but -C test_http_clone push origin main &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but rev-parse --verify main >expect &&
	but -C test_http_clone rev-parse --verify main >actual &&
	test_cmp expect actual &&

	test_cummit -C test_http_clone two &&
	but -C test_http_clone push --push-option=asdf --push-option="more structured text" origin main &&
	printf "asdf\nmore structured text\n" >expect &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but/hooks/pre-receive.push_options &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but/hooks/post-receive.push_options &&

	but -C "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but rev-parse --verify main >expect &&
	but -C test_http_clone rev-parse --verify main >actual &&
	test_cmp expect actual
'

test_expect_success 'push options keep quoted characters intact (http)' '
	mk_http_pair true &&

	test_cummit -C test_http_clone one &&
	but -C test_http_clone push --push-option="\"embedded quotes\"" origin main &&
	echo "\"embedded quotes\"" >expect &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH"/upstream.but/hooks/pre-receive.push_options
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
