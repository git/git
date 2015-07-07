#!/bin/sh

test_description='test for-each-refs usage of ref-filter APIs'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

if ! test_have_prereq GPG
then
	skip_all="skipping for-each-ref tests, GPG not available"
	test_done
fi

test_expect_success 'setup some history and refs' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	git checkout -b side &&
	test_commit four &&
	git tag -s -m "A signed tag message" signed-tag &&
	git tag -s -m "Annonated doubly" double-tag signed-tag &&
	git checkout master &&
	git update-ref refs/odd/spot master
'

test_expect_success 'filtering with --points-at' '
	cat >expect <<-\EOF &&
	refs/heads/master
	refs/odd/spot
	refs/tags/three
	EOF
	git for-each-ref --format="%(refname)" --points-at=master >actual &&
	test_cmp expect actual
'

test_expect_success 'check signed tags with --points-at' '
	sed -e "s/Z$//" >expect <<-\EOF &&
	refs/heads/side Z
	refs/tags/four Z
	refs/tags/signed-tag four
	EOF
	git for-each-ref --format="%(refname) %(*subject)" --points-at=side >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --merged' '
	cat >expect <<-\EOF &&
	refs/heads/master
	refs/odd/spot
	refs/tags/one
	refs/tags/three
	refs/tags/two
	EOF
	git for-each-ref --format="%(refname)" --merged=master >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --no-merged' '
	cat >expect <<-\EOF &&
	refs/heads/side
	refs/tags/double-tag
	refs/tags/four
	refs/tags/signed-tag
	EOF
	git for-each-ref --format="%(refname)" --no-merged=master >actual &&
	test_cmp expect actual
'

test_done
