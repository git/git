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
#    3) You may want to make sure the git executable is available
#       in your PATH before this script is sourced, as some caching
#       is performed while the script loads.  If git isn't found
#       at source time then all lookups will be done on demand,
#       which may be slightly slower.
#
#    4) Consider changing your PS1 to also show the current branch:
#        PS1='[\u@\h \W$(__git_ps1 " (%s)")]\$ '
#
#       The argument to __git_ps1 will be displayed only if you
#       are currently in a git repository.  The %s token will be
#       the name of the current branch.
#

__gitdir ()
{
	if [ -z "$1" ]; then
		if [ -n "$__git_dir" ]; then
			echo "$__git_dir"
		elif [ -d .git ]; then
			echo .git
		else
			git rev-parse --git-dir 2>/dev/null
		fi
	elif [ -d "$1/.git" ]; then
		echo "$1/.git"
	else
		echo "$1"
	fi
}

__git_ps1 ()
{
	local b="$(git symbolic-ref HEAD 2>/dev/null)"
	if [ -n "$b" ]; then
		if [ -n "$1" ]; then
			printf "$1" "${b##refs/heads/}"
		else
			printf " (%s)" "${b##refs/heads/}"
		fi
	fi
}

__git_heads ()
{
	local cmd i is_hash=y dir="$(__gitdir "$1")"
	if [ -d "$dir" ]; then
		for i in $(git --git-dir="$dir" \
			for-each-ref --format='%(refname)' \
			refs/heads ); do
			echo "${i#refs/heads/}"
		done
		return
	fi
	for i in $(git-ls-remote "$1" 2>/dev/null); do
		case "$is_hash,$i" in
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/heads/*) is_hash=y; echo "${i#refs/heads/}" ;;
		n,*) is_hash=y; echo "$i" ;;
		esac
	done
}

__git_refs ()
{
	local cmd i is_hash=y dir="$(__gitdir "$1")"
	if [ -d "$dir" ]; then
		if [ -e "$dir/HEAD" ]; then echo HEAD; fi
		for i in $(git --git-dir="$dir" \
			for-each-ref --format='%(refname)' \
			refs/tags refs/heads refs/remotes); do
			case "$i" in
				refs/tags/*)    echo "${i#refs/tags/}" ;;
				refs/heads/*)   echo "${i#refs/heads/}" ;;
				refs/remotes/*) echo "${i#refs/remotes/}" ;;
				*)              echo "$i" ;;
			esac
		done
		return
	fi
	for i in $(git-ls-remote "$dir" 2>/dev/null); do
		case "$is_hash,$i" in
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/tags/*) is_hash=y; echo "${i#refs/tags/}" ;;
		n,refs/heads/*) is_hash=y; echo "${i#refs/heads/}" ;;
		n,refs/remotes/*) is_hash=y; echo "${i#refs/remotes/}" ;;
		n,*) is_hash=y; echo "$i" ;;
		esac
	done
}

__git_refs2 ()
{
	local i
	for i in $(__git_refs "$1"); do
		echo "$i:$i"
	done
}

__git_refs_remotes ()
{
	local cmd i is_hash=y
	for i in $(git-ls-remote "$1" 2>/dev/null); do
		case "$is_hash,$i" in
		n,refs/heads/*)
			is_hash=y
			echo "$i:refs/remotes/$1/${i#refs/heads/}"
			;;
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/tags/*) is_hash=y;;
		n,*) is_hash=y; ;;
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
	for i in $(git --git-dir="$d" config --list); do
		case "$i" in
		remote.*.url=*)
			i="${i#remote.}"
			echo "${i/.url=*/}"
			;;
		esac
	done
}

__git_merge_strategies ()
{
	if [ -n "$__git_merge_strategylist" ]; then
		echo "$__git_merge_strategylist"
		return
	fi
	sed -n "/^all_strategies='/{
		s/^all_strategies='//
		s/'//
		p
		q
		}" "$(git --exec-path)/git-merge"
}
__git_merge_strategylist=
__git_merge_strategylist="$(__git_merge_strategies 2>/dev/null)"

__git_complete_file ()
{
	local pfx ls ref cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	?*:*)
		ref="${cur%%:*}"
		cur="${cur#*:}"
		case "$cur" in
		?*/*)
			pfx="${cur%/*}"
			cur="${cur##*/}"
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

__git_complete_revlist ()
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

__git_commands ()
{
	if [ -n "$__git_commandlist" ]; then
		echo "$__git_commandlist"
		return
	fi
	local i IFS=" "$'\n'
	for i in $(git help -a|egrep '^ ')
	do
		case $i in
		check-ref-format) : plumbing;;
		commit-tree)      : plumbing;;
		convert-objects)  : plumbing;;
		cvsserver)        : daemon;;
		daemon)           : daemon;;
		fetch-pack)       : plumbing;;
		hash-object)      : plumbing;;
		http-*)           : transport;;
		index-pack)       : plumbing;;
		local-fetch)      : plumbing;;
		mailinfo)         : plumbing;;
		mailsplit)        : plumbing;;
		merge-*)          : plumbing;;
		mktree)           : plumbing;;
		mktag)            : plumbing;;
		pack-objects)     : plumbing;;
		pack-redundant)   : plumbing;;
		pack-refs)        : plumbing;;
		parse-remote)     : plumbing;;
		patch-id)         : plumbing;;
		peek-remote)      : plumbing;;
		read-tree)        : plumbing;;
		receive-pack)     : plumbing;;
		rerere)           : plumbing;;
		rev-list)         : plumbing;;
		rev-parse)        : plumbing;;
		runstatus)        : plumbing;;
		sh-setup)         : internal;;
		shell)            : daemon;;
		send-pack)        : plumbing;;
		show-index)       : plumbing;;
		ssh-*)            : transport;;
		stripspace)       : plumbing;;
		symbolic-ref)     : plumbing;;
		unpack-file)      : plumbing;;
		unpack-objects)   : plumbing;;
		update-ref)       : plumbing;;
		update-server-info) : daemon;;
		upload-archive)   : plumbing;;
		upload-pack)      : plumbing;;
		write-tree)       : plumbing;;
		*) echo $i;;
		esac
	done
}
__git_commandlist=
__git_commandlist="$(__git_commands 2>/dev/null)"

__git_aliases ()
{
	local i IFS=$'\n'
	for i in $(git --git-dir="$(__gitdir)" config --list); do
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
		config --get "alias.$1")
	for word in $cmdline; do
		if [ "${word##-*}" ]; then
			echo $word
			return
		fi
	done
}

__git_whitespacelist="nowarn warn error error-all strip"

_git_am ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	if [ -d .dotest ]; then
		COMPREPLY=($(compgen -W "
			--skip --resolved
			" -- "$cur"))
		return
	fi
	case "$cur" in
	--whitespace=*)
		COMPREPLY=($(compgen -W "$__git_whitespacelist" \
			-- "${cur##--whitespace=}"))
		return
		;;
	--*)
		COMPREPLY=($(compgen -W "
			--signoff --utf8 --binary --3way --interactive
			--whitespace=
			" -- "$cur"))
		return
	esac
	COMPREPLY=()
}

_git_apply ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--whitespace=*)
		COMPREPLY=($(compgen -W "$__git_whitespacelist" \
			-- "${cur##--whitespace=}"))
		return
		;;
	--*)
		COMPREPLY=($(compgen -W "
			--stat --numstat --summary --check --index
			--cached --index-info --reverse --reject --unidiff-zero
			--apply --no-add --exclude=
			--whitespace= --inaccurate-eof --verbose
			" -- "$cur"))
		return
	esac
	COMPREPLY=()
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

_git_cherry_pick ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		COMPREPLY=($(compgen -W "
			--edit --no-commit
			" -- "$cur"))
		;;
	*)
		COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
		;;
	esac
}

_git_commit ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		COMPREPLY=($(compgen -W "
			--all --author= --signoff --verify --no-verify
			--edit --amend --include --only
			" -- "$cur"))
		return
	esac
	COMPREPLY=()
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
			cur="${cur#*:}"
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

_git_format_patch ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		COMPREPLY=($(compgen -W "
			--stdout --attach --thread
			--output-directory
			--numbered --start-number
			--keep-subject
			--signoff
			--in-reply-to=
			--full-index --binary
			" -- "$cur"))
		return
		;;
	esac
	__git_complete_revlist
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
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--pretty=*)
		COMPREPLY=($(compgen -W "
			oneline short medium full fuller email raw
			" -- "${cur##--pretty=}"))
		return
		;;
	--*)
		COMPREPLY=($(compgen -W "
			--max-count= --max-age= --since= --after=
			--min-age= --before= --until=
			--root --not --topo-order --date-order
			--no-merges
			--abbrev-commit --abbrev=
			--relative-date
			--author= --committer= --grep=
			--all-match
			--pretty= --name-status --name-only
			" -- "$cur"))
		return
		;;
	esac
	__git_complete_revlist
}

_git_merge ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "${COMP_WORDS[COMP_CWORD-1]}" in
	-s|--strategy)
		COMPREPLY=($(compgen -W "$(__git_merge_strategies)" -- "$cur"))
		return
	esac
	case "$cur" in
	--strategy=*)
		COMPREPLY=($(compgen -W "$(__git_merge_strategies)" \
			-- "${cur##--strategy=}"))
		return
		;;
	--*)
		COMPREPLY=($(compgen -W "
			--no-commit --no-summary --squash --strategy
			" -- "$cur"))
		return
	esac
	COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
}

_git_merge_base ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
}

_git_name_rev ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	COMPREPLY=($(compgen -W "--tags --all --stdin" -- "$cur"))
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
			cur="${cur#*:}"
			COMPREPLY=($(compgen -W "$(__git_refs "$remote")" -- "$cur"))
			;;
		*)
			COMPREPLY=($(compgen -W "$(__git_refs2)" -- "$cur"))
			;;
		esac
		;;
	esac
}

_git_rebase ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	if [ -d .dotest ]; then
		COMPREPLY=($(compgen -W "
			--continue --skip --abort
			" -- "$cur"))
		return
	fi
	case "${COMP_WORDS[COMP_CWORD-1]}" in
	-s|--strategy)
		COMPREPLY=($(compgen -W "$(__git_merge_strategies)" -- "$cur"))
		return
	esac
	case "$cur" in
	--strategy=*)
		COMPREPLY=($(compgen -W "$(__git_merge_strategies)" \
			-- "${cur##--strategy=}"))
		return
		;;
	--*)
		COMPREPLY=($(compgen -W "
			--onto --merge --strategy
			" -- "$cur"))
		return
	esac
	COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
}

_git_config ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	local prv="${COMP_WORDS[COMP_CWORD-1]}"
	case "$prv" in
	branch.*.remote)
		COMPREPLY=($(compgen -W "$(__git_remotes)" -- "$cur"))
		return
		;;
	branch.*.merge)
		COMPREPLY=($(compgen -W "$(__git_refs)" -- "$cur"))
		return
		;;
	remote.*.fetch)
		local remote="${prv#remote.}"
		remote="${remote%.fetch}"
		COMPREPLY=($(compgen -W "$(__git_refs_remotes "$remote")" \
			-- "$cur"))
		return
		;;
	remote.*.push)
		local remote="${prv#remote.}"
		remote="${remote%.push}"
		COMPREPLY=($(compgen -W "$(git --git-dir="$(__gitdir)" \
			for-each-ref --format='%(refname):%(refname)' \
			refs/heads)" -- "$cur"))
		return
		;;
	*.*)
		COMPREPLY=()
		return
		;;
	esac
	case "$cur" in
	--*)
		COMPREPLY=($(compgen -W "
			--global --list --replace-all
			--get --get-all --get-regexp
			--unset --unset-all
			" -- "$cur"))
		return
		;;
	branch.*.*)
		local pfx="${cur%.*}."
		cur="${cur##*.}"
		COMPREPLY=($(compgen -P "$pfx" -W "remote merge" -- "$cur"))
		return
		;;
	branch.*)
		local pfx="${cur%.*}."
		cur="${cur#*.}"
		COMPREPLY=($(compgen -P "$pfx" -S . \
			-W "$(__git_heads)" -- "$cur"))
		return
		;;
	remote.*.*)
		local pfx="${cur%.*}."
		cur="${cur##*.}"
		COMPREPLY=($(compgen -P "$pfx" -W "url fetch push" -- "$cur"))
		return
		;;
	remote.*)
		local pfx="${cur%.*}."
		cur="${cur#*.}"
		COMPREPLY=($(compgen -P "$pfx" -S . \
			-W "$(__git_remotes)" -- "$cur"))
		return
		;;
	esac
	COMPREPLY=($(compgen -W "
		apply.whitespace
		core.fileMode
		core.gitProxy
		core.ignoreStat
		core.preferSymlinkRefs
		core.logAllRefUpdates
		core.repositoryFormatVersion
		core.sharedRepository
		core.warnAmbiguousRefs
		core.compression
		core.legacyHeaders
		i18n.commitEncoding
		i18n.logOutputEncoding
		diff.color
		color.diff
		diff.renameLimit
		diff.renames
		pager.color
		color.pager
		status.color
		color.status
		log.showroot
		show.difftree
		showbranch.default
		whatchanged.difftree
		http.sslVerify
		http.sslCert
		http.sslKey
		http.sslCAInfo
		http.sslCAPath
		http.maxRequests
		http.lowSpeedLimit http.lowSpeedTime
		http.noEPSV
		pack.window
		repack.useDeltaBaseOffset
		pull.octopus pull.twohead
		merge.summary
		receive.unpackLimit
		receive.denyNonFastForwards
		user.name user.email
		tar.umask
		gitcvs.enabled
		gitcvs.logfile
		branch. remote.
	" -- "$cur"))
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
	case "$cur" in
	--pretty=*)
		COMPREPLY=($(compgen -W "
			oneline short medium full fuller email raw
			" -- "${cur##--pretty=}"))
		return
		;;
	--*)
		COMPREPLY=($(compgen -W "--pretty=" -- "$cur"))
		return
		;;
	esac
	__git_complete_file
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
		COMPREPLY=($(compgen -W "
			--git-dir= --version --exec-path
			$(__git_commands)
			$(__git_aliases)
			" -- "${COMP_WORDS[COMP_CWORD]}"))
		return;
	fi

	local expansion=$(__git_aliased_command "$command")
	[ "$expansion" ] && command="$expansion"

	case "$command" in
	am)          _git_am ;;
	apply)       _git_apply ;;
	branch)      _git_branch ;;
	cat-file)    _git_cat_file ;;
	checkout)    _git_checkout ;;
	cherry-pick) _git_cherry_pick ;;
	commit)      _git_commit ;;
	config)      _git_config ;;
	diff)        _git_diff ;;
	diff-tree)   _git_diff_tree ;;
	fetch)       _git_fetch ;;
	format-patch) _git_format_patch ;;
	log)         _git_log ;;
	ls-remote)   _git_ls_remote ;;
	ls-tree)     _git_ls_tree ;;
	merge)       _git_merge;;
	merge-base)  _git_merge_base ;;
	name-rev)    _git_name_rev ;;
	pull)        _git_pull ;;
	push)        _git_push ;;
	rebase)      _git_rebase ;;
	repo-config) _git_config ;;
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
complete -o default            -F _git_am git-am
complete -o default            -F _git_apply git-apply
complete -o default            -F _git_branch git-branch
complete -o default -o nospace -F _git_cat_file git-cat-file
complete -o default            -F _git_checkout git-checkout
complete -o default            -F _git_cherry_pick git-cherry-pick
complete -o default            -F _git_commit git-commit
complete -o default -o nospace -F _git_diff git-diff
complete -o default            -F _git_diff_tree git-diff-tree
complete -o default -o nospace -F _git_fetch git-fetch
complete -o default -o nospace -F _git_format_patch git-format-patch
complete -o default -o nospace -F _git_log git-log
complete -o default            -F _git_ls_remote git-ls-remote
complete -o default -o nospace -F _git_ls_tree git-ls-tree
complete -o default            -F _git_merge git-merge
complete -o default            -F _git_merge_base git-merge-base
complete -o default            -F _git_name_rev git-name-rev
complete -o default -o nospace -F _git_pull git-pull
complete -o default -o nospace -F _git_push git-push
complete -o default            -F _git_rebase git-rebase
complete -o default            -F _git_config git-config
complete -o default            -F _git_reset git-reset
complete -o default -o nospace -F _git_show git-show
complete -o default -o nospace -F _git_log git-show-branch
complete -o default -o nospace -F _git_log git-whatchanged

# The following are necessary only for Cygwin, and only are needed
# when the user has tab-completed the executable name and consequently
# included the '.exe' suffix.
#
if [ Cygwin = "$(uname -o 2>/dev/null)" ]; then
complete -o default            -F _git_apply git-apply.exe
complete -o default -o nospace -F _git git.exe
complete -o default            -F _git_branch git-branch.exe
complete -o default -o nospace -F _git_cat_file git-cat-file.exe
complete -o default -o nospace -F _git_diff git-diff.exe
complete -o default -o nospace -F _git_diff_tree git-diff-tree.exe
complete -o default -o nospace -F _git_format_patch git-format-patch.exe
complete -o default -o nospace -F _git_log git-log.exe
complete -o default -o nospace -F _git_ls_tree git-ls-tree.exe
complete -o default            -F _git_merge_base git-merge-base.exe
complete -o default            -F _git_name_rev git-name-rev.exe
complete -o default -o nospace -F _git_push git-push.exe
complete -o default            -F _git_config git-config
complete -o default -o nospace -F _git_show git-show.exe
complete -o default -o nospace -F _git_log git-show-branch.exe
complete -o default -o nospace -F _git_log git-whatchanged.exe
fi
