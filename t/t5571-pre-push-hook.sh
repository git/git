#!/bin/sh

test_description='check pre-push hooks'
. ./test-lib.sh

# Setup hook that always succeeds
HOOKDIR="$(git rev-parse --git-dir)/hooks"
HOOK="$HOOKDIR/pre-push"
mkdir -p "$HOOKDIR"
write_script "$HOOK" <<EOF
cat >/dev/null
exit 0
EOF

test_expect_success 'setup' '
	git config push.default upstream &&
	git init --bare repo1 &&
	git remote add parent1 repo1 &&
	test_commit one &&
	git push parent1 HEAD:foreign
'
write_script "$HOOK" <<EOF
cat >/dev/null
exit 1
EOF

COMMIT1="$(git rev-parse HEAD)"
export COMMIT1

test_expect_success 'push with failing hook' '
	test_commit two &&
	test_must_fail git push parent1 HEAD
'

test_expect_success '--no-verify bypasses hook' '
	git push --no-verify parent1 HEAD
'

COMMIT2="$(git rev-parse HEAD)"
export COMMIT2

write_script "$HOOK" <<'EOF'
echo "$1" >actual
echo "$2" >>actual
cat >>actual
EOF

cat >expected <<EOF
parent1
repo1
refs/heads/master $COMMIT2 refs/heads/foreign $COMMIT1
EOF

test_expect_success 'push with hook' '
	git push parent1 master:foreign &&
	diff expected actual
'

test_expect_success 'add a branch' '
	git checkout -b other parent1/foreign &&
	test_commit three
'

COMMIT3="$(git rev-parse HEAD)"
export COMMIT3

cat >expected <<EOF
parent1
repo1
refs/heads/other $COMMIT3 refs/heads/foreign $COMMIT2
EOF

test_expect_success 'push to default' '
	git push &&
	diff expected actual
'

cat >expected <<EOF
parent1
repo1
refs/tags/one $COMMIT1 refs/tags/tag1 $_z40
HEAD~ $COMMIT2 refs/heads/prev $_z40
EOF

test_expect_success 'push non-branches' '
	git push parent1 one:tag1 HEAD~:refs/heads/prev &&
	diff expected actual
'

cat >expected <<EOF
parent1
repo1
(delete) $_z40 refs/heads/prev $COMMIT2
EOF

test_expect_success 'push delete' '
	git push parent1 :prev &&
	diff expected actual
'

cat >expected <<EOF
repo1
repo1
HEAD $COMMIT3 refs/heads/other $_z40
EOF

test_expect_success 'push to URL' '
	git push repo1 HEAD &&
	diff expected actual
'

# Test that filling pipe buffers doesn't cause failure
# Too slow to leave enabled for general use
if false
then
	printf 'parent1\nrepo1\n' >expected
	nr=1000
	while test $nr -lt 2000
	do
		nr=$(( $nr + 1 ))
		git branch b/$nr $COMMIT3
		echo "refs/heads/b/$nr $COMMIT3 refs/heads/b/$nr $_z40" >>expected
	done

	test_expect_success 'push many refs' '
		git push parent1 "refs/heads/b/*:refs/heads/b/*" &&
		diff expected actual
	'
fi

test_done
