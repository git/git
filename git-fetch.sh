#!/bin/sh
#

USAGE='<fetch-options> <repository> <refspec>...'
. git-sh-setup
. git-parse-remote
_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"

LF='
'
IFS="$LF"

rloga=fetch
no_tags=
tags=
append=
force=
verbose=
update_head_ok=
exec=
upload_pack=
keep=--thin
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-a|--a|--ap|--app|--appe|--appen|--append)
		append=t
		;;
	--upl|--uplo|--uploa|--upload|--upload-|--upload-p|\
	--upload-pa|--upload-pac|--upload-pack)
		shift
		exec="--exec=$1" 
		upload_pack="-u $1"
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
		keep=--keep
		;;
	--reflog-action=*)
		rloga=`expr "z$1" : 'z-[^=]*=\(.*\)'`
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

remote_nick="$1"
remote=$(get_remote_url "$@")
refs=
rref=
rsync_slurped_objects=

rloga="$rloga $remote_nick"
test "$remote_nick" = "$remote" || rloga="$rloga $remote"

if test "" = "$append"
then
	: >"$GIT_DIR/FETCH_HEAD"
fi

append_fetch_head () {
    head_="$1"
    remote_="$2"
    remote_name_="$3"
    remote_nick_="$4"
    local_name_="$5"
    case "$6" in
    t) not_for_merge_='not-for-merge' ;;
    '') not_for_merge_= ;;
    esac

    # remote-nick is the URL given on the command line (or a shorthand)
    # remote-name is the $GIT_DIR relative refs/ path we computed
    # for this refspec.

    # the $note_ variable will be fed to git-fmt-merge-msg for further
    # processing.
    case "$remote_name_" in
    HEAD)
	note_= ;;
    refs/heads/*)
	note_="$(expr "$remote_name_" : 'refs/heads/\(.*\)')"
	note_="branch '$note_' of " ;;
    refs/tags/*)
	note_="$(expr "$remote_name_" : 'refs/tags/\(.*\)')"
	note_="tag '$note_' of " ;;
    refs/remotes/*)
	note_="$(expr "$remote_name_" : 'refs/remotes/\(.*\)')"
	note_="remote branch '$note_' of " ;;
    *)
	note_="$remote_name of " ;;
    esac
    remote_1_=$(expr "z$remote_" : 'z\(.*\)\.git/*$') &&
	remote_="$remote_1_"
    note_="$note_$remote_"

    # 2.6.11-tree tag would not be happy to be fed to resolve.
    if git-cat-file commit "$head_" >/dev/null 2>&1
    then
	headc_=$(git-rev-parse --verify "$head_^0") || exit
	echo "$headc_	$not_for_merge_	$note_" >>"$GIT_DIR/FETCH_HEAD"
    else
	echo "$head_	not-for-merge	$note_" >>"$GIT_DIR/FETCH_HEAD"
    fi

    update_local_ref "$local_name_" "$head_" "$note_"
}

update_local_ref () {
    # If we are storing the head locally make sure that it is
    # a fast forward (aka "reverse push").

    label_=$(git-cat-file -t $2)
    newshort_=$(git-rev-parse --short $2)
    if test -z "$1" ; then
	[ "$verbose" ] && echo >&2 "* fetched $3"
	[ "$verbose" ] && echo >&2 "  $label_: $newshort_"
	return 0
    fi
    oldshort_=$(git-rev-parse --short "$1" 2>/dev/null)
    mkdir -p "$(dirname "$GIT_DIR/$1")"
    case "$1" in
    refs/tags/*)
	# Tags need not be pointing at commits so there
	# is no way to guarantee "fast-forward" anyway.
	if test -f "$GIT_DIR/$1"
	then
		if now_=$(cat "$GIT_DIR/$1") && test "$now_" = "$2"
		then
			[ "$verbose" ] && echo >&2 "* $1: same as $3"
			[ "$verbose" ] && echo >&2 "  $label_: $newshort_" ||:
		else
			echo >&2 "* $1: updating with $3"
			echo >&2 "  $label_: $newshort_"
			git-update-ref -m "$rloga: updating tag" "$1" "$2"
		fi
	else
		echo >&2 "* $1: storing $3"
		echo >&2 "  $label_: $newshort_"
		git-update-ref -m "$rloga: storing tag" "$1" "$2"
	fi
	;;

    refs/heads/* | refs/remotes/*)
	# $1 is the ref being updated.
	# $2 is the new value for the ref.
	local=$(git-rev-parse --verify "$1^0" 2>/dev/null)
	if test "$local"
	then
	    # Require fast-forward.
	    mb=$(git-merge-base "$local" "$2") &&
	    case "$2,$mb" in
	    $local,*)
	        if test -n "$verbose"
		then
			echo >&2 "* $1: same as $3"
			echo >&2 "  $label_: $newshort_"
		fi
		;;
	    *,$local)
		echo >&2 "* $1: fast forward to $3"
		echo >&2 "  old..new: $oldshort_..$newshort_"
		git-update-ref -m "$rloga: fast-forward" "$1" "$2" "$local"
		;;
	    *)
		false
		;;
	    esac || {
		case ",$force,$single_force," in
		*,t,*)
			echo >&2 "* $1: forcing update to non-fast forward $3"
			echo >&2 "  old...new: $oldshort_...$newshort_"
			git-update-ref -m "$rloga: forced-update" "$1" "$2" "$local"
			;;
		*)
			echo >&2 "* $1: not updating to non-fast forward $3"
			echo >&2 "  old...new: $oldshort_...$newshort_"
			exit 1
			;;
		esac
	    }
	else
	    echo >&2 "* $1: storing $3"
	    echo >&2 "  $label_: $newshort_"
	    git-update-ref -m "$rloga: storing head" "$1" "$2"
	fi
	;;
    esac
}

case "$update_head_ok" in
'')
	orig_head=$(git-rev-parse --verify HEAD 2>/dev/null)
	;;
esac

# If --tags (and later --heads or --all) is specified, then we are
# not talking about defaults stored in Pull: line of remotes or
# branches file, and just fetch those and refspecs explicitly given.
# Otherwise we do what we always did.

reflist=$(get_remote_refs_for_fetch "$@")
if test "$tags"
then
	taglist=`IFS="	" &&
		  (
			git-ls-remote $upload_pack --tags "$remote" ||
			echo fail ouch
		  ) |
	          while read sha1 name
		  do
			case "$sha1" in
			fail)
				exit 1
			esac
			case "$name" in
			*^*) continue ;;
			esac
		  	if git-check-ref-format "$name"
			then
			    echo ".${name}:${name}"
			else
			    echo >&2 "warning: tag ${name} ignored"
			fi
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

fetch_main () {
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
	  if [ -n "$GIT_SSL_NO_VERIFY" ]; then
	      curl_extra_args="-k"
	  fi
	  if [ -n "$GIT_CURL_FTP_NO_EPSV" -o \
		"`git-repo-config --bool http.noEPSV`" = true ]; then
	      noepsv_opt="--disable-epsv"
	  fi
	  max_depth=5
	  depth=0
	  head="ref: $remote_name"
	  while (expr "z$head" : "zref:" && expr $depth \< $max_depth) >/dev/null
	  do
	    remote_name_quoted=$(@@PERL@@ -e '
	      my $u = $ARGV[0];
              $u =~ s/^ref:\s*//;
	      $u =~ s{([^-a-zA-Z0-9/.])}{sprintf"%%%02x",ord($1)}eg;
	      print "$u";
	  ' "$head")
	    head=$(curl -nsfL $curl_extra_args $noepsv_opt "$remote/$remote_name_quoted")
	    depth=$( expr \( $depth + 1 \) )
	  done
	  expr "z$head" : "z$_x40\$" >/dev/null ||
	      die "Failed to fetch $remote_name from $remote"
	  echo >&2 Fetching "$remote_name from $remote" using http
	  git-http-fetch -v -a "$head" "$remote/" || exit
	  ;;
      rsync://*)
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
      *)
	  # We will do git native transport with just one call later.
	  continue ;;
      esac

      append_fetch_head "$head" "$remote" \
	  "$remote_name" "$remote_nick" "$local_name" "$not_for_merge"

  done

  case "$remote" in
  http://* | https://* | ftp://* | rsync://* )
      ;; # we are already done.
  *)
    ( : subshell because we muck with IFS
      IFS=" 	$LF"
      (
	  git-fetch-pack $exec $keep "$remote" $rref || echo failed "$remote"
      ) |
      while read sha1 remote_name
      do
	  case "$sha1" in
	  failed)
		  echo >&2 "Fetch failure: $remote"
		  exit 1 ;;
	  esac
	  found=
	  single_force=
	  for ref in $refs
	  do
	      case "$ref" in
	      +$remote_name:*)
		  single_force=t
		  not_for_merge=
		  found="$ref"
		  break ;;
	      .+$remote_name:*)
		  single_force=t
		  not_for_merge=t
		  found="$ref"
		  break ;;
	      .$remote_name:*)
		  not_for_merge=t
		  found="$ref"
		  break ;;
	      $remote_name:*)
		  not_for_merge=
		  found="$ref"
		  break ;;
	      esac
	  done
	  local_name=$(expr "z$found" : 'z[^:]*:\(.*\)')
	  append_fetch_head "$sha1" "$remote" \
		  "$remote_name" "$remote_nick" "$local_name" "$not_for_merge"
      done
    ) || exit ;;
  esac

}

fetch_main "$reflist"

# automated tag following
case "$no_tags$tags" in
'')
	case "$reflist" in
	*:refs/*)
		# effective only when we are following remote branch
		# using local tracking branch.
		taglist=$(IFS=" " &&
		git-ls-remote $upload_pack --tags "$remote" |
		sed -ne 's|^\([0-9a-f]*\)[ 	]\(refs/tags/.*\)^{}$|\1 \2|p' |
		while read sha1 name
		do
			test -f "$GIT_DIR/$name" && continue
			git-check-ref-format "$name" || {
				echo >&2 "warning: tag ${name} ignored"
				continue
			}
			git-cat-file -t "$sha1" >/dev/null 2>&1 || continue
			echo >&2 "Auto-following $name"
			echo ".${name}:${name}"
		done)
	esac
	case "$taglist" in
	'') ;;
	?*)
		fetch_main "$taglist" ;;
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
			-m "$rloga: Undoing incorrectly fetched HEAD." \
			HEAD "$orig_head"
		die "Cannot fetch into the current branch."
	fi
	;;
esac
