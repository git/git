#
# bash completion support for core Git.
#
# Copyright (C) 2006,2007 Shawn O. Pearce <spearce@spearce.org>
# Conceptually based on gitcompletion (http://gitweb.hawaga.org.uk/).
# Distributed under the GNU General Public License, version 2.0.
#
# The contained completion routines provide support for completing:
#
#    *) local and remote branch names
#    *) local and remote tag names
#    *) .git/remotes file names
#    *) git 'subcommands'
#    *) tree paths within 'ref:path/to/file' expressions
#    *) common --long-options
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
# To submit patches:
#
#    *) Read Documentation/SubmittingPatches
#    *) Send all patches to the current maintainer:
#
#       "Shawn O. Pearce" <spearce@spearce.org>
#
#    *) Always CC the Git mailing list:
#
#       git@vger.kernel.org
#

case "$COMP_WORDBREAKS" in
*:*) : great ;;
*)   COMP_WORDBREAKS="$COMP_WORDBREAKS:"
esac

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
	local g="$(git rev-parse --git-dir 2>/dev/null)"
	if [ -n "$g" ]; then
		local r
		local b
		if [ -d "$g/rebase-apply" ]
		then
			if test -f "$g/rebase-apply/rebasing"
			then
				r="|REBASE"
			elif test -f "$g/rebase-apply/applying"
			then
				r="|AM"
			else
				r="|AM/REBASE"
			fi
			b="$(git symbolic-ref HEAD 2>/dev/null)"
		elif [ -f "$g/rebase-merge/interactive" ]
		then
			r="|REBASE-i"
			b="$(cat "$g/rebase-merge/head-name")"
		elif [ -d "$g/rebase-merge" ]
		then
			r="|REBASE-m"
			b="$(cat "$g/rebase-merge/head-name")"
		elif [ -f "$g/MERGE_HEAD" ]
		then
			r="|MERGING"
			b="$(git symbolic-ref HEAD 2>/dev/null)"
		else
			if [ -f "$g/BISECT_LOG" ]
			then
				r="|BISECTING"
			fi
			if ! b="$(git symbolic-ref HEAD 2>/dev/null)"
			then
				if ! b="$(git describe --exact-match HEAD 2>/dev/null)"
				then
					b="$(cut -c1-7 "$g/HEAD")..."
				fi
			fi
		fi

		if [ -n "$1" ]; then
			printf "$1" "${b##refs/heads/}$r"
		else
			printf " (%s)" "${b##refs/heads/}$r"
		fi
	fi
}

__gitcomp_1 ()
{
	local c IFS=' '$'\t'$'\n'
	for c in $1; do
		case "$c$2" in
		--*=*) printf %s$'\n' "$c$2" ;;
		*.)    printf %s$'\n' "$c$2" ;;
		*)     printf %s$'\n' "$c$2 " ;;
		esac
	done
}

__gitcomp ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	if [ $# -gt 2 ]; then
		cur="$3"
	fi
	case "$cur" in
	--*=)
		COMPREPLY=()
		;;
	*)
		local IFS=$'\n'
		COMPREPLY=($(compgen -P "$2" \
			-W "$(__gitcomp_1 "$1" "$4")" \
			-- "$cur"))
		;;
	esac
}

__git_heads ()
{
	local cmd i is_hash=y dir="$(__gitdir "$1")"
	if [ -d "$dir" ]; then
		git --git-dir="$dir" for-each-ref --format='%(refname:short)' \
			refs/heads
		return
	fi
	for i in $(git ls-remote "$1" 2>/dev/null); do
		case "$is_hash,$i" in
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/heads/*) is_hash=y; echo "${i#refs/heads/}" ;;
		n,*) is_hash=y; echo "$i" ;;
		esac
	done
}

__git_tags ()
{
	local cmd i is_hash=y dir="$(__gitdir "$1")"
	if [ -d "$dir" ]; then
		git --git-dir="$dir" for-each-ref --format='%(refname:short)' \
			refs/tags
		return
	fi
	for i in $(git ls-remote "$1" 2>/dev/null); do
		case "$is_hash,$i" in
		y,*) is_hash=n ;;
		n,*^{}) is_hash=y ;;
		n,refs/tags/*) is_hash=y; echo "${i#refs/tags/}" ;;
		n,*) is_hash=y; echo "$i" ;;
		esac
	done
}

__git_refs ()
{
	local i is_hash=y dir="$(__gitdir "$1")"
	local cur="${COMP_WORDS[COMP_CWORD]}" format refs
	if [ -d "$dir" ]; then
		case "$cur" in
		refs|refs/*)
			format="refname"
			refs="${cur%/*}"
			;;
		*)
			if [ -e "$dir/HEAD" ]; then echo HEAD; fi
			format="refname:short"
			refs="refs/tags refs/heads refs/remotes"
			;;
		esac
		git --git-dir="$dir" for-each-ref --format="%($format)" \
			$refs
		return
	fi
	for i in $(git ls-remote "$dir" 2>/dev/null); do
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
	for i in $(git ls-remote "$1" 2>/dev/null); do
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
	git merge -s help 2>&1 |
	sed -n -e '/[Aa]vailable strategies are: /,/^$/{
		s/\.$//
		s/.*://
		s/^[ 	]*//
		s/[ 	]*$//
		p
	}'
}
__git_merge_strategylist=
__git_merge_strategylist=$(__git_merge_strategies 2>/dev/null)

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

		case "$COMP_WORDBREAKS" in
		*:*) : great ;;
		*)   pfx="$ref:$pfx" ;;
		esac

		local IFS=$'\n'
		COMPREPLY=($(compgen -P "$pfx" \
			-W "$(git --git-dir="$(__gitdir)" ls-tree "$ls" \
				| sed '/^100... blob /{
				           s,^.*	,,
				           s,$, ,
				       }
				       /^120000 blob /{
				           s,^.*	,,
				           s,$, ,
				       }
				       /^040000 tree /{
				           s,^.*	,,
				           s,$,/,
				       }
				       s/^.*	//')" \
			-- "$cur"))
		;;
	*)
		__gitcomp "$(__git_refs)"
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
		__gitcomp "$(__git_refs)" "$pfx" "$cur"
		;;
	*..*)
		pfx="${cur%..*}.."
		cur="${cur#*..}"
		__gitcomp "$(__git_refs)" "$pfx" "$cur"
		;;
	*)
		__gitcomp "$(__git_refs)"
		;;
	esac
}

__git_all_commands ()
{
	if [ -n "$__git_all_commandlist" ]; then
		echo "$__git_all_commandlist"
		return
	fi
	local i IFS=" "$'\n'
	for i in $(git help -a|egrep '^ ')
	do
		case $i in
		*--*)             : helper pattern;;
		*) echo $i;;
		esac
	done
}
__git_all_commandlist=
__git_all_commandlist="$(__git_all_commands 2>/dev/null)"

__git_porcelain_commands ()
{
	if [ -n "$__git_porcelain_commandlist" ]; then
		echo "$__git_porcelain_commandlist"
		return
	fi
	local i IFS=" "$'\n'
	for i in "help" $(__git_all_commands)
	do
		case $i in
		*--*)             : helper pattern;;
		applymbox)        : ask gittus;;
		applypatch)       : ask gittus;;
		archimport)       : import;;
		cat-file)         : plumbing;;
		check-attr)       : plumbing;;
		check-ref-format) : plumbing;;
		checkout-index)   : plumbing;;
		commit-tree)      : plumbing;;
		count-objects)    : infrequent;;
		cvsexportcommit)  : export;;
		cvsimport)        : import;;
		cvsserver)        : daemon;;
		daemon)           : daemon;;
		diff-files)       : plumbing;;
		diff-index)       : plumbing;;
		diff-tree)        : plumbing;;
		fast-import)      : import;;
		fast-export)      : export;;
		fsck-objects)     : plumbing;;
		fetch-pack)       : plumbing;;
		fmt-merge-msg)    : plumbing;;
		for-each-ref)     : plumbing;;
		hash-object)      : plumbing;;
		http-*)           : transport;;
		index-pack)       : plumbing;;
		init-db)          : deprecated;;
		local-fetch)      : plumbing;;
		lost-found)       : infrequent;;
		ls-files)         : plumbing;;
		ls-remote)        : plumbing;;
		ls-tree)          : plumbing;;
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
		prune)            : plumbing;;
		prune-packed)     : plumbing;;
		quiltimport)      : import;;
		read-tree)        : plumbing;;
		receive-pack)     : plumbing;;
		reflog)           : plumbing;;
		repo-config)      : deprecated;;
		rerere)           : plumbing;;
		rev-list)         : plumbing;;
		rev-parse)        : plumbing;;
		runstatus)        : plumbing;;
		sh-setup)         : internal;;
		shell)            : daemon;;
		show-ref)         : plumbing;;
		send-pack)        : plumbing;;
		show-index)       : plumbing;;
		ssh-*)            : transport;;
		stripspace)       : plumbing;;
		symbolic-ref)     : plumbing;;
		tar-tree)         : deprecated;;
		unpack-file)      : plumbing;;
		unpack-objects)   : plumbing;;
		update-index)     : plumbing;;
		update-ref)       : plumbing;;
		update-server-info) : daemon;;
		upload-archive)   : plumbing;;
		upload-pack)      : plumbing;;
		write-tree)       : plumbing;;
		var)              : infrequent;;
		verify-pack)      : infrequent;;
		verify-tag)       : plumbing;;
		*) echo $i;;
		esac
	done
}
__git_porcelain_commandlist=
__git_porcelain_commandlist="$(__git_porcelain_commands 2>/dev/null)"

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

__git_find_subcommand ()
{
	local word subcommand c=1

	while [ $c -lt $COMP_CWORD ]; do
		word="${COMP_WORDS[c]}"
		for subcommand in $1; do
			if [ "$subcommand" = "$word" ]; then
				echo "$subcommand"
				return
			fi
		done
		c=$((++c))
	done
}

__git_has_doubledash ()
{
	local c=1
	while [ $c -lt $COMP_CWORD ]; do
		if [ "--" = "${COMP_WORDS[c]}" ]; then
			return 0
		fi
		c=$((++c))
	done
	return 1
}

__git_whitespacelist="nowarn warn error error-all fix"

_git_am ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}" dir="$(__gitdir)"
	if [ -d "$dir"/rebase-apply ]; then
		__gitcomp "--skip --resolved --abort"
		return
	fi
	case "$cur" in
	--whitespace=*)
		__gitcomp "$__git_whitespacelist" "" "${cur##--whitespace=}"
		return
		;;
	--*)
		__gitcomp "
			--signoff --utf8 --binary --3way --interactive
			--whitespace=
			"
		return
	esac
	COMPREPLY=()
}

_git_apply ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--whitespace=*)
		__gitcomp "$__git_whitespacelist" "" "${cur##--whitespace=}"
		return
		;;
	--*)
		__gitcomp "
			--stat --numstat --summary --check --index
			--cached --index-info --reverse --reject --unidiff-zero
			--apply --no-add --exclude=
			--whitespace= --inaccurate-eof --verbose
			"
		return
	esac
	COMPREPLY=()
}

_git_add ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--interactive --refresh --patch --update --dry-run
			--ignore-errors
			"
		return
	esac
	COMPREPLY=()
}

_git_archive ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--format=*)
		__gitcomp "$(git archive --list)" "" "${cur##--format=}"
		return
		;;
	--remote=*)
		__gitcomp "$(__git_remotes)" "" "${cur##--remote=}"
		return
		;;
	--*)
		__gitcomp "
			--format= --list --verbose
			--prefix= --remote= --exec=
			"
		return
		;;
	esac
	__git_complete_file
}

_git_bisect ()
{
	__git_has_doubledash && return

	local subcommands="start bad good skip reset visualize replay log run"
	local subcommand="$(__git_find_subcommand "$subcommands")"
	if [ -z "$subcommand" ]; then
		__gitcomp "$subcommands"
		return
	fi

	case "$subcommand" in
	bad|good|reset|skip)
		__gitcomp "$(__git_refs)"
		;;
	*)
		COMPREPLY=()
		;;
	esac
}

_git_branch ()
{
	local i c=1 only_local_ref="n" has_r="n"

	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		-d|-m)	only_local_ref="y" ;;
		-r)	has_r="y" ;;
		esac
		c=$((++c))
	done

	case "${COMP_WORDS[COMP_CWORD]}" in
	--*=*)	COMPREPLY=() ;;
	--*)
		__gitcomp "
			--color --no-color --verbose --abbrev= --no-abbrev
			--track --no-track --contains --merged --no-merged
			"
		;;
	*)
		if [ $only_local_ref = "y" -a $has_r = "n" ]; then
			__gitcomp "$(__git_heads)"
		else
			__gitcomp "$(__git_refs)"
		fi
		;;
	esac
}

_git_bundle ()
{
	local cmd="${COMP_WORDS[2]}"
	case "$COMP_CWORD" in
	2)
		__gitcomp "create list-heads verify unbundle"
		;;
	3)
		# looking for a file
		;;
	*)
		case "$cmd" in
			create)
				__git_complete_revlist
			;;
		esac
		;;
	esac
}

_git_checkout ()
{
	__git_has_doubledash && return

	__gitcomp "$(__git_refs)"
}

_git_cherry ()
{
	__gitcomp "$(__git_refs)"
}

_git_cherry_pick ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--edit --no-commit"
		;;
	*)
		__gitcomp "$(__git_refs)"
		;;
	esac
}

_git_clean ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--dry-run --quiet"
		return
		;;
	esac
	COMPREPLY=()
}

_git_clone ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--local
			--no-hardlinks
			--shared
			--reference
			--quiet
			--no-checkout
			--bare
			--mirror
			--origin
			--upload-pack
			--template=
			--depth
			"
		return
		;;
	esac
	COMPREPLY=()
}

_git_commit ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--all --author= --signoff --verify --no-verify
			--edit --amend --include --only --interactive
			"
		return
	esac
	COMPREPLY=()
}

_git_describe ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--all --tags --contains --abbrev= --candidates=
			--exact-match --debug --long --match --always
			"
		return
	esac
	__gitcomp "$(__git_refs)"
}

_git_diff ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--cached --stat --numstat --shortstat --summary
			--patch-with-stat --name-only --name-status --color
			--no-color --color-words --no-renames --check
			--full-index --binary --abbrev --diff-filter=
			--find-copies-harder --pickaxe-all --pickaxe-regex
			--text --ignore-space-at-eol --ignore-space-change
			--ignore-all-space --exit-code --quiet --ext-diff
			--no-ext-diff
			--no-prefix --src-prefix= --dst-prefix=
			--base --ours --theirs
			"
		return
		;;
	esac
	__git_complete_file
}

_git_fetch ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"

	if [ "$COMP_CWORD" = 2 ]; then
		__gitcomp "$(__git_remotes)"
	else
		case "$cur" in
		*:*)
			local pfx=""
			case "$COMP_WORDBREAKS" in
			*:*) : great ;;
			*)   pfx="${cur%%:*}:" ;;
			esac
			__gitcomp "$(__git_refs)" "$pfx" "${cur#*:}"
			;;
		*)
			__gitcomp "$(__git_refs2 "${COMP_WORDS[2]}")"
			;;
		esac
	fi
}

_git_format_patch ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--stdout --attach --thread
			--output-directory
			--numbered --start-number
			--numbered-files
			--keep-subject
			--signoff
			--in-reply-to=
			--full-index --binary
			--not --all
			--cover-letter
			--no-prefix --src-prefix= --dst-prefix=
			"
		return
		;;
	esac
	__git_complete_revlist
}

_git_gc ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--prune --aggressive"
		return
		;;
	esac
	COMPREPLY=()
}

_git_grep ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--cached
			--text --ignore-case --word-regexp --invert-match
			--full-name
			--extended-regexp --basic-regexp --fixed-strings
			--files-with-matches --name-only
			--files-without-match
			--count
			--and --or --not --all-match
			"
		return
		;;
	esac
	COMPREPLY=()
}

_git_help ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--all --info --man --web"
		return
		;;
	esac
	__gitcomp "$(__git_all_commands)
		attributes cli core-tutorial cvs-migration
		diffcore gitk glossary hooks ignore modules
		repository-layout tutorial tutorial-2
		workflows
		"
}

_git_init ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--shared=*)
		__gitcomp "
			false true umask group all world everybody
			" "" "${cur##--shared=}"
		return
		;;
	--*)
		__gitcomp "--quiet --bare --template= --shared --shared="
		return
		;;
	esac
	COMPREPLY=()
}

_git_ls_files ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--cached --deleted --modified --others --ignored
			--stage --directory --no-empty-directory --unmerged
			--killed --exclude= --exclude-from=
			--exclude-per-directory= --exclude-standard
			--error-unmatch --with-tree= --full-name
			--abbrev --ignored --exclude-per-directory
			"
		return
		;;
	esac
	COMPREPLY=()
}

_git_ls_remote ()
{
	__gitcomp "$(__git_remotes)"
}

_git_ls_tree ()
{
	__git_complete_file
}

_git_log ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--pretty=*)
		__gitcomp "
			oneline short medium full fuller email raw
			" "" "${cur##--pretty=}"
		return
		;;
	--date=*)
		__gitcomp "
			relative iso8601 rfc2822 short local default
		" "" "${cur##--date=}"
		return
		;;
	--*)
		__gitcomp "
			--max-count= --max-age= --since= --after=
			--min-age= --before= --until=
			--root --topo-order --date-order --reverse
			--no-merges --follow
			--abbrev-commit --abbrev=
			--relative-date --date=
			--author= --committer= --grep=
			--all-match
			--pretty= --name-status --name-only --raw
			--not --all
			--left-right --cherry-pick
			--graph
			--stat --numstat --shortstat
			--decorate --diff-filter=
			--color-words --walk-reflogs
			--parents --children --full-history
			--merge
			"
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
		__gitcomp "$(__git_merge_strategies)"
		return
	esac
	case "$cur" in
	--strategy=*)
		__gitcomp "$(__git_merge_strategies)" "" "${cur##--strategy=}"
		return
		;;
	--*)
		__gitcomp "
			--no-commit --no-stat --log --no-log --squash --strategy
			"
		return
	esac
	__gitcomp "$(__git_refs)"
}

_git_mergetool ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--tool=*)
		__gitcomp "
			kdiff3 tkdiff meld xxdiff emerge
			vimdiff gvimdiff ecmerge opendiff
			" "" "${cur##--tool=}"
		return
		;;
	--*)
		__gitcomp "--tool="
		return
		;;
	esac
	COMPREPLY=()
}

_git_merge_base ()
{
	__gitcomp "$(__git_refs)"
}

_git_mv ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--dry-run"
		return
		;;
	esac
	COMPREPLY=()
}

_git_name_rev ()
{
	__gitcomp "--tags --all --stdin"
}

_git_pull ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"

	if [ "$COMP_CWORD" = 2 ]; then
		__gitcomp "$(__git_remotes)"
	else
		__gitcomp "$(__git_refs "${COMP_WORDS[2]}")"
	fi
}

_git_push ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"

	if [ "$COMP_CWORD" = 2 ]; then
		__gitcomp "$(__git_remotes)"
	else
		case "$cur" in
		*:*)
			local pfx=""
			case "$COMP_WORDBREAKS" in
			*:*) : great ;;
			*)   pfx="${cur%%:*}:" ;;
			esac

			__gitcomp "$(__git_refs "${COMP_WORDS[2]}")" "$pfx" "${cur#*:}"
			;;
		+*)
			__gitcomp "$(__git_refs)" + "${cur#+}"
			;;
		*)
			__gitcomp "$(__git_refs)"
			;;
		esac
	fi
}

_git_rebase ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}" dir="$(__gitdir)"
	if [ -d "$dir"/rebase-apply ] || [ -d "$dir"/rebase-merge ]; then
		__gitcomp "--continue --skip --abort"
		return
	fi
	case "${COMP_WORDS[COMP_CWORD-1]}" in
	-s|--strategy)
		__gitcomp "$(__git_merge_strategies)"
		return
	esac
	case "$cur" in
	--strategy=*)
		__gitcomp "$(__git_merge_strategies)" "" "${cur##--strategy=}"
		return
		;;
	--*)
		__gitcomp "--onto --merge --strategy --interactive"
		return
	esac
	__gitcomp "$(__git_refs)"
}

_git_send_email ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--bcc --cc --cc-cmd --chain-reply-to --compose
			--dry-run --envelope-sender --from --identity
			--in-reply-to --no-chain-reply-to --no-signed-off-by-cc
			--no-suppress-from --no-thread --quiet
			--signed-off-by-cc --smtp-pass --smtp-server
			--smtp-server-port --smtp-ssl --smtp-user --subject
			--suppress-cc --suppress-from --thread --to
			--validate --no-validate"
		return
		;;
	esac
	COMPREPLY=()
}

_git_config ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	local prv="${COMP_WORDS[COMP_CWORD-1]}"
	case "$prv" in
	branch.*.remote)
		__gitcomp "$(__git_remotes)"
		return
		;;
	branch.*.merge)
		__gitcomp "$(__git_refs)"
		return
		;;
	remote.*.fetch)
		local remote="${prv#remote.}"
		remote="${remote%.fetch}"
		__gitcomp "$(__git_refs_remotes "$remote")"
		return
		;;
	remote.*.push)
		local remote="${prv#remote.}"
		remote="${remote%.push}"
		__gitcomp "$(git --git-dir="$(__gitdir)" \
			for-each-ref --format='%(refname):%(refname)' \
			refs/heads)"
		return
		;;
	pull.twohead|pull.octopus)
		__gitcomp "$(__git_merge_strategies)"
		return
		;;
	color.branch|color.diff|color.status)
		__gitcomp "always never auto"
		return
		;;
	color.*.*)
		__gitcomp "
			black red green yellow blue magenta cyan white
			bold dim ul blink reverse
			"
		return
		;;
	*.*)
		COMPREPLY=()
		return
		;;
	esac
	case "$cur" in
	--*)
		__gitcomp "
			--global --system --file=
			--list --replace-all
			--get --get-all --get-regexp
			--add --unset --unset-all
			--remove-section --rename-section
			"
		return
		;;
	branch.*.*)
		local pfx="${cur%.*}."
		cur="${cur##*.}"
		__gitcomp "remote merge" "$pfx" "$cur"
		return
		;;
	branch.*)
		local pfx="${cur%.*}."
		cur="${cur#*.}"
		__gitcomp "$(__git_heads)" "$pfx" "$cur" "."
		return
		;;
	remote.*.*)
		local pfx="${cur%.*}."
		cur="${cur##*.}"
		__gitcomp "
			url fetch push skipDefaultUpdate
			receivepack uploadpack tagopt
			" "$pfx" "$cur"
		return
		;;
	remote.*)
		local pfx="${cur%.*}."
		cur="${cur#*.}"
		__gitcomp "$(__git_remotes)" "$pfx" "$cur" "."
		return
		;;
	esac
	__gitcomp "
		apply.whitespace
		core.fileMode
		core.gitProxy
		core.ignoreStat
		core.preferSymlinkRefs
		core.logAllRefUpdates
		core.loosecompression
		core.repositoryFormatVersion
		core.sharedRepository
		core.warnAmbiguousRefs
		core.compression
		core.packedGitWindowSize
		core.packedGitLimit
		clean.requireForce
		color.branch
		color.branch.current
		color.branch.local
		color.branch.remote
		color.branch.plain
		color.diff
		color.diff.plain
		color.diff.meta
		color.diff.frag
		color.diff.old
		color.diff.new
		color.diff.commit
		color.diff.whitespace
		color.pager
		color.status
		color.status.header
		color.status.added
		color.status.changed
		color.status.untracked
		diff.renameLimit
		diff.renames
		fetch.unpackLimit
		format.headers
		format.subjectprefix
		gitcvs.enabled
		gitcvs.logfile
		gitcvs.allbinary
		gitcvs.dbname gitcvs.dbdriver gitcvs.dbuser gitcvs.dbpass
		gitcvs.dbtablenameprefix
		gc.packrefs
		gc.reflogexpire
		gc.reflogexpireunreachable
		gc.rerereresolved
		gc.rerereunresolved
		http.sslVerify
		http.sslCert
		http.sslKey
		http.sslCAInfo
		http.sslCAPath
		http.maxRequests
		http.lowSpeedLimit
		http.lowSpeedTime
		http.noEPSV
		i18n.commitEncoding
		i18n.logOutputEncoding
		log.showroot
		merge.tool
		merge.summary
		merge.verbosity
		pack.window
		pack.depth
		pack.windowMemory
		pack.compression
		pack.deltaCacheSize
		pack.deltaCacheLimit
		pull.octopus
		pull.twohead
		repack.useDeltaBaseOffset
		showbranch.default
		tar.umask
		transfer.unpackLimit
		receive.unpackLimit
		receive.denyNonFastForwards
		user.name
		user.email
		user.signingkey
		branch. remote.
	"
}

_git_remote ()
{
	local subcommands="add rm show prune update"
	local subcommand="$(__git_find_subcommand "$subcommands")"
	if [ -z "$subcommand" ]; then
		__gitcomp "$subcommands"
		return
	fi

	case "$subcommand" in
	rm|show|prune)
		__gitcomp "$(__git_remotes)"
		;;
	update)
		local i c='' IFS=$'\n'
		for i in $(git --git-dir="$(__gitdir)" config --list); do
			case "$i" in
			remotes.*)
				i="${i#remotes.}"
				c="$c ${i/=*/}"
				;;
			esac
		done
		__gitcomp "$c"
		;;
	*)
		COMPREPLY=()
		;;
	esac
}

_git_reset ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--mixed --hard --soft"
		return
		;;
	esac
	__gitcomp "$(__git_refs)"
}

_git_revert ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--edit --mainline --no-edit --no-commit --signoff"
		return
		;;
	esac
	__gitcomp "$(__git_refs)"
}

_git_rm ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "--cached --dry-run --ignore-unmatch --quiet"
		return
		;;
	esac
	COMPREPLY=()
}

_git_shortlog ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--max-count= --max-age= --since= --after=
			--min-age= --before= --until=
			--no-merges
			--author= --committer= --grep=
			--all-match
			--not --all
			--numbered --summary
			"
		return
		;;
	esac
	__git_complete_revlist
}

_git_show ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--pretty=*)
		__gitcomp "
			oneline short medium full fuller email raw
			" "" "${cur##--pretty=}"
		return
		;;
	--*)
		__gitcomp "--pretty="
		return
		;;
	esac
	__git_complete_file
}

_git_show_branch ()
{
	local cur="${COMP_WORDS[COMP_CWORD]}"
	case "$cur" in
	--*)
		__gitcomp "
			--all --remotes --topo-order --current --more=
			--list --independent --merge-base --no-name
			--sha1-name --topics --reflog
			"
		return
		;;
	esac
	__git_complete_revlist
}

_git_stash ()
{
	local subcommands='save list show apply clear drop pop create branch'
	local subcommand="$(__git_find_subcommand "$subcommands")"
	if [ -z "$subcommand" ]; then
		__gitcomp "$subcommands"
	else
		local cur="${COMP_WORDS[COMP_CWORD]}"
		case "$subcommand,$cur" in
		save,--*)
			__gitcomp "--keep-index"
			;;
		apply,--*)
			__gitcomp "--index"
			;;
		show,--*|drop,--*|pop,--*|branch,--*)
			COMPREPLY=()
			;;
		show,*|apply,*|drop,*|pop,*|branch,*)
			__gitcomp "$(git --git-dir="$(__gitdir)" stash list \
					| sed -n -e 's/:.*//p')"
			;;
		*)
			COMPREPLY=()
			;;
		esac
	fi
}

_git_submodule ()
{
	__git_has_doubledash && return

	local subcommands="add status init update summary foreach sync"
	if [ -z "$(__git_find_subcommand "$subcommands")" ]; then
		local cur="${COMP_WORDS[COMP_CWORD]}"
		case "$cur" in
		--*)
			__gitcomp "--quiet --cached"
			;;
		*)
			__gitcomp "$subcommands"
			;;
		esac
		return
	fi
}

_git_svn ()
{
	local subcommands="
		init fetch clone rebase dcommit log find-rev
		set-tree commit-diff info create-ignore propget
		proplist show-ignore show-externals
		"
	local subcommand="$(__git_find_subcommand "$subcommands")"
	if [ -z "$subcommand" ]; then
		__gitcomp "$subcommands"
	else
		local remote_opts="--username= --config-dir= --no-auth-cache"
		local fc_opts="
			--follow-parent --authors-file= --repack=
			--no-metadata --use-svm-props --use-svnsync-props
			--log-window-size= --no-checkout --quiet
			--repack-flags --user-log-author $remote_opts
			"
		local init_opts="
			--template= --shared= --trunk= --tags=
			--branches= --stdlayout --minimize-url
			--no-metadata --use-svm-props --use-svnsync-props
			--rewrite-root= $remote_opts
			"
		local cmt_opts="
			--edit --rmdir --find-copies-harder --copy-similarity=
			"

		local cur="${COMP_WORDS[COMP_CWORD]}"
		case "$subcommand,$cur" in
		fetch,--*)
			__gitcomp "--revision= --fetch-all $fc_opts"
			;;
		clone,--*)
			__gitcomp "--revision= $fc_opts $init_opts"
			;;
		init,--*)
			__gitcomp "$init_opts"
			;;
		dcommit,--*)
			__gitcomp "
				--merge --strategy= --verbose --dry-run
				--fetch-all --no-rebase $cmt_opts $fc_opts
				"
			;;
		set-tree,--*)
			__gitcomp "--stdin $cmt_opts $fc_opts"
			;;
		create-ignore,--*|propget,--*|proplist,--*|show-ignore,--*|\
		show-externals,--*)
			__gitcomp "--revision="
			;;
		log,--*)
			__gitcomp "
				--limit= --revision= --verbose --incremental
				--oneline --show-commit --non-recursive
				--authors-file=
				"
			;;
		rebase,--*)
			__gitcomp "
				--merge --verbose --strategy= --local
				--fetch-all $fc_opts
				"
			;;
		commit-diff,--*)
			__gitcomp "--message= --file= --revision= $cmt_opts"
			;;
		info,--*)
			__gitcomp "--url"
			;;
		*)
			COMPREPLY=()
			;;
		esac
	fi
}

_git_tag ()
{
	local i c=1 f=0
	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		-d|-v)
			__gitcomp "$(__git_tags)"
			return
			;;
		-f)
			f=1
			;;
		esac
		c=$((++c))
	done

	case "${COMP_WORDS[COMP_CWORD-1]}" in
	-m|-F)
		COMPREPLY=()
		;;
	-*|tag)
		if [ $f = 1 ]; then
			__gitcomp "$(__git_tags)"
		else
			COMPREPLY=()
		fi
		;;
	*)
		__gitcomp "$(__git_refs)"
		;;
	esac
}

_git ()
{
	local i c=1 command __git_dir

	while [ $c -lt $COMP_CWORD ]; do
		i="${COMP_WORDS[c]}"
		case "$i" in
		--git-dir=*) __git_dir="${i#--git-dir=}" ;;
		--bare)      __git_dir="." ;;
		--version|-p|--paginate) ;;
		--help) command="help"; break ;;
		*) command="$i"; break ;;
		esac
		c=$((++c))
	done

	if [ -z "$command" ]; then
		case "${COMP_WORDS[COMP_CWORD]}" in
		--*=*) COMPREPLY=() ;;
		--*)   __gitcomp "
			--paginate
			--no-pager
			--git-dir=
			--bare
			--version
			--exec-path
			--work-tree=
			--help
			"
			;;
		*)     __gitcomp "$(__git_porcelain_commands) $(__git_aliases)" ;;
		esac
		return
	fi

	local expansion=$(__git_aliased_command "$command")
	[ "$expansion" ] && command="$expansion"

	case "$command" in
	am)          _git_am ;;
	add)         _git_add ;;
	apply)       _git_apply ;;
	archive)     _git_archive ;;
	bisect)      _git_bisect ;;
	bundle)      _git_bundle ;;
	branch)      _git_branch ;;
	checkout)    _git_checkout ;;
	cherry)      _git_cherry ;;
	cherry-pick) _git_cherry_pick ;;
	clean)       _git_clean ;;
	clone)       _git_clone ;;
	commit)      _git_commit ;;
	config)      _git_config ;;
	describe)    _git_describe ;;
	diff)        _git_diff ;;
	fetch)       _git_fetch ;;
	format-patch) _git_format_patch ;;
	gc)          _git_gc ;;
	grep)        _git_grep ;;
	help)        _git_help ;;
	init)        _git_init ;;
	log)         _git_log ;;
	ls-files)    _git_ls_files ;;
	ls-remote)   _git_ls_remote ;;
	ls-tree)     _git_ls_tree ;;
	merge)       _git_merge;;
	mergetool)   _git_mergetool;;
	merge-base)  _git_merge_base ;;
	mv)          _git_mv ;;
	name-rev)    _git_name_rev ;;
	pull)        _git_pull ;;
	push)        _git_push ;;
	rebase)      _git_rebase ;;
	remote)      _git_remote ;;
	reset)       _git_reset ;;
	revert)      _git_revert ;;
	rm)          _git_rm ;;
	send-email)  _git_send_email ;;
	shortlog)    _git_shortlog ;;
	show)        _git_show ;;
	show-branch) _git_show_branch ;;
	stash)       _git_stash ;;
	submodule)   _git_submodule ;;
	svn)         _git_svn ;;
	tag)         _git_tag ;;
	whatchanged) _git_log ;;
	*)           COMPREPLY=() ;;
	esac
}

_gitk ()
{
	__git_has_doubledash && return

	local cur="${COMP_WORDS[COMP_CWORD]}"
	local g="$(git rev-parse --git-dir 2>/dev/null)"
	local merge=""
	if [ -f $g/MERGE_HEAD ]; then
		merge="--merge"
	fi
	case "$cur" in
	--*)
		__gitcomp "--not --all $merge"
		return
		;;
	esac
	__git_complete_revlist
}

complete -o default -o nospace -F _git git
complete -o default -o nospace -F _gitk gitk

# The following are necessary only for Cygwin, and only are needed
# when the user has tab-completed the executable name and consequently
# included the '.exe' suffix.
#
if [ Cygwin = "$(uname -o 2>/dev/null)" ]; then
complete -o default -o nospace -F _git git.exe
fi
