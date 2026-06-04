#!/bin/sh

test_description='GIT_EDITOR, core.editor, and stuff'

. ./test-lib.sh

unset EDITOR VISUAL GIT_EDITOR

test_expect_success 'determine default editor' '
	vi=$(TERM=vt100 git var GIT_EDITOR) &&
	test -n "$vi"
'

test_expect_success setup '
	if ! expr "$vi" : "[a-z]*$" >/dev/null
	then
		vi=
	fi &&

	for i in GIT_EDITOR core_editor EDITOR VISUAL $vi
	do
		write_script e-$i.sh <<-EOF || return 1
			echo "Edited by $i" >"\$1"
		EOF
	done &&

	if ! test -z "$vi"
	then
		mv e-$vi.sh $vi
	fi &&

	msg="Hand-edited" &&
	test_commit "$msg" &&
	test_commit_message HEAD -m "$msg"
'

test_expect_success 'dumb should error out when falling back on vi' '
	test_must_fail env TERM=dumb git commit --amend
'

test_expect_success 'dumb should prefer EDITOR to VISUAL' '
	TERM=dumb EDITOR=./e-EDITOR.sh VISUAL=./e-VISUAL.sh \
		git commit --amend &&
	test_commit_message HEAD -m "Edited by EDITOR"
'

for i in $vi EDITOR VISUAL core_editor GIT_EDITOR
do
	test_expect_success "Using $i" '
		if test "$i" = core_editor
		then
			test_config core.editor ./e-core_editor.sh
		fi &&
		(
			case "$i" in
			[A-Z]*)
				eval "$i=./e-$i.sh" &&
				export $i
				;;
			esac &&
			PATH="$PWD:$PATH" TERM=vt100 git commit --amend
		) &&
		test_commit_message HEAD -m "Edited by $i"
	'
done

test_expect_success 'Using editors with overrides' '
	(
		TERM=vt100 &&
		export TERM &&
		for i in $vi EDITOR VISUAL core_editor GIT_EDITOR
		do
			echo "Edited by $i" >expect &&
			case "$i" in
			core_editor)
				git config core.editor ./e-core_editor.sh
				;;
			[A-Z]*)
				eval "$i=./e-$i.sh" &&
				export $i
				;;
			esac &&
			PATH="$PWD:$PATH" git commit --amend &&
			test_commit_message HEAD expect || exit 1
		done
	)
'

test_expect_success 'editor with a space' '
	echo "echo space >\"\$1\"" >"e space.sh" &&
	chmod a+x "e space.sh" &&
	GIT_EDITOR="./e\ space.sh" git commit --amend &&
	test_commit_message HEAD -m space
'

test_expect_success 'core.editor with a space' '
	test_config core.editor \"./e\ space.sh\" &&
	git commit --amend &&
	test_commit_message HEAD -m space
'

test_done
