#!/bin/sh

test_description='GIT_EDITOR, core.editor, and stuff'

. ./test-lib.sh

unset EDITOR VISUAL GIT_EDITOR

test_expect_success 'determine default editor' '

	vi=$(TERM=vt100 git var GIT_EDITOR) &&
	test -n "$vi"

'

if ! expr "$vi" : '[a-z]*$' >/dev/null
then
	vi=
fi

for i in GIT_EDITOR core_editor EDITOR VISUAL $vi
do
	cat >e-$i.sh <<-EOF
	#!$SHELL_PATH
	echo "Edited by $i" >"\$1"
	EOF
	chmod +x e-$i.sh
done

if ! test -z "$vi"
then
	mv e-$vi.sh $vi
fi

test_expect_success setup '

	msg="Hand-edited" &&
	test_commit "$msg" &&
	echo "$msg" >expect &&
	git show -s --format=%s > actual &&
	test_cmp actual expect

'

TERM=dumb
export TERM
test_expect_success 'dumb should error out when falling back on vi' '

	if git commit --amend
	then
		echo "Oops?"
		false
	else
		: happy
	fi
'

test_expect_success 'dumb should prefer EDITOR to VISUAL' '

	EDITOR=./e-EDITOR.sh &&
	VISUAL=./e-VISUAL.sh &&
	export EDITOR VISUAL &&
	git commit --amend &&
	test "$(git show -s --format=%s)" = "Edited by EDITOR"

'

TERM=vt100
export TERM
for i in $vi EDITOR VISUAL core_editor GIT_EDITOR
do
	echo "Edited by $i" >expect
	unset EDITOR VISUAL GIT_EDITOR
	git config --unset-all core.editor
	case "$i" in
	core_editor)
		git config core.editor ./e-core_editor.sh
		;;
	[A-Z]*)
		eval "$i=./e-$i.sh"
		export $i
		;;
	esac
	test_expect_success "Using $i" '
		git --exec-path=. commit --amend &&
		git show -s --pretty=oneline |
		sed -e "s/^[0-9a-f]* //" >actual &&
		test_cmp actual expect
	'
done

unset EDITOR VISUAL GIT_EDITOR
git config --unset-all core.editor
for i in $vi EDITOR VISUAL core_editor GIT_EDITOR
do
	echo "Edited by $i" >expect
	case "$i" in
	core_editor)
		git config core.editor ./e-core_editor.sh
		;;
	[A-Z]*)
		eval "$i=./e-$i.sh"
		export $i
		;;
	esac
	test_expect_success "Using $i (override)" '
		git --exec-path=. commit --amend &&
		git show -s --pretty=oneline |
		sed -e "s/^[0-9a-f]* //" >actual &&
		test_cmp actual expect
	'
done

if echo 'echo space > "$1"' > "e space.sh"
then
	# FS supports spaces in filenames
	test_set_prereq SPACES_IN_FILENAMES
fi

test_expect_success SPACES_IN_FILENAMES 'editor with a space' '

	chmod a+x "e space.sh" &&
	GIT_EDITOR="./e\ space.sh" git commit --amend &&
	test space = "$(git show -s --pretty=format:%s)"

'

unset GIT_EDITOR
test_expect_success SPACES_IN_FILENAMES 'core.editor with a space' '

	git config core.editor \"./e\ space.sh\" &&
	git commit --amend &&
	test space = "$(git show -s --pretty=format:%s)"

'

test_done
