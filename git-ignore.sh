#!/bin/sh
#
# Copyright (c) 2016, Thurston Stone
#
# unit test: t7900

_verbose=0

SUBDIRECTORY_OK=Yes
OPTIONS_KEEPDASHDASH=
OPTIONS_STUCKLONG=t
# Would be nice to have examples, but rev-parse sees '*' as a symbol to hide everything afterwards
#e,ext		 add relative path for any file of that type (ex. path/to/*.ext)
#E,all-ext	 all files of that extention anywhere (ex. **/*.ext)
#d,dir		 all files under the parent directory (ex. directory/*)
#a,all-file	 all files of that file name (ex. **/filename.ext)
OPTIONS_SPEC="git ignore [options] [file|glob ...]
--
 Miscelleneous
edit		 open the pertinent gitignore with your default text editor (Requires \$EDITOR to be set)
v,verbose	 show verbose output
n,dry-run	 do not actually edit any .gitignore files
 Determine what files to add to the gitignore(s):
e,ext		 add relative path for any file of that type
E,all-ext	 all files of that extention anywhere
d,dir		 all files under the parent directory
a,all-file	 all files of that file name
 Determine what gitignore(s) to use:
p,parent-level=  number of parent directories containing the gitignore to edit. Set to 0 to put it in the local directory"A

. git-sh-setup
. git-sh-i18n

write_output () {
    if test $_verbose -eq 1
    then
	say $1
    fi
}

get_git_ignore () {
    directory=$1

    # if we don't yet have the repo root directory, get it
    if test -z "$repo_root"
    then
	#First, determine the root of the repository
	repo_root="$(git rev-parse --show-toplevel)/"
	write_output "repo_root=$repo_root"
    fi

    # get the path relative to the repo root
    rel_directory="${directory#$repo_root}"
    # if the relative path is the same as it was, try converting it to aa *nix
    # style path
    if test "$rel_directory" = "$directory"
    then
	# repo root 2 (cygwin-ified path) didn't work
	# try the other one
	write_output "changing repo_root from $repo_root"
	#On windows, this turns to C:\... instead of /c/... from some other commands
	repo_root=$(printf "$repo_root" | awk -F":" '{ if ($2) print "/" tolower($1) $2; else print $1 }')
	write_output "	to $repo_root"
	rel_directory="${directory#$repo_root}"
    fi
    # default gitignore
    gitignore="${repo_root}.gitignore"

    # ------------------------------------------------
    # Determine the correct git ignore and the path of
    # the file relative to it
    # ------------------------------------------------
    if test $_parent_level -ge 0
    then
	parent=${directory}
	write_output "parent=${parent}"

	if test $_parent_level -ne 0
	then
	    for i in $(seq 1 $_parent_level)
	    do
	      parent="$(dirname "$parent")/"
	      write_output "parent=${parent}"
	    done
	fi
	root_len=$(printf "${repo_root}" | wc -m)
	parent_len=$(printf "${parent}" | wc -m)
	if test $root_len -ge $parent_len
	then
	    write_output "root_len(${root_len}) >= parent_len(${parent_len})...
	    uh-oh"
	    gettextln "WARNING: Parent directory is outside of the repository"
	    parent="${repo_root}"
	else
	    write_output "root_len(${root_len}) < parent_len(${parent_len})...
	    good"
	fi
	rel_directory="${directory#$parent}"
	gitignore="${parent}.gitignore"
    fi

    write_output "rel_directory=${rel_directory}"
    write_output "gitignore=${gitignore}"

}

add_ignore () {
    # get the absolute path of the file
    file="$(cd "$(dirname "$1")"; pwd)/$(basename "$1")"
    write_output "file=$file"

    directory="$(dirname "$file")/"
    write_output "directory=$directory"
    get_git_ignore "$directory"

    filename=$(basename "$file")
    write_output "filename=$filename"
    extension="${filename##*.}"
    write_output "extension=$extension"
    # defaault line
    line="${rel_directory}${filename}"

    # ------------------------------------------------
    # Determine the correct line to add to the gitignore
    # based on user inputs
    # ------------------------------------------------
    if test $_ext -eq 1
    then
	line="${rel_directory}*.$extension"
    fi
    if test $_directory -eq 1
    then
	line="${rel_directory}*"
    fi
    if test $_file_anywhere -eq 1
    then
	line="**/$filename"
    fi
    if test $_ext_anywhere -eq 1
    then
	line="**/*.$extension"
    fi
    write_output "line=${line}"
    dryrun=""
    if test $_dry_run -eq 1
    then
	dryrun="$(gettext "DRY-RUN!")"
    fi
    say "$dryrun $(eval_gettext "Adding \$line to \$gitignore")"
    if test $_dry_run -eq 0
    then
	echo "$line" >>"$gitignore"
    fi
}

_ext=0
_directory=0
_file_anywhere=0
_ext_anywhere=0
_parent_level=-1
_edit=0
_dry_run=0

while test $# != 0
do
    case "$1" in
    --ext)
	_ext=1
	;;
    --all-ext)
	_ext_anywhere=1
	;;
    --dir)
	_directory=1
	;;
    --all-file)
	_file_anywhere=1
	;;
    --parent-level=*)
	_parent_level="${1#--parent-level=}"
	if ! echo $_parent_level | grep -q '^[0-9]\+$'
	then
	    gettextln "ILLEGAL PARAMETER: -p|--parent-level requires a numerical argument"
	    usage
	fi
	;;
    --dry-run)
	_dry_run=1
	;;
    --edit)
	if test -z $EDITOR
	then
	    gettextln "ERROR: Shell variable \$EDITOR must be set"
	    usage
	fi
	_edit=1
	;;
    --verbose)
	_verbose=1
	;;
    --)
	only_files_left=1
	;;
    *)
	if test $only_files_left -eq 1
	then
	    add_ignore "$1"
	fi
	;;
    esac
    shift
done
if test $_edit -eq 1
then
    get_git_ignore "$(pwd)/"
    git_editor "$gitignore"
fi
exit 0
