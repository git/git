# but-gui branch merge support
# Copyright (C) 2006, 2007 Shawn Pearce

class merge {

field w         ; # top level window
field w_rev     ; # mega-widget to pick the revision to merge

method _can_merge {} {
	global HEAD cummit_type file_states

	if {[string match amend* $cummit_type]} {
		info_popup [mc "Cannot merge while amending.

You must finish amending this cummit before starting any type of merge.
"]
		return 0
	}

	if {[cummitter_ident] eq {}} {return 0}
	if {![lock_index merge]} {return 0}

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {$cummit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup [mc "Last scanned state does not match repository state.

Another Git program has modified this repository since the last scan.  A rescan must be performed before a merge can be performed.

The rescan will be automatically started now.
"]
		unlock_index
		rescan ui_ready
		return 0
	}

	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		_O {
			continue; # and pray it works!
		}
		_U -
		U? {
			error_popup [mc "You are in the middle of a conflicted merge.

File %s has merge conflicts.

You must resolve them, stage the file, and cummit to complete the current merge.  Only then can you begin another merge.
" [short_path $path]]
			unlock_index
			return 0
		}
		?? {
			error_popup [mc "You are in the middle of a change.

File %s is modified.

You should complete the current cummit before starting a merge.  Doing so will help you abort a failed merge, should the need arise.
" [short_path $path]]
			unlock_index
			return 0
		}
		}
	}

	return 1
}

method _rev {} {
	if {[catch {$w_rev cummit_or_die}]} {
		return {}
	}
	return [$w_rev get]
}

method _visualize {} {
	set rev [_rev $this]
	if {$rev ne {}} {
		do_butk [list $rev --not HEAD]
	}
}

method _start {} {
	global HEAD current_branch remote_url
	global _last_merged_branch

	set name [_rev $this]
	if {$name eq {}} {
		return
	}

	set spec [$w_rev get_tracking_branch]
	set cmit [$w_rev get_cummit]

	set fh [open [butdir FETCH_HEAD] w]
	fconfigure $fh -translation lf
	if {$spec eq {}} {
		set remote .
		set branch $name
		set stitle $branch
	} else {
		set remote $remote_url([lindex $spec 1])
		if {[regexp {^[^:@]*@[^:]*:/} $remote]} {
			regsub {^[^:@]*@} $remote {} remote
		}
		set branch [lindex $spec 2]
		set stitle [mc "%s of %s" $branch $remote]
	}
	regsub ^refs/heads/ $branch {} branch
	puts $fh "$cmit\t\tbranch '$branch' of $remote"
	close $fh
	set _last_merged_branch $branch

	if {[but-version >= "2.5.0"]} {
		set cmd [list but merge --strategy=recursive FETCH_HEAD]
	} else {
		set cmd [list but]
		lappend cmd merge
		lappend cmd --strategy=recursive
		lappend cmd [but fmt-merge-msg <[butdir FETCH_HEAD]]
		lappend cmd HEAD
		lappend cmd $name
	}

	ui_status [mc "Merging %s and %s..." $current_branch $stitle]
	set cons [console::new [mc "Merge"] "merge $stitle"]
	console::exec $cons $cmd [cb _finish $cons]

	wm protocol $w WM_DELETE_WINDOW {}
	destroy $w
}

method _finish {cons ok} {
	console::done $cons $ok
	if {$ok} {
		set msg [mc "Merge completed successfully."]
	} else {
		set msg [mc "Merge failed.  Conflict resolution is required."]
	}
	unlock_index
	rescan [list ui_status $msg]
	delete_this
}

constructor dialog {} {
	global current_branch
	global M1B use_ttk NS

	if {![_can_merge $this]} {
		delete_this
		return
	}

	make_dialog top w
	wm title $top [mc "%s (%s): Merge" [appname] [reponame]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	set _start [cb _start]

	${NS}::label $w.header \
		-text [mc "Merge Into %s" $current_branch] \
		-font font_uibold
	pack $w.header -side top -fill x

	${NS}::frame $w.buttons
	${NS}::button $w.buttons.visualize \
		-text [mc Visualize] \
		-command [cb _visualize]
	pack $w.buttons.visualize -side left
	${NS}::button $w.buttons.merge \
		-text [mc Merge] \
		-command $_start
	pack $w.buttons.merge -side right
	${NS}::button $w.buttons.cancel \
		-text [mc "Cancel"] \
		-command [cb _cancel]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	set w_rev [::choose_rev::new_unmerged $w.rev [mc "Revision To Merge"]]
	pack $w.rev -anchor nw -fill both -expand 1 -pady 5 -padx 5

	bind $w <$M1B-Key-Return> $_start
	bind $w <Key-Return> $_start
	bind $w <Key-Escape> [cb _cancel]
	wm protocol $w WM_DELETE_WINDOW [cb _cancel]

	bind $w.buttons.merge <Visibility> [cb _visible]
	tkwait window $w
}

method _visible {} {
	grab $w
	if {[is_config_true gui.matchtrackingbranch]} {
		$w_rev pick_tracking_branch
	}
	$w_rev focus_filter
}

method _cancel {} {
	wm protocol $w WM_DELETE_WINDOW {}
	unlock_index
	destroy $w
	delete_this
}

}

namespace eval merge {

proc reset_hard {} {
	global HEAD cummit_type file_states

	if {[string match amend* $cummit_type]} {
		info_popup [mc "Cannot abort while amending.

You must finish amending this cummit.
"]
		return
	}

	if {![lock_index abort]} return

	if {[string match *merge* $cummit_type]} {
		set op_question [mc "Abort merge?

Aborting the current merge will cause *ALL* uncummitted changes to be lost.

Continue with aborting the current merge?"]
	} else {
		set op_question [mc "Reset changes?

Resetting the changes will cause *ALL* uncummitted changes to be lost.

Continue with resetting the current changes?"]
	}

	if {[ask_popup $op_question] eq {yes}} {
		set fd [but_read --stderr read-tree --reset -u -v HEAD]
		fconfigure $fd -blocking 0 -translation binary
		set status_bar_operation [$::main_status \
			start \
			[mc "Aborting"] \
			[mc "files reset"]]
		fileevent $fd readable [namespace code [list \
			_reset_wait $fd $status_bar_operation]]
	} else {
		unlock_index
	}
}

proc _reset_wait {fd status_bar_operation} {
	global ui_comm

	$status_bar_operation update_meter [read $fd]

	fconfigure $fd -blocking 1
	if {[eof $fd]} {
		set fail [catch {close $fd} err]
		unlock_index
		$status_bar_operation stop

		$ui_comm delete 0.0 end
		$ui_comm edit modified false

		catch {file delete [butdir MERGE_HEAD]}
		catch {file delete [butdir rr-cache MERGE_RR]}
		catch {file delete [butdir MERGE_RR]}
		catch {file delete [butdir SQUASH_MSG]}
		catch {file delete [butdir MERGE_MSG]}
		catch {file delete [butdir GITGUI_MSG]}

		if {$fail} {
			warn_popup "[mc "Abort failed."]\n\n$err"
		}
		rescan {ui_status [mc "Abort completed.  Ready."]}
	} else {
		fconfigure $fd -blocking 0
	}
}

}
