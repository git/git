# git-gui branch (create/delete) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc load_all_heads {} {
	global all_heads

	set all_heads [list]
	set fd [open "| git for-each-ref --format=%(refname) refs/heads" r]
	while {[gets $fd line] > 0} {
		if {[is_tracking_branch $line]} continue
		if {![regsub ^refs/heads/ $line {} name]} continue
		lappend all_heads $name
	}
	close $fd

	set all_heads [lsort $all_heads]
}

proc load_all_tags {} {
	set all_tags [list]
	set fd [open "| git for-each-ref --format=%(refname) refs/tags" r]
	while {[gets $fd line] > 0} {
		if {![regsub ^refs/tags/ $line {} name]} continue
		lappend all_tags $name
	}
	close $fd

	return [lsort $all_tags]
}

proc populate_branch_menu {} {
	global all_heads disable_on_lock

	set m .mbar.branch
	set last [$m index last]
	for {set i 0} {$i <= $last} {incr i} {
		if {[$m type $i] eq {separator}} {
			$m delete $i last
			set new_dol [list]
			foreach a $disable_on_lock {
				if {[lindex $a 0] ne $m || [lindex $a 2] < $i} {
					lappend new_dol $a
				}
			}
			set disable_on_lock $new_dol
			break
		}
	}

	if {$all_heads ne {}} {
		$m add separator
	}
	foreach b $all_heads {
		$m add radiobutton \
			-label $b \
			-command [list switch_branch $b] \
			-variable current_branch \
			-value $b
		lappend disable_on_lock \
			[list $m entryconf [$m index last] -state]
	}
}

proc do_create_branch_action {w} {
	global all_heads null_sha1 repo_config
	global create_branch_checkout create_branch_revtype
	global create_branch_head create_branch_trackinghead
	global create_branch_name create_branch_revexp
	global create_branch_tag

	set newbranch $create_branch_name
	if {$newbranch eq {}
		|| $newbranch eq $repo_config(gui.newbranchtemplate)} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Please supply a branch name."
		focus $w.desc.name_t
		return
	}
	if {![catch {git show-ref --verify -- "refs/heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Branch '$newbranch' already exists."
		focus $w.desc.name_t
		return
	}
	if {[catch {git check-ref-format "heads/$newbranch"}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "We do not like '$newbranch' as a branch name."
		focus $w.desc.name_t
		return
	}

	set rev {}
	switch -- $create_branch_revtype {
	head {set rev $create_branch_head}
	tracking {set rev $create_branch_trackinghead}
	tag {set rev $create_branch_tag}
	expression {set rev $create_branch_revexp}
	}
	if {[catch {set cmt [git rev-parse --verify "${rev}^0"]}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Invalid starting revision: $rev"
		return
	}
	if {[catch {
			git update-ref \
				-m "branch: Created from $rev" \
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
	if {$create_branch_checkout} {
		switch_branch $newbranch
	}
}

proc radio_selector {varname value args} {
	upvar #0 $varname var
	set var $value
}

trace add variable create_branch_head write \
	[list radio_selector create_branch_revtype head]
trace add variable create_branch_trackinghead write \
	[list radio_selector create_branch_revtype tracking]
trace add variable create_branch_tag write \
	[list radio_selector create_branch_revtype tag]

trace add variable delete_branch_head write \
	[list radio_selector delete_branch_checktype head]
trace add variable delete_branch_trackinghead write \
	[list radio_selector delete_branch_checktype tracking]

proc do_create_branch {} {
	global all_heads current_branch repo_config
	global create_branch_checkout create_branch_revtype
	global create_branch_head create_branch_trackinghead
	global create_branch_name create_branch_revexp
	global create_branch_tag

	set w .branch_editor
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Create New Branch} \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text Create \
		-default active \
		-command [list do_create_branch_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.desc -text {Branch Description}
	label $w.desc.name_l -text {Name:}
	entry $w.desc.name_t \
		-borderwidth 1 \
		-relief sunken \
		-width 40 \
		-textvariable create_branch_name \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {[~^:?*\[\0- ]} %S]} {return 0}
			return 1
		}
	grid $w.desc.name_l $w.desc.name_t -sticky we -padx {0 5}
	grid columnconfigure $w.desc 1 -weight 1
	pack $w.desc -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.from -text {Starting Revision}
	if {$all_heads ne {}} {
		radiobutton $w.from.head_r \
			-text {Local Branch:} \
			-value head \
			-variable create_branch_revtype
		eval tk_optionMenu $w.from.head_m create_branch_head $all_heads
		grid $w.from.head_r $w.from.head_m -sticky w
	}
	set all_trackings [all_tracking_branches]
	if {$all_trackings ne {}} {
		set create_branch_trackinghead [lindex $all_trackings 0]
		radiobutton $w.from.tracking_r \
			-text {Tracking Branch:} \
			-value tracking \
			-variable create_branch_revtype
		eval tk_optionMenu $w.from.tracking_m \
			create_branch_trackinghead \
			$all_trackings
		grid $w.from.tracking_r $w.from.tracking_m -sticky w
	}
	set all_tags [load_all_tags]
	if {$all_tags ne {}} {
		set create_branch_tag [lindex $all_tags 0]
		radiobutton $w.from.tag_r \
			-text {Tag:} \
			-value tag \
			-variable create_branch_revtype
		eval tk_optionMenu $w.from.tag_m create_branch_tag $all_tags
		grid $w.from.tag_r $w.from.tag_m -sticky w
	}
	radiobutton $w.from.exp_r \
		-text {Revision Expression:} \
		-value expression \
		-variable create_branch_revtype
	entry $w.from.exp_t \
		-borderwidth 1 \
		-relief sunken \
		-width 50 \
		-textvariable create_branch_revexp \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {\s} %S]} {return 0}
			if {%d == 1 && [string length %S] > 0} {
				set create_branch_revtype expression
			}
			return 1
		}
	grid $w.from.exp_r $w.from.exp_t -sticky we -padx {0 5}
	grid columnconfigure $w.from 1 -weight 1
	pack $w.from -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.postActions -text {Post Creation Actions}
	checkbutton $w.postActions.checkout \
		-text {Checkout after creation} \
		-variable create_branch_checkout
	pack $w.postActions.checkout -anchor nw
	pack $w.postActions -anchor nw -fill x -pady 5 -padx 5

	set create_branch_checkout 1
	set create_branch_head $current_branch
	set create_branch_revtype head
	set create_branch_name $repo_config(gui.newbranchtemplate)
	set create_branch_revexp {}

	bind $w <Visibility> "
		grab $w
		$w.desc.name_t icursor end
		focus $w.desc.name_t
	"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> "do_create_branch_action $w;break"
	wm title $w "[appname] ([reponame]): Create Branch"
	tkwait window $w
}

proc do_delete_branch_action {w} {
	global all_heads
	global delete_branch_checktype delete_branch_head delete_branch_trackinghead

	set check_rev {}
	switch -- $delete_branch_checktype {
	head {set check_rev $delete_branch_head}
	tracking {set check_rev $delete_branch_trackinghead}
	always {set check_rev {:none}}
	}
	if {$check_rev eq {:none}} {
		set check_cmt {}
	} elseif {[catch {set check_cmt [git rev-parse --verify "${check_rev}^0"]}]} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Invalid check revision: $check_rev"
		return
	}

	set to_delete [list]
	set not_merged [list]
	foreach i [$w.list.l curselection] {
		set b [$w.list.l get $i]
		if {[catch {set o [git rev-parse --verify $b]}]} continue
		if {$check_cmt ne {}} {
			if {$b eq $check_rev} continue
			if {[catch {set m [git merge-base $o $check_cmt]}]} continue
			if {$o ne $m} {
				lappend not_merged $b
				continue
			}
		}
		lappend to_delete [list $b $o]
	}
	if {$not_merged ne {}} {
		set msg "The following branches are not completely merged into $check_rev:

 - [join $not_merged "\n - "]"
		tk_messageBox \
			-icon info \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message $msg
	}
	if {$to_delete eq {}} return
	if {$delete_branch_checktype eq {always}} {
		set msg {Recovering deleted branches is difficult.

Delete the selected branches?}
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
		if {[catch {git update-ref -d "refs/heads/$b" $o} err]} {
			append failed " - $b: $err\n"
		} else {
			set x [lsearch -sorted -exact $all_heads $b]
			if {$x >= 0} {
				set all_heads [lreplace $all_heads $x $x]
			}
		}
	}

	if {$failed ne {}} {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Failed to delete branches:\n$failed"
	}

	set all_heads [lsort $all_heads]
	populate_branch_menu
	destroy $w
}

proc do_delete_branch {} {
	global all_heads tracking_branches current_branch
	global delete_branch_checktype delete_branch_head delete_branch_trackinghead

	set w .branch_editor
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text {Delete Local Branch} \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text Delete \
		-command [list do_delete_branch_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.list -text {Local Branches}
	listbox $w.list.l \
		-height 10 \
		-width 70 \
		-selectmode extended \
		-yscrollcommand [list $w.list.sby set]
	foreach h $all_heads {
		if {$h ne $current_branch} {
			$w.list.l insert end $h
		}
	}
	scrollbar $w.list.sby -command [list $w.list.l yview]
	pack $w.list.sby -side right -fill y
	pack $w.list.l -side left -fill both -expand 1
	pack $w.list -fill both -expand 1 -pady 5 -padx 5

	labelframe $w.validate -text {Delete Only If}
	radiobutton $w.validate.head_r \
		-text {Merged Into Local Branch:} \
		-value head \
		-variable delete_branch_checktype
	eval tk_optionMenu $w.validate.head_m delete_branch_head $all_heads
	grid $w.validate.head_r $w.validate.head_m -sticky w
	set all_trackings [all_tracking_branches]
	if {$all_trackings ne {}} {
		set delete_branch_trackinghead [lindex $all_trackings 0]
		radiobutton $w.validate.tracking_r \
			-text {Merged Into Tracking Branch:} \
			-value tracking \
			-variable delete_branch_checktype
		eval tk_optionMenu $w.validate.tracking_m \
			delete_branch_trackinghead \
			$all_trackings
		grid $w.validate.tracking_r $w.validate.tracking_m -sticky w
	}
	radiobutton $w.validate.always_r \
		-text {Always (Do not perform merge checks)} \
		-value always \
		-variable delete_branch_checktype
	grid $w.validate.always_r -columnspan 2 -sticky w
	grid columnconfigure $w.validate 1 -weight 1
	pack $w.validate -anchor nw -fill x -pady 5 -padx 5

	set delete_branch_head $current_branch
	set delete_branch_checktype head

	bind $w <Visibility> "grab $w; focus $w"
	bind $w <Key-Escape> "destroy $w"
	wm title $w "[appname] ([reponame]): Delete Branch"
	tkwait window $w
}

proc switch_branch {new_branch} {
	global HEAD commit_type current_branch repo_config

	if {![lock_index switch]} return

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {[string match amend* $commit_type]
		&& $curType eq {normal}
		&& $curHEAD eq $HEAD} {
	} elseif {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository since the last scan.  A rescan must be performed before the current branch can be changed.

The rescan will be automatically started now.
}
		unlock_index
		rescan {set ui_status_value {Ready.}}
		return
	}

	# -- Don't do a pointless switch.
	#
	if {$current_branch eq $new_branch} {
		unlock_index
		return
	}

	if {$repo_config(gui.trustmtime) eq {true}} {
		switch_branch_stage2 {} $new_branch
	} else {
		set ui_status_value {Refreshing file status...}
		set cmd [list git update-index]
		lappend cmd -q
		lappend cmd --unmerged
		lappend cmd --ignore-missing
		lappend cmd --refresh
		set fd_rf [open "| $cmd" r]
		fconfigure $fd_rf -blocking 0 -translation binary
		fileevent $fd_rf readable \
			[list switch_branch_stage2 $fd_rf $new_branch]
	}
}

proc switch_branch_stage2 {fd_rf new_branch} {
	global ui_status_value HEAD

	if {$fd_rf ne {}} {
		read $fd_rf
		if {![eof $fd_rf]} return
		close $fd_rf
	}

	set ui_status_value "Updating working directory to '$new_branch'..."
	set cmd [list git read-tree]
	lappend cmd -m
	lappend cmd -u
	lappend cmd --exclude-per-directory=.gitignore
	lappend cmd $HEAD
	lappend cmd $new_branch
	set fd_rt [open "| $cmd" r]
	fconfigure $fd_rt -blocking 0 -translation binary
	fileevent $fd_rt readable \
		[list switch_branch_readtree_wait $fd_rt $new_branch]
}

proc switch_branch_readtree_wait {fd_rt new_branch} {
	global selected_commit_type commit_type HEAD MERGE_HEAD PARENT
	global current_branch
	global ui_comm ui_status_value

	# -- We never get interesting output on stdout; only stderr.
	#
	read $fd_rt
	fconfigure $fd_rt -blocking 1
	if {![eof $fd_rt]} {
		fconfigure $fd_rt -blocking 0
		return
	}

	# -- The working directory wasn't in sync with the index and
	#    we'd have to overwrite something to make the switch. A
	#    merge is required.
	#
	if {[catch {close $fd_rt} err]} {
		regsub {^fatal: } $err {} err
		warn_popup "File level merge required.

$err

Staying on branch '$current_branch'."
		set ui_status_value "Aborted checkout of '$new_branch' (file level merging is required)."
		unlock_index
		return
	}

	# -- Update the symbolic ref.  Core git doesn't even check for failure
	#    here, it Just Works(tm).  If it doesn't we are in some really ugly
	#    state that is difficult to recover from within git-gui.
	#
	if {[catch {git symbolic-ref HEAD "refs/heads/$new_branch"} err]} {
		error_popup "Failed to set current branch.

This working directory is only partially switched.  We successfully updated your files, but failed to update an internal Git file.

This should not have occurred.  [appname] will now close and give up.

$err"
		do_quit
		return
	}

	# -- Update our repository state.  If we were previously in amend mode
	#    we need to toss the current buffer and do a full rescan to update
	#    our file lists.  If we weren't in amend mode our file lists are
	#    accurate and we can avoid the rescan.
	#
	unlock_index
	set selected_commit_type new
	if {[string match amend* $commit_type]} {
		$ui_comm delete 0.0 end
		$ui_comm edit reset
		$ui_comm edit modified false
		rescan {set ui_status_value "Checked out branch '$current_branch'."}
	} else {
		repository_state commit_type HEAD MERGE_HEAD
		set PARENT $HEAD
		set ui_status_value "Checked out branch '$current_branch'."
	}
}
