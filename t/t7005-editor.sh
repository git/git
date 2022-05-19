#!/bin/sh

test_description='BUT_EDITOR, core.editor, and stuff'

. ./test-lib.sh

unset EDITOR VISUAL BUT_EDITOR

test_expect_success 'determine default editor' '

	vi=$(TERM=vt100 but var BUT_EDITOR) &&
	test -n "$vi"

'

if ! expr "$vi" : '[a-z]*$' >/dev/null
then
	vi=
fi

for i in BUT_EDITOR core_editor EDITOR VISUAL $vi
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
	test_cummit "$msg" &&
	echo "$msg" >expect &&
	but show -s --format=%s > actual &&
	test_cmp expect actual

'

TERM=dumb
export TERM
test_expect_success 'dumb should error out when falling back on vi' '

	if but cummit --amend
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
	but cummit --amend &&
	test "$(but show -s --format=%s)" = "Edited by EDITOR"

'

TERM=vt100
export TERM
for i in $vi EDITOR VISUAL core_editor BUT_EDITOR
do
	echo "Edited by $i" >expect
	unset EDITOR VISUAL BUT_EDITOR
	but config --unset-all core.editor
	case "$i" in
	core_editor)
		but config core.editor ./e-core_editor.sh
		;;
	[A-Z]*)
		eval "$i=./e-$i.sh"
		export $i
		;;
	esac
	test_expect_success "Using $i" '
		but --exec-path=. cummit --amend &&
		but show -s --pretty=oneline |
		sed -e "s/^[0-9a-f]* //" >actual &&
		test_cmp expect actual
	'
done

unset EDITOR VISUAL BUT_EDITOR
but config --unset-all core.editor
for i in $vi EDITOR VISUAL core_editor BUT_EDITOR
do
	echo "Edited by $i" >expect
	case "$i" in
	core_editor)
		but config core.editor ./e-core_editor.sh
		;;
	[A-Z]*)
		eval "$i=./e-$i.sh"
		export $i
		;;
	esac
	test_expect_success "Using $i (override)" '
		but --exec-path=. cummit --amend &&
		but show -s --pretty=oneline |
		sed -e "s/^[0-9a-f]* //" >actual &&
		test_cmp expect actual
	'
done

test_expect_success 'editor with a space' '
	echo "echo space >\"\$1\"" >"e space.sh" &&
	chmod a+x "e space.sh" &&
	BUT_EDITOR="./e\ space.sh" but cummit --amend &&
	test space = "$(but show -s --pretty=format:%s)"

'

unset BUT_EDITOR
test_expect_success 'core.editor with a space' '

	but config core.editor \"./e\ space.sh\" &&
	but cummit --amend &&
	test space = "$(but show -s --pretty=format:%s)"

'

test_done
