# git-gui branch create support
# Copyright (C) 2006, 2007 Shawn Pearce

class branch_create {

field w              ; # widget path
field w_rev          ; # mega-widget to pick the initial revision
field w_name         ; # new branch name widget

field name         {}; # name of the branch the user has chosen
field name_type  user; # type of branch name to use

field opt_merge    ff; # type of merge to apply to existing branch
field opt_checkout  1; # automatically checkout the new branch?
field opt_fetch     1; # refetch tracking branch if used?
field reset_ok      0; # did the user agree to reset?

constructor dialog {} {
	global repo_config

	make_toplevel top w
	wm title $top "[appname] ([reponame]): Create Branch"
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	label $w.header -text {Create New Branch} -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text Create \
		-default active \
		-command [cb _create]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.desc -text {Branch Name}
	radiobutton $w.desc.name_r \
		-anchor w \
		-text {Name:} \
		-value user \
		-variable @name_type
	set w_name $w.desc.name_t
	entry $w_name \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable @name \
		-validate key \
		-validatecommand [cb _validate %d %S]
	grid $w.desc.name_r $w_name -sticky we -padx {0 5}

	radiobutton $w.desc.match_r \
		-anchor w \
		-text {Match Tracking Branch Name} \
		-value match \
		-variable @name_type
	grid $w.desc.match_r -sticky we -padx {0 5} -columnspan 2

	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	set w_rev [::choose_rev::new $w.rev {Starting Revision}]
	pack $w.rev -anchor nw -fill both -expand 1 -pady 5 -padx 5

	labelframe $w.options -text {Options}

	frame $w.options.merge
	label $w.options.merge.l -text {Update Existing Branch:}
	pack $w.options.merge.l -side left
	radiobutton $w.options.merge.no \
		-text No \
		-value none \
		-variable @opt_merge
	pack $w.options.merge.no -side left
	radiobutton $w.options.merge.ff \
		-text {Fast Forward Only} \
		-value ff \
		-variable @opt_merge
	pack $w.options.merge.ff -side left
	radiobutton $w.options.merge.reset \
		-text {Reset} \
		-value reset \
		-variable @opt_merge
	pack $w.options.merge.reset -side left
	pack $w.options.merge -anchor nw

	checkbutton $w.options.fetch \
		-text {Fetch Tracking Branch} \
		-variable @opt_fetch
	pack $w.options.fetch -anchor nw

	checkbutton $w.options.checkout \
		-text {Checkout After Creation} \
		-variable @opt_checkout
	pack $w.options.checkout -anchor nw
	pack $w.options -anchor nw -fill x -pady 5 -padx 5

	trace add variable @name_type write [cb _select]

	set name $repo_config(gui.newbranchtemplate)
	if {[is_config_true gui.matchtrackingbranch]} {
		set name_type match
	}

	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _create]\;break
	tkwait window $w
}

method _create {} {
	global repo_config
	global M1B

	set spec [$w_rev get_tracking_branch]
	switch -- $name_type {
	user {
		set newbranch $name
	}
	match {
		if {$spec eq {}} {
			tk_messageBox \
				-icon error \
				-type ok \
				-title [wm title $w] \
				-parent $w \
				-message "Please select a tracking branch."
			return
		}
		if {![regsub ^refs/heads/ [lindex $spec 2] {} newbranch]} {
			tk_messageBox \
				-icon error \
				-type ok \
				-title [wm title $w] \
				-parent $w \
				-message "Tracking branch [$w get] is not a branch in the remote repository."
			return
		}
	}
	}

	if {$newbranch eq {}
		|| $newbranch eq $repo_config(gui.newbranchtemplate)} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Please supply a branch name."
		focus $w_name
		return
	}

	if {[catch {git check-ref-format "heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "'$newbranch' is not an acceptable branch name."
		focus $w_name
		return
	}

	if {$spec ne {} && $opt_fetch} {
		set new {}
	} elseif {[catch {set new [$w_rev commit_or_die]}]} {
		return
	}

	set co [::checkout_op::new \
		[$w_rev get] \
		$new \
		refs/heads/$newbranch]
	$co parent $w
	$co enable_create   1
	$co enable_merge    $opt_merge
	$co enable_checkout $opt_checkout
	if {$spec ne {} && $opt_fetch} {
		$co enable_fetch $spec
	}

	if {[$co run]} {
		destroy $w
	} else {
		focus $w_name
	}
}

method _validate {d S} {
	if {$d == 1} {
		if {[regexp {[~^:?*\[\0- ]} $S]} {
			return 0
		}
		if {[string length $S] > 0} {
			set name_type user
		}
	}
	return 1
}

method _select {args} {
	if {$name_type eq {match}} {
		$w_rev pick_tracking_branch
	}
}

method _visible {} {
	grab $w
	if {$name_type eq {user}} {
		$w_name icursor end
		focus $w_name
	}
}

}
