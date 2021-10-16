#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

# This is a trivial implementation of an SSH_ASKPASS handler.
# Git-gui uses this script if none are already configured.

package require Tk

set answer {}
set yesno  0
set rc     255

if {$argc < 1} {
	set prompt "Enter your OpenSSH passphrase:"
} else {
	set prompt [join $argv " "]
	if {[regexp -nocase {\(yes\/no\)\?\s*$} $prompt]} {
		set yesno 1
	}
}

message .m -text $prompt -justify center -aspect 4000
pack .m -side top -fill x -padx 20 -pady 20 -expand 1

entry .e -textvariable answer -width 50
pack .e -side top -fill x -padx 10 -pady 10

proc on_show_input_changed {args} {
	global show_input
	if {$show_input} {
		.e configure -show ""
	} else {
		.e configure -show "*"
	}
}
trace add variable show_input write "on_show_input_changed"

set show_input 0

if {!$yesno} {
	checkbutton .cb_show -text "Show input" -variable show_input
	pack .cb_show -side top -anchor nw
}

frame .b
button .b.ok     -text OK     -command finish
button .b.cancel -text Cancel -command cancel

pack .b.ok -side left -expand 1
pack .b.cancel -side right -expand 1
pack .b -side bottom -fill x -padx 10 -pady 10

bind . <Visibility> {focus -force .e}
bind . <Key-Return> [list .b.ok invoke]
bind . <Key-Escape> [list .b.cancel invoke]
bind . <Destroy>    {set rc $rc}

proc cancel {} {
	set ::rc 255
}

proc finish {} {
	if {$::yesno} {
		if {$::answer ne "yes" && $::answer ne "no"} {
			tk_messageBox -icon error -title "Error" -type ok \
				-message "Only 'yes' or 'no' input allowed."
			return
		}
	}

	# On Windows, force the encoding to UTF-8: it is what `git.exe` expects
	if {$::tcl_platform(platform) eq {windows}} {
		set ::answer [encoding convertto utf-8 $::answer]
	}

	puts $::answer
	set ::rc 0
}

wm title . "OpenSSH"
tk::PlaceWindow .
vwait rc
exit $rc
