#!/bin/sh
# Distributed under the terms of the GNU General Public License v2
# Copyright (c) 2006 Fernando J. Pereda <ferdy@gentoo.org>
# Copyright (c) 2008 Natanael Copa <natanael.copa@gmail.com>
# Copyright (c) 2010 Eric S. Raymond <esr@thyrsus.com>
# Assistance and review by Petr Baudis, author of ciabot.pl,
# is gratefully acknowledged.
#
# This is a version 3.x of ciabot.sh; use -V to find the exact
# version.  Versions 1 and 2 were shipped in 2006 and 2008 and are not
# version-stamped.  The version 2 maintainer has passed the baton.
#
# Note: This script should be considered obsolete.
# There is a faster, better-documented rewrite in Python: find it as ciabot.py
# Use this only if your hosting site forbids Python hooks.
# It requires: git(1), hostname(1), cut(1), sendmail(1), and wget(1).
#
# Originally based on Git ciabot.pl by Petr Baudis.
# This script contains porcelain and porcelain byproducts.
#
# usage: ciabot.sh [-V] [-n] [-p projectname] [refname commit]
#
# This script is meant to be run either in a post-commit hook or in an
# update hook. Try it with -n to see the notification mail dumped to
# stdout and verify that it looks sane. With -V it dumps its version
# and exits.
#
# In post-commit, run it without arguments. It will query for
# current HEAD and the latest commit ID to get the information it
# needs.
#
# In update, you have to call it once per merged commit:
#
#       refname=$1
#       oldhead=$2
#       newhead=$3
#       for merged in $(git rev-list ${oldhead}..${newhead} | tac) ; do
#               /path/to/ciabot.sh ${refname} ${merged}
#       done
#
# The reason for the tac call is that git rev-list emits commits from
# most recent to least - better to ship notifactions from oldest to newest.
#
# Configuration variables affecting this script:
#
# ciabot.project = name of the project
# ciabot.repo = name of the project repo for gitweb/cgit purposes
# ciabot.revformat = format in which the revision is shown
#
# ciabot.project defaults to the directory name of the repository toplevel.
# ciabot.repo defaults to ciabot.project lowercased.
#
# This means that in the normal case you need not do any configuration at all,
# but setting the project name will speed it up slightly.
#
# The revformat variable may have the following values
# raw -> full hex ID of commit
# short -> first 12 chars of hex ID
# describe = -> describe relative to last tag, falling back to short
# The default is 'describe'.
#
# Note: the shell ancestors of this script used mail, not XML-RPC, in
# order to avoid stalling until timeout when the CIA XML-RPC server is
# down. It is unknown whether this is still an issue in 2010, but
# XML-RPC would be annoying to do from sh in any case. (XML-RPC does
# have the advantage that it guarantees notification of multiple commits
# shpped from an update in their actual order.)
#

# The project as known to CIA. You can set this with a -p option,
# or let it default to the directory name of the repo toplevel.
project=$(git config --get ciabot.project)

if [ -z $project ]
then
    here=`pwd`;
    while :; do
	if [ -d $here/.git ]
	then
	    project=`basename $here`
	    break
	elif [ $here = '/' ]
	then
	    echo "ciabot.sh: no .git below root!"
	    exit 1
	fi
	here=`dirname $here`
    done
fi

# Name of the repo for gitweb/cgit purposes
repo=$(git config --get ciabot.repo)
[ -z $repo] && repo=$(echo "${project}" | tr '[A-Z]' '[a-z]')

# What revision format do we want in the summary?
revformat=$(git config --get ciabot.revformat)

# Fully qualified domain name of the repo host.  You can hardwire this
# to make the script faster. The -f option works under Linux and FreeBSD,
# but not OpenBSD and NetBSD. But under OpenBSD and NetBSD,
# hostname without options gives the FQDN.
if hostname -f >/dev/null 2>&1
then
    hostname=`hostname -f`
else
    hostname=`hostname`
fi

# Changeset URL prefix for your repo: when the commit ID is appended
# to this, it should point at a CGI that will display the commit
# through gitweb or something similar. The defaults will probably
# work if you have a typical gitweb/cgit setup.
#urlprefix="http://${host}/cgi-bin/gitweb.cgi?p=${repo};a=commit;h="
urlprefix="http://${host}/cgi-bin/cgit.cgi/${repo}/commit/?id="

#
# You probably will not need to change the following:
#

# Identify the script. The 'generator' variable should change only
# when the script itself gets a new home and maintainer.
generator="http://www.catb.org/~esr/ciabot/ciabot.sh"
version=3.5

# Addresses for the e-mail
from="CIABOT-NOREPLY@${hostname}"
to="cia@cia.vc"

# SMTP client to use - may need to edit the absolute pathname for your system
sendmail="sendmail -t -f ${from}"

#
# No user-serviceable parts below this line:
#

# Should include all places sendmail is likely to lurk.
PATH="$PATH:/usr/sbin/"

mode=mailit
while getopts pnV opt
do
    case $opt in
	p) project=$2; shift ; shift ;;
	n) mode=dumpit; shift ;;
	V) echo "ciabot.sh: version $version"; exit 0; shift ;;
    esac
done

# Cough and die if user has not specified a project
if [ -z "$project" ]
then
    echo "ciabot.sh: no project specified, bailing out." >&2
    exit 1
fi

if [ $# -eq 0 ] ; then
	refname=$(git symbolic-ref HEAD 2>/dev/null)
	merged=$(git rev-parse HEAD)
else
	refname=$1
	merged=$2
fi

# This tries to turn your gitwebbish URL into a tinyurl so it will take up
# less space on the IRC notification line. Some repo sites (I'm looking at
# you, berlios.de!) forbid wget calls for security reasons.  On these,
# the code will fall back to the full un-tinyfied URL.
longurl=${urlprefix}${merged}
url=$(wget -O - -q http://tinyurl.com/api-create.php?url=${longurl} 2>/dev/null)
if [ -z "$url" ]; then
	url="${longurl}"
fi

refname=${refname##refs/heads/}

case $revformat in
raw) rev=$merged ;;
short) rev='' ;;
*) rev=$(git describe ${merged} 2>/dev/null) ;;
esac
[ -z ${rev} ] && rev=$(echo "$merged" | cut -c 1-12)

# We discard the part of the author's address after @.
# Might be nice to ship the full email address, if not
# for spammers' address harvesters - getting this wrong
# would make the freenode #commits channel into harvester heaven.
author=$(git log -1 '--pretty=format:%an <%ae>' $merged)
author=$(echo "$author" | sed -n -e '/^.*<\([^@]*\).*$/s--\1-p')

logmessage=$(git log -1 '--pretty=format:%s' $merged)
ts=$(git log -1 '--pretty=format:%at' $merged)
files=$(git diff-tree -r --name-only ${merged} | sed -e '1d' -e 's-.*-<file>&</file>-')

out="
<message>
  <generator>
    <name>CIA Shell client for Git</name>
    <version>${version}</version>
    <url>${generator}</url>
  </generator>
  <source>
    <project>${project}</project>
    <branch>$repo:${refname}</branch>
  </source>
  <timestamp>${ts}</timestamp>
  <body>
    <commit>
      <author>${author}</author>
      <revision>${rev}</revision>
      <files>
	${files}
      </files>
      <log>${logmessage} ${url}</log>
      <url>${url}</url>
    </commit>
  </body>
</message>"

if [ "$mode" = "dumpit" ]
then
    sendmail=cat
fi

${sendmail} << EOM
Message-ID: <${merged}.${author}@${project}>
From: ${from}
To: ${to}
Content-type: text/xml
Subject: DeliverXML
${out}
EOM

# vim: set tw=70 :
