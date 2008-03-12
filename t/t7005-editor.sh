#!/bin/sh

test_description='GIT_EDITOR, core.editor, and stuff'

. ./test-lib.sh

OLD_TERM="$TERM"

for i in GIT_EDITOR core_editor EDITOR VISUAL vi
do
	cat >e-$i.sh <<-EOF
	echo "Edited by $i" >"\$1"
	EOF
	chmod +x e-$i.sh
done
unset vi
mv e-vi.sh vi
unset EDITOR VISUAL GIT_EDITOR

test_expect_success setup '

	msg="Hand edited" &&
	echo "$msg" >expect &&
	git add vi &&
	test_tick &&
	git commit -m "$msg" &&
	git show -s --pretty=oneline |
	sed -e "s/^[0-9a-f]* //" >actual &&
	diff actual expect

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

TERM=vt100
export TERM
for i in vi EDITOR VISUAL core_editor GIT_EDITOR
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
		diff actual expect
	'
done

unset EDITOR VISUAL GIT_EDITOR
git config --unset-all core.editor
for i in vi EDITOR VISUAL core_editor GIT_EDITOR
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
		diff actual expect
	'
done

test_expect_success 'editor with a space' '

	if echo "echo space > \"\$1\"" > "e space.sh"
	then
		chmod a+x "e space.sh" &&
		GIT_EDITOR="./e\ space.sh" git commit --amend &&
		test space = "$(git show -s --pretty=format:%s)"
	else
		say "Skipping; FS does not support spaces in filenames"
	fi

'

unset GIT_EDITOR
test_expect_success 'core.editor with a space' '

	if test -f "e space.sh"
	then
		git config core.editor \"./e\ space.sh\" &&
		git commit --amend &&
		test space = "$(git show -s --pretty=format:%s)"
	else
		say "Skipping; FS does not support spaces in filenames"
	fi

'

TERM="$OLD_TERM"

test_done
