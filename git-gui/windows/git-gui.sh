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

set basedir [file dirname \
            [file dirname \
             [file dirname [info script]]]]
set bindir [file join $basedir bin]
set bindir "$bindir;[file join $basedir mingw bin]"
regsub -all ";" $bindir "\\;" bindir
set env(PATH) "$bindir;$env(PATH)"
unset bindir

source [file join [file dirname [info script]] git-gui.tcl]
