# git-gui branch create support
# Copyright (C) 2006, 2007 Shawn Pearce

class branch_create {

field w              ; # widget path
field w_rev          ; # mega-widget to pick the initial revision
field w_name         ; # new branch name widget

field name         {}; # name of the branch the user has chosen
field opt_checkout  1; # automatically checkout the new branch?

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

	labelframe $w.desc -text {Branch Description}
	label $w.desc.name_r \
		-anchor w \
		-text {Name:}
	set w_name $w.desc.name_t
	entry $w_name \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable @name \
		-validate key \
		-validatecommand [cb _validate %d %S]
	grid $w.desc.name_r $w_name -sticky we -padx {0 5}

	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	set w_rev [::choose_rev::new $w.rev {Starting Revision}]
	pack $w.rev -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.postActions -text {Post Creation Actions}
	checkbutton $w.postActions.checkout \
		-text {Checkout after creation} \
		-variable @opt_checkout
	pack $w.postActions.checkout -anchor nw
	pack $w.postActions -anchor nw -fill x -pady 5 -padx 5

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
	global all_heads

	set newbranch $name
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
	if {![catch {git show-ref --verify -- "refs/heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Branch '$newbranch' already exists."
		focus $w_name
		return
	}
	if {[catch {git check-ref-format "heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "We do not like '$newbranch' as a branch name."
		focus $w_name
		return
	}

	if {[catch {set cmt [$w_rev get_commit]}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Invalid starting revision: [$w_rev get]"
		return
	}
	if {[catch {
			git update-ref \
				-m "branch: Created from [$w_rev get]" \
				"refs/heads/$newbranch" \
				$cmt \
				$null_sha1
		} err]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Failed to create '$newbranch'.\n\n$err"
		return
	}

	lappend all_heads $newbranch
	set all_heads [lsort $all_heads]
	populate_branch_menu
	destroy $w
	if {$opt_checkout} {
		switch_branch $newbranch
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

}
