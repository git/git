# Shell library sourced instead of ./test-lib.sh by cvsimport tests.

. ./test-lib.sh

unset CVS_SERVER

if ! type cvs >/dev/null 2>&1
then
	skip_all='skipping cvsimport tests, cvs not found'
	test_done
fi

CVS="cvs -f"
export CVS

cvsps_version=$(cvsps -h 2>&1 | sed -ne 's/cvsps version //p')
case "$cvsps_version" in
2.1 | 2.2*)
	;;
'')
	skip_all='skipping cvsimport tests, cvsps not found'
	test_done
	;;
*)
	skip_all='skipping cvsimport tests, unsupported cvsps version'
	test_done
	;;
esac

setup_cvs_test_repository () {
	CVSROOT="$(pwd)/.cvsroot" &&
	cp -r "$TEST_DIRECTORY/$1/cvsroot" "$CVSROOT" &&
	export CVSROOT
}

test_cvs_co () {
	# Usage: test_cvs_co BRANCH_NAME
	rm -rf module-cvs-"$1"
	if [ "$1" = "master" ]
	then
		$CVS co -P -d module-cvs-"$1" -A module
	else
		$CVS co -P -d module-cvs-"$1" -r "$1" module
	fi
}

test_git_co () {
	# Usage: test_git_co BRANCH_NAME
	(cd module-git && git checkout "$1")
}

test_cmp_branch_file () {
	# Usage: test_cmp_branch_file BRANCH_NAME PATH
	# The branch must already be checked out of CVS and git.
	test_cmp module-cvs-"$1"/"$2" module-git/"$2"
}

test_cmp_branch_tree () {
	# Usage: test_cmp_branch_tree BRANCH_NAME
	# Check BRANCH_NAME out of CVS and git and make sure that all
	# of the files and directories are identical.

	test_cvs_co "$1" &&
	test_git_co "$1" &&
	(
		cd module-cvs-"$1"
		find . -type d -name CVS -prune -o -type f -print
	) | sort >module-cvs-"$1".list &&
	(
		cd module-git
		find . -type d -name .git -prune -o -type f -print
	) | sort >module-git-"$1".list &&
	test_cmp module-cvs-"$1".list module-git-"$1".list &&
	cat module-cvs-"$1".list | while read f
	do
		test_cmp_branch_file "$1" "$f" || return 1
	done
}
