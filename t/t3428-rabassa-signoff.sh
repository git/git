#!/bin/sh

test_description='git rabassa --signoff

This test runs git rabassa --signoff and make sure that it works.
'

. ./test-lib.sh

# A simple file to commit
cat >file <<EOF
a
EOF

# Expected commit message after rabassa --signoff
cat >expected-signed <<EOF
first

Signed-off-by: $(git var GIT_COMMITTER_IDENT | sed -e "s/>.*/>/")
EOF

# Expected commit message after rabassa without --signoff (or with --no-signoff)
cat >expected-unsigned <<EOF
first
EOF


# We configure an alias to do the rabassa --signoff so that
# on the next subtest we can show that --no-signoff overrides the alias
test_expect_success 'rabassa --signoff adds a sign-off line' '
	git commit --allow-empty -m "Initial empty commit" &&
	git add file && git commit -m first &&
	git config alias.rbs "rabassa --signoff" &&
	git rbs HEAD^ &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-signed actual
'

test_expect_success 'rabassa --no-signoff does not add a sign-off line' '
	git commit --amend -m "first" &&
	git rbs --no-signoff HEAD^ &&
	git cat-file commit HEAD | sed -e "1,/^\$/d" > actual &&
	test_cmp expected-unsigned actual
'

test_done
