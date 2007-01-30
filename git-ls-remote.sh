#!/bin/sh
#

usage () {
    echo >&2 "usage: $0 [--heads] [--tags] [-u|--upload-pack <upload-pack>]"
    echo >&2 "          <repository> <refs>..."
    exit 1;
}

die () {
    echo >&2 "$*"
    exit 1
}

exec=
while case "$#" in 0) break;; esac
do
  case "$1" in
  -h|--h|--he|--hea|--head|--heads)
  heads=heads; shift ;;
  -t|--t|--ta|--tag|--tags)
  tags=tags; shift ;;
  -u|--u|--up|--upl|--uploa|--upload|--upload-|--upload-p|--upload-pa|\
  --upload-pac|--upload-pack)
	shift
	exec="--upload-pack=$1"
	shift;;
  -u=*|--u=*|--up=*|--upl=*|--uplo=*|--uploa=*|--upload=*|\
  --upload-=*|--upload-p=*|--upload-pa=*|--upload-pac=*|--upload-pack=*)
	exec=--upload-pack=$(expr "$1" : '-[^=]*=\(.*\)')
	shift;;
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
http://* | https://* | ftp://* )
        if [ -n "$GIT_SSL_NO_VERIFY" ]; then
            curl_extra_args="-k"
        fi
	if [ -n "$GIT_CURL_FTP_NO_EPSV" -o \
		"`git-config --bool http.noEPSV`" = true ]; then
		curl_extra_args="${curl_extra_args} --disable-epsv"
	fi
	curl -nsf $curl_extra_args --header "Pragma: no-cache" "$peek_repo/info/refs" ||
		echo "failed	slurping"
	;;

rsync://* )
	mkdir $tmpdir &&
	rsync -rlq "$peek_repo/HEAD" $tmpdir &&
	rsync -rq "$peek_repo/refs" $tmpdir || {
		echo "failed	slurping"
		exit
	}
	head=$(cat "$tmpdir/HEAD") &&
	case "$head" in
	ref:' '*)
		head=$(expr "z$head" : 'zref: \(.*\)') &&
		head=$(cat "$tmpdir/$head") || exit
	esac &&
	echo "$head	HEAD"
	(cd $tmpdir && find refs -type f) |
	while read path
	do
		cat "$tmpdir/$path" | tr -d '\012'
		echo "	$path"
	done &&
	rm -fr $tmpdir
	;;

* )
	git-peek-remote $exec "$peek_repo" ||
		echo "failed	slurping"
	;;
esac |
sort -t '	' -k 2 |
while read sha1 path
do
	case "$sha1" in
	failed)
		exit 1 ;;
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
