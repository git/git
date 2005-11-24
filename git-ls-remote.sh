#!/bin/sh
#

usage () {
    echo >&2 "usage: $0 [--heads] [--tags] <repository> <refs>..."
    exit 1;
}

while case "$#" in 0) break;; esac
do
  case "$1" in
  -h|--h|--he|--hea|--head|--heads)
  heads=heads; shift ;;
  -t|--t|--ta|--tag|--tags)
  tags=tags; shift ;;
  --)
  shift; break ;;
  -*)
  usage ;;
  *)
  break ;;
  esac
done

case "$#" in 0) usage ;; esac

case ",$heads,$tags," in
,,,) heads=heads tags=tags other=other ;;
esac

. git-parse-remote
peek_repo="$(get_remote_url "$@")"
shift

tmp=.ls-remote-$$
trap "rm -fr $tmp-*" 0 1 2 3 15
tmpdir=$tmp-d

case "$peek_repo" in
http://* | https://* )
        if [ -n "$GIT_SSL_NO_VERIFY" ]; then
            curl_extra_args="-k"
        fi
	curl -nsf $curl_extra_args "$peek_repo/info/refs" ||
		echo "failed	slurping"
	;;

rsync://* )
	mkdir $tmpdir
	rsync -rq "$peek_repo/refs" $tmpdir || {
		echo "failed	slurping"
		exit
	}
	(cd $tmpdir && find refs -type f) |
	while read path
	do
		cat "$tmpdir/$path" | tr -d '\012'
		echo "	$path"
	done &&
	rm -fr $tmpdir
	;;

* )
	git-peek-remote "$peek_repo" ||
		echo "failed	slurping"
	;;
esac |
sort -t '	' -k 2 |
while read sha1 path
do
	case "$sha1" in
	failed)
		die "Failed to find remote refs"
	esac
	case "$path" in
	refs/heads/*)
		group=heads ;;
	refs/tags/*)
		group=tags ;;
	*)
		group=other ;;
	esac
	case ",$heads,$tags,$other," in
	*,$group,*)
		;;
	*)
		continue;;
	esac
	case "$#" in
	0)
		match=yes ;;
	*)
		match=no
		for pat
		do
			case "/$path" in
			*/$pat )
				match=yes
				break ;;
			esac
		done
	esac
	case "$match" in
	no)
		continue ;;
	esac
	echo "$sha1	$path"
done
