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

builtin_to_txt () {
       echo "$GIT_BUILD_DIR/Documentation/git-$1.txt"
}

txt_to_synopsis () {
	builtin="$1" &&
	out_dir="out/$builtin" &&
	out="$out_dir/txt.synopsis" &&
	if test -f "$out"
	then
		echo "$out" &&
		return 0
	fi &&
	b2t="$(builtin_to_txt "$builtin")" &&
	sed -n \
		-e '/^\[verse\]$/,/^$/ {
			/^$/d;
			/^\[verse\]$/d;

			p;
		}' \
		<"$b2t" >"$out" &&
	echo "$out"
}

check_dashed_labels () {
	! grep -E "<[^>_-]+_" "$1"
}

HT="	"

while read builtin
do
	# -h output assertions
	test_expect_success "$builtin -h output has no \t" '
		h2s="$(help_to_synopsis "$builtin")" &&
		! grep "$HT" "$h2s"
	'

	test_expect_success "$builtin -h output has dashed labels" '
		check_dashed_labels "$(help_to_synopsis "$builtin")"
	'

	txt="$(builtin_to_txt "$builtin")" &&
	preq="$(echo BUILTIN_TXT_$builtin | tr '[:lower:]-' '[:upper:]_')" &&

	if test -f "$txt"
	then
		test_set_prereq "$preq"
	fi &&

	# *.txt output assertions
	test_expect_success "$preq" "$builtin *.txt SYNOPSIS has dashed labels" '
		check_dashed_labels "$(txt_to_synopsis "$builtin")"
	'
done <builtins

test_done
