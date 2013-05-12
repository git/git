#!/bin/sh
#
# Copyright (c) 2010 Junio C Hamano.
#

case "$action" in
continue)
	git am --resolved --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	return
	;;
skip)
	git am --skip --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	return
	;;
esac

test -n "$rebase_root" && root_flag=--root

ret=0
if test -n "$keep_empty"
then
	# we have to do this the hard way.  git format-patch completely squashes
	# empty commits and even if it didn't the format doesn't really lend
	# itself well to recording empty patches.  fortunately, cherry-pick
	# makes this easy
	git cherry-pick --allow-empty "$revisions"
	ret=$?
else
	rm -f "$GIT_DIR/rebased-patches"

	git format-patch -k --stdout --full-index --ignore-if-in-upstream \
		--src-prefix=a/ --dst-prefix=b/ --no-renames --no-cover-letter \
		$root_flag "$revisions" >"$GIT_DIR/rebased-patches"
	ret=$?

	if test 0 != $ret
	then
		rm -f "$GIT_DIR/rebased-patches"
		case "$head_name" in
		refs/heads/*)
			git checkout -q "$head_name"
			;;
		*)
			git checkout -q "$orig_head"
			;;
		esac

		cat >&2 <<-EOF

		git encountered an error while preparing the patches to replay
		these revisions:

		    $revisions

		As a result, git cannot rebase them.
		EOF
		return $?
	fi

	git am $git_am_opt --rebasing --resolvemsg="$resolvemsg" <"$GIT_DIR/rebased-patches"
	ret=$?

	rm -f "$GIT_DIR/rebased-patches"
fi

if test 0 != $ret
then
	test -d "$state_dir" && write_basic_state
	return $ret
fi

move_to_original_branch
