#!/bin/sh
# Tcl ignores the next line -*- tcl -*- \
exec wish "$0" -- "$@"

if { $argc >=2 && [lindex $argv 0] == "--working-dir" } {
	cd [lindex $argv 1]
	set argv [lrange $argv 2 end]
	incr argc -2
}

set gitguidir [file dirname [info script]]
regsub -all ";" $gitguidir "\\;" gitguidir
set env(PATH) "$gitguidir;$env(PATH)"
unset gitguidir

source [file join [file dirname [info script]] git-gui.tcl]
