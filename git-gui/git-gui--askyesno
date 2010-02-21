#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

# This is an implementation of a simple yes no dialog
# which is injected into the git commandline by git gui
# in case a yesno question needs to be answered.

set NS {}
set use_ttk [package vsatisfies [package provide Tk] 8.5]
if {$use_ttk} {
	set NS ttk
}

if {$argc < 1} {
	puts stderr "Usage: $argv0 <question>"
	exit 1
} else {
	set prompt [join $argv " "]
}

${NS}::frame .t
${NS}::label .t.m -text $prompt -justify center -width 40
.t.m configure -wraplength 400
pack .t.m -side top -fill x -padx 20 -pady 20 -expand 1
pack .t -side top -fill x -ipadx 20 -ipady 20 -expand 1

${NS}::frame .b
${NS}::frame .b.left -width 200
${NS}::button .b.yes -text Yes -command yes
${NS}::button .b.no  -text No  -command no


pack .b.left -side left -expand 1 -fill x
pack .b.yes -side left -expand 1
pack .b.no -side right -expand 1 -ipadx 5
pack .b -side bottom -fill x -ipadx 20 -ipady 15

bind . <Key-Return> {exit 0}
bind . <Key-Escape> {exit 1}

proc no {} {
	exit 1
}

proc yes {} {
	exit 0
}

wm title . "Question?"
tk::PlaceWindow .
