# git-gui about git-gui dialog
# Copyright (C) 2006, 2007 Shawn Pearce

proc do_about {} {
	global appvers copyright oguilib
	global tcl_patchLevel tk_patchLevel
	global ui_comm_spell

	set w .about_dialog
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	pack [git_logo $w.git_logo] -side left -fill y -padx 10 -pady 10
	label $w.header -text [mc "About %s" [appname]] \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.close -text {Close} \
		-default active \
		-command [list destroy $w]
	pack $w.buttons.close -side right
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	label $w.desc \
		-text "[mc "git-gui - a graphical user interface for Git."]\n$copyright" \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid
	pack $w.desc -side top -fill x -padx 5 -pady 5

	set v {}
	append v "git-gui version $appvers\n"
	append v "[git version]\n"
	append v "\n"
	if {$tcl_patchLevel eq $tk_patchLevel} {
		append v "Tcl/Tk version $tcl_patchLevel"
	} else {
		append v "Tcl version $tcl_patchLevel"
		append v ", Tk version $tk_patchLevel"
	}
	if {[info exists ui_comm_spell]
		&& [$ui_comm_spell version] ne {}} {
		append v "\n"
		append v [$ui_comm_spell version]
	}

	set d {}
	append d "git wrapper: $::_git\n"
	append d "git exec dir: [gitexec]\n"
	append d "git-gui lib: $oguilib"

	label $w.vers \
		-text $v \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid
	pack $w.vers -side top -fill x -padx 5 -pady 5

	label $w.dirs \
		-text $d \
		-padx 5 -pady 5 \
		-justify left \
		-anchor w \
		-borderwidth 1 \
		-relief solid
	pack $w.dirs -side top -fill x -padx 5 -pady 5

	menu $w.ctxm -tearoff 0
	$w.ctxm add command \
		-label {Copy} \
		-command "
		clipboard clear
		clipboard append -format STRING -type STRING -- \[$w.vers cget -text\]
	"

	bind $w <Visibility> "grab $w; focus $w.buttons.close"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> "destroy $w"
	bind_button3 $w.vers "tk_popup $w.ctxm %X %Y; grab $w; focus $w"
	wm title $w "About [appname]"
	tkwait window $w
}
