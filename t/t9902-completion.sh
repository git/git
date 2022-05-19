#!/bin/sh
#
# Copyright (c) 2012-2020 Felipe Contreras
#

test_description='test bash completion'

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
#     with "but-check" that are not part of the subcommands this build
#     will ship, e.g.  "check-ignore".  The tests for completion for
#     subcommand names tests how "check" is expanded; we limit the
#     possible candidates to "checkout" and "check-attr" to make sure
#     "check-attr", which is known by the filter function as a
#     subcommand to be thrown out, while excluding other random files
#     that happen to begin with "check" to avoid letting them get in
#     the way.
#
# (2) A test makes sure that common subcommands are included in the
#     completion for "but <TAB>", and a plumbing is excluded.  "add",
#     "rebase" and "ls-files" are listed for this.

BUT_TESTING_ALL_COMMAND_LIST='add checkout check-attr rebase ls-files'
BUT_TESTING_PORCELAIN_COMMAND_LIST='add checkout rebase'

. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash"

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
	__but_wrap__but_main && print_comp
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

# Test __butcomp.
# The first argument is the typed text so far (cur); the rest are
# passed to __butcomp.  Expected output comes is read from the
# standard input, like test_completion().
test_butcomp ()
{
	local -a COMPREPLY &&
	sed -e 's/Z$//' >expected &&
	local cur="$1" &&
	shift &&
	__butcomp "$@" &&
	print_comp &&
	test_cmp expected out
}

# Test __butcomp_nl
# Arguments are:
# 1: current word (cur)
# -: the rest are passed to __butcomp_nl
test_butcomp_nl ()
{
	local -a COMPREPLY &&
	sed -e 's/Z$//' >expected &&
	local cur="$1" &&
	shift &&
	__butcomp_nl "$@" &&
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

test_expect_success 'setup for __but_find_repo_path/__butdir tests' '
	mkdir -p subdir/subsubdir &&
	mkdir -p non-repo &&
	but init -b main otherrepo
'

test_expect_success '__but_find_repo_path - from command line (through $__but_dir)' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		__but_dir="$ROOT/otherrepo/.but" &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - .but directory in cwd' '
	echo ".but" >expected &&
	(
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - .but directory in parent' '
	echo "$ROOT/.but" >expected &&
	(
		cd subdir/subsubdir &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - cwd is a .but directory' '
	echo "." >expected &&
	(
		cd .but &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - parent is a .but directory' '
	echo "$ROOT/.but" >expected &&
	(
		cd .but/objects &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - $BUT_DIR set while .but directory in cwd' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		BUT_DIR="$ROOT/otherrepo/.but" &&
		export BUT_DIR &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - $BUT_DIR set while .but directory in parent' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		BUT_DIR="$ROOT/otherrepo/.but" &&
		export BUT_DIR &&
		cd subdir &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - from command line while "but -C"' '
	echo "$ROOT/.but" >expected &&
	(
		__but_dir="$ROOT/.but" &&
		__but_C_args=(-C otherrepo) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - relative dir from command line and "but -C"' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		cd subdir &&
		__but_dir="otherrepo/.but" &&
		__but_C_args=(-C ..) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - $BUT_DIR set while "but -C"' '
	echo "$ROOT/.but" >expected &&
	(
		BUT_DIR="$ROOT/.but" &&
		export BUT_DIR &&
		__but_C_args=(-C otherrepo) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - relative dir in $BUT_DIR and "but -C"' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		cd subdir &&
		BUT_DIR="otherrepo/.but" &&
		export BUT_DIR &&
		__but_C_args=(-C ..) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - "but -C" while .but directory in cwd' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		__but_C_args=(-C otherrepo) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - "but -C" while cwd is a .but directory' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		cd .but &&
		__but_C_args=(-C .. -C otherrepo) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - "but -C" while .but directory in parent' '
	echo "$ROOT/otherrepo/.but" >expected &&
	(
		cd subdir &&
		__but_C_args=(-C .. -C otherrepo) &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - non-existing path in "but -C"' '
	(
		__but_C_args=(-C non-existing) &&
		test_must_fail __but_find_repo_path &&
		printf "$__but_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_find_repo_path - non-existing path in $__but_dir' '
	(
		__but_dir="non-existing" &&
		test_must_fail __but_find_repo_path &&
		printf "$__but_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_find_repo_path - non-existing $BUT_DIR' '
	(
		BUT_DIR="$ROOT/non-existing" &&
		export BUT_DIR &&
		test_must_fail __but_find_repo_path &&
		printf "$__but_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_find_repo_path - butfile in cwd' '
	echo "$ROOT/otherrepo/.but" >expected &&
	echo "butdir: $ROOT/otherrepo/.but" >subdir/.but &&
	test_when_finished "rm -f subdir/.but" &&
	(
		cd subdir &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - butfile in parent' '
	echo "$ROOT/otherrepo/.but" >expected &&
	echo "butdir: $ROOT/otherrepo/.but" >subdir/.but &&
	test_when_finished "rm -f subdir/.but" &&
	(
		cd subdir/subsubdir &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success SYMLINKS '__but_find_repo_path - resulting path avoids symlinks' '
	echo "$ROOT/otherrepo/.but" >expected &&
	mkdir otherrepo/dir &&
	test_when_finished "rm -rf otherrepo/dir" &&
	ln -s otherrepo/dir link &&
	test_when_finished "rm -f link" &&
	(
		cd link &&
		__but_find_repo_path &&
		echo "$__but_repo_path" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_find_repo_path - not a but repository' '
	(
		cd non-repo &&
		BUT_CEILING_DIRECTORIES="$ROOT" &&
		export BUT_CEILING_DIRECTORIES &&
		test_must_fail __but_find_repo_path &&
		printf "$__but_repo_path" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__butdir - finds repo' '
	echo "$ROOT/.but" >expected &&
	(
		cd subdir/subsubdir &&
		__butdir >"$actual"
	) &&
	test_cmp expected "$actual"
'


test_expect_success '__butdir - returns error when cannot find repo' '
	(
		__but_dir="non-existing" &&
		test_must_fail __butdir >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__butdir - repo as argument' '
	echo "otherrepo/.but" >expected &&
	(
		__butdir "otherrepo" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__butdir - remote as argument' '
	echo "remote" >expected &&
	(
		__butdir "remote" >"$actual"
	) &&
	test_cmp expected "$actual"
'


test_expect_success '__but_dequote - plain unquoted word' '
	__but_dequote unquoted-word &&
	verbose test unquoted-word = "$dequoted_word"
'

# input:    b\a\c\k\'\\\"s\l\a\s\h\es
# expected: back'\"slashes
test_expect_success '__but_dequote - backslash escaped' '
	__but_dequote "b\a\c\k\\'\''\\\\\\\"s\l\a\s\h\es" &&
	verbose test "back'\''\\\"slashes" = "$dequoted_word"
'

# input:    sin'gle\' '"quo'ted
# expected: single\ "quoted
test_expect_success '__but_dequote - single quoted' '
	__but_dequote "'"sin'gle\\\\' '\\\"quo'ted"'" &&
	verbose test '\''single\ "quoted'\'' = "$dequoted_word"
'

# input:    dou"ble\\" "\"\quot"ed
# expected: double\ "\quoted
test_expect_success '__but_dequote - double quoted' '
	__but_dequote '\''dou"ble\\" "\"\quot"ed'\'' &&
	verbose test '\''double\ "\quoted'\'' = "$dequoted_word"
'

# input: 'open single quote
test_expect_success '__but_dequote - open single quote' '
	__but_dequote "'\''open single quote" &&
	verbose test "open single quote" = "$dequoted_word"
'

# input: "open double quote
test_expect_success '__but_dequote - open double quote' '
	__but_dequote "\"open double quote" &&
	verbose test "open double quote" = "$dequoted_word"
'


test_expect_success '__butcomp_direct - puts everything into COMPREPLY as-is' '
	sed -e "s/Z$//g" >expected <<-EOF &&
	with-trailing-space Z
	without-trailing-spaceZ
	--option Z
	--option=Z
	$invalid_variable_name Z
	EOF
	(
		cur=should_be_ignored &&
		__butcomp_direct "$(cat expected)" &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__butcomp - trailing space - options' '
	test_butcomp "--re" "--dry-run --reuse-message= --reedit-message=
		--reset-author" <<-EOF
	--reuse-message=Z
	--reedit-message=Z
	--reset-author Z
	EOF
'

test_expect_success '__butcomp - trailing space - config keys' '
	test_butcomp "br" "branch. branch.autosetupmerge
		branch.autosetuprebase browser." <<-\EOF
	branch.Z
	branch.autosetupmerge Z
	branch.autosetuprebase Z
	browser.Z
	EOF
'

test_expect_success '__butcomp - option parameter' '
	test_butcomp "--strategy=re" "octopus ours recursive resolve subtree" \
		"" "re" <<-\EOF
	recursive Z
	resolve Z
	EOF
'

test_expect_success '__butcomp - prefix' '
	test_butcomp "branch.me" "remote merge mergeoptions rebase" \
		"branch.maint." "me" <<-\EOF
	branch.maint.merge Z
	branch.maint.mergeoptions Z
	EOF
'

test_expect_success '__butcomp - suffix' '
	test_butcomp "branch.me" "master maint next seen" "branch." \
		"ma" "." <<-\EOF
	branch.master.Z
	branch.maint.Z
	EOF
'

test_expect_success '__butcomp - ignore optional negative options' '
	test_butcomp "--" "--abc --def --no-one -- --no-two" <<-\EOF
	--abc Z
	--def Z
	--no-one Z
	--no-... Z
	EOF
'

test_expect_success '__butcomp - ignore/narrow optional negative options' '
	test_butcomp "--a" "--abc --abcdef --no-one -- --no-two" <<-\EOF
	--abc Z
	--abcdef Z
	EOF
'

test_expect_success '__butcomp - ignore/narrow optional negative options' '
	test_butcomp "--n" "--abc --def --no-one -- --no-two" <<-\EOF
	--no-one Z
	--no-... Z
	EOF
'

test_expect_success '__butcomp - expand all negative options' '
	test_butcomp "--no-" "--abc --def --no-one -- --no-two" <<-\EOF
	--no-one Z
	--no-two Z
	EOF
'

test_expect_success '__butcomp - expand/narrow all negative options' '
	test_butcomp "--no-o" "--abc --def --no-one -- --no-two" <<-\EOF
	--no-one Z
	EOF
'

test_expect_success '__butcomp - equal skip' '
	test_butcomp "--option=" "--option=" <<-\EOF &&

	EOF
	test_butcomp "option=" "option=" <<-\EOF

	EOF
'

test_expect_success '__butcomp - doesnt fail because of invalid variable name' '
	__butcomp "$invalid_variable_name"
'

read -r -d "" refs <<-\EOF
main
maint
next
seen
EOF

test_expect_success '__butcomp_nl - trailing space' '
	test_butcomp_nl "m" "$refs" <<-EOF
	main Z
	maint Z
	EOF
'

test_expect_success '__butcomp_nl - prefix' '
	test_butcomp_nl "--fixup=m" "$refs" "--fixup=" "m" <<-EOF
	--fixup=main Z
	--fixup=maint Z
	EOF
'

test_expect_success '__butcomp_nl - suffix' '
	test_butcomp_nl "branch.ma" "$refs" "branch." "ma" "." <<-\EOF
	branch.main.Z
	branch.maint.Z
	EOF
'

test_expect_success '__butcomp_nl - no suffix' '
	test_butcomp_nl "ma" "$refs" "" "ma" "" <<-\EOF
	mainZ
	maintZ
	EOF
'

test_expect_success '__butcomp_nl - doesnt fail because of invalid variable name' '
	__butcomp_nl "$invalid_variable_name"
'

test_expect_success '__but_remotes - list remotes from $BUT_DIR/remotes and from config file' '
	cat >expect <<-EOF &&
	remote_from_file_1
	remote_from_file_2
	remote_in_config_1
	remote_in_config_2
	EOF
	test_when_finished "rm -rf .but/remotes" &&
	mkdir -p .but/remotes &&
	>.but/remotes/remote_from_file_1 &&
	>.but/remotes/remote_from_file_2 &&
	test_when_finished "but remote remove remote_in_config_1" &&
	but remote add remote_in_config_1 but://remote_1 &&
	test_when_finished "but remote remove remote_in_config_2" &&
	but remote add remote_in_config_2 but://remote_2 &&
	(
		__but_remotes >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__but_is_configured_remote' '
	test_when_finished "but remote remove remote_1" &&
	but remote add remote_1 but://remote_1 &&
	test_when_finished "but remote remove remote_2" &&
	but remote add remote_2 but://remote_2 &&
	(
		verbose __but_is_configured_remote remote_2 &&
		test_must_fail __but_is_configured_remote non-existent
	)
'

test_expect_success 'setup for ref completion' '
	but cummit --allow-empty -m initial &&
	but branch -M main &&
	but branch matching-branch &&
	but tag matching-tag &&
	(
		cd otherrepo &&
		but cummit --allow-empty -m initial &&
		but branch -m main main-in-other &&
		but branch branch-in-other &&
		but tag tag-in-other
	) &&
	but remote add other "$ROOT/otherrepo/.but" &&
	but fetch --no-tags other &&
	rm -f .but/FETCH_HEAD &&
	but init thirdrepo
'

test_expect_success '__but_refs - simple' '
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
		__but_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - full refs' '
	cat >expected <<-EOF &&
	refs/heads/main
	refs/heads/matching-branch
	refs/remotes/other/branch-in-other
	refs/remotes/other/main-in-other
	refs/tags/matching-tag
	EOF
	(
		cur=refs/heads/ &&
		__but_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - repo given on the command line' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	tag-in-other
	EOF
	(
		__but_dir="$ROOT/otherrepo/.but" &&
		cur= &&
		__but_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - remote on local file system' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	tag-in-other
	EOF
	(
		cur= &&
		__but_refs otherrepo >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - remote on local file system - full refs' '
	cat >expected <<-EOF &&
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cur=refs/ &&
		__but_refs otherrepo >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - configured remote' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	EOF
	(
		cur= &&
		__but_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - configured remote - full refs' '
	cat >expected <<-EOF &&
	HEAD
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cur=refs/ &&
		__but_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - configured remote - repo given on the command line' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	EOF
	(
		cd thirdrepo &&
		__but_dir="$ROOT/.but" &&
		cur= &&
		__but_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - configured remote - full refs - repo given on the command line' '
	cat >expected <<-EOF &&
	HEAD
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cd thirdrepo &&
		__but_dir="$ROOT/.but" &&
		cur=refs/ &&
		__but_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - configured remote - remote name matches a directory' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	EOF
	mkdir other &&
	test_when_finished "rm -rf other" &&
	(
		cur= &&
		__but_refs other >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - URL remote' '
	cat >expected <<-EOF &&
	HEAD
	branch-in-other
	main-in-other
	tag-in-other
	EOF
	(
		cur= &&
		__but_refs "file://$ROOT/otherrepo/.but" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - URL remote - full refs' '
	cat >expected <<-EOF &&
	HEAD
	refs/heads/branch-in-other
	refs/heads/main-in-other
	refs/tags/tag-in-other
	EOF
	(
		cur=refs/ &&
		__but_refs "file://$ROOT/otherrepo/.but" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - non-existing remote' '
	(
		cur= &&
		__but_refs non-existing >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_refs - non-existing remote - full refs' '
	(
		cur=refs/ &&
		__but_refs non-existing >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_refs - non-existing URL remote' '
	(
		cur= &&
		__but_refs "file://$ROOT/non-existing" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_refs - non-existing URL remote - full refs' '
	(
		cur=refs/ &&
		__but_refs "file://$ROOT/non-existing" >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_refs - not in a but repository' '
	(
		BUT_CEILING_DIRECTORIES="$ROOT" &&
		export BUT_CEILING_DIRECTORIES &&
		cd subdir &&
		cur= &&
		__but_refs >"$actual"
	) &&
	test_must_be_empty "$actual"
'

test_expect_success '__but_refs - unique remote branches for but checkout DWIMery' '
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
		but update-ref $remote_ref main &&
		test_when_finished "but update-ref -d $remote_ref" || return 1
	done &&
	(
		cur= &&
		__but_refs "" 1 >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - after --opt=' '
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
		__but_refs "" "" "" "" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - after --opt= - full refs' '
	cat >expected <<-EOF &&
	refs/heads/main
	refs/heads/matching-branch
	refs/remotes/other/branch-in-other
	refs/remotes/other/main-in-other
	refs/tags/matching-tag
	EOF
	(
		cur="--opt=refs/" &&
		__but_refs "" "" "" refs/ >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but refs - excluding refs' '
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
		__but_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but refs - excluding full refs' '
	cat >expected <<-EOF &&
	^refs/heads/main
	^refs/heads/matching-branch
	^refs/remotes/other/branch-in-other
	^refs/remotes/other/main-in-other
	^refs/tags/matching-tag
	EOF
	(
		cur=^refs/ &&
		__but_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'setup for filtering matching refs' '
	but branch matching/branch &&
	but tag matching/tag &&
	but -C otherrepo branch matching/branch-in-other &&
	but fetch --no-tags other &&
	rm -f .but/FETCH_HEAD
'

test_expect_success '__but_refs - do not filter refs unless told so' '
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
		__but_refs >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - only matching refs' '
	cat >expected <<-EOF &&
	matching-branch
	matching/branch
	matching-tag
	matching/tag
	EOF
	(
		cur=mat &&
		__but_refs "" "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - only matching refs - full refs' '
	cat >expected <<-EOF &&
	refs/heads/matching-branch
	refs/heads/matching/branch
	EOF
	(
		cur=refs/heads/mat &&
		__but_refs "" "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - only matching refs - remote on local file system' '
	cat >expected <<-EOF &&
	main-in-other
	matching/branch-in-other
	EOF
	(
		cur=ma &&
		__but_refs otherrepo "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - only matching refs - configured remote' '
	cat >expected <<-EOF &&
	main-in-other
	matching/branch-in-other
	EOF
	(
		cur=ma &&
		__but_refs other "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - only matching refs - remote - full refs' '
	cat >expected <<-EOF &&
	refs/heads/main-in-other
	refs/heads/matching/branch-in-other
	EOF
	(
		cur=refs/heads/ma &&
		__but_refs other "" "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_refs - only matching refs - checkout DWIMery' '
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
		but update-ref $remote_ref main &&
		test_when_finished "but update-ref -d $remote_ref" || return 1
	done &&
	(
		cur=mat &&
		__but_refs "" 1 "" "$cur" >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success 'teardown after filtering matching refs' '
	but branch -d matching/branch &&
	but tag -d matching/tag &&
	but update-ref -d refs/remotes/other/matching/branch-in-other &&
	but -C otherrepo branch -D matching/branch-in-other
'

test_expect_success '__but_refs - for-each-ref format specifiers in prefix' '
	cat >expected <<-EOF &&
	evil-%%-%42-%(refname)..main
	EOF
	(
		cur="evil-%%-%42-%(refname)..mai" &&
		__but_refs "" "" "evil-%%-%42-%(refname).." mai >"$actual"
	) &&
	test_cmp expected "$actual"
'

test_expect_success '__but_complete_refs - simple' '
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
		__but_complete_refs &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_refs - matching' '
	sed -e "s/Z$//" >expected <<-EOF &&
	matching-branch Z
	matching-tag Z
	EOF
	(
		cur=mat &&
		__but_complete_refs &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_refs - remote' '
	sed -e "s/Z$//" >expected <<-EOF &&
	HEAD Z
	branch-in-other Z
	main-in-other Z
	EOF
	(
		cur= &&
		__but_complete_refs --remote=other &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_refs - track' '
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
		__but_complete_refs --track &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_refs - current word' '
	sed -e "s/Z$//" >expected <<-EOF &&
	matching-branch Z
	matching-tag Z
	EOF
	(
		cur="--option=mat" &&
		__but_complete_refs --cur="${cur#*=}" &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_refs - prefix' '
	sed -e "s/Z$//" >expected <<-EOF &&
	v1.0..matching-branch Z
	v1.0..matching-tag Z
	EOF
	(
		cur=v1.0..mat &&
		__but_complete_refs --pfx=v1.0.. --cur=mat &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_refs - suffix' '
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
		__but_complete_refs --sfx=. &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_fetch_refspecs - simple' '
	sed -e "s/Z$//" >expected <<-EOF &&
	HEAD:HEAD Z
	branch-in-other:branch-in-other Z
	main-in-other:main-in-other Z
	EOF
	(
		cur= &&
		__but_complete_fetch_refspecs other &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_fetch_refspecs - matching' '
	sed -e "s/Z$//" >expected <<-EOF &&
	branch-in-other:branch-in-other Z
	EOF
	(
		cur=br &&
		__but_complete_fetch_refspecs other "" br &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_fetch_refspecs - prefix' '
	sed -e "s/Z$//" >expected <<-EOF &&
	+HEAD:HEAD Z
	+branch-in-other:branch-in-other Z
	+main-in-other:main-in-other Z
	EOF
	(
		cur="+" &&
		__but_complete_fetch_refspecs other "+" ""  &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_fetch_refspecs - fully qualified' '
	sed -e "s/Z$//" >expected <<-EOF &&
	refs/heads/branch-in-other:refs/heads/branch-in-other Z
	refs/heads/main-in-other:refs/heads/main-in-other Z
	refs/tags/tag-in-other:refs/tags/tag-in-other Z
	EOF
	(
		cur=refs/ &&
		__but_complete_fetch_refspecs other "" refs/ &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success '__but_complete_fetch_refspecs - fully qualified & prefix' '
	sed -e "s/Z$//" >expected <<-EOF &&
	+refs/heads/branch-in-other:refs/heads/branch-in-other Z
	+refs/heads/main-in-other:refs/heads/main-in-other Z
	+refs/tags/tag-in-other:refs/tags/tag-in-other Z
	EOF
	(
		cur=+refs/ &&
		__but_complete_fetch_refspecs other + refs/ &&
		print_comp
	) &&
	test_cmp expected out
'

test_expect_success 'but switch - with no options, complete local branches and unique remote branch names for DWIM logic' '
	test_completion "but switch " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - completes refs and unique remote branches for DWIM' '
	test_completion "but checkout " <<-\EOF
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

test_expect_success 'but switch - with --no-guess, complete only local branches' '
	test_completion "but switch --no-guess " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - with BUT_COMPLETION_CHECKOUT_NO_GUESS=1, complete only local branches' '
	BUT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "but switch " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - --guess overrides BUT_COMPLETION_CHECKOUT_NO_GUESS=1, complete local branches and unique remote names for DWIM logic' '
	BUT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "but switch --guess " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - a later --guess overrides previous --no-guess, complete local and remote unique branches for DWIM' '
	test_completion "but switch --no-guess --guess " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - a later --no-guess overrides previous --guess, complete only local branches' '
	test_completion "but switch --guess --no-guess " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - with BUT_COMPLETION_NO_GUESS=1 only completes refs' '
	BUT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "but checkout " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - --guess overrides BUT_COMPLETION_NO_GUESS=1, complete refs and unique remote branches for DWIM' '
	BUT_COMPLETION_CHECKOUT_NO_GUESS=1 test_completion "but checkout --guess " <<-\EOF
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

test_expect_success 'but checkout - with --no-guess, only completes refs' '
	test_completion "but checkout --no-guess " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - a later --guess overrides previous --no-guess, complete refs and unique remote branches for DWIM' '
	test_completion "but checkout --no-guess --guess " <<-\EOF
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

test_expect_success 'but checkout - a later --no-guess overrides previous --guess, complete only refs' '
	test_completion "but checkout --guess --no-guess " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with checkout.guess = false, only completes refs' '
	test_config checkout.guess false &&
	test_completion "but checkout " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with checkout.guess = true, completes refs and unique remote branches for DWIM' '
	test_config checkout.guess true &&
	test_completion "but checkout " <<-\EOF
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

test_expect_success 'but checkout - a later --guess overrides previous checkout.guess = false, complete refs and unique remote branches for DWIM' '
	test_config checkout.guess false &&
	test_completion "but checkout --guess " <<-\EOF
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

test_expect_success 'but checkout - a later --no-guess overrides previous checkout.guess = true, complete only refs' '
	test_config checkout.guess true &&
	test_completion "but checkout --no-guess " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with --detach, complete all references' '
	test_completion "but switch --detach " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with --detach, complete only references' '
	test_completion "but checkout --detach " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'setup sparse-checkout tests' '
	# set up sparse-checkout repo
	but init sparse-checkout &&
	(
		cd sparse-checkout &&
		mkdir -p folder1/0/1 folder2/0 folder3 &&
		touch folder1/0/1/t.txt &&
		touch folder2/0/t.txt &&
		touch folder3/t.txt &&
		but add . &&
		but cummit -am "Initial cummit"
	)
'

test_expect_success 'sparse-checkout completes subcommands' '
	test_completion "but sparse-checkout " <<-\EOF
	list Z
	init Z
	set Z
	add Z
	reapply Z
	disable Z
	EOF
'

test_expect_success 'cone mode sparse-checkout completes directory names' '
	# initialize sparse-checkout definitions
	but -C sparse-checkout sparse-checkout set --cone folder1/0 folder3 &&

	# test tab completion
	(
		cd sparse-checkout &&
		test_completion "but sparse-checkout set f" <<-\EOF
		folder1/
		folder2/
		folder3/
		EOF
	) &&

	(
		cd sparse-checkout &&
		test_completion "but sparse-checkout set folder1/" <<-\EOF
		folder1/0/
		EOF
	) &&

	(
		cd sparse-checkout &&
		test_completion "but sparse-checkout set folder1/0/" <<-\EOF
		folder1/0/1/
		EOF
	) &&

	(
		cd sparse-checkout/folder1 &&
		test_completion "but sparse-checkout add 0" <<-\EOF
		0/
		EOF
	)
'

test_expect_success 'cone mode sparse-checkout completes directory names with spaces and accents' '
	# reset sparse-checkout
	but -C sparse-checkout sparse-checkout disable &&
	(
		cd sparse-checkout &&
		mkdir "directory with spaces" &&
		mkdir "directory-with-áccent" &&
		>"directory with spaces/randomfile" &&
		>"directory-with-áccent/randomfile" &&
		but add . &&
		but cummit -m "Add directory with spaces and directory with accent" &&
		but sparse-checkout set --cone "directory with spaces" \
			"directory-with-áccent" &&
		test_completion "but sparse-checkout add dir" <<-\EOF &&
		directory with spaces/
		directory-with-áccent/
		EOF
		rm -rf "directory with spaces" &&
		rm -rf "directory-with-áccent" &&
		but add . &&
		but cummit -m "Remove directory with spaces and directory with accent"
	)
'

# use FUNNYNAMES to avoid running on Windows, which doesn't permit tabs in paths
test_expect_success FUNNYNAMES 'cone mode sparse-checkout completes directory names with tabs' '
	# reset sparse-checkout
	but -C sparse-checkout sparse-checkout disable &&
	(
		cd sparse-checkout &&
		mkdir "$(printf "directory\twith\ttabs")" &&
		>"$(printf "directory\twith\ttabs")/randomfile" &&
		but add . &&
		but cummit -m "Add directory with tabs" &&
		but sparse-checkout set --cone \
			"$(printf "directory\twith\ttabs")" &&
		test_completion "but sparse-checkout add dir" <<-\EOF &&
		directory	with	tabs/
		EOF
		rm -rf "$(printf "directory\twith\ttabs")" &&
		but add . &&
		but cummit -m "Remove directory with tabs"
	)
'

# use FUNNYNAMES to avoid running on Windows, and !CYGWIN for Cygwin, as neither permit backslashes in paths
test_expect_success FUNNYNAMES,!CYGWIN 'cone mode sparse-checkout completes directory names with backslashes' '
	# reset sparse-checkout
	but -C sparse-checkout sparse-checkout disable &&
	(
		cd sparse-checkout &&
		mkdir "directory\with\backslashes" &&
		>"directory\with\backslashes/randomfile" &&
		but add . &&
		but cummit -m "Add directory with backslashes" &&
		but sparse-checkout set --cone \
			"directory\with\backslashes" &&
		test_completion "but sparse-checkout add dir" <<-\EOF &&
		directory\with\backslashes/
		EOF
		rm -rf "directory\with\backslashes" &&
		but add . &&
		but cummit -m "Remove directory with backslashes"
	)
'

test_expect_success 'non-cone mode sparse-checkout uses bash completion' '
	# reset sparse-checkout repo to non-cone mode
	but -C sparse-checkout sparse-checkout disable &&
	but -C sparse-checkout sparse-checkout set --no-cone &&

	(
		cd sparse-checkout &&
		# expected to be empty since we have not configured
		# custom completion for non-cone mode
		test_completion "but sparse-checkout set f" <<-\EOF

		EOF
	)
'

test_expect_success 'but sparse-checkout set --cone completes directory names' '
	but -C sparse-checkout sparse-checkout disable &&

	(
		cd sparse-checkout &&
		test_completion "but sparse-checkout set --cone f" <<-\EOF
		folder1/
		folder2/
		folder3/
		EOF
	)
'

test_expect_success 'but switch - with -d, complete all references' '
	test_completion "but switch -d " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -d, complete only references' '
	test_completion "but checkout -d " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with --track, complete only remote branches' '
	test_completion "but switch --track " <<-\EOF
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with --track, complete only remote branches' '
	test_completion "but checkout --track " <<-\EOF
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with --no-track, complete only local branch names' '
	test_completion "but switch --no-track " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - with --no-track, complete only local references' '
	test_completion "but checkout --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with -c, complete all references' '
	test_completion "but switch -c new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with -C, complete all references' '
	test_completion "but switch -C new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with -c and --track, complete all references' '
	test_completion "but switch -c new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with -C and --track, complete all references' '
	test_completion "but switch -C new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with -c and --no-track, complete all references' '
	test_completion "but switch -c new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - with -C and --no-track, complete all references' '
	test_completion "but switch -C new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -b, complete all references' '
	test_completion "but checkout -b new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -B, complete all references' '
	test_completion "but checkout -B new-branch " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -b and --track, complete all references' '
	test_completion "but checkout -b new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -B and --track, complete all references' '
	test_completion "but checkout -B new-branch --track " <<-EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -b and --no-track, complete all references' '
	test_completion "but checkout -b new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but checkout - with -B and --no-track, complete all references' '
	test_completion "but checkout -B new-branch --no-track " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'but switch - for -c, complete local branches and unique remote branches' '
	test_completion "but switch -c " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - for -C, complete local branches and unique remote branches' '
	test_completion "but switch -C " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - for -c with --no-guess, complete local branches only' '
	test_completion "but switch --no-guess -c " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - for -C with --no-guess, complete local branches only' '
	test_completion "but switch --no-guess -C " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - for -c with --no-track, complete local branches only' '
	test_completion "but switch --no-track -c " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - for -C with --no-track, complete local branches only' '
	test_completion "but switch --no-track -C " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - for -b, complete local branches and unique remote branches' '
	test_completion "but checkout -b " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - for -B, complete local branches and unique remote branches' '
	test_completion "but checkout -B " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - for -b with --no-guess, complete local branches only' '
	test_completion "but checkout --no-guess -b " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - for -B with --no-guess, complete local branches only' '
	test_completion "but checkout --no-guess -B " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - for -b with --no-track, complete local branches only' '
	test_completion "but checkout --no-track -b " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - for -B with --no-track, complete local branches only' '
	test_completion "but checkout --no-track -B " <<-\EOF
	main Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - with --orphan completes local branch names and unique remote branch names' '
	test_completion "but switch --orphan " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but switch - --orphan with branch already provided completes nothing else' '
	test_completion "but switch --orphan main " <<-\EOF

	EOF
'

test_expect_success 'but checkout - with --orphan completes local branch names and unique remote branch names' '
	test_completion "but checkout --orphan " <<-\EOF
	branch-in-other Z
	main Z
	main-in-other Z
	matching-branch Z
	EOF
'

test_expect_success 'but checkout - --orphan with branch already provided completes local refs for a start-point' '
	test_completion "but checkout --orphan main " <<-\EOF
	HEAD Z
	main Z
	matching-branch Z
	matching-tag Z
	other/branch-in-other Z
	other/main-in-other Z
	EOF
'

test_expect_success 'teardown after ref completion' '
	but branch -d matching-branch &&
	but tag -d matching-tag &&
	but remote remove other
'


test_path_completion ()
{
	test $# = 2 || BUG "not 2 parameters to test_path_completion"

	local cur="$1" expected="$2"
	echo "$expected" >expected &&
	(
		# In the following tests calling this function we only
		# care about how __but_complete_index_file() deals with
		# unusual characters in path names.  By requesting only
		# untracked files we do not have to bother adding any
		# paths to the index in those tests.
		__but_complete_index_file --others &&
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

test_expect_success '__but_complete_index_file - simple' '
	test_path_completion simple simple-dir &&  # Bash is supposed to
						   # add the trailing /.
	test_path_completion simple-dir/simple simple-dir/simple-file
'

test_expect_success \
    '__but_complete_index_file - escaped characters on cmdline' '
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
    '__but_complete_index_file - quoted characters on cmdline' '
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

test_expect_success '__but_complete_index_file - UTF-8 in ls-files output' '
	test_path_completion á árvíztűrő &&
	test_path_completion árvíztűrő/С "árvíztűrő/Сайн яваарай"
'

test_expect_success FUNNIERNAMES \
    '__but_complete_index_file - C-style escapes in ls-files output' '
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
    '__but_complete_index_file - \nnn-escaped characters in ls-files output' '
	test_path_completion sep '$'separators\034in\035dir'' &&
	test_path_completion '$'separators\034i'' \
			     '$'separators\034in\035dir'' &&
	test_path_completion '$'separators\034in\035dir/sep'' \
			     '$'separators\034in\035dir/sep\036in\037file'' &&
	test_path_completion '$'separators\034in\035dir/sep\036i'' \
			     '$'separators\034in\035dir/sep\036in\037file''
'

test_expect_success FUNNYNAMES \
    '__but_complete_index_file - removing repeated quoted path components' '
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

test_expect_success '__but_find_on_cmdline - single match' '
	echo list >expect &&
	(
		words=(but command --opt list) &&
		cword=${#words[@]} &&
		__but_cmd_idx=1 &&
		__but_find_on_cmdline "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__but_find_on_cmdline - multiple matches' '
	echo remove >expect &&
	(
		words=(but command -o --opt remove list add) &&
		cword=${#words[@]} &&
		__but_cmd_idx=1 &&
		__but_find_on_cmdline "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__but_find_on_cmdline - no match' '
	(
		words=(but command --opt branch) &&
		cword=${#words[@]} &&
		__but_cmd_idx=1 &&
		__but_find_on_cmdline "add list remove" >actual
	) &&
	test_must_be_empty actual
'

test_expect_success '__but_find_on_cmdline - single match with index' '
	echo "3 list" >expect &&
	(
		words=(but command --opt list) &&
		cword=${#words[@]} &&
		__but_cmd_idx=1 &&
		__but_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__but_find_on_cmdline - multiple matches with index' '
	echo "4 remove" >expect &&
	(
		words=(but command -o --opt remove list add) &&
		cword=${#words[@]} &&
		__but_cmd_idx=1 &&
		__but_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__but_find_on_cmdline - no match with index' '
	(
		words=(but command --opt branch) &&
		cword=${#words[@]} &&
		__but_cmd_idx=1 &&
		__but_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_must_be_empty actual
'

test_expect_success '__but_find_on_cmdline - ignores matches before command with index' '
	echo "6 remove" >expect &&
	(
		words=(but -C remove command -o --opt remove list add) &&
		cword=${#words[@]} &&
		__but_cmd_idx=3 &&
		__but_find_on_cmdline --show-idx "add list remove" >actual
	) &&
	test_cmp expect actual
'

test_expect_success '__but_get_config_variables' '
	cat >expect <<-EOF &&
	name-1
	name-2
	EOF
	test_config interesting.name-1 good &&
	test_config interesting.name-2 good &&
	test_config subsection.interesting.name-3 bad &&
	__but_get_config_variables interesting >actual &&
	test_cmp expect actual
'

test_expect_success '__but_pretty_aliases' '
	cat >expect <<-EOF &&
	author
	hash
	EOF
	test_config pretty.author "%an %ae" &&
	test_config pretty.hash %H &&
	__but_pretty_aliases >actual &&
	test_cmp expect actual
'

test_expect_success 'basic' '
	run_completion "but " &&
	# built-in
	grep -q "^add \$" out &&
	# script
	grep -q "^rebase \$" out &&
	# plumbing
	! grep -q "^ls-files \$" out &&

	run_completion "but r" &&
	! grep -q -v "^r" out
'

test_expect_success 'double dash "but" itself' '
	test_completion "but --" <<-\EOF
	--paginate Z
	--no-pager Z
	--but-dir=
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

test_expect_success 'double dash "but checkout"' '
	test_completion "but checkout --" <<-\EOF
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
	test_completion "but --ver" "--version " &&
	test_completion "but --hel" "--help " &&
	test_completion "but --exe" <<-\EOF &&
	--exec-path Z
	--exec-path=
	EOF
	test_completion "but --htm" "--html-path " &&
	test_completion "but --pag" "--paginate " &&
	test_completion "but --no-p" "--no-pager " &&
	test_completion "but --but" "--but-dir=" &&
	test_completion "but --wor" "--work-tree=" &&
	test_completion "but --nam" "--namespace=" &&
	test_completion "but --bar" "--bare " &&
	test_completion "but --inf" "--info-path " &&
	test_completion "but --no-r" "--no-replace-objects "
'

test_expect_success 'general options plus command' '
	test_completion "but --version check" "checkout " &&
	test_completion "but --paginate check" "checkout " &&
	test_completion "but --but-dir=foo check" "checkout " &&
	test_completion "but --bare check" "checkout " &&
	test_completion "but --exec-path=foo check" "checkout " &&
	test_completion "but --html-path check" "checkout " &&
	test_completion "but --no-pager check" "checkout " &&
	test_completion "but --work-tree=foo check" "checkout " &&
	test_completion "but --namespace=foo check" "checkout " &&
	test_completion "but --paginate check" "checkout " &&
	test_completion "but --info-path check" "checkout " &&
	test_completion "but --no-replace-objects check" "checkout " &&
	test_completion "but --but-dir some/path check" "checkout " &&
	test_completion "but -c conf.var=value check" "checkout " &&
	test_completion "but -C some/path check" "checkout " &&
	test_completion "but --work-tree some/path check" "checkout " &&
	test_completion "but --namespace name/space check" "checkout "
'

test_expect_success 'but --help completion' '
	test_completion "but --help ad" "add " &&
	test_completion "but --help core" "core-tutorial "
'

test_expect_success 'completion.commands removes multiple commands' '
	test_config completion.commands "-cherry -mergetool" &&
	but --list-cmds=list-mainporcelain,list-complete,config >out &&
	! grep -E "^(cherry|mergetool)$" out
'

test_expect_success 'setup for integration tests' '
	echo content >file1 &&
	echo more >file2 &&
	but add file1 file2 &&
	but cummit -m one &&
	but branch mybranch &&
	but tag mytag
'

test_expect_success 'checkout completes ref names' '
	test_completion "but checkout m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'but -C <path> checkout uses the right repo' '
	test_completion "but -C subdir -C subsubdir -C .. -C ../otherrepo checkout b" <<-\EOF
	branch-in-other Z
	EOF
'

test_expect_success 'show completes all refs' '
	test_completion "but show m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success '<ref>: completes paths' '
	test_completion "but show mytag:f" <<-\EOF
	file1Z
	file2Z
	EOF
'

test_expect_success 'complete tree filename with spaces' '
	echo content >"name with spaces" &&
	but add "name with spaces" &&
	but cummit -m spaces &&
	test_completion "but show HEAD:nam" <<-\EOF
	name with spacesZ
	EOF
'

test_expect_success 'complete tree filename with metacharacters' '
	echo content >"name with \${meta}" &&
	but add "name with \${meta}" &&
	but cummit -m meta &&
	test_completion "but show HEAD:nam" <<-\EOF
	name with ${meta}Z
	name with spacesZ
	EOF
'

test_expect_success PERL 'send-email' '
	test_completion "but send-email --cov" <<-\EOF &&
	--cover-from-description=Z
	--cover-letter Z
	EOF
	test_completion "but send-email --val" <<-\EOF &&
	--validate Z
	EOF
	test_completion "but send-email ma" "main "
'

test_expect_success 'complete files' '
	but init tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	echo "expected" > .butignore &&
	echo "out" >> .butignore &&
	echo "out_sorted" >> .butignore &&

	but add .butignore &&
	test_completion "but cummit " ".butignore" &&

	but cummit -m ignore &&

	touch new &&
	test_completion "but add " "new" &&

	but add new &&
	but cummit -a -m new &&
	test_completion "but add " "" &&

	but mv new modified &&
	echo modify > modified &&
	test_completion "but add " "modified" &&

	mkdir -p some/deep &&
	touch some/deep/path &&
	test_completion "but add some/" "some/deep" &&
	but clean -f some &&

	touch untracked &&

	: TODO .butignore should not be here &&
	test_completion "but rm " <<-\EOF &&
	.butignore
	modified
	EOF

	test_completion "but clean " "untracked" &&

	: TODO .butignore should not be here &&
	test_completion "but mv " <<-\EOF &&
	.butignore
	modified
	EOF

	mkdir dir &&
	touch dir/file-in-dir &&
	but add dir/file-in-dir &&
	but cummit -m dir &&

	mkdir untracked-dir &&

	: TODO .butignore should not be here &&
	test_completion "but mv modified " <<-\EOF &&
	.butignore
	dir
	modified
	untracked
	untracked-dir
	EOF

	test_completion "but cummit " "modified" &&

	: TODO .butignore should not be here &&
	test_completion "but ls-files " <<-\EOF &&
	.butignore
	dir
	modified
	EOF

	touch momified &&
	test_completion "but add mom" "momified"
'

test_expect_success "simple alias" '
	test_config alias.co checkout &&
	test_completion "but co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success "recursive alias" '
	test_config alias.co checkout &&
	test_config alias.cod "co --detached" &&
	test_completion "but cod m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success "completion uses <cmd> completion for alias: !sh -c 'but <cmd> ...'" '
	test_config alias.co "!sh -c '"'"'but checkout ...'"'"'" &&
	test_completion "but co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'completion uses <cmd> completion for alias: !f () { VAR=val but <cmd> ... }' '
	test_config alias.co "!f () { VAR=val but checkout ... ; } f" &&
	test_completion "but co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'completion used <cmd> completion for alias: !f() { : but <cmd> ; ... }' '
	test_config alias.co "!f() { : but checkout ; if ... } f" &&
	test_completion "but co m" <<-\EOF
	main Z
	mybranch Z
	mytag Z
	EOF
'

test_expect_success 'completion without explicit _but_xxx function' '
	test_completion "but version --" <<-\EOF
	--build-options Z
	--no-build-options Z
	EOF
'

test_expect_failure 'complete with tilde expansion' '
	but init tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	touch ~/tmp/file &&

	test_completion "but add ~/tmp/" "~/tmp/file"
'

test_expect_success 'setup other remote for remote reference completion' '
	but remote add other otherrepo &&
	but fetch other
'

for flag in -d --delete
do
	test_expect_success "__but_complete_remote_or_refspec - push $flag other" '
		sed -e "s/Z$//" >expected <<-EOF &&
		main-in-other Z
		EOF
		(
			words=(but push '$flag' other ma) &&
			cword=${#words[@]} cur=${words[cword-1]} &&
			__but_cmd_idx=1 &&
			__but_complete_remote_or_refspec &&
			print_comp
		) &&
		test_cmp expected out
	'

	test_expect_failure "__but_complete_remote_or_refspec - push other $flag" '
		sed -e "s/Z$//" >expected <<-EOF &&
		main-in-other Z
		EOF
		(
			words=(but push other '$flag' ma) &&
			cword=${#words[@]} cur=${words[cword-1]} &&
			__but_cmd_idx=1 &&
			__but_complete_remote_or_refspec &&
			print_comp
		) &&
		test_cmp expected out
	'
done

test_expect_success 'but config - section' '
	test_completion "but config br" <<-\EOF
	branch.Z
	browser.Z
	EOF
'

test_expect_success 'but config - variable name' '
	test_completion "but config log.d" <<-\EOF
	log.date Z
	log.decorate Z
	log.diffMerges Z
	EOF
'

test_expect_success 'but config - value' '
	test_completion "but config color.pager " <<-\EOF
	false Z
	true Z
	EOF
'

test_expect_success 'but -c - section' '
	test_completion "but -c br" <<-\EOF
	branch.Z
	browser.Z
	EOF
'

test_expect_success 'but -c - variable name' '
	test_completion "but -c log.d" <<-\EOF
	log.date=Z
	log.decorate=Z
	log.diffMerges=Z
	EOF
'

test_expect_success 'but -c - value' '
	test_completion "but -c color.pager=" <<-\EOF
	false Z
	true Z
	EOF
'

test_expect_success 'but clone --config= - section' '
	test_completion "but clone --config=br" <<-\EOF
	branch.Z
	browser.Z
	EOF
'

test_expect_success 'but clone --config= - variable name' '
	test_completion "but clone --config=log.d" <<-\EOF
	log.date=Z
	log.decorate=Z
	log.diffMerges=Z
	EOF
'

test_expect_success 'but clone --config= - value' '
	test_completion "but clone --config=color.pager=" <<-\EOF
	false Z
	true Z
	EOF
'

test_expect_success 'options with value' '
	test_completion "but merge -X diff-algorithm=" <<-\EOF

	EOF
'

test_expect_success 'sourcing the completion script clears cached commands' '
	(
		__but_compute_all_commands &&
		verbose test -n "$__but_all_commands" &&
		. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash" &&
		verbose test -z "$__but_all_commands"
	)
'

test_expect_success 'sourcing the completion script clears cached merge strategies' '
	(
		__but_compute_merge_strategies &&
		verbose test -n "$__but_merge_strategies" &&
		. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash" &&
		verbose test -z "$__but_merge_strategies"
	)
'

test_expect_success 'sourcing the completion script clears cached --options' '
	(
		__butcomp_builtin checkout &&
		verbose test -n "$__butcomp_builtin_checkout" &&
		__butcomp_builtin notes_edit &&
		verbose test -n "$__butcomp_builtin_notes_edit" &&
		. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash" &&
		verbose test -z "$__butcomp_builtin_checkout" &&
		verbose test -z "$__butcomp_builtin_notes_edit"
	)
'

test_expect_success 'option aliases are not shown by default' '
	test_completion "but clone --recurs" "--recurse-submodules "
'

test_expect_success 'option aliases are shown with BUT_COMPLETION_SHOW_ALL' '
	(
		. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash" &&
		BUT_COMPLETION_SHOW_ALL=1 && export BUT_COMPLETION_SHOW_ALL &&
		test_completion "but clone --recurs" <<-\EOF
		--recurse-submodules Z
		--recursive Z
		EOF
	)
'

test_expect_success 'plumbing commands are excluded without BUT_COMPLETION_SHOW_ALL_COMMANDS' '
	(
		. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash" &&
		sane_unset BUT_TESTING_PORCELAIN_COMMAND_LIST &&

		# Just mainporcelain, not plumbing commands
		run_completion "but c" &&
		grep checkout out &&
		! grep cat-file out
	)
'

test_expect_success 'all commands are shown with BUT_COMPLETION_SHOW_ALL_COMMANDS (also main non-builtin)' '
	(
		. "$BUT_BUILD_DIR/contrib/completion/but-completion.bash" &&
		BUT_COMPLETION_SHOW_ALL_COMMANDS=1 &&
		export BUT_COMPLETION_SHOW_ALL_COMMANDS &&
		sane_unset BUT_TESTING_PORCELAIN_COMMAND_LIST &&

		# Both mainporcelain and plumbing commands
		run_completion "but c" &&
		grep checkout out &&
		grep cat-file out &&

		# Check "butk", a "main" command, but not a built-in + more plumbing
		run_completion "but g" &&
		grep butk out &&
		grep get-tar-cummit-id out
	)
'

test_expect_success '__but_complete' '
	unset -f __but_wrap__but_main &&

	__but_complete foo __but_main &&
	__but_have_func __but_wrap__but_main &&
	unset -f __but_wrap__but_main &&

	__but_complete gf _but_fetch &&
	__but_have_func __but_wrap_but_fetch &&

	__but_complete foo but &&
	__but_have_func __but_wrap__but_main &&
	unset -f __but_wrap__but_main &&

	__but_complete gd but_diff &&
	__but_have_func __but_wrap_but_diff &&

	test_must_fail __but_complete ga missing
'

test_done
