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
		-value no \
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

	checkbutton $w.options.checkout \
		-text {Checkout After Creation} \
		-variable @opt_checkout
	pack $w.options.checkout -anchor nw
	pack $w.options -anchor nw -fill x -pady 5 -padx 5

	set name $repo_config(gui.newbranchtemplate)

	bind $w <Visibility> "
		grab $w
		$w_name icursor end
		focus $w_name
	"
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _create]\;break
	tkwait window $w
}

method _create {} {
	global null_sha1 repo_config
	global all_heads current_branch

	switch -- $name_type {
	user {
		set newbranch $name
	}
	match {
		set spec [$w_rev get_tracking_branch]
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

	if {$newbranch eq $current_branch} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "'$newbranch' already exists and is the current branch."
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

	if {[catch {set new [$w_rev commit_or_die]}]} {
		return
	}

	set ref refs/heads/$newbranch
	if {[catch {set cur [git rev-parse --verify "$ref^0"]}]} {
		# Assume it does not exist, and that is what the error was.
		#
		set reflog_msg "branch: Created from [$w_rev get]"
		set cur $null_sha1
	} elseif {$opt_merge eq {no}} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Branch '$newbranch' already exists."
		focus $w_name
		return
	} else {
		set mrb {}
		catch {set mrb [git merge-base $new $cur]}
		switch -- $opt_merge {
		ff {
			if {$mrb eq $new} {
				# The current branch is actually newer.
				#
				set new $cur
			} elseif {$mrb eq $cur} {
				# The current branch is older.
				#
				set reflog_msg "merge [$w_rev get]: Fast-forward"
			} else {
				tk_messageBox \
					-icon error \
					-type ok \
					-title [wm title $w] \
					-parent $w \
					-message "Branch '$newbranch' already exists.\n\nIt cannot fast-forward to [$w_rev get].\nA merge is required."
				focus $w_name
				return
			}
		}
		reset {
			if {$mrb eq $cur} {
				# The current branch is older.
				#
				set reflog_msg "merge [$w_rev get]: Fast-forward"
			} else {
				# The current branch will lose things.
				#
				if {[_confirm_reset $this $newbranch $cur $new]} {
					set reflog_msg "reset [$w_rev get]"
				} else {
					return
				}
			}
		}
		default {
			tk_messageBox \
				-icon error \
				-type ok \
				-title [wm title $w] \
				-parent $w \
				-message "Branch '$newbranch' already exists."
			focus $w_name
			return
		}
		}
	}

	if {$new ne $cur} {
		if {[catch {
				git update-ref -m $reflog_msg $ref $new $cur
			} err]} {
			tk_messageBox \
				-icon error \
				-type ok \
				-title [wm title $w] \
				-parent $w \
				-message "Failed to create '$newbranch'.\n\n$err"
			return
		}
	}

	if {$cur eq $null_sha1} {
		lappend all_heads $newbranch
		set all_heads [lsort -uniq $all_heads]
		populate_branch_menu
	}

	destroy $w
	if {$opt_checkout} {
		switch_branch $newbranch
	}
}

method _confirm_reset {newbranch cur new} {
	set reset_ok 0
	set gitk [list do_gitk [list $cur ^$new]]

	set c $w.confirm_reset
	toplevel $c
	wm title $c "Confirm Branch Reset"
	wm geometry $c "+[winfo rootx $w]+[winfo rooty $w]"

	pack [label $c.msg1 \
		-anchor w \
		-justify left \
		-text "Resetting '$newbranch' to [$w_rev get] will lose the following commits:" \
		] -anchor w

	set list $c.list.l
	frame $c.list
	text $list \
		-font font_diff \
		-width 80 \
		-height 10 \
		-wrap none \
		-xscrollcommand [list $c.list.sbx set] \
		-yscrollcommand [list $c.list.sby set]
	scrollbar $c.list.sbx -orient h -command [list $list xview]
	scrollbar $c.list.sby -orient v -command [list $list yview]
	pack $c.list.sbx -fill x -side bottom
	pack $c.list.sby -fill y -side right
	pack $list -fill both -expand 1
	pack $c.list -fill both -expand 1 -padx 5 -pady 5

	pack [label $c.msg2 \
		-anchor w \
		-justify left \
		-text "Recovering lost commits may not be easy." \
		]
	pack [label $c.msg3 \
		-anchor w \
		-justify left \
		-text "Reset '$newbranch'?" \
		]

	frame $c.buttons
	button $c.buttons.visualize \
		-text Visualize \
		-command $gitk
	pack $c.buttons.visualize -side left
	button $c.buttons.reset \
		-text Reset \
		-command "
			set @reset_ok 1
			destroy $c
		"
	pack $c.buttons.reset -side right
	button $c.buttons.cancel \
		-default active \
		-text Cancel \
		-command [list destroy $c]
	pack $c.buttons.cancel -side right -padx 5
	pack $c.buttons -side bottom -fill x -pady 10 -padx 10

	set fd [open "| git rev-list --pretty=oneline $cur ^$new" r]
	while {[gets $fd line] > 0} {
		set abbr [string range $line 0 7]
		set subj [string range $line 41 end]
		$list insert end "$abbr  $subj\n"
	}
	close $fd
	$list configure -state disabled

	bind $c    <Key-v> $gitk

	bind $c <Visibility> "
		grab $c
		focus $c.buttons.cancel
	"
	bind $c <Key-Return> [list destroy $c]
	bind $c <Key-Escape> [list destroy $c]
	tkwait window $c
	return $reset_ok
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

}
