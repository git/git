#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

if { $argc >=2 && [lindex $argv 0] == "--working-dir" } {
	cd [lindex $argv 1]
	set argv [lrange $argv 2 end]
	incr argc -2
}

set bindir [file dirname \
            [file dirname \
             [file dirname [info script]]]]
set bindir [file join $bindir bin]
regsub -all ";" $bindir "\\;" bindir
set env(PATH) "$bindir;$env(PATH)"
unset bindir

source [file join [file dirname [info script]] git-gui.tcl]
