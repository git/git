#!/bin/sh

test_description='but rebase --signoff

This test runs but rebase --signoff and make sure that it works.
'

. ./test-lib.sh

# A simple file to cummit
cat >file <<EOF
a
EOF

# Expected cummit message for initial cummit after rebase --signoff
cat >expected-initial-signed <<EOF
Initial empty cummit

Signed-off-by: $(but var BUT_CUMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

# Expected cummit message after rebase --signoff
cat >expected-signed <<EOF
first

Signed-off-by: $(but var BUT_CUMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

# Expected cummit message after rebase without --signoff (or with --no-signoff)
cat >expected-unsigned <<EOF
first
EOF


# We configure an alias to do the rebase --signoff so that
# on the next subtest we can show that --no-signoff overrides the alias
test_expect_success 'rebase --signoff adds a sign-off line' '
	but cummit --allow-empty -m "Initial empty cummit" &&
	but add file && but cummit -m first &&
	but config alias.rbs "rebase --signoff" &&
	but rbs HEAD^ &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase --no-signoff does not add a sign-off line' '
	but cummit --amend -m "first" &&
	but rbs --no-signoff HEAD^ &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-unsigned actual
'

test_expect_success 'rebase --exec --signoff adds a sign-off line' '
	test_when_finished "rm exec" &&
	but cummit --amend -m "first" &&
	but rebase --exec "touch exec" --signoff HEAD^ &&
	test_path_is_file exec &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase --root --signoff adds a sign-off line' '
	but cummit --amend -m "first" &&
	but rebase --root --keep-empty --signoff &&
	but cat-file commit HEAD^ | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-initial-signed actual &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase -i --signoff fails' '
	but cummit --amend -m "first" &&
	but rebase -i --signoff HEAD^ &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rebase -m --signoff fails' '
	but cummit --amend -m "first" &&
	but rebase -m --signoff HEAD^ &&
	but cat-file commit HEAD | sed -e "1,/^\$/d" >actual &&
	test_cmp expected-signed actual
'
test_done
