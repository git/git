#!/bin/sh
#
# Copyright (c) 2012 Robert Luberda
#

test_description='concurrent git svn dcommit'

. ./lib-git-svn.sh



test_expect_success 'setup svn repository' '
	svn_cmd checkout "$svnrepo" work.svn &&
	(
		cd work.svn &&
		echo >file && echo > auto_updated_file &&
		svn_cmd add file auto_updated_file &&
		svn_cmd commit -m "initial commit"
	) &&
	svn_cmd checkout "$svnrepo" work-auto-commits.svn
'
N=0
next_N()
{
	N=$(( $N + 1 ))
}

# Setup SVN repository hooks to emulate SVN failures or concurrent commits
# The function adds
# either pre-commit  hook, which causes SVN commit given in second argument
#                    to fail
# or     post-commit hook, which creates a new commit (a new line added to
#                    auto_updated_file) after given SVN commit
# The first argument contains a type of the hook
# The second argument contains a number (not SVN revision) of commit
# the hook should be applied for (each time the hook is run, the given
# number is decreased by one until it gets 0, in which case the hook
# will execute its real action)
setup_hook()
{
	hook_type="$1"  # "pre-commit" or "post-commit"
	skip_revs="$2"
	[ "$hook_type" = "pre-commit" ] ||
		[ "$hook_type" = "post-commit" ] ||
		{ echo "ERROR: invalid argument ($hook_type)" \
			"passed to setup_hook" >&2 ; return 1; }
	echo "cnt=$skip_revs" > "$hook_type-counter"
	rm -f "$rawsvnrepo/hooks/"*-commit # drop previous hooks
	hook="$rawsvnrepo/hooks/$hook_type"
	cat > "$hook" <<- 'EOF1'
		#!/bin/sh
		set -e
		cd "$1/.."  # "$1" is repository location
		exec >> svn-hook.log 2>&1
		hook="$(basename "$0")"
		echo "*** Executing $hook $@"
		set -x
		. ./$hook-counter
		cnt="$(($cnt - 1))"
		echo "cnt=$cnt" > ./$hook-counter
		[ "$cnt" = "0" ] || exit 0
EOF1
	if [ "$hook_type" = "pre-commit" ]; then
		echo "echo 'commit disallowed' >&2; exit 1" >>"$hook"
	else
		echo "PATH=\"$PATH\"; export PATH" >>"$hook"
		echo "svnconf=\"$svnconf\"" >>"$hook"
		cat >>"$hook" <<- 'EOF2'
			cd work-auto-commits.svn
			svn up --config-dir "$svnconf"
			echo "$$" >> auto_updated_file
			svn commit --config-dir "$svnconf" \
				-m "auto-committing concurrent change"
			exit 0
EOF2
	fi
	chmod 755 "$hook"
}

check_contents()
{
	gitdir="$1"
	(cd ../work.svn && svn_cmd up) &&
	test_cmp file ../work.svn/file &&
	test_cmp auto_updated_file ../work.svn/auto_updated_file
}

test_expect_success 'check if post-commit hook creates a concurrent commit' '
	setup_hook post-commit 1 &&
	(
		cd work.svn &&
		cp auto_updated_file au_file_saved &&
		echo 1 >> file &&
		svn_cmd commit -m "changing file" &&
		svn_cmd up &&
		! test_cmp auto_updated_file au_file_saved
	)
'

test_expect_success 'check if pre-commit hook fails' '
	setup_hook pre-commit 2 &&
	(
		cd work.svn &&
		echo 2 >> file &&
		svn_cmd commit -m "changing file once again" &&
		echo 3 >> file &&
		! svn_cmd commit -m "this commit should fail" &&
		svn_cmd revert file
	)
'

test_expect_success 'dcommit error handling' '
	setup_hook pre-commit 2 &&
	next_N && git svn clone "$svnrepo" work$N.git &&
	(
		cd work$N.git &&
		echo 1 >> file && git commit -am "commit change $N.1" &&
		echo 2 >> file && git commit -am "commit change $N.2" &&
		echo 3 >> file && git commit -am "commit change $N.3" &&
		# should fail to dcommit 2nd and 3rd change
		# but still should leave the repository in reasonable state
		test_must_fail git svn dcommit &&
		git update-index --refresh &&
		git show HEAD~2   | grep -q git-svn-id &&
		! git show HEAD~1 | grep -q git-svn-id &&
		! git show HEAD   | grep -q git-svn-id
	)
'

test_expect_success 'dcommit concurrent change in non-changed file' '
	setup_hook post-commit 2 &&
	next_N && git svn clone "$svnrepo" work$N.git &&
	(
		cd work$N.git &&
		echo 1 >> file && git commit -am "commit change $N.1" &&
		echo 2 >> file && git commit -am "commit change $N.2" &&
		echo 3 >> file && git commit -am "commit change $N.3" &&
		# should rebase and leave the repository in reasonable state
		git svn dcommit &&
		git update-index --refresh &&
		check_contents &&
		git show HEAD~3 | grep -q git-svn-id &&
		git show HEAD~2 | grep -q git-svn-id &&
		git show HEAD~1 | grep -q auto-committing &&
		git show HEAD   | grep -q git-svn-id
	)
'

# An utility function used in the following test
delete_first_line()
{
	file="$1" &&
	sed 1d < "$file" > "${file}.tmp" &&
	rm "$file" &&
	mv "${file}.tmp" "$file"
}

test_expect_success 'dcommit concurrent non-conflicting change' '
	setup_hook post-commit 2 &&
	next_N && git svn clone "$svnrepo" work$N.git &&
	(
		cd work$N.git &&
		cat file >> auto_updated_file &&
			git commit -am "commit change $N.1" &&
		delete_first_line auto_updated_file &&
			git commit -am "commit change $N.2" &&
		delete_first_line auto_updated_file &&
			git commit -am "commit change $N.3" &&
		# should rebase and leave the repository in reasonable state
		git svn dcommit &&
		git update-index --refresh &&
		check_contents &&
		git show HEAD~3 | grep -q git-svn-id &&
		git show HEAD~2 | grep -q git-svn-id &&
		git show HEAD~1 | grep -q auto-committing &&
		git show HEAD   | grep -q git-svn-id
	)
'

test_expect_success 'dcommit --no-rebase concurrent non-conflicting change' '
	setup_hook post-commit 2 &&
	next_N && git svn clone "$svnrepo" work$N.git &&
	(
		cd work$N.git &&
		cat file >> auto_updated_file &&
			git commit -am "commit change $N.1" &&
		delete_first_line auto_updated_file &&
			git commit -am "commit change $N.2" &&
		delete_first_line auto_updated_file &&
			git commit -am "commit change $N.3" &&
		# should fail as rebase is needed
		test_must_fail git svn dcommit --no-rebase &&
		# but should leave HEAD unchanged
		git update-index --refresh &&
		! git show HEAD~2 | grep -q git-svn-id &&
		! git show HEAD~1 | grep -q git-svn-id &&
		! git show HEAD   | grep -q git-svn-id
	)
'

test_expect_success 'dcommit fails on concurrent conflicting change' '
	setup_hook post-commit 1 &&
	next_N && git svn clone "$svnrepo" work$N.git &&
	(
		cd work$N.git &&
		echo a >> file &&
			git commit -am "commit change $N.1" &&
		echo b >> auto_updated_file &&
			git commit -am "commit change $N.2" &&
		echo c >> auto_updated_file &&
			git commit -am "commit change $N.3" &&
		test_must_fail git svn dcommit && # rebase should fail
		test_must_fail git update-index --refresh
	)
'

test_done
