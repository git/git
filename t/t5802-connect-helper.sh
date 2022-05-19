#!/bin/sh

test_description='ext::cmd remote "connect" helper'
. ./test-lib.sh

test_expect_success setup '
	but config --global protocol.ext.allow user &&
	test_tick &&
	but cummit --allow-empty -m initial &&
	test_tick &&
	but cummit --allow-empty -m second &&
	test_tick &&
	but cummit --allow-empty -m third &&
	test_tick &&
	but tag -a -m "tip three" three &&

	test_tick &&
	but cummit --allow-empty -m fourth
'

test_expect_success clone '
	cmd=$(echo "echo >&2 ext::sh invoked && %S .." | sed -e "s/ /% /g") &&
	but clone "ext::sh -c %S% ." dst &&
	but for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		but config remote.origin.url "ext::sh -c $cmd" &&
		but for-each-ref refs/heads/ refs/tags/
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'update following tag' '
	test_tick &&
	but cummit --allow-empty -m fifth &&
	test_tick &&
	but tag -a -m "tip five" five &&
	but for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		but pull &&
		but for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'update backfilled tag' '
	test_tick &&
	but cummit --allow-empty -m sixth &&
	test_tick &&
	but tag -a -m "tip two" two three^1 &&
	but for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		but pull &&
		but for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'update backfilled tag without primary transfer' '
	test_tick &&
	but tag -a -m "tip one " one two^1 &&
	but for-each-ref refs/heads/ refs/tags/ >expect &&
	(
		cd dst &&
		but pull &&
		but for-each-ref refs/heads/ refs/tags/ >../actual
	) &&
	test_cmp expect actual
'


test_expect_success 'set up fake but-daemon' '
	mkdir remote &&
	but init --bare remote/one.but &&
	mkdir remote/host &&
	but init --bare remote/host/two.but &&
	write_script fake-daemon <<-\EOF &&
	but daemon --inetd \
		--informative-errors \
		--export-all \
		--base-path="$TRASH_DIRECTORY/remote" \
		--interpolated-path="$TRASH_DIRECTORY/remote/%H%D" \
		"$TRASH_DIRECTORY/remote"
	EOF
	export TRASH_DIRECTORY &&
	PATH=$TRASH_DIRECTORY:$PATH
'

test_expect_success 'ext command can connect to but daemon (no vhost)' '
	rm -rf dst &&
	but clone "ext::fake-daemon %G/one.but" dst
'

test_expect_success 'ext command can connect to but daemon (vhost)' '
	rm -rf dst &&
	but clone "ext::fake-daemon %G/two.but %Vhost" dst
'

test_done
