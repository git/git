# git-gui branch merge support
# Copyright (C) 2006, 2007 Shawn Pearce

class merge {

field w         ; # top level window
field w_rev     ; # mega-widget to pick the revision to merge

method _can_merge {} {
	global HEAD commit_type file_states

	if {[string match amend* $commit_type]} {
		info_popup [mc "Cannot merge while amending.

You must finish amending this commit before starting any type of merge.
"]
		return 0
	}

	if {[committer_ident] eq {}} {return 0}
	if {![lock_index merge]} {return 0}

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {$commit_type ne $curType || $HEAD ne $curHEAD} {
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
		U? {
			error_popup [mc "You are in the middle of a conflicted merge.

File %s has merge conflicts.

You must resolve them, stage the file, and commit to complete the current merge.  Only then can you begin another merge.
" [short_path $path]]
			unlock_index
			return 0
		}
		?? {
			error_popup [mc "You are in the middle of a change.

File %s is modified.

You should complete the current commit before starting a merge.  Doing so will help you abort a failed merge, should the need arise.
" [short_path $path]]
			unlock_index
			return 0
		}
		}
	}

	return 1
}

method _rev {} {
	if {[catch {$w_rev commit_or_die}]} {
		return {}
	}
	return [$w_rev get]
}

method _visualize {} {
	set rev [_rev $this]
	if {$rev ne {}} {
		do_gitk [list $rev --not HEAD]
	}
}

method _start {} {
	global HEAD current_branch remote_url

	set name [_rev $this]
	if {$name eq {}} {
		return
	}

	set spec [$w_rev get_tracking_branch]
	set cmit [$w_rev get_commit]

	set fh [open [gitdir FETCH_HEAD] w]
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

	set cmd [list git]
	lappend cmd merge
	lappend cmd --strategy=recursive
	lappend cmd [git fmt-merge-msg <[gitdir FETCH_HEAD]]
	lappend cmd HEAD
	lappend cmd $name

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
	global M1B

	if {![_can_merge $this]} {
		delete_this
		return
	}

	make_toplevel top w
	wm title $top [append "[appname] ([reponame]): " [mc "Merge"]]
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	set _start [cb _start]

	label $w.header \
		-text [mc "Merge Into %s" $current_branch] \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.visualize \
		-text [mc Visualize] \
		-command [cb _visualize]
	pack $w.buttons.visualize -side left
	button $w.buttons.merge \
		-text [mc Merge] \
		-command $_start
	pack $w.buttons.merge -side right
	button $w.buttons.cancel \
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
	global HEAD commit_type file_states

	if {[string match amend* $commit_type]} {
		info_popup [mc "Cannot abort while amending.

You must finish amending this commit.
"]
		return
	}

	if {![lock_index abort]} return

	if {[string match *merge* $commit_type]} {
		set op_question [mc "Abort merge?

Aborting the current merge will cause *ALL* uncommitted changes to be lost.

Continue with aborting the current merge?"]
	} else {
		set op_question [mc "Reset changes?

Resetting the changes will cause *ALL* uncommitted changes to be lost.

Continue with resetting the current changes?"]
	}

	if {[ask_popup $op_question] eq {yes}} {
		set fd [git_read --stderr read-tree --reset -u -v HEAD]
		fconfigure $fd -blocking 0 -translation binary
		fileevent $fd readable [namespace code [list _reset_wait $fd]]
		$::main_status start [mc "Aborting"] [mc "files reset"]
	} else {
		unlock_index
	}
}

proc _reset_wait {fd} {
	global ui_comm

	$::main_status update_meter [read $fd]

	fconfigure $fd -blocking 1
	if {[eof $fd]} {
		set fail [catch {close $fd} err]
		$::main_status stop
		unlock_index

		$ui_comm delete 0.0 end
		$ui_comm edit modified false

		catch {file delete [gitdir MERGE_HEAD]}
		catch {file delete [gitdir rr-cache MERGE_RR]}
		catch {file delete [gitdir SQUASH_MSG]}
		catch {file delete [gitdir MERGE_MSG]}
		catch {file delete [gitdir GITGUI_MSG]}

		if {$fail} {
			warn_popup "[mc "Abort failed."]\n\n$err"
		}
		rescan {ui_status [mc "Abort completed.  Ready."]}
	} else {
		fconfigure $fd -blocking 0
	}
}

}
