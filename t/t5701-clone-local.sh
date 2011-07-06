#!/bin/sh

test_description='test local clone'
. ./test-lib.sh

D=`pwd`

test_expect_success 'preparing origin repository' '
	: >file && git add . && git commit -m1 &&
	git clone --bare . a.git &&
	git clone --bare . x &&
	test "$(GIT_CONFIG=a.git/config git config --bool core.bare)" = true &&
	test "$(GIT_CONFIG=x/config git config --bool core.bare)" = true
	git bundle create b1.bundle --all HEAD &&
	git bundle create b2.bundle --all &&
	mkdir dir &&
	cp b1.bundle dir/b3
	cp b1.bundle b4
'

test_expect_success 'local clone without .git suffix' '
	cd "$D" &&
	git clone -l -s a b &&
	cd b &&
	test "$(GIT_CONFIG=.git/config git config --bool core.bare)" = false &&
	git fetch
'

test_expect_success 'local clone with .git suffix' '
	cd "$D" &&
	git clone -l -s a.git c &&
	cd c &&
	git fetch
'

test_expect_success 'local clone from x' '
	cd "$D" &&
	git clone -l -s x y &&
	cd y &&
	git fetch
'

test_expect_success 'local clone from x.git that does not exist' '
	cd "$D" &&
	if git clone -l -s x.git z
	then
		echo "Oops, should have failed"
		false
	else
		echo happy
	fi
'

test_expect_success 'With -no-hardlinks, local will make a copy' '
	cd "$D" &&
	git clone --bare --no-hardlinks x w &&
	cd w &&
	linked=$(find objects -type f ! -links 1 | wc -l) &&
	test 0 = $linked
'

test_expect_success 'Even without -l, local will make a hardlink' '
	cd "$D" &&
	rm -fr w &&
	git clone -l --bare x w &&
	cd w &&
	copied=$(find objects -type f -links 1 | wc -l) &&
	test 0 = $copied
'

test_expect_success 'local clone of repo with nonexistent ref in HEAD' '
	cd "$D" &&
	echo "ref: refs/heads/nonexistent" > a.git/HEAD &&
	git clone a d &&
	cd d &&
	git fetch &&
	test ! -e .git/refs/remotes/origin/HEAD'

test_expect_success 'bundle clone without .bundle suffix' '
	cd "$D" &&
	git clone dir/b3 &&
	cd b3 &&
	git fetch
'

test_expect_success 'bundle clone with .bundle suffix' '
	cd "$D" &&
	git clone b1.bundle &&
	cd b1 &&
	git fetch
'

test_expect_success 'bundle clone from b4' '
	cd "$D" &&
	git clone b4 bdl &&
	cd bdl &&
	git fetch
'

test_expect_success 'bundle clone from b4.bundle that does not exist' '
	cd "$D" &&
	if git clone b4.bundle bb
	then
		echo "Oops, should have failed"
		false
	else
		echo happy
	fi
'

test_expect_success 'bundle clone with nonexistent HEAD' '
	cd "$D" &&
	git clone b2.bundle b2 &&
	cd b2 &&
	git fetch
	test ! -e .git/refs/heads/master
'

test_done
