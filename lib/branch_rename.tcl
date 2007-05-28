# git-gui branch rename support
# Copyright (C) 2007 Shawn Pearce

class branch_rename {

field w
field oldname
field newname

constructor dialog {} {
	global all_heads current_branch

	make_toplevel top w
	wm title $top "[appname] ([reponame]): Rename Branch"
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	set oldname $current_branch
	set newname [get_config gui.newbranchtemplate]

	label $w.header -text {Rename Branch} -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.rename -text Rename \
		-default active \
		-command [cb _rename]
	pack $w.buttons.rename -side right
	button $w.buttons.cancel -text {Cancel} \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	frame $w.rename
	label $w.rename.oldname_l -text {Branch:}
	eval tk_optionMenu $w.rename.oldname_m @oldname $all_heads

	label $w.rename.newname_l -text {New Name:}
	entry $w.rename.newname_t \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable @newname \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {[~^:?*\[\0- ]} %S]} {return 0}
			return 1
		}

	grid $w.rename.oldname_l $w.rename.oldname_m -sticky w  -padx {0 5}
	grid $w.rename.newname_l $w.rename.newname_t -sticky we -padx {0 5}
	grid columnconfigure $w.rename 1 -weight 1
	pack $w.rename -anchor nw -fill x -pady 5 -padx 5

	bind $w <Key-Return> [cb _rename]
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Visibility> "
		grab $w
		$w.rename.newname_t icursor end
		focus $w.rename.newname_t
	"
	bind $w.header <Destroy> [list delete_this $this]
	tkwait window $w
}

method _rename {} {
	global all_heads current_branch

	if {$oldname eq {}} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Please select a branch to rename."
		focus $w.rename.oldname_m
		return
	}
	if {$newname eq {}
		|| $newname eq [get_config gui.newbranchtemplate]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Please supply a branch name."
		focus $w.rename.newname_t
		return
	}
	if {![catch {git show-ref --verify -- "refs/heads/$newname"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Branch '$newname' already exists."
		focus $w.rename.newname_t
		return
	}
	if {[catch {git check-ref-format "heads/$newname"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "We do not like '$newname' as a branch name."
		focus $w.rename.newname_t
		return
	}

	if {[catch {git branch -m $oldname $newname} err]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Failed to rename '$oldname'.\n\n$err"
		return
	}

	set oldidx [lsearch -exact -sorted $all_heads $oldname]
	if {$oldidx >= 0} {
		set all_heads [lreplace $all_heads $oldidx $oldidx]
	}
	lappend all_heads $newname
	set all_heads [lsort $all_heads]
	populate_branch_menu

	if {$current_branch eq $oldname} {
		set current_branch $newname
	}

	destroy $w
}

}
