# git-gui branch (create/delete) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc error_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	set cmd [list tk_messageBox \
		-icon error \
		-type ok \
		-title "$title: error" \
		-message $msg]
	if {[winfo ismapped .]} {
		lappend cmd -parent .
	}
	eval $cmd
}

proc warn_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	set cmd [list tk_messageBox \
		-icon warning \
		-type ok \
		-title "$title: warning" \
		-message $msg]
	if {[winfo ismapped .]} {
		lappend cmd -parent .
	}
	eval $cmd
}

proc info_popup {msg {parent .}} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	tk_messageBox \
		-parent $parent \
		-icon info \
		-type ok \
		-title $title \
		-message $msg
}

proc ask_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	set cmd [list tk_messageBox \
		-icon question \
		-type yesno \
		-title $title \
		-message $msg]
	if {[winfo ismapped .]} {
		lappend cmd -parent .
	}
	eval $cmd
}

proc hook_failed_popup {hook msg} {
	set w .hookfail
	toplevel $w

	frame $w.m
	label $w.m.l1 -text "$hook hook failed:" \
		-anchor w \
		-justify left \
		-font font_uibold
	text $w.m.t \
		-background white -borderwidth 1 \
		-relief sunken \
		-width 80 -height 10 \
		-font font_diff \
		-yscrollcommand [list $w.m.sby set]
	label $w.m.l2 \
		-text {You must correct the above errors before committing.} \
		-anchor w \
		-justify left \
		-font font_uibold
	scrollbar $w.m.sby -command [list $w.m.t yview]
	pack $w.m.l1 -side top -fill x
	pack $w.m.l2 -side bottom -fill x
	pack $w.m.sby -side right -fill y
	pack $w.m.t -side left -fill both -expand 1
	pack $w.m -side top -fill both -expand 1 -padx 5 -pady 10

	$w.m.t insert 1.0 $msg
	$w.m.t conf -state disabled

	button $w.ok -text OK \
		-width 15 \
		-command "destroy $w"
	pack $w.ok -side bottom -anchor e -pady 10 -padx 10

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Return> "destroy $w"
	wm title $w "[appname] ([reponame]): error"
	tkwait window $w
}
