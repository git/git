#!/bin/sh

test_description='prune $GIT_DIR/repos'

. ./test-lib.sh

test_expect_success 'prune --repos on normal repo' '
	git prune --repos &&
	test_must_fail git prune --repos abc
'

test_expect_success 'prune files inside $GIT_DIR/repos' '
	mkdir .git/repos &&
	: >.git/repos/abc &&
	git prune --repos --verbose >actual &&
	cat >expect <<EOF &&
Removing repos/abc: not a valid directory
EOF
	test_i18ncmp expect actual &&
	! test -f .git/repos/abc &&
	! test -d .git/repos
'

test_expect_success 'prune directories without gitdir' '
	mkdir -p .git/repos/def/abc &&
	: >.git/repos/def/def &&
	cat >expect <<EOF &&
Removing repos/def: gitdir file does not exist
EOF
	git prune --repos --verbose >actual &&
	test_i18ncmp expect actual &&
	! test -d .git/repos/def &&
	! test -d .git/repos
'

test_expect_success POSIXPERM 'prune directories with unreadable gitdir' '
	mkdir -p .git/repos/def/abc &&
	: >.git/repos/def/def &&
	: >.git/repos/def/gitdir &&
	chmod u-r .git/repos/def/gitdir &&
	git prune --repos --verbose >actual &&
	test_i18ngrep "Removing repos/def: unable to read gitdir file" actual &&
	! test -d .git/repos/def &&
	! test -d .git/repos
'

test_expect_success 'prune directories with invalid gitdir' '
	mkdir -p .git/repos/def/abc &&
	: >.git/repos/def/def &&
	: >.git/repos/def/gitdir &&
	git prune --repos --verbose >actual &&
	test_i18ngrep "Removing repos/def: invalid gitdir file" actual &&
	! test -d .git/repos/def &&
	! test -d .git/repos
'

test_expect_success 'prune directories with gitdir pointing to nowhere' '
	mkdir -p .git/repos/def/abc &&
	: >.git/repos/def/def &&
	echo "$TRASH_DIRECTORY"/nowhere >.git/repos/def/gitdir &&
	git prune --repos --verbose >actual &&
	test_i18ngrep "Removing repos/def: gitdir file points to non-existent location" actual &&
	! test -d .git/repos/def &&
	! test -d .git/repos
'

test_expect_success 'not prune locked checkout' '
	test_when_finished rm -r .git/repos
	mkdir -p .git/repos/ghi &&
	: >.git/repos/ghi/locked &&
	git prune --repos &&
	test -d .git/repos/ghi
'

test_expect_success 'not prune recent checkouts' '
	test_when_finished rm -r .git/repos
	mkdir zz &&
	mkdir -p .git/repos/jlm &&
	echo "$TRASH_DIRECTORY"/zz >.git/repos/jlm/gitdir &&
	git prune --repos --verbose --expire=2.days.ago &&
	test -d .git/repos/jlm
'

test_done
