#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

# This is an implementation of a simple yes no dialog
# which is injected into the git commandline by git gui
# in case a yesno question needs to be answered.
#
# The window title, which defaults to "Question?", can be
# overridden via the optional `--title` command-line
# option.

set NS {}
set use_ttk [package vsatisfies [package provide Tk] 8.5]
if {$use_ttk} {
	set NS ttk
}

set title "Question?"
if {$argc < 1} {
	puts stderr "Usage: $argv0 <question>"
	exit 1
} else {
	if {$argc > 2 && [lindex $argv 0] == "--title"} {
		set title [lindex $argv 1]
		set argv [lreplace $argv 0 1]
	}
	set prompt [join $argv " "]
}

${NS}::frame .t
${NS}::label .t.m -text $prompt -justify center -width 400px
.t.m configure -wraplength 400px
pack .t.m -side top -fill x -padx 20 -pady 20 -expand 1
pack .t -side top -fill x -ipadx 20 -ipady 20 -expand 1

${NS}::frame .b
${NS}::frame .b.left -width 200
${NS}::button .b.yes -text Yes -command {exit 0}
${NS}::button .b.no  -text No  -command {exit 1}

pack .b.left -side left -expand 1 -fill x
pack .b.yes -side left -expand 1
pack .b.no -side right -expand 1 -ipadx 5
pack .b -side bottom -fill x -ipadx 20 -ipady 15

bind . <Key-Return> {exit 0}
bind . <Key-Escape> {exit 1}

if {$::tcl_platform(platform) eq {windows}} {
	set icopath [file dirname [file normalize $argv0]]
	if {[file tail $icopath] eq {git-core}} {
		set icopath [file dirname $icopath]
	}
	set icopath [file dirname $icopath]
	set icopath [file join $icopath share git git-for-windows.ico]
	if {[file exists $icopath]} {
		wm iconbitmap . -default $icopath
	}
}

if {$::tcl_platform(platform) eq {windows}} {
	set icopath [file dirname [file normalize $argv0]]
	if {[file tail $icopath] eq {git-core}} {
		set icopath [file dirname $icopath]
	}
	set icopath [file dirname $icopath]
	set icopath [file join $icopath share git git-for-windows.ico]
	if {[file exists $icopath]} {
		wm iconbitmap . -default $icopath
	}
}

wm title . $title
tk::PlaceWindow .
