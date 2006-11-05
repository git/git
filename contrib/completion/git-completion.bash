#
# bash completion support for core Git.
#
# Copyright (C) 2006 Shawn Pearce
# Conceptually based on gitcompletion (http://gitweb.hawaga.org.uk/).
#
# The contained completion routines provide support for completing:
#
#    *) local and remote branch names
#    *) local and remote tag names
#    *) .git/remotes file names
#    *) git 'subcommands'
#    *) tree paths within 'ref:path/to/file' expressions
#
# To use these routines:
#
#    1) Copy this file to somewhere (e.g. ~/.git-completion.sh).
#    2) Added the following line to your .bashrc:
#        source ~/.git-completion.sh
#

__gitdir ()
{
	echo "${__git_dir:-$(git rev-parse --git-dir 2>/dev/null)}"
}

__git_refs ()
{
	local cmd i is_hash=y dir="${1:-$(__gitdir)}"
	if [ -d "$dir" ]; then
		cmd=git-peek-remote
	else
		cmd=git-ls-remote
	fi
	for i in $($cmd "$dir" 2>/dev/null); do
		case "$is_hash,$i" in
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/tags/*) is_hash=y; echo "${i#refs/tags/}" ;;
		n,refs/heads/*) is_hash=y; echo "${i#refs/heads/}" ;;
		n,*) is_hash=y; echo "$i" ;;
		esac
	done
}

__git_refs2 ()
{
	local cmd i is_hash=y dir="${1:-$(__gitdir)}"
	if [ -d "$dir" ]; then
		cmd=git-peek-remote
	else
		cmd=git-ls-remote
	fi
	for i in $($cmd "$dir" 2>/dev/null); do
		case "$is_hash,$i" in
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/tags/*) is_hash=y; echo "${i#refs/tags/}:${i#refs/tags/}" ;;
		n,refs/heads/*) is_hash=y; echo "${i#refs/heads/}:${i#refs/heads/}" ;;
		n,*) is_hash=y; echo "$i:$i" ;;
		esac
	done
}

__git_remotes ()
{
	local i ngoff IFS=$'\n' d="$(__gitdir)"
	shopt -q nullglob || ngoff=1
	shopt -s nullglob
	for i in "$d/remotes"/*; do
		echo ${i#$d/remotes/}
	done
	[ "$ngoff" ] && shopt -u nullglob
	for i in $(git --git-dir="$d" repo-config --list); do
		case "$i" in
		remote.*.url=*)
			i="${i#remote.}"
			echo "${i/.url=*/}"
			;;
		esac
	done
}

__git_complete_file ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	?*:*)
		local pfx ls ref="$(echo "$cur" | sed 's,:.*$,,')"
		cur="$(echo "$cur" | sed 's,^.*:,,')"
		case "$cur" in
		?*/*)
			pfx="$(echo "$cur" | sed 's,/[^/]*$,,')"
			cur="$(echo "$cur" | sed 's,^.*/,,')"
			ls="$ref:$pfx"
			pfx="$pfx/"
			;;
		*)
			ls="$ref"
			;;
	    esac
		COMPREPLY=($(compgen -P "$pfx" \
			-W "$(git --git-dir="$(__gitdir)" ls-tree "$ls" \
				| sed '/^100... blob /s,^.*	,,
				       /^040000 tree /{
				           s,^.*	,,
				           s,$,/,
				       }
				       s/^.*	//')" \
			-- "$cur"))
		;;
	*)
		COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
		;;
	esac
}

__git_aliases ()
{
	local i IFS=$'\n'
	for i in $(git --git-dir="$(__gitdir)" repo-config --list); do
		case "$i" in
		alias.*)
			i="${i#alias.}"
			echo "${i/=*/}"
			;;
		esac
	done
}

__git_aliased_command ()
{
	local word cmdline=$(git --git-dir="$(__gitdir)" \
		repo-config --get "alias.$1")
	for word in $cmdline; do
		if [ "${word##-*}" ]; then
			echo $word
			return
		fi
	done
}

_git_branch ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "-l -f -d -D $(__git_refs)" -- "$cur"))
}

_git_cat_file ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "${COMP_WORDS[0]},$COMP_CWORD" in
	git-cat-file*,1)
		COMPREPLY=($(compgen -W "-p -t blob tree commit tag" -- "$cur"))
		;;
	git,2)
		COMPREPLY=($(compgen -W "-p -t blob tree commit tag" -- "$cur"))
		;;
	*)
		__git_complete_file
		;;
	esac
}

_git_checkout ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "-l -b $(__git_refs)" -- "$cur"))
}

_git_diff ()
{
	__git_complete_file
}

_git_diff_tree ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "-r -p -M $(__git_refs)" -- "$cur"))
}

_git_fetch ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"

	case "${COMP_WORDS[0]},$COMP_CWORD" in
	git-fetch*,1)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		;;
	git,2)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		;;
	*)
		case "$cur" in
		*:*)
	        cur=$(echo "$cur" | sed 's/^.*://')
			COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
			;;
		*)
			local remote
			case "${COMP_WORDS[0]}" in
			git-fetch) remote="${COMP_WORDS[1]}" ;;
			git)       remote="${COMP_WORDS[2]}" ;;
			esac
			COMPREPLY=($(compgen -W "$(__git_refs2 "$remote")" -- "$cur"))
			;;
		esac
		;;
	esac
}

_git_ls_remote ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
}

_git_ls_tree ()
{
	__git_complete_file
}

_git_log ()
{
	local pfx cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	*...*)
		pfx="${cur%...*}..."
		cur="${cur#*...}"
		COMPREPLY=($(compgen -P "$pfx" -W "$(__git_refs)" -- "$cur"))
		;;
	*..*)
		pfx="${cur%..*}.."
		cur="${cur#*..}"
		COMPREPLY=($(compgen -P "$pfx" -W "$(__git_refs)" -- "$cur"))
		;;
	*)
		COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
		;;
	esac
}

_git_merge_base ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
}

_git_pull ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"

	case "${COMP_WORDS[0]},$COMP_CWORD" in
	git-pull*,1)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		;;
	git,2)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		;;
	*)
		local remote
		case "${COMP_WORDS[0]}" in
		git-pull)  remote="${COMP_WORDS[1]}" ;;
		git)       remote="${COMP_WORDS[2]}" ;;
		esac
		COMPREPLY=($(compgen -W "$(__git_refs "$remote")" -- "$cur"))
		;;
	esac
}

_git_push ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"

	case "${COMP_WORDS[0]},$COMP_CWORD" in
	git-push*,1)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		;;
	git,2)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		;;
	*)
		case "$cur" in
		*:*)
			local remote
			case "${COMP_WORDS[0]}" in
			git-push)  remote="${COMP_WORDS[1]}" ;;
			git)       remote="${COMP_WORDS[2]}" ;;
			esac
	        cur=$(echo "$cur" | sed 's/^.*://')
			COMPREPLY=($(compgen -W "$(__git_refs "$remote")" -- "$cur"))
			;;
		*)
			COMPREPLY=($(compgen -W "$(__git_refs2)" -- "$cur"))
			;;
		esac
		;;
	esac
}

_git_reset ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	local opt="--mixed --hard --soft"
	COMPREPLY=($(compgen -W "$opt $(__git_refs)" -- "$cur"))
}

_git_show ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
}

_git ()
{
	local i c=1 command __git_dir

	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		--git-dir=*) __git_dir="${i#--git-dir=}" ;;
		--bare)      __git_dir="." ;;
		--version|--help|-p|--paginate) ;;
		*) command="$i"; break ;;
		esac
		c=$((++c))
	done

	if [ $c -eq $COMP_CWORD -a -z "$command" ]; then
		COMPREPLY=($(compgen \
			-W "--git-dir= --version \
				$(git help -a|egrep '^ ') \
			    $(__git_aliases)" \
			-- "${COMP_WORDS[COMP_CWORD]}"))
		return;
	fi

	local expansion=$(__git_aliased_command "$command")
	[ "$expansion" ] && command="$expansion"

	case "$command" in
	branch)      _git_branch ;;
	cat-file)    _git_cat_file ;;
	checkout)    _git_checkout ;;
	diff)        _git_diff ;;
	diff-tree)   _git_diff_tree ;;
	fetch)       _git_fetch ;;
	log)         _git_log ;;
	ls-remote)   _git_ls_remote ;;
	ls-tree)     _git_ls_tree ;;
	merge-base)  _git_merge_base ;;
	pull)        _git_pull ;;
	push)        _git_push ;;
	reset)       _git_reset ;;
	show)        _git_show ;;
	show-branch) _git_log ;;
	whatchanged) _git_log ;;
	*)           COMPREPLY=() ;;
	esac
}

_gitk ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "--all $(__git_refs)" -- "$cur"))
}

complete -o default -o nospace -F _git git
complete -o default            -F _gitk gitk
complete -o default            -F _git_branch git-branch
complete -o default -o nospace -F _git_cat_file git-cat-file
complete -o default            -F _git_checkout git-checkout
complete -o default -o nospace -F _git_diff git-diff
complete -o default            -F _git_diff_tree git-diff-tree
complete -o default -o nospace -F _git_fetch git-fetch
complete -o default -o nospace -F _git_log git-log
complete -o default            -F _git_ls_remote git-ls-remote
complete -o default -o nospace -F _git_ls_tree git-ls-tree
complete -o default            -F _git_merge_base git-merge-base
complete -o default -o nospace -F _git_pull git-pull
complete -o default -o nospace -F _git_push git-push
complete -o default            -F _git_reset git-reset
complete -o default            -F _git_show git-show
complete -o default -o nospace -F _git_log git-show-branch
complete -o default -o nospace -F _git_log git-whatchanged

# The following are necessary only for Cygwin, and only are needed
# when the user has tab-completed the executable name and consequently
# included the '.exe' suffix.
#
if [ Cygwin = "$(uname -o 2>/dev/null)" ]; then
complete -o default -o nospace -F _git git.exe
complete -o default            -F _git_branch git-branch.exe
complete -o default -o nospace -F _git_cat_file git-cat-file.exe
complete -o default -o nospace -F _git_diff git-diff.exe
complete -o default -o nospace -F _git_diff_tree git-diff-tree.exe
complete -o default -o nospace -F _git_log git-log.exe
complete -o default -o nospace -F _git_ls_tree git-ls-tree.exe
complete -o default            -F _git_merge_base git-merge-base.exe
complete -o default -o nospace -F _git_push git-push.exe
complete -o default -o nospace -F _git_log git-show-branch.exe
complete -o default -o nospace -F _git_log git-whatchanged.exe
fi
