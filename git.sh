#!/bin/sh

cmd=
path=$(dirname $0)
case "$#" in
0)	;;
*)	cmd="$1"
	shift
	case "$cmd" in
	-v|--v|--ve|--ver|--vers|--versi|--versio|--version)
		echo "git version @@GIT_VERSION@@"
		exit 0 ;;
	esac
	test -x $path/git-$cmd && exec $path/git-$cmd "$@" ;;
	test -x $path/git-$cmd.exe && exec $path/git-$cmd.exe "$@" ;;
esac

echo "Usage: git COMMAND [OPTIONS] [TARGET]"
if [ -n "$cmd" ]; then
    echo " git command '$cmd' not found: commands are:"
else
    echo " git commands are:"
fi

cat <<\EOF
    add apply archimport bisect branch checkout cherry clone
    commit count-objects cvsimport diff fetch format-patch
    fsck-cache get-tar-commit-id init-db log ls-remote octopus
    pack-objects parse-remote patch-id prune pull push rebase
    relink rename repack request-pull reset resolve revert
    send-email shortlog show-branch status tag verify-tag
    whatchanged
EOF
