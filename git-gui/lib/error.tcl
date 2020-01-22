# git-gui branch (create/delete) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc _error_parent {} {
	set p [grab current .]
	if {$p eq {}} {
		return .
	}
	return $p
}

proc error_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	set cmd [list tk_messageBox \
		-icon error \
		-type ok \
		-title [mc "%s: error" $title] \
		-message $msg]
	if {[winfo ismapped [_error_parent]]} {
		lappend cmd -parent [_error_parent]
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
		-title [mc "%s: warning" $title] \
		-message $msg]
	if {[winfo ismapped [_error_parent]]} {
		lappend cmd -parent [_error_parent]
	}
	eval $cmd
}

proc info_popup {msg} {
	set title [appname]
	if {[reponame] ne {}} {
		append title " ([reponame])"
	}
	tk_messageBox \
		-parent [_error_parent] \
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
	if {[winfo ismapped [_error_parent]]} {
		lappend cmd -parent [_error_parent]
	}
	eval $cmd
}

proc hook_failed_popup {hook msg {is_fatal 1}} {
	global use_ttk NS
	set w .hookfail
	Dialog $w
	wm withdraw $w

	${NS}::frame $w.m
	${NS}::label $w.m.l1 -text [mc "%s hook failed:" $hook] \
		-anchor w \
		-justify left \
		-font font_uibold
	text $w.m.t \
		-background white \
		-foreground black \
		-borderwidth 1 \
		-relief sunken \
		-width 80 -height 10 \
		-font font_diff \
		-yscrollcommand [list $w.m.sby set]
	${NS}::scrollbar $w.m.sby -command [list $w.m.t yview]
	pack $w.m.l1 -side top -fill x
	if {$is_fatal} {
		${NS}::label $w.m.l2 \
			-text [mc "You must correct the above errors before committing."] \
			-anchor w \
			-justify left \
			-font font_uibold
		pack $w.m.l2 -side bottom -fill x
	}
	pack $w.m.sby -side right -fill y
	pack $w.m.t -side left -fill both -expand 1
	pack $w.m -side top -fill both -expand 1 -padx 5 -pady 10

	$w.m.t insert 1.0 $msg
	$w.m.t conf -state disabled

	${NS}::button $w.ok -text OK \
		-width 15 \
		-command "destroy $w"
	pack $w.ok -side bottom -anchor e -pady 10 -padx 10

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Return> "destroy $w"
	wm title $w [mc "%s (%s): error" [appname] [reponame]]
	wm deiconify $w
	tkwait window $w
}
