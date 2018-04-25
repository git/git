# This shell script fragment is sourced by git-rebase to implement
# its default, fast, patch-based, non-interactive mode.
#
# Copyright (c) 2010 Junio C Hamano.
#

git_rebase__am () {

case "$action" in
continue)
	git am --resolved --resolvemsg="$resolvemsg" \
		${gpg_sign_opt:+"$gpg_sign_opt"} &&
	move_to_original_branch
	return
	;;
skip)
	git am --skip --resolvemsg="$resolvemsg" &&
	move_to_original_branch
	return
	;;
show-current-patch)
	exec git am --show-current-patch
	;;
esac

if test -z "$rebase_root"
	# this is now equivalent to ! -z "$upstream"
then
	revisions=$upstream...$orig_head
else
	revisions=$onto...$orig_head
fi

ret=0
rm -f "$GIT_DIR/rebased-patches"

git format-patch -k --stdout --full-index --cherry-pick --right-only \
	--src-prefix=a/ --dst-prefix=b/ --no-renames --no-cover-letter \
	--pretty=mboxrd \
	$git_format_patch_opt \
	"$revisions" ${restrict_revision+^$restrict_revision} \
	>"$GIT_DIR/rebased-patches"
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
	return $ret
fi

git am $git_am_opt --rebasing --resolvemsg="$resolvemsg" \
	--patch-format=mboxrd \
	$allow_rerere_autoupdate \
	${gpg_sign_opt:+"$gpg_sign_opt"} <"$GIT_DIR/rebased-patches"
ret=$?

rm -f "$GIT_DIR/rebased-patches"

if test 0 != $ret
then
	test -d "$state_dir" && write_basic_state
	return $ret
fi

move_to_original_branch

}
