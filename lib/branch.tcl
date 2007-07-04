# git-gui branch (create/delete) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc load_all_heads {} {
	global all_heads
	global some_heads_tracking

	set rh refs/heads
	set rh_len [expr {[string length $rh] + 1}]
	set all_heads [list]
	set fd [open "| git for-each-ref --format=%(refname) $rh" r]
	while {[gets $fd line] > 0} {
		if {!$some_heads_tracking || ![is_tracking_branch $line]} {
			lappend all_heads [string range $line $rh_len end]
		}
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

proc radio_selector {varname value args} {
	upvar #0 $varname var
	set var $value
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
