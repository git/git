#!/bin/sh

test_description='GIT_EDITOR, core.editor, and stuff'

. ./test-lib.sh

for i in GIT_EDITOR core_editor EDITOR VISUAL vi
do
	cat >e-$i.sh <<-EOF
	echo "Edited by $i" >"\$1"
	EOF
	chmod +x e-$i.sh
done
unset vi
mv e-vi.sh vi
PATH=".:$PATH"
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
		exit 1
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
		git commit --amend &&
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
		git commit --amend &&
		git show -s --pretty=oneline |
		sed -e "s/^[0-9a-f]* //" >actual &&
		diff actual expect
	'
done

test_done
