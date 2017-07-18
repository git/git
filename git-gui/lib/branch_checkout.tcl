# git-gui branch checkout support
# Copyright (C) 2007 Shawn Pearce

class branch_checkout {

field w              ; # widget path
field w_rev          ; # mega-widget to pick the initial revision

field opt_fetch     1; # refetch tracking branch if used?
field opt_detach    0; # force a detached head case?

constructor dialog {} {
	global use_ttk NS
	make_dialog top w
	wm withdraw $w
	wm title $top [append "[appname] ([reponame]): " [mc "Checkout Branch"]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	${NS}::label $w.header -text [mc "Checkout Branch"] \
		-font font_uibold -anchor center
	pack $w.header -side top -fill x

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.create -text [mc Checkout] \
		-default active \
		-command [cb _checkout]
	pack $w.buttons.create -side right
	${NS}::button $w.buttons.cancel -text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	set w_rev [::choose_rev::new $w.rev [mc Revision]]
	$w_rev bind_listbox <Double-Button-1> [cb _checkout]
	pack $w.rev -anchor nw -fill both -expand 1 -pady 5 -padx 5

	${NS}::labelframe $w.options -text [mc Options]

	${NS}::checkbutton $w.options.fetch \
		-text [mc "Fetch Tracking Branch"] \
		-variable @opt_fetch
	pack $w.options.fetch -anchor nw

	${NS}::checkbutton $w.options.detach \
		-text [mc "Detach From Local Branch"] \
		-variable @opt_detach
	pack $w.options.detach -anchor nw

	pack $w.options -anchor nw -fill x -pady 5 -padx 5

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _checkout]\;break
	wm deiconify $w
	tkwait window $w
}

method _checkout {} {
	set spec [$w_rev get_tracking_branch]
	if {$spec ne {} && $opt_fetch} {
		set new {}
	} elseif {[catch {set new [$w_rev commit_or_die]}]} {
		return
	}

	if {$opt_detach} {
		set ref {}
	} else {
		set ref [$w_rev get_local_branch]
	}

	set co [::checkout_op::new [$w_rev get] $new $ref]
	$co parent $w
	$co enable_checkout 1
	if {$spec ne {} && $opt_fetch} {
		$co enable_fetch $spec
	}

	if {[$co run]} {
		destroy $w
	} else {
		$w_rev focus_filter
	}
}

method _visible {} {
	grab $w
	$w_rev focus_filter
}

}
