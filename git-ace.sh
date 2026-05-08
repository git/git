#!/bin/sh

USAGE="<command> [<args>]
   or: ace init
   or: ace create [--parent <branch> | --root] <branch> [<start-point>]
   or: ace checkout <branch>
   or: ace resolve <branch>
   or: ace rename <old> <new>
   or: ace status
   or: ace delete [--force] [--with-descendants] <branch>
   or: ace hg export-bookmarks [<path>]
   or: ace hg import-bookmarks [<path>]
   or: ace hg export-named-branches [<path>]
   or: ace hg import-named-branches [<path>]
   or: ace agent list
   or: ace agent run <agent> [<branch>] -- [<arg>...]
   or: ace set-parent <branch> <parent>
   or: ace clear-parent <branch>
   or: ace parent [<branch>]
   or: ace children [--recursive] <branch>
   or: ace chain [<branch>]
   or: ace tree [<branch>]
   or: ace rebase-stack <branch>
   or: ace merge-stack <branch>
   or: ace foreach-descendant <branch> -- <command> [<arg>...]"
LONG_USAGE="Manage infinitely nested branch trees with explicit parent-child metadata.

The branch hierarchy is stored under .git/ace/branches and can model any depth.
Slash-delimited branch names such as feature/ui/header are recommended, but the
Ace parent-child metadata is authoritative.

Commands:
  init                create Ace metadata storage for the repository
  create              create a branch and attach it to a parent branch
  checkout            resolve an Ace branch name and check it out
  resolve             print the backing Git branch for a branch name
  rename              rename an Ace branch and rewrite related metadata
  status              show stack-wide status, health, and topology
  delete              delete an Ace branch and optionally its descendants
  hg                  import and export Mercurial-compatible bookmark data
  agent               invoke Claude Code, OpenCode, or another configured agent
  set-parent          assign or replace a branch parent
  clear-parent        remove a branch's parent metadata
  parent              print the parent of a branch
  children            list direct or recursive descendants of a branch
  chain               print the ancestor chain from root to branch
  tree                render the branch tree from the chosen root
  rebase-stack        rebase every descendant onto its current parent
  merge-stack         merge every parent into its descendants
  foreach-descendant  run a command while checking out each descendant"
SUBDIRECTORY_OK=Yes

. git-sh-setup
require_work_tree
cd_to_toplevel

ace_branches_dir=$(git rev-parse --git-path ace/branches) || exit 1

ensure_store () {
	mkdir -p "$ace_branches_dir" || die "could not create Ace metadata directory"
}

ensure_branch_name () {
	git check-ref-format --branch "$1" >/dev/null 2>&1 ||
		die "invalid branch name: $1"
}

ensure_local_branch () {
	git show-ref --verify --quiet "refs/heads/$1" ||
		die "unknown local branch: $1"
}

current_branch () {
	git symbolic-ref --quiet --short HEAD ||
		die "Ace commands require a checked out branch"
}

branch_dir () {
	printf '%s/%s\n' "$ace_branches_dir" "$1"
}

list_ace_branches () {
	test -d "$ace_branches_dir" || return 0
	find "$ace_branches_dir" -type f -name ref -print | sort | while read -r ace_ref_path
	do
		ace_virtual_name=${ace_ref_path#"$ace_branches_dir"/}
		ace_virtual_name=${ace_virtual_name%/ref}
		printf '%s\n' "$ace_virtual_name"
	done | sort
}

parent_file () {
	printf '%s/parent\n' "$(branch_dir "$1")"
}

ref_file () {
	printf '%s/ref\n' "$(branch_dir "$1")"
}

branch_parent () {
	ace_parent_file=$(parent_file "$1")
	test -f "$ace_parent_file" || return 0
	sed -n '1p' "$ace_parent_file"
}

branch_ref () {
	ace_ref_path=$(ref_file "$1")
	test -f "$ace_ref_path" || return 0
	sed -n '1p' "$ace_ref_path"
}

set_branch_ref () {
	ace_branch_name=$1
	ace_real_ref=$2
	ace_ref_path=$(ref_file "$ace_branch_name")
	mkdir -p "$(dirname "$ace_ref_path")" || die "could not prepare Ace ref metadata for $ace_branch_name"
	printf '%s\n' "$ace_real_ref" >"$ace_ref_path" || die "could not write Ace ref metadata for $ace_branch_name"
}

is_ace_branch () {
	test -f "$(ref_file "$1")"
}

virtual_branch_for_real () {
	ace_real_ref=$1
	test -d "$ace_branches_dir" || return 0
	find "$ace_branches_dir" -type f -name ref -print | sort | while read -r ace_ref_path
	do
		ace_virtual_name=${ace_ref_path#"$ace_branches_dir"/}
		ace_virtual_name=${ace_virtual_name%/ref}
		ace_stored_ref=$(sed -n '1p' "$ace_ref_path")
		if test "$ace_stored_ref" = "$ace_real_ref"
		then
			printf '%s\n' "$ace_virtual_name"
			break
		fi
	done
}

resolve_real_branch () {
	ace_name=$1
	ace_real_ref=$(branch_ref "$ace_name")
	if test -n "$ace_real_ref"
	then
		printf '%s\n' "$ace_real_ref"
		return 0
	fi
	ensure_local_branch "$ace_name"
	printf '%s\n' "$ace_name"
}

current_display_branch () {
	ace_current_real=$(current_branch)
	ace_virtual_name=$(virtual_branch_for_real "$ace_current_real")
	if test -n "$ace_virtual_name"
	then
		printf '%s\n' "$ace_virtual_name"
	else
		printf '%s\n' "$ace_current_real"
	fi
}

make_real_branch_name () {
	ace_virtual_name=$1
	ace_slug=$(printf '%s' "$ace_virtual_name" | tr '/:@ ' '____' | tr -cd 'A-Za-z0-9._-')
	ace_hash=$(printf '%s' "$ace_virtual_name" | git hash-object --stdin | cut -c1-12)
	test -n "$ace_slug" || ace_slug=branch
	printf 'ace/%s-%s\n' "$ace_slug" "$ace_hash"
}

inferred_parent_name () {
	case "$1" in
	*/*)
		printf '%s\n' "${1%/*}"
		;;
	*)
		return 1
		;;
	esac
}

replace_branch_prefix () {
	ace_old_prefix=$1
	ace_new_prefix=$2
	ace_branch_name=$3
	case "$ace_branch_name" in
	"$ace_old_prefix")
		printf '%s\n' "$ace_new_prefix"
		;;
	"$ace_old_prefix"/*)
		printf '%s/%s\n' "$ace_new_prefix" "${ace_branch_name#"$ace_old_prefix"/}"
		;;
	*)
		printf '%s\n' "$ace_branch_name"
		;;
	esac
}

ensure_known_branch () {
	if is_ace_branch "$1"
	then
		return 0
	fi
	ensure_local_branch "$1"
}

agent_command () {
	ace_agent_name=$1
	ace_agent_command=$(git config --get "ace.agent.$ace_agent_name.command")
	if test -n "$ace_agent_command"
	then
		printf '%s\n' "$ace_agent_command"
	else
		printf '%s\n' "$ace_agent_name"
	fi
}

run_agent () {
	ace_agent_name=$1
	ace_branch_name=$2
	shift 2
	ace_real_branch=$(resolve_real_branch "$ace_branch_name") || return 1
	ace_parent_name=$(branch_parent "$ace_branch_name")
	ace_agent_command=$(agent_command "$ace_agent_name")
	ACE_AGENT_NAME=$ace_agent_name
	ACE_BRANCH=$ace_branch_name
	ACE_REAL_BRANCH=$ace_real_branch
	ACE_PARENT_BRANCH=$ace_parent_name
	ACE_REPOSITORY_ROOT=$(git rev-parse --show-toplevel)
	ACE_VCS_BACKEND=git
	ACE_INTEROP_TARGETS=git,mercurial
	export ACE_AGENT_NAME ACE_BRANCH ACE_REAL_BRANCH ACE_PARENT_BRANCH
	export ACE_REPOSITORY_ROOT ACE_VCS_BACKEND ACE_INTEROP_TARGETS
	"$ace_agent_command" "$@"
}

export_hg_bookmarks () {
	ace_output_path=$1
	: >"$ace_output_path" || die "could not write Mercurial bookmark export: $ace_output_path"
	for ace_branch_name in $(list_ace_branches)
	do
		ace_real_branch=$(resolve_real_branch "$ace_branch_name") || return 1
		ace_oid=$(git rev-parse --verify "$ace_real_branch") || return 1
		printf '%s %s\n' "$ace_oid" "$ace_branch_name" >>"$ace_output_path" ||
			die "could not append bookmark export for $ace_branch_name"
	done
}

list_root_ace_branches () {
	for ace_branch_name in $(list_ace_branches)
	do
		ace_parent_name=$(branch_parent "$ace_branch_name")
		test -n "$ace_parent_name" || printf '%s\n' "$ace_branch_name"
	done
}

export_hg_named_branches () {
	ace_output_path=$1
	: >"$ace_output_path" || die "could not write Mercurial named branch export: $ace_output_path"
	for ace_branch_name in $(list_root_ace_branches)
	do
		ace_real_branch=$(resolve_real_branch "$ace_branch_name") || return 1
		ace_oid=$(git rev-parse --verify "$ace_real_branch") || return 1
		printf '%s %s\n' "$ace_oid" "$ace_branch_name" >>"$ace_output_path" ||
			die "could not append named branch export for $ace_branch_name"
	done
}

import_hg_bookmarks () {
	ace_input_path=$1
	test -f "$ace_input_path" || die "Mercurial bookmark file not found: $ace_input_path"
	ensure_store
	while IFS=' ' read -r ace_oid ace_branch_name
	do
		test -n "$ace_oid" || continue
		test -n "$ace_branch_name" || die "invalid Mercurial bookmark line: missing bookmark name"
		git rev-parse --verify "$ace_oid^{commit}" >/dev/null 2>&1 ||
			die "unknown commit for Mercurial bookmark $ace_branch_name: $ace_oid"
		if is_ace_branch "$ace_branch_name"
		then
			ace_real_branch=$(resolve_real_branch "$ace_branch_name") || exit 1
		else
			ensure_branch_name "$ace_branch_name"
			ace_real_branch=$(make_real_branch_name "$ace_branch_name")
			set_branch_ref "$ace_branch_name" "$ace_real_branch"
		fi
		git update-ref "refs/heads/$ace_real_branch" "$ace_oid" ||
			die "could not update backing branch for Mercurial bookmark $ace_branch_name"
		ace_inferred_parent=$(inferred_parent_name "$ace_branch_name") || ace_inferred_parent=
		if test -n "$ace_inferred_parent" && is_ace_branch "$ace_inferred_parent"
		then
			set_branch_parent "$ace_branch_name" "$ace_inferred_parent"
		fi
	done <"$ace_input_path"
}

import_hg_named_branches () {
	ace_input_path=$1
	test -f "$ace_input_path" || die "Mercurial named branch file not found: $ace_input_path"
	ensure_store
	while IFS=' ' read -r ace_oid ace_branch_name
	do
		test -n "$ace_oid" || continue
		test -n "$ace_branch_name" || die "invalid Mercurial named branch line: missing branch name"
		git rev-parse --verify "$ace_oid^{commit}" >/dev/null 2>&1 ||
			die "unknown commit for Mercurial named branch $ace_branch_name: $ace_oid"
		if is_ace_branch "$ace_branch_name"
		then
			ace_real_branch=$(resolve_real_branch "$ace_branch_name") || exit 1
		else
			ensure_branch_name "$ace_branch_name"
			ace_real_branch=$(make_real_branch_name "$ace_branch_name")
			set_branch_ref "$ace_branch_name" "$ace_real_branch"
			clear_branch_parent "$ace_branch_name"
		fi
		git update-ref "refs/heads/$ace_real_branch" "$ace_oid" ||
			die "could not update backing branch for Mercurial named branch $ace_branch_name"
		clear_branch_parent "$ace_branch_name"
	done <"$ace_input_path"
}

set_branch_parent () {
	ace_child_name=$1
	ace_parent_name=$2
	ace_parent_file=$(parent_file "$ace_child_name")
	mkdir -p "$(dirname "$ace_parent_file")" || die "could not prepare Ace metadata for $ace_child_name"
	printf '%s\n' "$ace_parent_name" >"$ace_parent_file" || die "could not write parent metadata for $ace_child_name"
}

clear_branch_parent () {
	rm -f "$(parent_file "$1")" || die "could not clear parent metadata for $1"
}

rename_ace_branch () {
	ace_old_name=$1
	ace_new_name=$2
	ace_old_dir=$(branch_dir "$ace_old_name")
	ace_new_dir=$(branch_dir "$ace_new_name")
	test -d "$ace_old_dir" || die "unknown Ace branch: $ace_old_name"
	if is_ace_branch "$ace_new_name"
	then
		die "Ace branch already exists: $ace_new_name"
	fi
	ensure_branch_name "$ace_new_name"
	ace_real_branch=$(resolve_real_branch "$ace_old_name") || exit 1
	ace_new_real_branch=$(make_real_branch_name "$ace_new_name")
	git branch -m "$ace_real_branch" "$ace_new_real_branch" || exit 1
	mkdir -p "$(dirname "$ace_new_dir")" || die "could not prepare Ace metadata directory for $ace_new_name"
	mv "$ace_old_dir" "$ace_new_dir" || die "could not rename Ace metadata for $ace_old_name"
	set_branch_ref "$ace_new_name" "$ace_new_real_branch"
	for ace_branch_name in $(list_ace_branches)
	do
		ace_parent_name=$(branch_parent "$ace_branch_name")
		test -n "$ace_parent_name" || continue
		ace_rewritten_parent=$(replace_branch_prefix "$ace_old_name" "$ace_new_name" "$ace_parent_name")
		if test "$ace_parent_name" != "$ace_rewritten_parent"
		then
			set_branch_parent "$ace_branch_name" "$ace_rewritten_parent"
		fi
	done
	ace_new_parent=$(branch_parent "$ace_new_name")
	if test -n "$ace_new_parent"
	then
		ace_rewritten_parent=$(replace_branch_prefix "$ace_old_name" "$ace_new_name" "$ace_new_parent")
		if test "$ace_new_parent" != "$ace_rewritten_parent"
		then
			set_branch_parent "$ace_new_name" "$ace_rewritten_parent"
		fi
	fi
}

delete_ace_branch () {
	ace_force_delete=$1
	ace_with_descendants=$2
	ace_branch_name=$3
	ace_real_branch=$(resolve_real_branch "$ace_branch_name") || exit 1
	ace_descendants=$(print_descendants "$ace_branch_name")
	if test -n "$ace_descendants" && test -z "$ace_with_descendants"
	then
		die "Ace branch has descendants; use --with-descendants to delete the whole subtree"
	fi
	ace_current_real=$(current_branch)
	ace_parent_name=$(branch_parent "$ace_branch_name")
	ace_fallback_branch=
	ace_delete_current=
	if test "$ace_current_real" = "$ace_real_branch"
	then
		ace_delete_current=1
	fi
	if test -z "$ace_delete_current"
	then
		for ace_descendant_name in $ace_descendants
		do
			if test "$ace_current_real" = "$(resolve_real_branch "$ace_descendant_name")"
			then
				ace_delete_current=1
				break
			fi
		done
	fi
	if test -n "$ace_delete_current"
	then
		if test -n "$ace_parent_name"
		then
			ace_fallback_branch=$(resolve_real_branch "$ace_parent_name") || exit 1
		else
			for ace_candidate in $(git for-each-ref --format='%(refname:short)' refs/heads)
			do
				test "$ace_candidate" != "$ace_real_branch" && {
					ace_fallback_branch=$ace_candidate
					break
				}
			done
		fi
		test -n "$ace_fallback_branch" || die "cannot delete the current branch without another branch to check out"
		git checkout "$ace_fallback_branch" >/dev/null 2>&1 || exit 1
	fi
	ace_delete_args=-d
	test -n "$ace_force_delete" && ace_delete_args=-D
	if test -n "$ace_with_descendants"
	then
		for ace_descendant_name in $(print_descendants "$ace_branch_name" | sort -r)
		do
			git branch "$ace_delete_args" "$(resolve_real_branch "$ace_descendant_name")" || exit 1
			rm -rf "$(branch_dir "$ace_descendant_name")" || die "could not remove Ace metadata for $ace_descendant_name"
		done
	fi
	git branch "$ace_delete_args" "$ace_real_branch" || exit 1
	rm -rf "$(branch_dir "$ace_branch_name")" || die "could not remove Ace metadata for $ace_branch_name"
	for ace_other_branch in $(list_ace_branches)
	do
		ace_other_parent=$(branch_parent "$ace_other_branch")
		test "$ace_other_parent" = "$ace_branch_name" && clear_branch_parent "$ace_other_branch"
	done
	return 0
}

list_children () {
	ace_parent_name=$1
	test -d "$ace_branches_dir" || return 0
	find "$ace_branches_dir" -type f -name parent -print | sort | while read -r ace_parent_file
	do
		ace_child_name=${ace_parent_file#"$ace_branches_dir"/}
		ace_child_name=${ace_child_name%/parent}
		ace_recorded_parent=$(sed -n '1p' "$ace_parent_file")
		test "$ace_recorded_parent" = "$ace_parent_name" && printf '%s\n' "$ace_child_name"
	done
}

print_descendants () {
	ace_branch_name=$1
	for ace_child_name in $(list_children "$ace_branch_name")
	do
		printf '%s\n' "$ace_child_name"
		print_descendants "$ace_child_name" || return 1
	done
}

has_ancestor () {
	ace_target_name=$1
	ace_branch_name=$2
	while :
	do
		ace_parent_name=$(branch_parent "$ace_branch_name")
		test -n "$ace_parent_name" || return 1
		test "$ace_parent_name" = "$ace_target_name" && return 0
		ace_branch_name=$ace_parent_name
	done
}

assert_no_cycle () {
	ace_child_name=$1
	ace_parent_name=$2
	test "$ace_child_name" = "$ace_parent_name" &&
		die "refusing to create cycle: $ace_child_name cannot parent itself"
	has_ancestor "$ace_child_name" "$ace_parent_name" &&
		die "refusing to create cycle: $ace_parent_name already descends from $ace_child_name"
}

print_chain () {
	ace_chain_branch=$1
	ace_chain_parent=$(branch_parent "$ace_chain_branch")
	if test -n "$ace_chain_parent"
	then
		(print_chain "$ace_chain_parent") || return 1
	fi
	printf '%s\n' "$ace_chain_branch"
}

print_tree () {
	ace_branch_name=$1
	ace_depth=$2
	indent=''
	ace_count=$ace_depth
	while test "$ace_count" -gt 0
	do
		indent="$indent  "
		ace_count=$((ace_count - 1))
	done
	printf '%s%s\n' "$indent" "$ace_branch_name"
	for ace_child_name in $(list_children "$ace_branch_name")
	do
		print_tree "$ace_child_name" $((ace_depth + 1)) || return 1
	done
}


print_status_tree () {
	ace_branch_name=$1
	ace_depth=$2
	ace_parent_name=$3
	indent=""
	ace_count=$ace_depth
	while test "$ace_count" -gt 0
	do
		indent="$indent  "
		ace_count=$((ace_count - 1))
	done
	ace_real_branch=$(resolve_real_branch "$ace_branch_name" 2>/dev/null)
	sync_info=""
	if test -z "$ace_real_branch" || ! git show-ref --verify --quiet "refs/heads/$ace_real_branch"
	then
		sync_info="[DANGLING]"
		DRIFT_WARNINGS="${DRIFT_WARNINGS}⚠️  $ace_branch_name is dangling (backing ref missing).
"
	else
		if test -n "$ace_parent_name"
		then
			ace_real_parent=$(resolve_real_branch "$ace_parent_name" 2>/dev/null)
			if test -n "$ace_real_parent" && git show-ref --verify --quiet "refs/heads/$ace_real_parent"
			then
				ahead=$(git rev-list --count "${ace_real_parent}..${ace_real_branch}" 2>/dev/null || echo "?")
				behind=$(git rev-list --count "${ace_real_branch}..${ace_real_parent}" 2>/dev/null || echo "?")
				sync_info="(ahead $ahead, behind $behind)"
				base=$(git merge-base "$ace_real_parent" "$ace_real_branch" 2>/dev/null)
				parent_oid=$(git rev-parse "$ace_real_parent" 2>/dev/null)
				if test "$base" != "$parent_oid"
				then
					sync_info="$sync_info [needs rebase]"
					DRIFT_WARNINGS="${DRIFT_WARNINGS}⚠️  $ace_branch_name is out of sync with $ace_parent_name. Run \`git ace rebase-stack $ace_parent_name\`.
"
				else
					sync_info="$sync_info [synced]"
				fi
			else
				sync_info="[ORPHANED PARENT]"
			fi
		else
			sync_info="[root]"
		fi
	fi
	marker=""
	test "$ace_branch_name" = "$CURRENT_VIRTUAL_BRANCH" && marker=" *CURRENT*"
	if test "$ace_depth" -eq 0
	then
		printf "%s%s %s%s
" "$indent" "$ace_branch_name" "$sync_info" "$marker"
	else
		printf "%s│
" "$indent"
		printf "%s└── %s %s%s
" "$indent" "$ace_branch_name" "$sync_info" "$marker"
	fi
	if test "$ace_branch_name" = "$CURRENT_VIRTUAL_BRANCH"
	then
		uncommitted=$(git status --porcelain | grep -c "^" || echo 0)
		test "$uncommitted" -gt 0 && printf "%s      ~ %s uncommitted changes
" "$indent" "$uncommitted"
	fi
	for ace_child_name in $(list_children "$ace_branch_name")
	do
		print_status_tree "$ace_child_name" $((ace_depth + 1)) "$ace_branch_name" || return 1
	done
}

run_descendant_command () {
	ace_branch_name=$1
	shift
	for ace_child_name in $(list_children "$ace_branch_name")
	do
		git checkout "$(resolve_real_branch "$ace_child_name")" >/dev/null 2>&1 || return 1
		"$@" || return 1
		run_descendant_command "$ace_child_name" "$@" || return 1
	done
}

rebase_descendants () {
	ace_branch_name=$1
	for ace_child_name in $(list_children "$ace_branch_name")
	do
		git checkout "$(resolve_real_branch "$ace_child_name")" >/dev/null 2>&1 || return 1
		git rebase "$(resolve_real_branch "$ace_branch_name")" || return 1
		rebase_descendants "$ace_child_name" || return 1
	done
}

merge_descendants () {
	ace_branch_name=$1
	for ace_child_name in $(list_children "$ace_branch_name")
	do
		git checkout "$(resolve_real_branch "$ace_child_name")" >/dev/null 2>&1 || return 1
		git merge --no-edit "$(resolve_real_branch "$ace_branch_name")" || return 1
		merge_descendants "$ace_child_name" || return 1
	done
}

cmd=${1:-}
test -n "$cmd" || usage
shift

case "$cmd" in
init)
	ensure_store
	;;
create)
	ensure_store
	parent=
	root_branch=
	while test $# -gt 0
	do
		case "$1" in
		--parent)
			test $# -ge 2 || usage
			parent=$2
			shift 2
			;;
		--root)
			root_branch=1
			shift
			;;
		--)
			shift
			break
			;;
		-*)
			die "unknown option for create: $1"
			;;
		*)
			break
			;;
		esac
	done
	test $# -ge 1 && test $# -le 2 || usage
	branch=$1
	start_point=$2
	ensure_branch_name "$branch"
	test -n "$parent" || test -n "$root_branch" || parent=$(current_display_branch)
	if test -n "$parent"
	then
		ensure_known_branch "$parent"
		assert_no_cycle "$branch" "$parent"
		if test -n "$start_point"
		then
			start_point=$(resolve_real_branch "$start_point")
		else
			start_point=$(resolve_real_branch "$parent")
		fi
	else
		if test -n "$start_point"
		then
			start_point=$(resolve_real_branch "$start_point")
		else
			start_point=$(current_branch)
		fi
	fi
	ace_real_branch=$(make_real_branch_name "$branch")
	git branch "$ace_real_branch" "$start_point" || exit 1
	set_branch_ref "$branch" "$ace_real_branch"
	if test -n "$parent"
	then
		set_branch_parent "$branch" "$parent"
	fi
	;;
checkout)
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	git checkout "$(resolve_real_branch "$1")" || exit 1
	;;
resolve)
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	resolve_real_branch "$1"
	;;
rename)
	test $# -eq 2 || usage
	ensure_known_branch "$1"
	rename_ace_branch "$1" "$2"
	;;

status)
	ensure_store
	CURRENT_VIRTUAL_BRANCH=$(current_display_branch)
	DRIFT_WARNINGS=""
	if ! is_ace_branch "$CURRENT_VIRTUAL_BRANCH"
	then
		git status
		exit 0
	fi
	branch=$CURRENT_VIRTUAL_BRANCH
	while parent=$(branch_parent "$branch") && test -n "$parent"
	do
		branch=$parent
	done
	echo "Stack Status:"
	echo ""
	print_status_tree "$branch" 0 ""
	test -n "$DRIFT_WARNINGS" && printf "
Health:
%b" "$DRIFT_WARNINGS"
	;;
delete)
	ace_force_delete=
	ace_with_descendants=
	while test $# -gt 0
	do
		case "$1" in
		--force)
			ace_force_delete=1
			shift
			;;
		--with-descendants)
			ace_with_descendants=1
			shift
			;;
		--)
			shift
			break
			;;
		-*)
			die "unknown option for delete: $1"
			;;
		*)
			break
			;;
		esac
	done
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	delete_ace_branch "$ace_force_delete" "$ace_with_descendants" "$1"
	;;
hg)
	test $# -ge 1 || usage
	case "$1" in
	export-bookmarks)
		ace_output_path=${2:-.hg-bookmarks}
		test $# -le 2 || usage
		export_hg_bookmarks "$ace_output_path"
		;;
	import-bookmarks)
		ace_input_path=${2:-.hg-bookmarks}
		test $# -le 2 || usage
		import_hg_bookmarks "$ace_input_path"
		;;
	export-named-branches)
		ace_output_path=${2:-.hg-named-branches}
		test $# -le 2 || usage
		export_hg_named_branches "$ace_output_path"
		;;
	import-named-branches)
		ace_input_path=${2:-.hg-named-branches}
		test $# -le 2 || usage
		import_hg_named_branches "$ace_input_path"
		;;
	*)
		die "unknown Ace Mercurial command: $1"
		;;
	esac
	;;
agent)
	test $# -ge 1 || usage
	case "$1" in
	list)
		printf '%s\n' claude-code
		printf '%s\n' opencode
		;;
	run)
		test $# -ge 3 || usage
		ace_agent_name=$2
		shift 2
		if test $# -ge 2 && test "$2" = "--"
		then
			ace_agent_branch=$1
			shift
		else
			ace_agent_branch=$(current_display_branch)
		fi
		test "$1" = "--" || usage
		shift
		ensure_known_branch "$ace_agent_branch"
		run_agent "$ace_agent_name" "$ace_agent_branch" "$@" || exit 1
		;;
	*)
		die "unknown Ace agent command: $1"
		;;
	esac
	;;
set-parent)
	ensure_store
	test $# -eq 2 || usage
	branch=$1
	parent=$2
	ensure_known_branch "$branch"
	ensure_known_branch "$parent"
	assert_no_cycle "$branch" "$parent"
	set_branch_parent "$branch" "$parent"
	;;
clear-parent)
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	clear_branch_parent "$1"
	;;
parent)
	branch=${1:-$(current_display_branch)}
	test $# -le 1 || usage
	ensure_known_branch "$branch"
	branch_parent "$branch"
	;;
children)
	recursive=
	if test "$1" = "--recursive"
	then
		recursive=1
		shift
	fi
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	if test -n "$recursive"
	then
		print_descendants "$1"
	else
		list_children "$1"
	fi
	;;
chain)
	branch=${1:-$(current_display_branch)}
	test $# -le 1 || usage
	ensure_known_branch "$branch"
	print_chain "$branch"
	;;
tree)
	branch=${1:-$(current_display_branch)}
	test $# -le 1 || usage
	ensure_known_branch "$branch"
	while parent=$(branch_parent "$branch") && test -n "$parent"
	do
		branch=$parent
	done
	print_tree "$branch" 0
	;;
rebase-stack)
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	original=$(current_branch)
	rebase_descendants "$1"
	status=$?
	git checkout "$original" >/dev/null 2>&1 || status=$?
	exit "$status"
	;;
merge-stack)
	test $# -eq 1 || usage
	ensure_known_branch "$1"
	original=$(current_branch)
	merge_descendants "$1"
	status=$?
	git checkout "$original" >/dev/null 2>&1 || status=$?
	exit "$status"
	;;
foreach-descendant)
	test $# -ge 3 || usage
	branch=$1
	shift
	test "$1" = "--" || usage
	shift
	ensure_known_branch "$branch"
	original=$(current_branch)
	run_descendant_command "$branch" "$@"
	status=$?
	git checkout "$original" >/dev/null 2>&1 || status=$?
	exit "$status"
	;;
*)
	die "unknown Ace command: $cmd"
	;;
esac
