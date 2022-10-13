#!/bin/sh

test_description='assert (unbuilt) Documentation/*.txt and -h output'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup: list of builtins' '
	git --list-cmds=builtins >builtins
'

help_to_synopsis () {
	builtin="$1" &&
	out_dir="out/$builtin" &&
	out="$out_dir/help.synopsis" &&
	if test -f "$out"
	then
		echo "$out" &&
		return 0
	fi &&
	mkdir -p "$out_dir" &&
	test_expect_code 129 git $builtin -h >"$out.raw" 2>&1 &&
	sed -n \
		-e '1,/^$/ {
			/^$/d;
			s/^usage: //;
			s/^ *or: //;
			p;
		}' <"$out.raw" >"$out" &&
	echo "$out"
}

HT="	"

while read builtin
do
	# -h output assertions
	test_expect_success "$builtin -h output has no \t" '
		h2s="$(help_to_synopsis "$builtin")" &&
		! grep "$HT" "$h2s"
	'
done <builtins

test_done
