#!/bin/sh
#
# Copyright (c) 2010 Junio C Hamano.
#

. git-sh-setup

case "$action" in
continue)
	git am --resolved --3way --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	exit
	;;
skip)
	git am --skip -3 --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	exit
	;;
esac

test -n "$rebase_root" && root_flag=--root

git format-patch -k --stdout --full-index --ignore-if-in-upstream \
	--src-prefix=a/ --dst-prefix=b/ \
	--no-renames $root_flag "$revisions" |
git am $git_am_opt --rebasing --resolvemsg="$resolvemsg" &&
move_to_original_branch
ret=$?
test 0 != $ret -a -d "$state_dir" &&
	echo $head_name > "$state_dir/head-name" &&
	echo $onto > "$state_dir/onto" &&
	echo $orig_head > "$state_dir/orig-head" &&
	echo "$GIT_QUIET" > "$state_dir/quiet"
exit $ret
