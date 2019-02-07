#!/bin/sh

test_description='ext::cmd remote "connect" helper'
. ./test-lib.sh

test_expect_success setup '
	git config --global protocol.ext.allow user &&
	test_tick &&
	git commit --allow-empty -m initial &&
	test_tick &&
	git commit --allow-empty -m second &&
	test_tick &&
	git commit --allow-empty -m third &&
	test_tick &&
	git tag -a -m "tip three" three &&

	test_tick &&
	git commit --allow-empty -m fourth
'

test_expect_success clone '
	cmd=$(echo "echo >&2 ext::sh invoked && %S .." | sed -e "s/ /% /g") &&
	git clone "ext::sh -c %S% ." dst &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git config remote.origin.url "ext::sh -c $cmd" &&
		git for-each-ref refs/heads/ refs/tags/
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'update following tag' '
	test_tick &&
	git commit --allow-empty -m fifth &&
	test_tick &&
	git tag -a -m "tip five" five &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git pull &&
		git for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'update backfilled tag' '
	test_tick &&
	git commit --allow-empty -m sixth &&
	test_tick &&
	git tag -a -m "tip two" two three^1 &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git pull &&
		git for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'update backfilled tag without primary transfer' '
	test_tick &&
	git tag -a -m "tip one " one two^1 &&
	git for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		git pull &&
		git for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'


test_expect_success 'set up fake git-daemon' '
	mkdir remote &&
	git init --bare remote/one.git &&
	mkdir remote/host &&
	git init --bare remote/host/two.git &&
	write_script fake-daemon <<-\EOF &&
	git daemon --inetd \
		--informative-errors \
		--export-all \
		--base-path="$TRASH_DIRECTORY/remote" \
		--interpolated-path="$TRASH_DIRECTORY/remote/%H%D" \
		"$TRASH_DIRECTORY/remote"
	EOF
	export TRASH_DIRECTORY &&
	PATH=$TRASH_DIRECTORY$PATH_SEP$PATH
'

test_expect_success 'ext command can connect to git daemon (no vhost)' '
	rm -rf dst &&
	git clone "ext::fake-daemon %G/one.git" dst
'

test_expect_success 'ext command can connect to git daemon (vhost)' '
	rm -rf dst &&
	git clone "ext::fake-daemon %G/two.git %Vhost" dst
'

test_done
