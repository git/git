#!/bin/sh
#
# Copyright (c) 2006 Josh England
#

test_description='Test the post-checkout hook.'
. ./test-lib.sh

test_expect_success setup '
	test_commit one &&
	test_commit two &&
	test_commit rebase-on-me &&
	git reset --hard HEAD^ &&
	test_commit three three &&
	mv .git/hooks-disabled .git/hooks
'

cat >.git/hooks/post-checkout <<'EOF'
#!/bin/sh
echo $@ > .git/post-checkout.args
EOF
chmod u+x .git/hooks/post-checkout

test_expect_success 'post-checkout runs as expected ' '
	git checkout master &&
	test -e .git/post-checkout.args
'

test_expect_success 'post-checkout receives the right arguments with HEAD unchanged ' '
	read old new flag < .git/post-checkout.args &&
	test $old = $new && test $flag = 1
'

test_expect_success 'post-checkout runs as expected ' '
	git checkout master &&
	test -e .git/post-checkout.args
'

test_expect_success 'post-checkout args are correct with git checkout -b ' '
	git checkout -b new1 &&
	read old new flag < .git/post-checkout.args &&
	test $old = $new && test $flag = 1
'

test_expect_success 'post-checkout receives the right args with HEAD changed ' '
	git checkout two &&
	read old new flag < .git/post-checkout.args &&
	test $old != $new && test $flag = 1
'

test_expect_success 'post-checkout receives the right args when not switching branches ' '
	git checkout master -- three &&
	read old new flag < .git/post-checkout.args &&
	test $old = $new && test $flag = 0
'

test_expect_success 'post-checkout is triggered on rebase' '
	git checkout -b rebase-test master &&
	rm -f .git/post-checkout.args &&
	git rebase rebase-on-me &&
	read old new flag < .git/post-checkout.args &&
	test $old != $new && test $flag = 1
'

test_expect_success 'post-checkout is triggered on rebase with fast-forward' '
	git checkout -b ff-rebase-test rebase-on-me^ &&
	rm -f .git/post-checkout.args &&
	git rebase rebase-on-me &&
	read old new flag < .git/post-checkout.args &&
	test $old != $new && test $flag = 1
'

if test "$(git config --bool core.filemode)" = true; then
mkdir -p templates/hooks
cat >templates/hooks/post-checkout <<'EOF'
#!/bin/sh
echo $@ > $GIT_DIR/post-checkout.args
EOF
chmod +x templates/hooks/post-checkout

test_expect_success 'post-checkout hook is triggered by clone' '
	git clone --template=templates . clone3 &&
	test -f clone3/.git/post-checkout.args
'
fi

test_done
