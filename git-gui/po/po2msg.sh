#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec tclsh "$0" -- "$@"

# This is a really stupid program, which serves as an alternative to
# msgfmt.  It _only_ translates to Tcl mode, does _not_ validate the
# input, and does _not_ output any statistics.

proc u2a {s} {
	set res ""
	foreach i [split $s ""] {
		scan $i %c c
		if {$c<128} {
			# escape '[', '\', '$' and ']'
			if {$c == 0x5b || $c == 0x5d || $c == 0x24} {
				append res "\\"
			}
			append res $i
		} else {
			append res \\u[format %04.4x $c]
		}
	}
	return $res
}

set output_directory "."
set lang "dummy"
set files [list]
set show_statistics 0

# parse options
for {set i 0} {$i < $argc} {incr i} {
	set arg [lindex $argv $i]
	if {$arg == "--statistics"} {
		incr show_statistics
		continue
	}
	if {$arg == "--tcl"} {
		# we know
		continue
	}
	if {$arg == "-l"} {
		incr i
		set lang [lindex $argv $i]
		continue
	}
	if {$arg == "-d"} {
		incr i
		set tmp [lindex $argv $i]
		regsub "\[^/\]$" $tmp "&/" output_directory
		continue
	}
	lappend files $arg
}

proc flush_msg {} {
	global msgid msgstr mode lang out fuzzy
	global translated_count fuzzy_count not_translated_count

	if {![info exists msgid] || $mode == ""} {
		return
	}
	set mode ""
	if {$fuzzy == 1} {
		incr fuzzy_count
		set fuzzy 0
		return
	}

	if {$msgid == ""} {
		set prefix "set ::msgcat::header"
	} else {
		if {$msgstr == ""} {
			incr not_translated_count
			return
		}
		set prefix "::msgcat::mcset $lang \"[u2a $msgid]\""
		incr translated_count
	}

	puts $out "$prefix \"[u2a $msgstr]\""
}

set fuzzy 0
set translated_count 0
set fuzzy_count 0
set not_translated_count 0
foreach file $files {
	regsub "^.*/\(\[^/\]*\)\.po$" $file "$output_directory\\1.msg" outfile
	set in [open $file "r"]
	fconfigure $in -encoding utf-8
	set out [open $outfile "w"]

	set mode ""
	while {[gets $in line] >= 0} {
		if {[regexp "^#" $line]} {
			if {[regexp ", fuzzy" $line]} {
				set fuzzy 1
			} else {
				flush_msg
			}
			continue
		} elseif {[regexp "^msgid \"(.*)\"$" $line dummy match]} {
			flush_msg
			set msgid $match
			set mode "msgid"
		} elseif {[regexp "^msgstr \"(.*)\"$" $line dummy match]} {
			set msgstr $match
			set mode "msgstr"
		} elseif {$line == ""} {
			flush_msg
		} elseif {[regexp "^\"(.*)\"$" $line dummy match]} {
			if {$mode == "msgid"} {
				append msgid $match
			} elseif {$mode == "msgstr"} {
				append msgstr $match
			} else {
				puts stderr "I do not know what to do: $match"
			}
		} else {
			puts stderr "Cannot handle $line"
		}
	}
	flush_msg
	close $in
	close $out
}

if {$show_statistics} {
	set str ""

	append str  "$translated_count translated message"
	if {$translated_count != 1} {
		append str s
	}

	if {$fuzzy_count > 1} {
		append str  ", $fuzzy_count fuzzy translation"
		if {$fuzzy_count != 1} {
			append str s
		}
	}
	if {$not_translated_count > 0} {
		append str  ", $not_translated_count untranslated message"
		if {$not_translated_count != 1} {
			append str s
		}
	}

	append str  .
	puts $str
}
