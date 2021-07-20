#!/bin/sh
#
# Copyright (c) 2007 Johannes Schindelin
#

test_description='Test shared repository initialization'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Remove a default ACL from the test dir if possible.
setfacl -k . 2>/dev/null

# User must have read permissions to the repo -> failure on --shared=0400
test_expect_success 'shared = 0400 (faulty permission u-w)' '
	test_when_finished "rm -rf sub" &&
	mkdir sub && (
		cd sub &&
		test_must_fail git init --shared=0400
	)
'

# TODO(hanwen): for REFTABLE should inspect group-readable of .git/reftable/
for u in 002 022
do
	test_expect_success REFFILES,POSIXPERM "shared=1 does not clear bits preset by umask $u" '
		mkdir sub && (
			cd sub &&
			umask $u &&
			git init --shared=1 &&
			test 1 = "$(git config core.sharedrepository)"
		) &&
		actual=$(ls -l sub/.git/HEAD) &&
		case "$actual" in
		-rw-rw-r--*)
			: happy
			;;
		*)
			echo Oops, .git/HEAD is not 0664 but $actual
			false
			;;
		esac
	'
	rm -rf sub
done

test_expect_success 'shared=all' '
	mkdir sub &&
	cd sub &&
	git init --shared=all &&
	test 2 = $(git config core.sharedrepository)
'

test_expect_success POSIXPERM 'update-server-info honors core.sharedRepository' '
	: > a1 &&
	git add a1 &&
	test_tick &&
	git commit -m a1 &&
	umask 0277 &&
	git update-server-info &&
	actual="$(ls -l .git/info/refs)" &&
	case "$actual" in
	-r--r--r--*)
		: happy
		;;
	*)
		echo Oops, .git/info/refs is not 0444
		false
		;;
	esac
'

for u in	0660:rw-rw---- \
		0640:rw-r----- \
		0600:rw------- \
		0666:rw-rw-rw- \
		0664:rw-rw-r--
do
	x=$(expr "$u" : ".*:\([rw-]*\)") &&
	y=$(echo "$x" | sed -e "s/w/-/g") &&
	u=$(expr "$u" : "\([0-7]*\)") &&
	git config core.sharedrepository "$u" &&
	umask 0277 &&

	test_expect_success POSIXPERM "shared = $u ($y) ro" '

		rm -f .git/info/refs &&
		git update-server-info &&
		actual="$(test_modebits .git/info/refs)" &&
		verbose test "x$actual" = "x-$y"

	'

	umask 077 &&
	test_expect_success POSIXPERM "shared = $u ($x) rw" '

		rm -f .git/info/refs &&
		git update-server-info &&
		actual="$(test_modebits .git/info/refs)" &&
		verbose test "x$actual" = "x-$x"

	'

done

test_expect_success POSIXPERM 'info/refs respects umask in unshared repo' '
	rm -f .git/info/refs &&
	test_unconfig core.sharedrepository &&
	umask 002 &&
	git update-server-info &&
	echo "-rw-rw-r--" >expect &&
	test_modebits .git/info/refs >actual &&
	test_cmp expect actual
'

# For reftable, the check on .git/reftable/ is sufficient.
test_expect_success REFFILES,POSIXPERM 'git reflog expire honors core.sharedRepository' '
	umask 077 &&
	git config core.sharedRepository group &&
	git reflog expire --all &&
	actual="$(ls -l .git/logs/refs/heads/main)" &&
	case "$actual" in
	-rw-rw-*)
		: happy
		;;
	*)
		echo Ooops, .git/logs/refs/heads/main is not 066x [$actual]
		false
		;;
	esac
'

test_expect_success POSIXPERM 'forced modes' '
	mkdir -p templates/hooks &&
	echo update-server-info >templates/hooks/post-update &&
	chmod +x templates/hooks/post-update &&
	echo : >random-file &&
	mkdir new &&
	(
		cd new &&
		umask 002 &&
		git init --shared=0660 --template=templates &&
		>frotz &&
		git add frotz &&
		git commit -a -m initial &&
		git repack
	) &&
	# List repository files meant to be protected; note that
	# COMMIT_EDITMSG does not matter---0mode is not about a
	# repository with a work tree.
	find new/.git -type f -name COMMIT_EDITMSG -prune -o -print |
	xargs ls -ld >actual &&

	# Everything must be unaccessible to others
	test -z "$(sed -e "/^.......---/d" actual)" &&

	# All directories must have either 2770 or 770
	test -z "$(sed -n -e "/^drwxrw[sx]---/d" -e "/^d/p" actual)" &&

	# post-update hook must be 0770
	test -z "$(sed -n -e "/post-update/{
		/^-rwxrwx---/d
		p
	}" actual)" &&

	# All files inside objects must be accessible by us
	test -z "$(sed -n -e "/objects\//{
		/^d/d
		/^-r.-r.----/d
		p
	}" actual)"
'

test_expect_success POSIXPERM 'remote init does not use config from cwd' '
	git config core.sharedrepository 0666 &&
	umask 0022 &&
	git init --bare child.git &&
	echo "-rw-r--r--" >expect &&
	test_modebits child.git/config >actual &&
	test_cmp expect actual
'

test_expect_success POSIXPERM 're-init respects core.sharedrepository (local)' '
	git config core.sharedrepository 0666 &&
	umask 0022 &&
	echo whatever >templates/foo &&
	git init --template=templates &&
	echo "-rw-rw-rw-" >expect &&
	test_modebits .git/foo >actual &&
	test_cmp expect actual
'

test_expect_success POSIXPERM 're-init respects core.sharedrepository (remote)' '
	rm -rf child.git &&
	umask 0022 &&
	git init --bare --shared=0666 child.git &&
	test_path_is_missing child.git/foo &&
	git init --bare --template=templates child.git &&
	echo "-rw-rw-rw-" >expect &&
	test_modebits child.git/foo >actual &&
	test_cmp expect actual
'

test_expect_success REFFILES,POSIXPERM 'template can set core.sharedrepository' '
	rm -rf child.git &&
	umask 0022 &&
	git config core.sharedrepository 0666 &&
	cp .git/config templates/config &&
	git init --bare --template=templates child.git &&
	echo "-rw-rw-rw-" >expect &&
	test_modebits child.git/HEAD >actual &&
	test_cmp expect actual
'

test_done
