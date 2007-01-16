#!/bin/sh
#

USAGE='<fetch-options> <repository> <refspec>...'
SUBDIRECTORY_OK=Yes
. git-sh-setup
set_reflog_action "fetch $*"
cd_to_toplevel ;# probably unnecessary...

. git-parse-remote
_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"

LF='
'
IFS="$LF"

no_tags=
tags=
append=
force=
verbose=
update_head_ok=
exec=
keep=
shallow_depth=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-a|--a|--ap|--app|--appe|--appen|--append)
		append=t
		;;
	--upl|--uplo|--uploa|--upload|--upload-|--upload-p|\
	--upload-pa|--upload-pac|--upload-pack)
		shift
		exec="--upload-pack=$1"
		;;
	--upl=*|--uplo=*|--uploa=*|--upload=*|\
	--upload-=*|--upload-p=*|--upload-pa=*|--upload-pac=*|--upload-pack=*)
		exec=--upload-pack=$(expr "z$1" : 'z-[^=]*=\(.*\)')
		shift
		;;
	-f|--f|--fo|--for|--forc|--force)
		force=t
		;;
	-t|--t|--ta|--tag|--tags)
		tags=t
		;;
	-n|--n|--no|--no-|--no-t|--no-ta|--no-tag|--no-tags)
		no_tags=t
		;;
	-u|--u|--up|--upd|--upda|--updat|--update|--update-|--update-h|\
	--update-he|--update-hea|--update-head|--update-head-|\
	--update-head-o|--update-head-ok)
		update_head_ok=t
		;;
	-v|--verbose)
		verbose=Yes
		;;
	-k|--k|--ke|--kee|--keep)
		keep='-k -k'
		;;
	--depth=*)
		shallow_depth="--depth=`expr "z$1" : 'z-[^=]*=\(.*\)'`"
		;;
	--depth)
		shift
		shallow_depth="--depth=$1"
		;;
	-*)
		usage
		;;
	*)
		break
		;;
	esac
	shift
done

case "$#" in
0)
	origin=$(get_default_remote)
	test -n "$(get_remote_url ${origin})" ||
		die "Where do you want to fetch from today?"
	set x $origin ; shift ;;
esac

if test -z "$exec"
then
	# No command line override and we have configuration for the remote.
	exec="--upload-pack=$(get_uploadpack $1)"
fi

remote_nick="$1"
remote=$(get_remote_url "$@")
refs=
rref=
rsync_slurped_objects=

if test "" = "$append"
then
	: >"$GIT_DIR/FETCH_HEAD"
fi

# Global that is reused later
ls_remote_result=$(git ls-remote $exec "$remote") ||
	die "Cannot get the repository state from $remote"

append_fetch_head () {
	flags=
	test -n "$verbose" && flags="$flags -v"
	test -n "$force" && flags="$flags -f"
	GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION" \
		git-fetch--tool append-fetch-head $flags "$@"
}

update_local_ref () {
	flags=
	test -n "$verbose" && flags="$flags -v"
	test -n "$force" && flags="$flags -f"
	GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION" \
		git-fetch--tool update-local-ref $flags "$@"
}

# updating the current HEAD with git-fetch in a bare
# repository is always fine.
if test -z "$update_head_ok" && test $(is_bare_repository) = false
then
	orig_head=$(git-rev-parse --verify HEAD 2>/dev/null)
fi

# If --tags (and later --heads or --all) is specified, then we are
# not talking about defaults stored in Pull: line of remotes or
# branches file, and just fetch those and refspecs explicitly given.
# Otherwise we do what we always did.

reflist=$(get_remote_refs_for_fetch "$@")
if test "$tags"
then
	taglist=`IFS='	' &&
		  echo "$ls_remote_result" |
		  git-show-ref --exclude-existing=refs/tags/ |
	          while read sha1 name
		  do
			echo ".${name}:${name}"
		  done` || exit
	if test "$#" -gt 1
	then
		# remote URL plus explicit refspecs; we need to merge them.
		reflist="$reflist$LF$taglist"
	else
		# No explicit refspecs; fetch tags only.
		reflist=$taglist
	fi
fi

fetch_native () {
  reflist="$1"
  refs=
  rref=

  for ref in $reflist
  do
      refs="$refs$LF$ref"

      # These are relative path from $GIT_DIR, typically starting at refs/
      # but may be HEAD
      if expr "z$ref" : 'z\.' >/dev/null
      then
	  not_for_merge=t
	  ref=$(expr "z$ref" : 'z\.\(.*\)')
      else
	  not_for_merge=
      fi
      if expr "z$ref" : 'z+' >/dev/null
      then
	  single_force=t
	  ref=$(expr "z$ref" : 'z+\(.*\)')
      else
	  single_force=
      fi
      remote_name=$(expr "z$ref" : 'z\([^:]*\):')
      local_name=$(expr "z$ref" : 'z[^:]*:\(.*\)')

      rref="$rref$LF$remote_name"
  done

    ( : subshell because we muck with IFS
      IFS=" 	$LF"
      (
	  git-fetch-pack --thin $exec $keep $shallow_depth "$remote" $rref ||
	  echo failed "$remote"
      ) |
      (
	flags=
	test -n "$verbose" && flags="$flags -v"
	test -n "$force" && flags="$flags -f"
	GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION" \
		git-fetch--tool native-store \
			$flags "$remote" "$remote_nick" "$refs"
      )
    ) || exit

}

fetch_dumb () {
  reflist="$1"
  refs=
  rref=

  for ref in $reflist
  do
      refs="$refs$LF$ref"

      # These are relative path from $GIT_DIR, typically starting at refs/
      # but may be HEAD
      if expr "z$ref" : 'z\.' >/dev/null
      then
	  not_for_merge=t
	  ref=$(expr "z$ref" : 'z\.\(.*\)')
      else
	  not_for_merge=
      fi
      if expr "z$ref" : 'z+' >/dev/null
      then
	  single_force=t
	  ref=$(expr "z$ref" : 'z+\(.*\)')
      else
	  single_force=
      fi
      remote_name=$(expr "z$ref" : 'z\([^:]*\):')
      local_name=$(expr "z$ref" : 'z[^:]*:\(.*\)')

      rref="$rref$LF$remote_name"

      # There are transports that can fetch only one head at a time...
      case "$remote" in
      http://* | https://* | ftp://*)
	  test -n "$shallow_depth" &&
		die "shallow clone with http not supported"
	  proto=`expr "$remote" : '\([^:]*\):'`
	  if [ -n "$GIT_SSL_NO_VERIFY" ]; then
	      curl_extra_args="-k"
	  fi
	  if [ -n "$GIT_CURL_FTP_NO_EPSV" -o \
		"`git-config --bool http.noEPSV`" = true ]; then
	      noepsv_opt="--disable-epsv"
	  fi

	  # Find $remote_name from ls-remote output.
	  head=$(
		IFS='	'
		echo "$ls_remote_result" |
		while read sha1 name
		do
			test "z$name" = "z$remote_name" || continue
			echo "$sha1"
			break
		done
	  )
	  expr "z$head" : "z$_x40\$" >/dev/null ||
		die "No such ref $remote_name at $remote"
	  echo >&2 "Fetching $remote_name from $remote using $proto"
	  git-http-fetch -v -a "$head" "$remote/" || exit
	  ;;
      rsync://*)
	  test -n "$shallow_depth" &&
		die "shallow clone with rsync not supported"
	  TMP_HEAD="$GIT_DIR/TMP_HEAD"
	  rsync -L -q "$remote/$remote_name" "$TMP_HEAD" || exit 1
	  head=$(git-rev-parse --verify TMP_HEAD)
	  rm -f "$TMP_HEAD"
	  test "$rsync_slurped_objects" || {
	      rsync -av --ignore-existing --exclude info \
		  "$remote/objects/" "$GIT_OBJECT_DIRECTORY/" || exit

	      # Look at objects/info/alternates for rsync -- http will
	      # support it natively and git native ones will do it on
	      # the remote end.  Not having that file is not a crime.
	      rsync -q "$remote/objects/info/alternates" \
		  "$GIT_DIR/TMP_ALT" 2>/dev/null ||
		  rm -f "$GIT_DIR/TMP_ALT"
	      if test -f "$GIT_DIR/TMP_ALT"
	      then
		  resolve_alternates "$remote" <"$GIT_DIR/TMP_ALT" |
		  while read alt
		  do
		      case "$alt" in 'bad alternate: '*) die "$alt";; esac
		      echo >&2 "Getting alternate: $alt"
		      rsync -av --ignore-existing --exclude info \
		      "$alt" "$GIT_OBJECT_DIRECTORY/" || exit
		  done
		  rm -f "$GIT_DIR/TMP_ALT"
	      fi
	      rsync_slurped_objects=t
	  }
	  ;;
      esac

      append_fetch_head "$head" "$remote" \
	  "$remote_name" "$remote_nick" "$local_name" "$not_for_merge" || exit

  done

}

fetch_main () {
	case "$remote" in
	http://* | https://* | ftp://* | rsync://* )
		fetch_dumb "$@"
		;;
	*)
		fetch_native "$@"
		;;
	esac
}

fetch_main "$reflist" || exit

# automated tag following
case "$no_tags$tags" in
'')
	case "$reflist" in
	*:refs/*)
		# effective only when we are following remote branch
		# using local tracking branch.
		taglist=$(IFS='	' &&
		echo "$ls_remote_result" |
		git-show-ref --exclude-existing=refs/tags/ |
		while read sha1 name
		do
			git-cat-file -t "$sha1" >/dev/null 2>&1 || continue
			echo >&2 "Auto-following $name"
			echo ".${name}:${name}"
		done)
	esac
	case "$taglist" in
	'') ;;
	?*)
		# do not deepen a shallow tree when following tags
		shallow_depth=
		fetch_main "$taglist" || exit ;;
	esac
esac

# If the original head was empty (i.e. no "master" yet), or
# if we were told not to worry, we do not have to check.
case "$orig_head" in
'')
	;;
?*)
	curr_head=$(git-rev-parse --verify HEAD 2>/dev/null)
	if test "$curr_head" != "$orig_head"
	then
	    git-update-ref \
			-m "$GIT_REFLOG_ACTION: Undoing incorrectly fetched HEAD." \
			HEAD "$orig_head"
		die "Cannot fetch into the current branch."
	fi
	;;
esac
