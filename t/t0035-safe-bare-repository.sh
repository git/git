#!/bin/sh

test_description='verify safe.bareRepository checks'

. ./test-lib.sh

pwd="$(pwd)"

expect_accepted_implicit () {
	test_when_finished 'rm "$pwd/trace.perf"' &&
	GIT_TRACE2_PERF="$pwd/trace.perf" git "$@" rev-parse --git-dir &&
	# Note: we're intentionally only checking that the bare repo has a
	# directory *prefix* of $pwd
	grep -F "implicit-bare-repository:$pwd" "$pwd/trace.perf"
}

expect_accepted_explicit () {
	test_when_finished 'rm "$pwd/trace.perf"' &&
	GIT_DIR="$1" GIT_TRACE2_PERF="$pwd/trace.perf" git rev-parse --git-dir &&
	! grep -F "implicit-bare-repository:$pwd" "$pwd/trace.perf"
}

expect_rejected () {
	test_when_finished 'rm "$pwd/trace.perf"' &&
	test_env GIT_TRACE2_PERF="$pwd/trace.perf" \
		test_must_fail git "$@" rev-parse --git-dir 2>err &&
	grep -F "cannot use bare repository" err &&
	grep -F "implicit-bare-repository:$pwd" "$pwd/trace.perf"
}

test_expect_success 'setup an embedded bare repo, secondary worktree and submodule' '
	git init outer-repo &&
	git init --bare --initial-branch=main outer-repo/bare-repo &&
	git -C outer-repo worktree add ../outer-secondary &&
	test_path_is_dir outer-secondary &&
	(
		cd outer-repo &&
		test_commit A &&
		git push bare-repo +HEAD:refs/heads/main &&
		git -c protocol.file.allow=always \
			submodule add --name subn -- ./bare-repo subd
	) &&
	test_path_is_dir outer-repo/.git/worktrees/outer-secondary &&
	test_path_is_dir outer-repo/.git/modules/subn
'

test_expect_success 'safe.bareRepository unset' '
	test_unconfig --global safe.bareRepository &&
	expect_accepted_implicit -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository=all' '
	test_config_global safe.bareRepository all &&
	expect_accepted_implicit -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository=explicit' '
	test_config_global safe.bareRepository explicit &&
	expect_rejected -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository in the repository' '
	# safe.bareRepository must not be "explicit", otherwise
	# git config fails with "fatal: not in a git directory" (like
	# safe.directory)
	test_config -C outer-repo/bare-repo safe.bareRepository all &&
	test_config_global safe.bareRepository explicit &&
	expect_rejected -C outer-repo/bare-repo
'

test_expect_success 'safe.bareRepository on the command line' '
	test_config_global safe.bareRepository explicit &&
	expect_accepted_implicit -C outer-repo/bare-repo \
		-c safe.bareRepository=all
'

test_expect_success 'safe.bareRepository in included file' '
	cat >gitconfig-include <<-\EOF &&
	[safe]
		bareRepository = explicit
	EOF
	git config --global --add include.path "$(pwd)/gitconfig-include" &&
	expect_rejected -C outer-repo/bare-repo
'

test_expect_success 'no trace when GIT_DIR is explicitly provided' '
	expect_accepted_explicit "$pwd/outer-repo/bare-repo"
'

test_expect_success 'no trace when "bare repository" is .git' '
	expect_accepted_implicit -C outer-repo/.git
'

test_expect_success 'no trace when "bare repository" is a subdir of .git' '
	expect_accepted_implicit -C outer-repo/.git/objects
'

test_expect_success 'no trace in $GIT_DIR of secondary worktree' '
	expect_accepted_implicit -C outer-repo/.git/worktrees/outer-secondary
'

test_expect_success 'no trace in $GIT_DIR of a submodule' '
	expect_accepted_implicit -C outer-repo/.git/modules/subn
'

test_done
