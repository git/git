#!/bin/sh
#
# This program launch a web browser on the html page
# describing a git command.
#
# Copyright (c) 2007 Christian Couder
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is heavily stolen from git-mergetool.sh, by
# Theodore Y. Ts'o (thanks) that is:
#
# Copyright (c) 2006 Theodore Y. Ts'o
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Junio C Hamano or any other official
# git maintainer.
#

USAGE='[--browser=browser|--tool=browser] [--config=conf.var] url/file ...'

# This must be capable of running outside of git directory, so
# the vanilla git-sh-setup should not be used.
NONGIT_OK=Yes
. git-sh-setup

valid_custom_tool()
{
	browser_cmd="$(git config "browser.$1.cmd")"
	test -n "$browser_cmd"
}

valid_tool() {
	case "$1" in
		firefox | iceweasel | konqueror | w3m | links | lynx | dillo | open)
			;; # happy
		*)
			valid_custom_tool "$1" || return 1
			;;
	esac
}

init_browser_path() {
	browser_path=$(git config "browser.$1.path")
	test -z "$browser_path" && browser_path="$1"
}

while test $# != 0
do
    case "$1" in
	-b|--browser*|-t|--tool*)
	    case "$#,$1" in
		*,*=*)
		    browser=`expr "z$1" : 'z-[^=]*=\(.*\)'`
		    ;;
		1,*)
		    usage ;;
		*)
		    browser="$2"
		    shift ;;
	    esac
	    ;;
	-c|--config*)
	    case "$#,$1" in
		*,*=*)
		    conf=`expr "z$1" : 'z-[^=]*=\(.*\)'`
		    ;;
		1,*)
		    usage ;;
		*)
		    conf="$2"
		    shift ;;
	    esac
	    ;;
	--)
	    break
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

test $# = 0 && usage

if test -z "$browser"
then
    for opt in "$conf" "web.browser"
    do
	test -z "$opt" && continue
	browser="`git config $opt`"
	test -z "$browser" || break
    done
    if test -n "$browser" && ! valid_tool "$browser"; then
	echo >&2 "git config option $opt set to unknown browser: $browser"
	echo >&2 "Resetting to default..."
	unset browser
    fi
fi

if test -z "$browser" ; then
    if test -n "$DISPLAY"; then
	browser_candidates="firefox iceweasel konqueror w3m links lynx dillo"
	if test "$KDE_FULL_SESSION" = "true"; then
	    browser_candidates="konqueror $browser_candidates"
	fi
    else
	browser_candidates="w3m links lynx"
    fi
    # SECURITYSESSIONID indicates an OS X GUI login session
    if test -n "$SECURITYSESSIONID"; then
	browser_candidates="open $browser_candidates"
    fi

    for i in $browser_candidates; do
	init_browser_path $i
	if type "$browser_path" > /dev/null 2>&1; then
	    browser=$i
	    break
	fi
    done
    test -z "$browser" && die "No known browser available."
else
    valid_tool "$browser" || die "Unknown browser '$browser'."

    init_browser_path "$browser"

    if test -z "$browser_cmd" && ! type "$browser_path" > /dev/null 2>&1; then
	die "The browser $browser is not available as '$browser_path'."
    fi
fi

case "$browser" in
    firefox|iceweasel)
	# Check version because firefox < 2.0 does not support "-new-tab".
	vers=$(expr "$($browser_path -version)" : '.* \([0-9][0-9]*\)\..*')
	NEWTAB='-new-tab'
	test "$vers" -lt 2 && NEWTAB=''
	"$browser_path" $NEWTAB "$@" &
	;;
    konqueror)
	case "$(basename "$browser_path")" in
	    konqueror)
		# It's simpler to use kfmclient to open a new tab in konqueror.
		browser_path="$(echo "$browser_path" | sed -e 's/konqueror$/kfmclient/')"
		type "$browser_path" > /dev/null 2>&1 || die "No '$browser_path' found."
		eval "$browser_path" newTab "$@"
		;;
	    kfmclient)
		eval "$browser_path" newTab "$@"
		;;
	    *)
		"$browser_path" "$@" &
		;;
	esac
	;;
    w3m|links|lynx|open)
	eval "$browser_path" "$@"
	;;
    dillo)
	"$browser_path" "$@" &
	;;
    *)
	if test -n "$browser_cmd"; then
	    ( eval $browser_cmd "$@" )
	fi
	;;
esac
