#!/bin/sh
# Copyright 2008 Lukas Sandström <luksan@gmail.com>
#
# AppendPatch - A script to be used together with ExternalEditorRevived
# for Mozilla Thunderbird to properly include patches inline in e-mails.

# ExternalEditorRevived extension can be installed using the Add-ons
# manager in thunderbird, the source is available at
# https://github.com/Frederick888/external-editor-revived

CONFFILE=~/.appprc

if [ -e "$CONFFILE" ] ; then
	LAST_DIR=$(grep -m 1 "^LAST_DIR=" "${CONFFILE}"|sed -e 's/^LAST_DIR=//')
	cd "${LAST_DIR}"
else
	cd > /dev/null
fi

PATCH=$(zenity --file-selection)

if [ "$?" != "0" ] ; then
	#zenity --error --text "No patchfile given."
	exit 1
fi

cd - > /dev/null

# The headers are separated from the message body by a blanks
# line. However the message uses CR LF line ending so on platforms
# where the native line ending is LF we see a line with a single CR.
SEP="$(printf '^\r\\{0,1\\}$')"
SUBJECT=$(sed -n -e '/^Subject: /p' "${PATCH}")
HEADERS=$(sed -e "/${SEP}/"',$d' $1)
BODY=$(sed -e "1,/${SEP}/d" $1)
CMT_MSG=$(sed -e '1,/^$/d' -e '/^---$/,$d' "${PATCH}")
DIFF=$(sed -e '1,/^---$/d' "${PATCH}")

CCS=$(printf '%s\n%s\n' "$CMT_MSG" "$HEADERS" | sed -n -e 's/^Cc: \(.*\)$/\1,/gp' \
	-e 's/^Signed-off-by: \(.*\)/\1,/gp')

echo "$SUBJECT" > $1
echo "Cc: $CCS" >> $1
echo "$HEADERS" | sed -e '/^Subject: /d' -e '/^Cc: /d' >> $1
echo >> $1

echo "$CMT_MSG" >> $1
echo "---" >> $1
if [ "x${BODY}x" != "xx" ] ; then
	echo >> $1
	echo "$BODY" >> $1
	echo >> $1
fi
echo "$DIFF" >> $1

LAST_DIR=$(dirname "${PATCH}")

grep -v "^LAST_DIR=" "${CONFFILE}" > "${CONFFILE}_"
echo "LAST_DIR=${LAST_DIR}" >> "${CONFFILE}_"
mv "${CONFFILE}_" "${CONFFILE}"
