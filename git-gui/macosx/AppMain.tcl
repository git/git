set butexecdir {@@butexecdir@@}
if { [info exists ::env(GIT_GUI_LIB_DIR) ] } {
	set butguilib $::env(GIT_GUI_LIB_DIR)
} else {
	set butguilib {@@GITGUI_LIBDIR@@}
}

set env(PATH) "$butexecdir:$env(PATH)"

if {[string first -psn [lindex $argv 0]] == 0} {
	lset argv 0 [file join $butexecdir but-gui]
}

if {[file tail [lindex $argv 0]] eq {butk}} {
	set argv0 [lindex $argv 0]
	set AppMain_source $argv0
} else {
	set argv0 [file join $butexecdir [file tail [lindex $argv 0]]]
	set AppMain_source [file join $butguilib but-gui.tcl]
	if {[info exists env(PWD)]} {
		cd $env(PWD)
	} elseif {[pwd] eq {/}} {
		cd $env(HOME)
	}
}

unset butexecdir butguilib
set argv [lrange $argv 1 end]
source $AppMain_source
