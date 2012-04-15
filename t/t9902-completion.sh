#!/bin/sh
#
# Copyright (c) 2012 Felipe Contreras
#

if test -n "$BASH" && test -z "$POSIXLY_CORRECT"; then
	# we are in full-on bash mode
	true
elif type bash >/dev/null 2>&1; then
	# execute in full-on bash mode
	unset POSIXLY_CORRECT
	exec bash "$0" "$@"
else
	echo '1..0 #SKIP skipping bash completion tests; bash not available'
	exit 0
fi

test_description='test bash completion'

. ./test-lib.sh

complete ()
{
	# do nothing
	return 0
}

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
	(( _cword = ${#_words[@]} - 1 ))
	_git && print_comp
}

test_completion ()
{
	test $# -gt 1 && echo "$2" > expected
	run_completion "$@" &&
	test_cmp expected out
}

newline=$'\n'

test_expect_success '__gitcomp - trailing space - options' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	--reuse-message=Z
	--reedit-message=Z
	--reset-author Z
	EOF
	(
		local -a COMPREPLY &&
		cur="--re" &&
		__gitcomp "--dry-run --reuse-message= --reedit-message=
				--reset-author" &&
		IFS="$newline" &&
		echo "${COMPREPLY[*]}" > out
	) &&
	test_cmp expected out
'

test_expect_success '__gitcomp - trailing space - config keys' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	branch.Z
	branch.autosetupmerge Z
	branch.autosetuprebase Z
	browser.Z
	EOF
	(
		local -a COMPREPLY &&
		cur="br" &&
		__gitcomp "branch. branch.autosetupmerge
				branch.autosetuprebase browser." &&
		IFS="$newline" &&
		echo "${COMPREPLY[*]}" > out
	) &&
	test_cmp expected out
'

test_expect_success '__gitcomp - option parameter' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	recursive Z
	resolve Z
	EOF
	(
		local -a COMPREPLY &&
		cur="--strategy=re" &&
		__gitcomp "octopus ours recursive resolve subtree
			" "" "re" &&
		IFS="$newline" &&
		echo "${COMPREPLY[*]}" > out
	) &&
	test_cmp expected out
'

test_expect_success '__gitcomp - prefix' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	branch.maint.merge Z
	branch.maint.mergeoptions Z
	EOF
	(
		local -a COMPREPLY &&
		cur="branch.me" &&
		__gitcomp "remote merge mergeoptions rebase
			" "branch.maint." "me" &&
		IFS="$newline" &&
		echo "${COMPREPLY[*]}" > out
	) &&
	test_cmp expected out
'

test_expect_success '__gitcomp - suffix' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	branch.master.Z
	branch.maint.Z
	EOF
	(
		local -a COMPREPLY &&
		cur="branch.me" &&
		__gitcomp "master maint next pu
			" "branch." "ma" "." &&
		IFS="$newline" &&
		echo "${COMPREPLY[*]}" > out
	) &&
	test_cmp expected out
'

test_expect_success 'basic' '
	run_completion "git \"\"" &&
	# built-in
	grep -q "^add \$" out &&
	# script
	grep -q "^filter-branch \$" out &&
	# plumbing
	! grep -q "^ls-files \$" out &&

	run_completion "git f" &&
	! grep -q -v "^f" out
'

test_expect_success 'double dash "git" itself' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	--paginate Z
	--no-pager Z
	--git-dir=
	--bare Z
	--version Z
	--exec-path Z
	--html-path Z
	--info-path Z
	--work-tree=
	--namespace=
	--no-replace-objects Z
	--help Z
	EOF
	test_completion "git --"
'

test_expect_success 'double dash "git checkout"' '
	sed -e "s/Z$//" >expected <<-\EOF &&
	--quiet Z
	--ours Z
	--theirs Z
	--track Z
	--no-track Z
	--merge Z
	--conflict=
	--orphan Z
	--patch Z
	EOF
	test_completion "git checkout --"
'

test_expect_success 'general options' '
	test_completion "git --ver" "--version " &&
	test_completion "git --hel" "--help " &&
	test_completion "git --exe" "--exec-path " &&
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
test_done
