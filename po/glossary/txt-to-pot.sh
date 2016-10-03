#!/bin/sh
# This is a very, _very_, simple script to convert a tab-separated
# .txt file into a .pot/.po.
# Its not clever but it took me 2 minutes to write :)
# Michael Twomey <michael.twomey@ireland.sun.com>
# 23 March 2001
# with slight GnuCash modifications by Christian Stimming <stimming@tuhh.de>
# 19 Aug 2001, 23 Jul 2007

#check args
if [ $# -eq 0 ]
then
	cat <<!
Usage: $(basename $0) git-gui-glossary.txt > git-gui-glossary.pot
!
	exit 1;
fi

GLOSSARY_CSV="$1";

if [ ! -f "$GLOSSARY_CSV" ]
then
	echo "Can't find $GLOSSARY_CSV.";
	exit 1;
fi

cat <<!
# SOME DESCRIPTIVE TITLE.
# Copyright (C) YEAR Free Software Foundation, Inc.
# FIRST AUTHOR <EMAIL@ADDRESS>, YEAR.
#
#, fuzzy
msgid ""
msgstr ""
"Project-Id-Version: PACKAGE VERSION\n"
"POT-Creation-Date: $(date +'%Y-%m-%d %H:%M%z')\n"
"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\n"
"Last-Translator: FULL NAME <EMAIL@ADDRESS>\n"
"Language-Team: LANGUAGE <LL@li.org>\n"
"MIME-Version: 1.0\n"
"Content-Type: text/plain; charset=CHARSET\n"
"Content-Transfer-Encoding: ENCODING\n"

!

#Yes this is the most simple awk script you've ever seen :)
awk -F'\t' '{if ($2 != "") print "#. "$2; print "msgid "$1; print "msgstr \"\"\n"}' \
$GLOSSARY_CSV
