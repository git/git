#!/bin/sh
#
# Copyright (c) 2010 Junio C Hamano.
#

case "$action" in
continue)
	git am --resolved --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	exit
	;;
skip)
	git am --skip --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	exit
	;;
esac

test -n "$rebase_root" && root_flag=--root

if test -n "$keep_empty"
then
	# we have to do this the hard way.  git format-patch completely squashes
	# empty commits and even if it didn't the format doesn't really lend
	# itself well to recording empty patches.  fortunately, cherry-pick
	# makes this easy
	git cherry-pick --allow-empty "$revisions"
else
	git format-patch -k --stdout --full-index --ignore-if-in-upstream \
		--src-prefix=a/ --dst-prefix=b/ \
		--no-renames $root_flag "$revisions" |
	git am $git_am_opt --rebasing --resolvemsg="$resolvemsg"
fi && move_to_original_branch

ret=$?
test 0 != $ret -a -d "$state_dir" && write_basic_state
exit $ret
