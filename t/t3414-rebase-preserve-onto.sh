#!/bin/sh
#
# Copyright (c) 2009 Greg Price
#

test_description='git rebase -p should respect --onto

In a rebase with --onto, we should rewrite all the commits that
aren'"'"'t on top of $ONTO, even if they are on top of $UPSTREAM.
'
. ./test-lib.sh

if ! test_have_prereq REBASE_P; then
	skip_all='skipping git rebase -p tests, as asked for'
	test_done
fi

. "$TEST_DIRECTORY"/lib-rebase.sh

# Set up branches like this:
# A1---B1---E1---F1---G1
#  \    \             /
#   \    \--C1---D1--/
#    H1

test_expect_success 'setup' '
	test_commit A1 &&
	test_commit B1 &&
	test_commit C1 &&
	test_commit D1 &&
	git reset --hard B1 &&
	test_commit E1 &&
	test_commit F1 &&
	test_merge G1 D1 &&
	git reset --hard A1 &&
	test_commit H1
'

# Now rebase merge G1 from both branches' base B1, both should move:
# A1---B1---E1---F1---G1
#  \    \             /
#   \    \--C1---D1--/
#    \
#     H1---E2---F2---G2
#      \             /
#       \--C2---D2--/

test_expect_success 'rebase from B1 onto H1' '
	git checkout G1 &&
	git rebase -p --onto H1 B1 &&
	test "$(git rev-parse HEAD^1^1^1)" = "$(git rev-parse H1)" &&
	test "$(git rev-parse HEAD^2^1^1)" = "$(git rev-parse H1)"
'

# On the other hand if rebase from E1 which is within one branch,
# then the other branch stays:
# A1---B1---E1---F1---G1
#  \    \             /
#   \    \--C1---D1--/
#    \             \
#     H1-----F3-----G3

test_expect_success 'rebase from E1 onto H1' '
	git checkout G1 &&
	git rebase -p --onto H1 E1 &&
	test "$(git rev-parse HEAD^1^1)" = "$(git rev-parse H1)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse D1)"
'

# And the same if we rebase from a commit in the second-parent branch.
# A1---B1---E1---F1----G1
#  \    \          \   /
#   \    \--C1---D1-\-/
#    \               \
#     H1------D3------G4

test_expect_success 'rebase from C1 onto H1' '
	git checkout G1 &&
	git rev-list --first-parent --pretty=oneline C1..G1 &&
	git rebase -p --onto H1 C1 &&
	test "$(git rev-parse HEAD^2^1)" = "$(git rev-parse H1)" &&
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse F1)"
'

test_done
