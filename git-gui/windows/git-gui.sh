#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

if { $argc >=2 && [lindex $argv 0] == "--working-dir" } {
	set workdir [lindex $argv 1]
	cd $workdir
	if {[lindex [file split $workdir] end] eq {.git}} {
		# Workaround for Explorer right click "Git GUI Here" on .git/
		cd ..
	}
	set argv [lrange $argv 2 end]
	incr argc -2
}

set thisdir [file normalize [file dirname [info script]]]
source [file join $thisdir git-gui.tcl]
