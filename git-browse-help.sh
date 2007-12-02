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

USAGE='[--browser=browser|--tool=browser] [cmd to display] ...'
SUBDIRECTORY_OK=Yes
OPTIONS_SPEC=
. git-sh-setup

# Install data.
html_dir="@@HTMLDIR@@"

test -f "$html_dir/git.html" || die "No documentation directory found."

valid_tool() {
	case "$1" in
		firefox | iceweasel | konqueror | w3m | links | lynx | dillo)
			;; # happy
		*)
			return 1
			;;
	esac
}

init_browser_path() {
	browser_path=`git config browser.$1.path`
	test -z "$browser_path" && browser_path=$1
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

if test -z "$browser"; then
    for opt in "help.browser" "web.browser"
    do
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
    echo "browser candidates: $browser_candidates"
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

    if ! type "$browser_path" > /dev/null 2>&1; then
	die "The browser $browser is not available as '$browser_path'."
    fi
fi

pages=$(for p in "$@"; do echo "$html_dir/$p.html" ; done)
test -z "$pages" && pages="$html_dir/git.html"

case "$browser" in
    firefox|iceweasel)
	# Check version because firefox < 2.0 does not support "-new-tab".
	vers=$(expr "$($browser_path -version)" : '.* \([0-9][0-9]*\)\..*')
	NEWTAB='-new-tab'
	test "$vers" -lt 2 && NEWTAB=''
	nohup "$browser_path" $NEWTAB $pages &
	;;
    konqueror)
	case "$(basename "$browser_path")" in
	    konqueror)
		# It's simpler to use kfmclient to open a new tab in konqueror.
		browser_path="$(echo "$browser_path" | sed -e 's/konqueror$/kfmclient/')"
		type "$browser_path" > /dev/null 2>&1 || die "No '$browser_path' found."
		eval "$browser_path" newTab $pages
		;;
	    kfmclient)
		eval "$browser_path" newTab $pages
		;;
	    *)
	        nohup "$browser_path" $pages &
		;;
	esac
	;;
    w3m|links|lynx)
	eval "$browser_path" $pages
	;;
    dillo)
	nohup "$browser_path" $pages &
	;;
esac
