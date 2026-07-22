# git-gui branch delete support
# Copyright (C) 2007 Shawn Pearce

class branch_delete {

field w               ; # widget path
field w_heads         ; # listbox of local head names
field w_check         ; # revision picker for merge test
field w_delete        ; # delete button

constructor dialog {} {
	global current_branch

	make_dialog top w
	wm withdraw $w
	wm title $top [mc "%s (%s): Delete Branch" [appname] [reponame]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	ttk::label $w.header -text [mc "Delete Local Branch"] \
		-font font_uibold -anchor center
	pack $w.header -side top -fill x

	ttk::frame $w.buttons
	set w_delete $w.buttons.delete
	ttk::button $w_delete \
		-text [mc Delete] \
		-default active \
		-state disabled \
		-command [cb _delete]
	pack $w_delete -side right
	ttk::button $w.buttons.cancel \
		-text [mc Cancel] \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	ttk::labelframe $w.list -text [mc "Local Branches"]
	set w_heads $w.list.l
	slistbox $w_heads \
		-height 10 \
		-width 70 \
		-selectmode extended \
		-exportselection false
	pack $w.list.l -side left -fill both -expand 1
	pack $w.list -fill both -expand 1 -pady 5 -padx 5

	set w_check [choose_rev::new \
		$w.check \
		[mc "Delete Only If Merged Into"] \
		]
	$w_check none [mc "Always (Do not perform merge checks)"]
	pack $w.check -anchor nw -fill x -pady 5 -padx 5

	foreach h [load_all_heads] {
		if {$h ne $current_branch} {
			$w_heads insert end $h
		}
	}

	bind $w_heads <<ListboxSelect>> [cb _select]
	bind $w <Visibility> "
		grab $w
		focus $w
	"
	bind $w <Key-Escape> [list destroy $w]
	bind $w <Key-Return> [cb _delete]\;break
	wm deiconify $w
	tkwait window $w
}

method _select {} {
	if {[$w_heads curselection] eq {}} {
		$w_delete configure -state disabled
	} else {
		$w_delete configure -state normal
	}
}

method _delete {} {
	if {[catch {set check_cmt [$w_check commit_or_die]}]} {
		return
	}

	set to_delete [list]
	set not_merged [list]
	foreach i [$w_heads curselection] {
		set b [$w_heads get $i]
		if {[catch {
			set o [git rev-parse --verify "refs/heads/$b"]
		}]} continue
		if {$check_cmt ne {}} {
			if {[catch {set m [git merge-base $o $check_cmt]}]} continue
			if {$o ne $m} {
				lappend not_merged $b
				continue
			}
		}
		lappend to_delete [list $b $o]
	}
	if {$not_merged ne {}} {
		set msg "[mc "The following branches are not completely merged into %s:" [$w_check get]]

 - [join $not_merged "\n - "]"
		tk_messageBox \
			-icon info \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message $msg
	}
	if {$to_delete eq {}} return
	if {$check_cmt eq {}} {
		set msg [mc "Recovering deleted branches is difficult.\n\nDelete the selected branches?"]
		if {[tk_messageBox \
			-icon warning \
			-type yesno \
			-title [wm title $w] \
			-parent $w \
			-message $msg] ne yes} {
			return
		}
	}

	set failed {}
	foreach i $to_delete {
		set b [lindex $i 0]
		set o [lindex $i 1]
		if {[catch {git branch -D $b} err]} {
			append failed [mc " - %s:" $b] " $err\n"
		}
	}

	if {$failed ne {}} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message [mc "Failed to delete branches:\n%s" $failed]
	}

	destroy $w
}

}
