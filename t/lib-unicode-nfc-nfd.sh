# Help detect how Unicode NFC and NFD are handled on the filesystem.

# A simple character that has a NFD form.
#
# NFC:       U+00e9 LATIN SMALL LETTER E WITH ACUTE
# UTF8(NFC): \xc3 \xa9
#
# NFD:       U+0065 LATIN SMALL LETTER E
#            U+0301 COMBINING ACUTE ACCENT
# UTF8(NFD): \x65  +  \xcc \x81
#
utf8_nfc=$(printf "\xc3\xa9")
utf8_nfd=$(printf "\x65\xcc\x81")

# Is the OS or the filesystem "Unicode composition sensitive"?
#
# That is, does the OS or the filesystem allow files to exist with
# both the NFC and NFD spellings?  Or, does the OS/FS lie to us and
# tell us that the NFC and NFD forms are equivalent.
#
# This is or may be independent of what type of filesystem we have,
# since it might be handled by the OS at a layer above the FS.
# Testing shows on MacOS using APFS, HFS+, and FAT32 reports a
# collision, for example.
#
# This does not tell us how the Unicode pathname will be spelled
# on disk, but rather only that the two spelling "collide".  We
# will examine the actual on disk spelling in a later prereq.
#
test_lazy_prereq UNICODE_COMPOSITION_SENSITIVE '
	mkdir trial_${utf8_nfc} &&
	mkdir trial_${utf8_nfd}
'

# Is the spelling of an NFC pathname preserved on disk?
#
# On MacOS with HFS+ and FAT32, NFC paths are converted into NFD
# and on APFS, NFC paths are preserved.  As we have established
# above, this is independent of "composition sensitivity".
#
# 0000000 63 5f c3 a9
#
# (/usr/bin/od output contains different amount of whitespace
# on different platforms, so we need the wildcards here.)
#
test_lazy_prereq UNICODE_NFC_PRESERVED '
	mkdir c_${utf8_nfc} &&
	ls | od -t x1 | grep "63 *5f *c3 *a9"
'

# Is the spelling of an NFD pathname preserved on disk?
#
# 0000000 64 5f 65 cc 81
#
test_lazy_prereq UNICODE_NFD_PRESERVED '
	mkdir d_${utf8_nfd} &&
	ls | od -t x1 | grep "64 *5f *65 *cc *81"
'
	mkdir c_${utf8_nfc} &&
	mkdir d_${utf8_nfd} &&

# The following _DOUBLE_ forms are more for my curiosity,
# but there may be quirks lurking when there are multiple
# combining characters in non-canonical order.

# Unicode also allows multiple combining characters
# that can be decomposed in pieces.
#
# NFC:        U+1f67 GREEK SMALL LETTER OMEGA WITH DASIA AND PERISPOMENI
# UTF8(NFC):  \xe1 \xbd \xa7
#
# NFD1:       U+1f61 GREEK SMALL LETTER OMEGA WITH DASIA
#             U+0342 COMBINING GREEK PERISPOMENI
# UTF8(NFD1): \xe1 \xbd \xa1  +  \xcd \x82
#
# But U+1f61 decomposes into
# NFD2:       U+03c9 GREEK SMALL LETTER OMEGA
#             U+0314 COMBINING REVERSED COMMA ABOVE
# UTF8(NFD2): \xcf \x89  +  \xcc \x94
#
# Yielding:   \xcf \x89  +  \xcc \x94  +  \xcd \x82
#
# Note that I've used the canonical ordering of the
# combinining characters.  It is also possible to
# swap them.  My testing shows that that non-standard
# ordering also causes a collision in mkdir.  However,
# the resulting names don't draw correctly on the
# terminal (implying that the on-disk format also has
# them out of order).
#
greek_nfc=$(printf "\xe1\xbd\xa7")
greek_nfd1=$(printf "\xe1\xbd\xa1\xcd\x82")
greek_nfd2=$(printf "\xcf\x89\xcc\x94\xcd\x82")

# See if a double decomposition also collides.
#
test_lazy_prereq UNICODE_DOUBLE_COMPOSITION_SENSITIVE '
	mkdir trial_${greek_nfc} &&
	mkdir trial_${greek_nfd2}
'

# See if the NFC spelling appears on the disk.
#
test_lazy_prereq UNICODE_DOUBLE_NFC_PRESERVED '
	mkdir c_${greek_nfc} &&
	ls | od -t x1 | grep "63 *5f *e1 *bd *a7"
'

# See if the NFD spelling appears on the disk.
#
test_lazy_prereq UNICODE_DOUBLE_NFD_PRESERVED '
	mkdir d_${greek_nfd2} &&
	ls | od -t x1 | grep "64 *5f *cf *89 *cc *94 *cd *82"
'

# The following is for debugging. I found it useful when
# trying to understand the various (OS, FS) quirks WRT
# Unicode and how composition/decomposition is handled.
# For example, when trying to understand how (macOS, APFS)
# and (macOS, HFS) and (macOS, FAT32) compare.
#
# It is rather noisy, so it is disabled by default.
#
if test "$unicode_debug" = "true"
then
	if test_have_prereq UNICODE_COMPOSITION_SENSITIVE
	then
		echo NFC and NFD are distinct on this OS/filesystem.
	else
		echo NFC and NFD are aliases on this OS/filesystem.
	fi

	if test_have_prereq UNICODE_NFC_PRESERVED
	then
		echo NFC maintains original spelling.
	else
		echo NFC is modified.
	fi

	if test_have_prereq UNICODE_NFD_PRESERVED
	then
		echo NFD maintains original spelling.
	else
		echo NFD is modified.
	fi

	if test_have_prereq UNICODE_DOUBLE_COMPOSITION_SENSITIVE
	then
		echo DOUBLE NFC and NFD are distinct on this OS/filesystem.
	else
		echo DOUBLE NFC and NFD are aliases on this OS/filesystem.
	fi

	if test_have_prereq UNICODE_DOUBLE_NFC_PRESERVED
	then
		echo Double NFC maintains original spelling.
	else
		echo Double NFC is modified.
	fi

	if test_have_prereq UNICODE_DOUBLE_NFD_PRESERVED
	then
		echo Double NFD maintains original spelling.
	else
		echo Double NFD is modified.
	fi
fi
