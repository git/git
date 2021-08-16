#!/bin/sh
#
# Copyright (c) 2012-2020 Felipe Contreras
#

test_description='test bash completion'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=master
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-bash.sh

complete ()
{
	# do nothing
	return 0
}

# Be careful when updating these lists:
#
# (1) The build tree may have build artifact from different branch, or
#     the user's $PATH may have a random executable that may begin
#     with "git-check" that are not part of the subcommands this build
#     will ship, e.g.  "check-ignore".  The tests for completion for
#     subcommand names tests how "check" is expanded; we limit the
#     possible candidates to "checkout" and "check-attr" to make sure
#     "check-attr", which is known by the filter function as a
#     subcommand to be thrown out, while excluding other random files
#     that happen to begin with "check" to avoid letting them get in
#     the way.
#
# (2) A test makes sure that common subcommands are included in the
#     completion for "git <TAB>", and a plumbing is excluded.  "add",
#     "rebase" and "ls-files" are listed for this.

GIT_TESTING_ALL_COMMAND_LIST='add checkout check-attr rebase ls-files'
GIT_TESTING_PORCELAIN_COMMAND_LIST='add checkout rebase'

. "$GIT_BUILD_DIR/contrib/completion/git-completion.bash"

# We don't need this function to actually join words or do anything special.
# Also, it's cleaner to avoid touching bash's internal completion variables.
# So let's override it with a minimal version for testing purposes.
_get_comp_words_by_ref ()
{
	while [ $# -gt 0 ]; do
		case "$1" in
		cur)
			cur=${_words[_cword]}
			;;
		prev)
			prev=${_words[_cword-1]}
			;;
		words)
			words=("${_words[@]}")
			;;
		cword)
			cword=$_cword
			;;
		esac
		shift
	done
}

print_comp ()
{
	local IFS=$'\n'
	echo "${COMPREPLY[*]}" > out
}

run_completion ()
{
	local -a COMPREPLY _words
	local _cword
	_words=( $1 )
	test "${1: -1}" = ' ' && _words[${#_words[@]}+1]=''
	(( _cword = ${#_words[@]} - 1 ))
	__git_wrap__git_main && print_comp
}

# Test high-level completion
# Arguments are:
# 1: typed text so far (cur)
# 2: expected completion
test_completion ()
{
	if test $# -gt 1
	then
		printf '%s\n' "$2" >expected
	else
		sed -e 's/Z$//' |sort >expected
	fi &&
	run_completion "$1" &&
	sort out >out_sorted &&
	test_cmp expected out_sorted
}

# Test __gitcomp.
# The first argument is the typed text so far (cur); the rest are
# passed to __gitcomp.  Expected output comes is read from the
# standard input, like test_completion().
test_gitcomp ()
{
	local -a COMPREPLY &&
	sed -e 's/Z$//' >expected &&
	local cur="$1" &&
	shift &&
	__gitcomp "$@" &&
	print_comp &&
	test_cmp expected out
}

# Test __gitcomp_nl
# Arguments are:
# 1: current word (cur)
# -: the rest are passed to __gitcomp_nl
test_gitcomp_nl ()
{
	local -a COMPREPLY &&
	sed -e 's/Z$//' >expected &&
	local cur="$1" &&
	shift &&
	__gitcomp_nl "$@" &&
	print_comp &&
	test_cmp expected out
}

invalid_variable_name='${foo.bar}'

actual="$TRASH_DIRECTORY/actual"

if test_have_prereq MINGW
then
	ROOT="$(pwd -W)"
else
	ROOT="$(pwd)"
fi

test_expect_success 'setup for __git_find_repo_path/__gitdir tests' '
	mkdir -p subdir/subsubdir &&
	mkdir -p non-repo &&
	git init -b main otherrepo
'

test_expect_success '__git_find_repo_path - from command line (through $__git_dir)' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		__git_dir="$ROOT/otherrepo/.git" &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - .git directory in cwd' '
	echo ".git" >expected &&
	(
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - .git directory in parent' '
	echo "$ROOT/.git" >expected &&
	(
		cd subdir/subsubdir &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - cwd is a .git directory' '
	echo "." >expected &&
	(
		cd .git &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - parent is a .git directory' '
	echo "$ROOT/.git" >expected &&
	(
		cd .git/objects &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - $GIT_DIR set while .git directory in cwd' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		GIT_DIR="$ROOT/otherrepo/.git" &&
		export GIT_DIR &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - $GIT_DIR set while .git directory in parent' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		GIT_DIR="$ROOT/otherrepo/.git" &&
		export GIT_DIR &&
		cd subdir &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - from command line while "git -C"' '
	echo "$ROOT/.git" >expected &&
	(
		__git_dir="$ROOT/.git" &&
		__git_C_args=(-C otherrepo) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - relative dir from command line and "git -C"' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		cd subdir &&
		__git_dir="otherrepo/.git" &&
		__git_C_args=(-C ..) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - $GIT_DIR set while "git -C"' '
	echo "$ROOT/.git" >expected &&
	(
		GIT_DIR="$ROOT/.git" &&
		export GIT_DIR &&
		__git_C_args=(-C otherrepo) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - relative dir in $GIT_DIR and "git -C"' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		cd subdir &&
		GIT_DIR="otherrepo/.git" &&
		export GIT_DIR &&
		__git_C_args=(-C ..) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - "git -C" while .git directory in cwd' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		__git_C_args=(-C otherrepo) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - "git -C" while cwd is a .git directory' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		cd .git &&
		__git_C_args=(-C .. -C otherrepo) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - "git -C" while .git directory in parent' '
	echo "$ROOT/otherrepo/.git" >expected &&
	(
		cd subdir &&
		__git_C_args=(-C .. -C otherrepo) &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - non-existing path in "git -C"' '
	(
		__git_C_args=(-C non-existing) &&
		test_must_fail __git_find_repo_path &&
		printf "$__git_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_find_repo_path - non-existing path in $__git_dir' '
	(
		__git_dir="non-existing" &&
		test_must_fail __git_find_repo_path &&
		printf "$__git_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_find_repo_path - non-existing $GIT_DIR' '
	(
		GIT_DIR="$ROOT/non-existing" &&
		export GIT_DIR &&
		test_must_fail __git_find_repo_path &&
		printf "$__git_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_find_repo_path - gitfile in cwd' '
	echo "$ROOT/otherrepo/.git" >expected &&
	echo "gitdir: $ROOT/otherrepo/.git" >subdir/.git &&
	test_when_finished "rm -f subdir/.git" &&
	(
		cd subdir &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - gitfile in parent' '
	echo "$ROOT/otherrepo/.git" >expected &&
	echo "gitdir: $ROOT/otherrepo/.git" >subdir/.git &&
	test_when_finished "rm -f subdir/.git" &&
	(
		cd subdir/subsubdir &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success SYMLINKS '__git_find_repo_path - resulting path avoids symlinks' '
	echo "$ROOT/otherrepo/.git" >expected &&
	mkdir otherrepo/dir &&
	test_when_finished "rm -rf otherrepo/dir" &&
	ln -s otherrepo/dir link &&
	test_when_finished "rm -f link" &&
	(
		cd link &&
		__git_find_repo_path &&
		echo "$__git_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_find_repo_path - not a git repository' '
	(
		cd non-repo &&
		GIT_CEILING_DIRECTORIES="$ROOT" &&
		export GIT_CEILING_DIRECTORIES &&
		test_must_fail __git_find_repo_path &&
		printf "$__git_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__gitdir - finds repo' '
	echo "$ROOT/.git" >expected &&
	(
		cd subdir/subsubdir &&
		__gitdir >"$actual"
	) &&
	test_cmp expected "$actual"
'


test_expect_success '__gitdir - returns error when cannot find repo' '
	(
		__git_dir="non-existing" &&
		test_must_fail __gitdir >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__gitdir - repo as argument' '
	echo "otherrepo/.git" >expected &&
	(
		__gitdir "otherrepo" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__gitdir - remote as argument' '
	echo "remote" >expected &&
	(
		__gitdir "remote" >"$actual"
	) &&
	test_cmp expected "$actual"
'


test_expect_success '__git_dequote - plain unquoted word' '
	__git_dequote unquoted-word &&
	verbose test unquoted-word = "$dequoted_word"
'

# input:    b\a\c\k\'\\\"s\l\a\s\h\es
# expected: back'\"slashes
test_expect_success '__git_dequote - backslash escaped' '
	__git_dequote "b\a\c\k\\'\''\\\\\\\"s\l\a\s\h\es" &&
	verbose test "back'\''\\\"slashes" = "$dequoted_word"
'

# input:    sin'gle\' '"quo'ted
# expected: single\ "quoted
test_expect_success '__git_dequote - single quoted' '
	__git_dequote "'"sin'gle\\\\' '\\\"quo'ted"'" &&
	verbose test '\''single\ "quoted'\'' = "$dequoted_word"
'

# input:    dou"ble\\" "\"\quot"ed
# expected: double\ "\quoted
test_expect_success '__git_dequote - double quoted' '
	__git_dequote '\''dou"ble\\" "\"\quot"ed'\'' &&
	verbose test '\''double\ "\quoted'\'' = "$dequoted_word"
'

# input: 'open single quote
test_expect_success '__git_dequote - open single quote' '
	__git_dequote "'\''open single quote" &&
	verbose test "open single quote" = "$dequoted_word"
'

# input: "open double quote
test_expect_success '__git_dequote - open double quote' '
	__git_dequote "\"open double quote" &&
	verbose test "open double quote" = "$dequoted_word"
'


test_expect_success '__gitcomp_direct - puts everything into COMPREPLY as-is' '
	sed -e "s/Z$//g" >expected <<-EOF &&
	with-trailing-space Z
	without-trailing-spaceZ
	--option Z
	--option=Z
	$invalid_variable_name Z
	EOF
	(
		cur=should_be_ignored &&
		__gitcomp_direct "$(cat expected)" &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__gitcomp - trailing space - options' '
	test_gitcomp "--re" "--dry-run --reuse-message= --reedit-message=
		--reset-author" <<-EOF
	--reuse-message=Z
	--reedit-message=Z
	--reset-author Z
	EOF
'

test_expect_success '__gitcomp - trailing space - config keys' '
	test_gitcomp "br" "branch. branch.autosetupmerge
		branch.autosetuprebase browser." <<-\EOF
	branch.Z
	branch.autosetupmerge Z
	branch.autosetuprebase Z
	browser.Z
	EOF
'

test_expect_success '__gitcomp - option parameter' '
	test_gitcomp "--strategy=re" "octopus ours recursive resolve subtree" \
		"" "re" <<-\EOF
	recursive Z
	resolve Z
	EOF
'

test_expect_success '__gitcomp - prefix' '
	test_gitcomp "branch.me" "remote merge mergeoptions rebase" \
		"branch.maint." "me" <<-\EOF
	branch.maint.merge Z
	branch.maint.mergeoptions Z
	EOF
'

test_expect_success '__gitcomp - suffix' '
	test_gitcomp "branch.me" "master maint next seen" "branch." \
		"ma" "." <<-\EOF
	branch.master.Z
	branch.maint.Z
	EOF
'

test_expect_success '__gitcomp - ignore optional negative options' '
	test_gitcomp "--" "--abc --def --no-one -- --no-two" <<-\EOF
	--abc Z
	--def Z
	--no-one Z
	--no-... Z
	EOF
'

test_expect_success '__gitcomp - ignore/narrow optional negative options' '
	test_gitcomp "--a" "--abc --abcdef --no-one -- --no-two" <<-\EOF
	--abc Z
	--abcdef Z
	EOF
'

test_expect_success '__gitcomp - ignore/narrow optional negative options' '
	test_gitcomp "--n" "--abc --def --no-one -- --no-two" <<-\EOF
	--no-one Z
	--no-... Z
	EOF
'

test_expect_success '__gitcomp - expand all negative options' '
	test_gitcomp "--no-" "--abc --def --no-one -- --no-two" <<-\EOF
	--no-one Z
	--no-two Z
	EOF
'

test_expect_success '__gitcomp - expand/narrow all negative options' '
	test_gitcomp "--no-o" "--abc --def --no-one -- --no-two" <<-\EOF
	--no-one Z
	EOF
'

test_expect_success '__gitcomp - equal skip' '
	test_gitcomp "--option=" "--option=" <<-\EOF &&

	EOF
	test_gitcomp "option=" "option=" <<-\EOF

	EOF
'

test_expect_success '__gitcomp - doesnt fail because of invalid variable name' '
	__gitcomp "$invalid_variable_name"
'

read -r -d "" refs <<-\EOF
main
maint
next
seen
EOF

test_expect_success '__gitcomp_nl - trailing space' '
	test_gitcomp_nl "m" "$refs" <<-EOF
	main Z
	maint Z
	EOF
'

test_expect_success '__gitcomp_nl - prefix' '
	test_gitcomp_nl "--fixup=m" "$refs" "--fixup=" "m" <<-EOF
	--fixup=main Z
	--fixup=maint Z
	EOF
'

test_expect_success '__gitcomp_nl - suffix' '
	test_gitcomp_nl "branch.ma" "$refs" "branch." "ma" "." <<-\EOF
	branch.main.Z
	branch.maint.Z
	EOF
'

test_expect_success '__gitcomp_nl - no suffix' '
	test_gitcomp_nl "ma" "$refs" "" "ma" "" <<-\EOF
	mainZ
	maintZ
	EOF
'

test_expect_success '__gitcomp_nl - doesnt fail because of invalid variable name' '
	__gitcomp_nl "$invalid_variable_name"
'

test_expect_success '__git_remotes - list remotes from $GIT_DIR/remotes and from config file' '
	cat >expect <<-EOF &&
	remote_from_file_1
	remote_from_file_2
	remote_in_config_1
	remote_in_config_2
	EOF
	test_when_finished "rm -rf .git/remotes" &&
	mkdir -p .git/remotes &&
	>.git/remotes/remote_from_file_1 &&
	>.git/remotes/remote_from_file_2 &&
	test_when_finished "git remote remove remote_in_config_1" &&
	git remote add remote_in_config_1 git://remote_1 &&
	test_when_finished "git remote remove remote_in_config_2" &&
	git remote add remote_in_config_2 git://remote_2 &&
	(
		__git_remotes >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__git_is_configured_remote' '
	test_when_finished "git remote remove remote_1" &&
	git remote add remote_1 git://remote_1 &&
	test_when_finished "git remote remove remote_2" &&
	git remote add remote_2 git://remote_2 &&
	(
		verbose __git_is_configured_remote remote_2 &&
		test_must_fail __git_is_configured_remote non-existent
	)
'

test_expect_success 'setup for ref completion' '
	git commit --allow-empty -m initial &&
	git branch -M main &&
	git branch matching-branch &&
	git tag matching-tag &&
	(
		cd otherrepo &&
		git commit --allow-empty -m initial &&
		git branch -m main main-in-other &&
		git branch branch-in-other &&
		git tag tag-in-other
	) &&
	git remote add other "$ROOT/otherrepo/.git" &&
	git fetch --no-tags other &&
	rm -f .git/FETCH_HEAD &&
	git init thirdrepo
'

test_expect_success '__git_refs - simple' '
	cat >expected <<-EOF &&
	HEAD
	main
	matching-branch
	other/branch-in-other
	other/main-in-other
	matching-tag
	EOF
	(
		cur= &&
		__git_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - full refs' '
	cat >expected <<-EOF &&
	refs/heads/main
	refs/heads/matching-branch
	refs/remotes/other/branch-in-other
	refs/remotes/other/main-in-other
	refs/tags/matching-tag
	EOF
	(
		cur=refs/heads/ &&
		__git_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - repo given on the command line' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	tag-in-other
	EOF
	(
		__git_dir="$ROOT/otherrepo/.git" &&
		cur= &&
		__git_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - remote on local file system' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	tag-in-other
	EOF
	(
		cur= &&
		__git_refs otherrepo >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - remote on local file system - full refs' '
	cat >expected <<-EOF &&
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cur=refs/ &&
		__git_refs otherrepo >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - configured remote' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	EOF
	(
		cur= &&
		__git_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - configured remote - full refs' '
	cat >expected <<-EOF &&
	HEAD
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cur=refs/ &&
		__git_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - configured remote - repo given on the command line' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	EOF
	(
		cd thirdrepo &&
		__git_dir="$ROOT/.git" &&
		cur= &&
		__git_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - configured remote - full refs - repo given on the command line' '
	cat >expected <<-EOF &&
	HEAD
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cd thirdrepo &&
		__git_dir="$ROOT/.git" &&
		cur=refs/ &&
		__git_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - configured remote - remote name matches a directory' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	EOF
	mkdir other &&
	test_when_finished "rm -rf other" &&
	(
		cur= &&
		__git_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - URL remote' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	tag-in-other
	EOF
	(
		cur= &&
		__git_refs "file://$ROOT/otherrepo/.git" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - URL remote - full refs' '
	cat >expected <<-EOF &&
	HEAD
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cur=refs/ &&
		__git_refs "file://$ROOT/otherrepo/.git" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - non-existing remote' '
	(
		cur= &&
		__git_refs non-existing >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_refs - non-existing remote - full refs' '
	(
		cur=refs/ &&
		__git_refs non-existing >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_refs - non-existing URL remote' '
	(
		cur= &&
		__git_refs "file://$ROOT/non-existing" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_refs - non-existing URL remote - full refs' '
	(
		cur=refs/ &&
		__git_refs "file://$ROOT/non-existing" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_refs - not in a git repository' '
	(
		GIT_CEILING_DIRECTORIES="$ROOT" &&
		export GIT_CEILING_DIRECTORIES &&
		cd subdir &&
		cur= &&
		__git_refs >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__git_refs - unique remote branches for git checkout DWIMery' '
	cat >expected <<-EOF &&
	HEAD
	main
	matching-branch
	other/ambiguous
	other/branch-in-other
	other/main-in-other
	remote/ambiguous
	remote/branch-in-remote
	matching-tag
	branch-in-other
	branch-in-remote
	main-in-other
	EOF
	for remote_ref in refs/remotes/other/ambiguous \
		refs/remotes/remote/ambiguous \
		refs/remotes/remote/branch-in-remote
	do
		git update-ref $remote_ref main &&
		test_when_finished "git update-ref -d $remote_ref"
	done &&
	(
		cur= &&
		__git_refs "" 1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - after --opt=' '
	cat >expected <<-EOF &&
	HEAD
	main
	matching-branch
	other/branch-in-other
	other/main-in-other
	matching-tag
	EOF
	(
		cur="--opt=" &&
		__git_refs "" "" "" "" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - after --opt= - full refs' '
	cat >expected <<-EOF &&
	refs/heads/main
	refs/heads/matching-branch
	refs/remotes/other/branch-in-other
	refs/remotes/other/main-in-other
	refs/tags/matching-tag
	EOF
	(
		cur="--opt=refs/" &&
		__git_refs "" "" "" refs/ >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git refs - excluding refs' '
	cat >expected <<-EOF &&
	^HEAD
	^main
	^matching-branch
	^other/branch-in-other
	^other/main-in-other
	^matching-tag
	EOF
	(
		cur=^ &&
		__git_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git refs - excluding full refs' '
	cat >expected <<-EOF &&
	^refs/heads/main
	^refs/heads/matching-branch
	^refs/remotes/other/branch-in-other
	^refs/remotes/other/main-in-other
	^refs/tags/matching-tag
	EOF
	(
		cur=^refs/ &&
		__git_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'setup for filtering matching refs' '
	git branch matching/branch &&
	git tag matching/tag &&
	git -C otherrepo branch matching/branch-in-other &&
	git fetch --no-tags other &&
	rm -f .git/FETCH_HEAD
'

test_expect_success '__git_refs - do not filter refs unless told so' '
	cat >expected <<-EOF &&
	HEAD
	main
	matching-branch
	matching/branch
	other/branch-in-other
	other/main-in-other
	other/matching/branch-in-other
	matching-tag
	matching/tag
	EOF
	(
		cur=main &&
		__git_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - only matching refs' '
	cat >expected <<-EOF &&
	matching-branch
	matching/branch
	matching-tag
	matching/tag
	EOF
	(
		cur=mat &&
		__git_refs "" "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - only matching refs - full refs' '
	cat >expected <<-EOF &&
	refs/heads/matching-branch
	refs/heads/matching/branch
	EOF
	(
		cur=refs/heads/mat &&
		__git_refs "" "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - only matching refs - remote on local file system' '
	cat >expected <<-EOF &&
	main-in-other
	matching/branch-in-other
	EOF
	(
		cur=ma &&
		__git_refs otherrepo "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - only matching refs - configured remote' '
	cat >expected <<-EOF &&
	main-in-other
	matching/branch-in-other
	EOF
	(
		cur=ma &&
		__git_refs other "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - only matching refs - remote - full refs' '
	cat >expected <<-EOF &&
	refs/heads/main-in-other
	refs/heads/matching/branch-in-other
	EOF
	(
		cur=refs/heads/ma &&
		__git_refs other "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_refs - only matching refs - checkout DWIMery' '
	cat >expected <<-EOF &&
	matching-branch
	matching/branch
	matching-tag
	matching/tag
	matching/branch-in-other
	EOF
	for remote_ref in refs/remotes/other/ambiguous \
		refs/remotes/remote/ambiguous \
		refs/remotes/remote/branch-in-remote
	do
		git update-ref $remote_ref main &&
		test_when_finished "git update-ref -d $remote_ref"
	done &&
	(
		cur=mat &&
		__git_refs "" 1 "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'teardown after filtering matching refs' '
	git branch -d matching/branch &&
	git tag -d matching/tag &&
	git update-ref -d refs/remotes/other/matching/branch-in-other &&
	git -C otherrepo branch -D matching/branch-in-other
'

test_expect_success '__git_refs - for-each-ref format specifiers in prefix' '
	cat >expected <<-EOF &&
	evil-%%-%42-%(refname)..main
	EOF
	(
		cur="evil-%%-%42-%(refname)..mai" &&
		__git_refs "" "" "evil-%%-%42-%(refname).." mai >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__git_complete_refs - simple' '
	sed -e "s/Z$//" >expected <<-EOF &&
	HEAD Z
	main Z
	matching-branch Z
	other/branch-in-other Z
	other/main-in-other Z
	matching-tag Z
	EOF
	(
		cur= &&
		__git_complete_refs &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_refs - matching' '
	sed -e "s/Z$//" >expected <<-EOF &&
	matching-branch Z
	matching-tag Z
	EOF
	(
		cur=mat &&
		__git_complete_refs &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_refs - remote' '
	sed -e "s/Z$//" >expected <<-EOF &&
	HEAD Z
	branch-in-other Z
	main-in-other Z
	EOF
	(
		cur= &&
		__git_complete_refs --remote=other &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_refs - track' '
	sed -e "s/Z$//" >expected <<-EOF &&
	HEAD Z
	main Z
	matching-branch Z
	other/branch-in-other Z
	other/main-in-other Z
	matching-tag Z
	branch-in-other Z
	main-in-other Z
	EOF
	(
		cur= &&
		__git_complete_refs --track &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_refs - current word' '
	sed -e "s/Z$//" >expected <<-EOF &&
	matching-branch Z
	matching-tag Z
	EOF
	(
		cur="--option=mat" &&
		__git_complete_refs --cur="${cur#*=}" &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_refs - prefix' '
	sed -e "s/Z$//" >expected <<-EOF &&
	v1.0..matching-branch Z
	v1.0..matching-tag Z
	EOF
	(
		cur=v1.0..mat &&
		__git_complete_refs --pfx=v1.0.. --cur=mat &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_refs - suffix' '
	cat >expected <<-EOF &&
	HEAD.
	main.
	matching-branch.
	other/branch-in-other.
	other/main-in-other.
	matching-tag.
	EOF
	(
		cur= &&
		__git_complete_refs --sfx=. &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_fetch_refspecs - simple' '
	sed -e "s/Z$//" >expected <<-EOF &&
	HEAD:HEAD Z
	branch-in-other:branch-in-other Z
	main-in-other:main-in-other Z
	EOF
	(
		cur= &&
		__git_complete_fetch_refspecs other &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_fetch_refspecs - matching' '
	sed -e "s/Z$//" >expected <<-EOF &&
	branch-in-other:branch-in-other Z
	EOF
	(
		cur=br &&
		__git_complete_fetch_refspecs other "" br &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_fetch_refspecs - prefix' '
	sed -e "s/Z$//" >expected <<-EOF &&
	+HEAD:HEAD Z
	+branch-in-other:branch-in-other Z
	+main-in-other:main-in-other Z
	EOF
	(
		cur="+" &&
		__git_complete_fetch_refspecs other "+" ""  &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_fetch_refspecs - fully qualified' '
	sed -e "s/Z$//" >expected <<-EOF &&
	refs/heads/branch-in-other:refs/heads/branch-in-other Z
	refs/heads/main-in-other:refs/heads/main-in-other Z
	refs/tags/tag-in-other:refs/tags/tag-in-other Z
	EOF
	(
		cur=refs/ &&
		__git_complete_fetch_refspecs other "" refs/ &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__git_complete_fetch_refspecs - fully qualified & prefix' '
	sed -e "s/Z$//" >expected <<-EOF &&
	+refs/heads/branch-in-other:refs/heads/branch-in-other Z
	+refs/heads/main-in-other:refs/heads/main-in-other Z
	+refs/tags/tag-in-other:refs/tags/tag-in-other Z
	EOF
	(
		cur=+refs/ &&
		__git_complete_fetch_refspecs other + refs/ &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success 'git switch - with no options, complete local branches and unique remote branch names for DWIM logic' '
	test_completion "git switch " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - completes refs and unique remote branches for DWIM' '
	test_completion "git checkout " <<-\EOF
	HEAD Z
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with --no-guess, complete only local branches' '
	test_completion "git switch --no-guess " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - with GIT_COMPLETION_CHECKOUT_NO_GUESS=1, complete only local branches' '
	GIT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "git switch " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - --guess overrides GIT_COMPLETION_CHECKOUT_NO_GUESS=1, complete local branches and unique remote names for DWIM logic' '
	GIT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "git switch --guess " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - a later --guess overrides previous --no-guess, complete local and remote unique branches for DWIM' '
	test_completion "git switch --no-guess --guess " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - a later --no-guess overrides previous --guess, complete only local branches' '
	test_completion "git switch --guess --no-guess " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - with GIT_COMPLETION_NO_GUESS=1 only completes refs' '
	GIT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "git checkout " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - --guess overrides GIT_COMPLETION_NO_GUESS=1, complete refs and unique remote branches for DWIM' '
	GIT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "git checkout --guess " <<-\EOF
	HEAD Z
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with --no-guess, only completes refs' '
	test_completion "git checkout --no-guess " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - a later --guess overrides previous --no-guess, complete refs and unique remote branches for DWIM' '
	test_completion "git checkout --no-guess --guess " <<-\EOF
	HEAD Z
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - a later --no-guess overrides previous --guess, complete only refs' '
	test_completion "git checkout --guess --no-guess " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with checkout.guess = false, only completes refs' '
	test_config checkout.guess false &&
	test_completion "git checkout " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with checkout.guess = true, completes refs and unique remote branches for DWIM' '
	test_config checkout.guess true &&
	test_completion "git checkout " <<-\EOF
	HEAD Z
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - a later --guess overrides previous checkout.guess = false, complete refs and unique remote branches for DWIM' '
	test_config checkout.guess false &&
	test_completion "git checkout --guess " <<-\EOF
	HEAD Z
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - a later --no-guess overrides previous checkout.guess = true, complete only refs' '
	test_config checkout.guess true &&
	test_completion "git checkout --no-guess " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with --detach, complete all references' '
	test_completion "git switch --detach " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with --detach, complete only references' '
	test_completion "git checkout --detach " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -d, complete all references' '
	test_completion "git switch -d " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -d, complete only references' '
	test_completion "git checkout -d " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with --track, complete only remote branches' '
	test_completion "git switch --track " <<-\EOF
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with --track, complete only remote branches' '
	test_completion "git checkout --track " <<-\EOF
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with --no-track, complete only local branch names' '
	test_completion "git switch --no-track " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - with --no-track, complete only local references' '
	test_completion "git checkout --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -c, complete all references' '
	test_completion "git switch -c new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -C, complete all references' '
	test_completion "git switch -C new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -c and --track, complete all references' '
	test_completion "git switch -c new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -C and --track, complete all references' '
	test_completion "git switch -C new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -c and --no-track, complete all references' '
	test_completion "git switch -c new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - with -C and --no-track, complete all references' '
	test_completion "git switch -C new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -b, complete all references' '
	test_completion "git checkout -b new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -B, complete all references' '
	test_completion "git checkout -B new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -b and --track, complete all references' '
	test_completion "git checkout -b new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -B and --track, complete all references' '
	test_completion "git checkout -B new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -b and --no-track, complete all references' '
	test_completion "git checkout -b new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git checkout - with -B and --no-track, complete all references' '
	test_completion "git checkout -B new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'git switch - for -c, complete local branches and unique remote branches' '
	test_completion "git switch -c " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - for -C, complete local branches and unique remote branches' '
	test_completion "git switch -C " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - for -c with --no-guess, complete local branches only' '
	test_completion "git switch --no-guess -c " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - for -C with --no-guess, complete local branches only' '
	test_completion "git switch --no-guess -C " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - for -c with --no-track, complete local branches only' '
	test_completion "git switch --no-track -c " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - for -C with --no-track, complete local branches only' '
	test_completion "git switch --no-track -C " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - for -b, complete local branches and unique remote branches' '
	test_completion "git checkout -b " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - for -B, complete local branches and unique remote branches' '
	test_completion "git checkout -B " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - for -b with --no-guess, complete local branches only' '
	test_completion "git checkout --no-guess -b " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - for -B with --no-guess, complete local branches only' '
	test_completion "git checkout --no-guess -B " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - for -b with --no-track, complete local branches only' '
	test_completion "git checkout --no-track -b " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - for -B with --no-track, complete local branches only' '
	test_completion "git checkout --no-track -B " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - with --orphan completes local branch names and unique remote branch names' '
	test_completion "git switch --orphan " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git switch - --orphan with branch already provided completes nothing else' '
	test_completion "git switch --orphan main " <<-\EOF

	EOF
'

test_expect_success 'git checkout - with --orphan completes local branch names and unique remote branch names' '
	test_completion "git checkout --orphan " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'git checkout - --orphan with branch already provided completes local refs for a start-point' '
	test_completion "git checkout --orphan main " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'teardown after ref completion' '
	git branch -d matching-branch &&
	git tag -d matching-tag &&
	git remote remove other
'


test_path_completion ()
{
	test $# = 2 || BUG "not 2 parameters to test_path_completion"

	local cur="$1" expected="$2"
	echo "$expected" >expected &&
	(
		# In the following tests calling this function we only
		# care about how __git_complete_index_file() deals with
		# unusual characters in path names.  By requesting only
		# untracked files we do not have to bother adding any
		# paths to the index in those tests.
		__git_complete_index_file --others &&
		print_comp
	) &&
	test_cmp expected out
}

test_expect_success 'setup for path completion tests' '
	mkdir simple-dir \
	      "spaces in dir" \
	      árvíztűrő &&
	touch simple-dir/simple-file \
	      "spaces in dir/spaces in file" \
	      "árvíztűrő/Сайн яваарай" &&
	if test_have_prereq !MINGW &&
	   mkdir BS\\dir \
		 '$'separators\034in\035dir'' &&
	   touch BS\\dir/DQ\"file \
		 '$'separators\034in\035dir/sep\036in\037file''
	then
		test_set_prereq FUNNIERNAMES
	else
		rm -rf BS\\dir '$'separators\034in\035dir''
	fi
'

test_expect_success '__git_complete_index_file - simple' '
	test_path_completion simple simple-dir &&  # Bash is supposed to
						   # add the trailing /.
	test_path_completion simple-dir/simple simple-dir/simple-file
'

test_expect_success \
    '__git_complete_index_file - escaped characters on cmdline' '
	test_path_completion spac "spaces in dir" &&  # Bash will turn this
						      # into "spaces\ in\ dir"
	test_path_completion "spaces\\ i" \
			     "spaces in dir" &&
	test_path_completion "spaces\\ in\\ dir/s" \
			     "spaces in dir/spaces in file" &&
	test_path_completion "spaces\\ in\\ dir/spaces\\ i" \
			     "spaces in dir/spaces in file"
'

test_expect_success \
    '__git_complete_index_file - quoted characters on cmdline' '
	# Testing with an opening but without a corresponding closing
	# double quote is important.
	test_path_completion \"spac "spaces in dir" &&
	test_path_completion "\"spaces i" \
			     "spaces in dir" &&
	test_path_completion "\"spaces in dir/s" \
			     "spaces in dir/spaces in file" &&
	test_path_completion "\"spaces in dir/spaces i" \
			     "spaces in dir/spaces in file"
'

test_expect_success '__git_complete_index_file - UTF-8 in ls-files output' '
	test_path_completion á árvíztűrő &&
	test_path_completion árvíztűrő/С "árvíztűrő/Сайн яваарай"
'

test_expect_success FUNNIERNAMES \
    '__git_complete_index_file - C-style escapes in ls-files output' '
	test_path_completion BS \
			     BS\\dir &&
	test_path_completion BS\\\\d \
			     BS\\dir &&
	test_path_completion BS\\\\dir/DQ \
			     BS\\dir/DQ\"file &&
	test_path_completion BS\\\\dir/DQ\\\"f \
			     BS\\dir/DQ\"file
'

test_expect_success FUNNIERNAMES \
    '__git_complete_index_file - \nnn-escaped characters in ls-files output' '
	test_path_completion sep '$'separators\034in\035dir'' &&
	test_path_completion '$'separators\034i'' \
			     '$'separators\034in\035dir'' &&
	test_path_completion '$'separators\034in\035dir/sep'' \
			     '$'separators\034in\035dir/sep\036in\037file'' &&
	test_path_completion '$'separators\034in\035dir/sep\036i'' \
			     '$'separators\034in\035dir/sep\036in\037file''
'

test_expect_success FUNNYNAMES \
    '__git_complete_index_file - removing repeated quoted path components' '
	test_when_finished rm -r repeated-quoted &&
	mkdir repeated-quoted &&      # A directory whose name in itself
				      # would not be quoted ...
	>repeated-quoted/0-file &&
	>repeated-quoted/1\"file &&   # ... but here the file makes the
				      # dirname quoted ...
	>repeated-quoted/2-file &&
	>repeated-quoted/3\"file &&   # ... and here, too.

	# Still, we shold only list the directory name only once.
	test_path_completion repeated repeated-quoted
'

test_expect_success 'teardown after path completion tests' '
	rm -rf simple-dir "spaces in dir" árvíztűrő \
	       BS\\dir '$'separators\034in\035dir''
'

test_expect_success '__git_find_on_cmdline - single match' '
	echo list >expect &&
	(
		words=(git command --opt list) &&
		cword=${#words[@]} &&
		__git_cmd_idx=1 &&
		__git_find_on_cmdline "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__git_find_on_cmdline - multiple matches' '
	echo remove >expect &&
	(
		words=(git command -o --opt remove list add) &&
		cword=${#words[@]} &&
		__git_cmd_idx=1 &&
		__git_find_on_cmdline "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__git_find_on_cmdline - no match' '
	(
		words=(git command --opt branch) &&
		cword=${#words[@]} &&
		__git_cmd_idx=1 &&
		__git_find_on_cmdline "add list remove" >actual
	) &&
	test_must_be_empty actual
'

test_expect_success '__git_find_on_cmdline - single match with index' '
	echo "3 list" >expect &&
	(
		words=(git command --opt list) &&
		cword=${#words[@]} &&
		__git_cmd_idx=1 &&
		__git_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__git_find_on_cmdline - multiple matches with index' '
	echo "4 remove" >expect &&
	(
		words=(git command -o --opt remove list add) &&
		cword=${#words[@]} &&
		__git_cmd_idx=1 &&
		__git_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__git_find_on_cmdline - no match with index' '
	(
		words=(git command --opt branch) &&
		cword=${#words[@]} &&
		__git_cmd_idx=1 &&
		__git_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_must_be_empty actual
'

test_expect_success '__git_find_on_cmdline - ignores matches before command with index' '
	echo "6 remove" >expect &&
	(
		words=(git -C remove command -o --opt remove list add) &&
		cword=${#words[@]} &&
		__git_cmd_idx=3 &&
		__git_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__git_get_config_variables' '
	cat >expect <<-EOF &&
	name-1
	name-2
	EOF
	test_config interesting.name-1 good &&
	test_config interesting.name-2 good &&
	test_config subsection.interesting.name-3 bad &&
	__git_get_config_variables interesting >actual &&
	test_cmp expect actual
'

test_expect_success '__git_pretty_aliases' '
	cat >expect <<-EOF &&
	author
	hash
	EOF
	test_config pretty.author "%an %ae" &&
	test_config pretty.hash %H &&
	__git_pretty_aliases >actual &&
	test_cmp expect actual
'

test_expect_success 'basic' '
	run_completion "git " &&
	# built-in
	grep -q "^add \$" out &&
	# script
	grep -q "^rebase \$" out &&
	# plumbing
	! grep -q "^ls-files \$" out &&

	run_completion "git r" &&
	! grep -q -v "^r" out
'

test_expect_success 'double dash "git" itself' '
	test_completion "git --" <<-\EOF
	--paginate Z
	--no-pager Z
	--git-dir=
	--bare Z
	--version Z
	--exec-path Z
	--exec-path=
	--html-path Z
	--man-path Z
	--info-path Z
	--work-tree=
	--namespace=
	--no-replace-objects Z
	--help Z
	EOF
'

test_expect_success 'double dash "git checkout"' '
	test_completion "git checkout --" <<-\EOF
	--quiet Z
	--detach Z
	--track Z
	--orphan=Z
	--ours Z
	--theirs Z
	--merge Z
	--conflict=Z
	--patch Z
	--ignore-skip-worktree-bits Z
	--ignore-other-worktrees Z
	--recurse-submodules Z
	--progress Z
	--guess Z
	--no-guess Z
	--no-... Z
	--overlay Z
	--pathspec-file-nul Z
	--pathspec-from-file=Z
	EOF
'

test_expect_success 'general options' '
	test_completion "git --ver" "--version " &&
	test_completion "git --hel" "--help " &&
	test_completion "git --exe" <<-\EOF &&
	--exec-path Z
	--exec-path=
	EOF
	test_completion "git --htm" "--html-path " &&
	test_completion "git --pag" "--paginate " &&
	test_completion "git --no-p" "--no-pager " &&
	test_completion "git --git" "--git-dir=" &&
	test_completion "git --wor" "--work-tree=" &&
	test_completion "git --nam" "--namespace=" &&
	test_completion "git --bar" "--bare " &&
	test_completion "git --inf" "--info-path " &&
	test_completion "git --no-r" "--no-replace-objects "
'

test_expect_success 'general options plus command' '
	test_completion "git --version check" "checkout " &&
	test_completion "git --paginate check" "checkout " &&
	test_completion "git --git-dir=foo check" "checkout " &&
	test_completion "git --bare check" "checkout " &&
	test_completion "git --exec-path=foo check" "checkout " &&
	test_completion "git --html-path check" "checkout " &&
	test_completion "git --no-pager check" "checkout " &&
	test_completion "git --work-tree=foo check" "checkout " &&
	test_completion "git --namespace=foo check" "checkout " &&
	test_completion "git --paginate check" "checkout " &&
	test_completion "git --info-path check" "checkout " &&
	test_completion "git --no-replace-objects check" "checkout " &&
	test_completion "git --git-dir some/path check" "checkout " &&
	test_completion "git -c conf.var=value check" "checkout " &&
	test_completion "git -C some/path check" "checkout " &&
	test_completion "git --work-tree some/path check" "checkout " &&
	test_completion "git --namespace name/space check" "checkout "
'

test_expect_success 'git --help completion' '
	test_completion "git --help ad" "add " &&
	test_completion "git --help core" "core-tutorial "
'

test_expect_success 'completion.commands removes multiple commands' '
	test_config completion.commands "-cherry -mergetool" &&
	git --list-cmds=list-mainporcelain,list-complete,config >out &&
	! grep -E "^(cherry|mergetool)$" out
'

test_expect_success 'setup for integration tests' '
	echo content >file1 &&
	echo more >file2 &&
	git add file1 file2 &&
	git commit -m one &&
	git branch mybranch &&
	git tag mytag
'

test_expect_success 'checkout completes ref names' '
	test_completion "git checkout m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'git -C <path> checkout uses the right repo' '
	test_completion "git -C subdir -C subsubdir -C .. -C ../otherrepo checkout b" <<-\EOF
	branch-in-other Z
	EOF
'

test_expect_success 'show completes all refs' '
	test_completion "git show m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success '<ref>: completes paths' '
	test_completion "git show mytag:f" <<-\EOF
	file1Z
	file2Z
	EOF
'

test_expect_success 'complete tree filename with spaces' '
	echo content >"name with spaces" &&
	git add "name with spaces" &&
	git commit -m spaces &&
	test_completion "git show HEAD:nam" <<-\EOF
	name with spacesZ
	EOF
'

test_expect_success 'complete tree filename with metacharacters' '
	echo content >"name with \${meta}" &&
	git add "name with \${meta}" &&
	git commit -m meta &&
	test_completion "git show HEAD:nam" <<-\EOF
	name with ${meta}Z
	name with spacesZ
	EOF
'

test_expect_success PERL 'send-email' '
	test_completion "git send-email --cov" <<-\EOF &&
	--cover-from-description=Z
	--cover-letter Z
	EOF
	test_completion "git send-email ma" "main "
'

test_expect_success 'complete files' '
	git init tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	echo "expected" > .gitignore &&
	echo "out" >> .gitignore &&
	echo "out_sorted" >> .gitignore &&

	git add .gitignore &&
	test_completion "git commit " ".gitignore" &&

	git commit -m ignore &&

	touch new &&
	test_completion "git add " "new" &&

	git add new &&
	git commit -a -m new &&
	test_completion "git add " "" &&

	git mv new modified &&
	echo modify > modified &&
	test_completion "git add " "modified" &&

	mkdir -p some/deep &&
	touch some/deep/path &&
	test_completion "git add some/" "some/deep" &&
	git clean -f some &&

	touch untracked &&

	: TODO .gitignore should not be here &&
	test_completion "git rm " <<-\EOF &&
	.gitignore
	modified
	EOF

	test_completion "git clean " "untracked" &&

	: TODO .gitignore should not be here &&
	test_completion "git mv " <<-\EOF &&
	.gitignore
	modified
	EOF

	mkdir dir &&
	touch dir/file-in-dir &&
	git add dir/file-in-dir &&
	git commit -m dir &&

	mkdir untracked-dir &&

	: TODO .gitignore should not be here &&
	test_completion "git mv modified " <<-\EOF &&
	.gitignore
	dir
	modified
	untracked
	untracked-dir
	EOF

	test_completion "git commit " "modified" &&

	: TODO .gitignore should not be here &&
	test_completion "git ls-files " <<-\EOF &&
	.gitignore
	dir
	modified
	EOF

	touch momified &&
	test_completion "git add mom" "momified"
'

test_expect_success "simple alias" '
	test_config alias.co checkout &&
	test_completion "git co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success "recursive alias" '
	test_config alias.co checkout &&
	test_config alias.cod "co --detached" &&
	test_completion "git cod m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success "completion uses <cmd> completion for alias: !sh -c 'git <cmd> ...'" '
	test_config alias.co "!sh -c '"'"'git checkout ...'"'"'" &&
	test_completion "git co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'completion uses <cmd> completion for alias: !f () { VAR=val git <cmd> ... }' '
	test_config alias.co "!f () { VAR=val git checkout ... ; } f" &&
	test_completion "git co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'completion used <cmd> completion for alias: !f() { : git <cmd> ; ... }' '
	test_config alias.co "!f() { : git checkout ; if ... } f" &&
	test_completion "git co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'completion without explicit _git_xxx function' '
	test_completion "git version --" <<-\EOF
	--build-options Z
	--no-build-options Z
	EOF
'

test_expect_failure 'complete with tilde expansion' '
	git init tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	touch ~/tmp/file &&

	test_completion "git add ~/tmp/" "~/tmp/file"
'

test_expect_success 'setup other remote for remote reference completion' '
	git remote add other otherrepo &&
	git fetch other
'

for flag in -d --delete
do
	test_expect_success "__git_complete_remote_or_refspec - push $flag other" '
		sed -e "s/Z$//" >expected <<-EOF &&
		main-in-other Z
		EOF
		(
			words=(git push '$flag' other ma) &&
			cword=${#words[@]} cur=${words[cword-1]} &&
			__git_cmd_idx=1 &&
			__git_complete_remote_or_refspec &&
			print_comp
		) &&
		test_cmp expected out
	'

	test_expect_failure "__git_complete_remote_or_refspec - push other $flag" '
		sed -e "s/Z$//" >expected <<-EOF &&
		main-in-other Z
		EOF
		(
			words=(git push other '$flag' ma) &&
			cword=${#words[@]} cur=${words[cword-1]} &&
			__git_cmd_idx=1 &&
			__git_complete_remote_or_refspec &&
			print_comp
		) &&
		test_cmp expected out
	'
done

test_expect_success 'git config - section' '
	test_completion "git config br" <<-\EOF
	branch.Z
	browser.Z
	EOF
'

test_expect_success 'git config - variable name' '
	test_completion "git config log.d" <<-\EOF
	log.date Z
	log.decorate Z
	log.diffMerges Z
	EOF
'

test_expect_success 'git config - value' '
	test_completion "git config color.pager " <<-\EOF
	false Z
	true Z
	EOF
'

test_expect_success 'git -c - section' '
	test_completion "git -c br" <<-\EOF
	branch.Z
	browser.Z
	EOF
'

test_expect_success 'git -c - variable name' '
	test_completion "git -c log.d" <<-\EOF
	log.date=Z
	log.decorate=Z
	log.diffMerges=Z
	EOF
'

test_expect_success 'git -c - value' '
	test_completion "git -c color.pager=" <<-\EOF
	false Z
	true Z
	EOF
'

test_expect_success 'git clone --config= - section' '
	test_completion "git clone --config=br" <<-\EOF
	branch.Z
	browser.Z
	EOF
'

test_expect_success 'git clone --config= - variable name' '
	test_completion "git clone --config=log.d" <<-\EOF
	log.date=Z
	log.decorate=Z
	log.diffMerges=Z
	EOF
'

test_expect_success 'git clone --config= - value' '
	test_completion "git clone --config=color.pager=" <<-\EOF
	false Z
	true Z
	EOF
'

test_expect_success 'options with value' '
	test_completion "git merge -X diff-algorithm=" <<-\EOF

	EOF
'

test_expect_success 'sourcing the completion script clears cached commands' '
	__git_compute_all_commands &&
	verbose test -n "$__git_all_commands" &&
	. "$GIT_BUILD_DIR/contrib/completion/git-completion.bash" &&
	verbose test -z "$__git_all_commands"
'

test_expect_success 'sourcing the completion script clears cached merge strategies' '
	__git_compute_merge_strategies &&
	verbose test -n "$__git_merge_strategies" &&
	. "$GIT_BUILD_DIR/contrib/completion/git-completion.bash" &&
	verbose test -z "$__git_merge_strategies"
'

test_expect_success 'sourcing the completion script clears cached --options' '
	__gitcomp_builtin checkout &&
	verbose test -n "$__gitcomp_builtin_checkout" &&
	__gitcomp_builtin notes_edit &&
	verbose test -n "$__gitcomp_builtin_notes_edit" &&
	. "$GIT_BUILD_DIR/contrib/completion/git-completion.bash" &&
	verbose test -z "$__gitcomp_builtin_checkout" &&
	verbose test -z "$__gitcomp_builtin_notes_edit"
'

test_expect_success 'option aliases are not shown by default' '
	test_completion "git clone --recurs" "--recurse-submodules "
'

test_expect_success 'option aliases are shown with GIT_COMPLETION_SHOW_ALL' '
	. "$GIT_BUILD_DIR/contrib/completion/git-completion.bash" &&
	GIT_COMPLETION_SHOW_ALL=1 && export GIT_COMPLETION_SHOW_ALL &&
	test_completion "git clone --recurs" <<-\EOF
	--recurse-submodules Z
	--recursive Z
	EOF
'

test_expect_success '__git_complete' '
	unset -f __git_wrap__git_main &&

	__git_complete foo __git_main &&
	__git_have_func __git_wrap__git_main &&
	unset -f __git_wrap__git_main &&

	__git_complete gf _git_fetch &&
	__git_have_func __git_wrap_git_fetch &&

	__git_complete foo git &&
	__git_have_func __git_wrap__git_main &&
	unset -f __git_wrap__git_main &&

	__git_complete gd git_diff &&
	__git_have_func __git_wrap_git_diff &&

	test_must_fail __git_complete ga missing
'

test_done
