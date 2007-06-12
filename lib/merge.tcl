# git-gui branch merge support
# Copyright (C) 2006, 2007 Shawn Pearce

namespace eval merge {

proc _can_merge {} {
	global HEAD commit_type file_states

	if {[string match amend* $commit_type]} {
		info_popup {Cannot merge while amending.

You must finish amending this commit before starting any type of merge.
}
		return 0
	}

	if {[committer_ident] eq {}} {return 0}
	if {![lock_index merge]} {return 0}

	# -- Our in memory state should match the repository.
	#
	repository_state curType curHEAD curMERGE_HEAD
	if {$commit_type ne $curType || $HEAD ne $curHEAD} {
		info_popup {Last scanned state does not match repository state.

Another Git program has modified this repository since the last scan.  A rescan must be performed before a merge can be performed.

The rescan will be automatically started now.
}
		unlock_index
		rescan {set ui_status_value {Ready.}}
		return 0
	}

	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		_O {
			continue; # and pray it works!
		}
		U? {
			error_popup "You are in the middle of a conflicted merge.

File [short_path $path] has merge conflicts.

You must resolve them, add the file, and commit to complete the current merge.  Only then can you begin another merge.
"
			unlock_index
			return 0
		}
		?? {
			error_popup "You are in the middle of a change.

File [short_path $path] is modified.

You should complete the current commit before starting a merge.  Doing so will help you abort a failed merge, should the need arise.
"
			unlock_index
			return 0
		}
		}
	}

	return 1
}

proc _refs {w list} {
	set r {}
	foreach i [$w.source.l curselection] {
		lappend r [lindex [lindex $list $i] 0]
	}
	return $r
}

proc _visualize {w list} {
	set revs [_refs $w $list]
	if {$revs eq {}} return
	lappend revs --not HEAD
	do_gitk $revs
}

proc _start {w list} {
	global HEAD ui_status_value current_branch

	set cmd [list git merge]
	set names [_refs $w $list]
	set revcnt [llength $names]
	append cmd { } $names

	if {$revcnt == 0} {
		return
	} elseif {$revcnt == 1} {
		set unit branch
	} elseif {$revcnt <= 15} {
		set unit branches

		if {[tk_dialog \
		$w.confirm_octopus \
		[wm title $w] \
		"Use octopus merge strategy?

You are merging $revcnt branches at once.  This requires using the octopus merge driver, which may not succeed if there are file-level conflicts.
" \
		question \
		0 \
		{Cancel} \
		{Use octopus} \
		] != 1} return
	} else {
		tk_messageBox \
			-icon error \
			-type ok \
			-title [wm title $w] \
			-parent $w \
			-message "Too many branches selected.

You have requested to merge $revcnt branches in an octopus merge.  This exceeds Git's internal limit of 15 branches per merge.

Please select fewer branches.  To merge more than 15 branches, merge the branches in batches.
"
		return
	}

	set msg "Merging $current_branch, [join $names {, }]"
	set ui_status_value "$msg..."
	set cons [console::new "Merge" $msg]
	console::exec $cons $cmd \
		[namespace code [list _finish $revcnt $cons]]

	wm protocol $w WM_DELETE_WINDOW {}
	destroy $w
}

proc _finish {revcnt w ok} {
	console::done $w $ok
	if {$ok} {
		set msg {Merge completed successfully.}
	} else {
		if {$revcnt != 1} {
			info_popup "Octopus merge failed.

Your merge of $revcnt branches has failed.

There are file-level conflicts between the branches which must be resolved manually.

The working directory will now be reset.

You can attempt this merge again by merging only one branch at a time." $w

			set fd [open "| git read-tree --reset -u HEAD" r]
			fconfigure $fd -blocking 0 -translation binary
			fileevent $fd readable \
				[namespace code [list _reset_wait $fd]]
			set ui_status_value {Aborting... please wait...}
			return
		}

		set msg {Merge failed.  Conflict resolution is required.}
	}
	unlock_index
	rescan [list set ui_status_value $msg]
}

proc dialog {} {
	global current_branch
	global M1B

	if {![_can_merge]} return

	set fmt {list %(objectname) %(*objectname) %(refname) %(subject)}
	set cmd [list git for-each-ref --tcl --format=$fmt]
	lappend cmd refs/heads
	lappend cmd refs/remotes
	lappend cmd refs/tags
	set fr_fd [open "| $cmd" r]
	fconfigure $fr_fd -translation binary
	while {[gets $fr_fd line] > 0} {
		set line [eval $line]
		set ref [lindex $line 2]
		regsub ^refs/(heads|remotes|tags)/ $ref {} ref
		set subj($ref) [lindex $line 3]
		lappend sha1([lindex $line 0]) $ref
		if {[lindex $line 1] ne {}} {
			lappend sha1([lindex $line 1]) $ref
		}
	}
	close $fr_fd

	set to_show {}
	set fr_fd [open "| git rev-list --all --not HEAD"]
	while {[gets $fr_fd line] > 0} {
		if {[catch {set ref $sha1($line)}]} continue
		foreach n $ref {
			lappend to_show [list $n $line]
		}
	}
	close $fr_fd
	set to_show [lsort -unique $to_show]

	set w .merge_setup
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	set _visualize [namespace code [list _visualize $w $to_show]]
	set _start [namespace code [list _start $w $to_show]]

	label $w.header \
		-text "Merge Into $current_branch" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.visualize -text Visualize -command $_visualize
	pack $w.buttons.visualize -side left
	button $w.buttons.create -text Merge -command $_start
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text {Cancel} -command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.source -text {Source Branches}
	listbox $w.source.l \
		-height 10 \
		-width 70 \
		-font font_diff \
		-selectmode extended \
		-yscrollcommand [list $w.source.sby set]
	scrollbar $w.source.sby -command [list $w.source.l yview]
	pack $w.source.sby -side right -fill y
	pack $w.source.l -side left -fill both -expand 1
	pack $w.source -fill both -expand 1 -pady 5 -padx 5

	foreach ref $to_show {
		set n [lindex $ref 0]
		if {[string length $n] > 20} {
			set n "[string range $n 0 16]..."
		}
		$w.source.l insert end [format {%s %-20s %s} \
			[string range [lindex $ref 1] 0 5] \
			$n \
			$subj([lindex $ref 0])]
	}

	bind $w.source.l <Key-K> [list event generate %W <Shift-Key-Up>]
	bind $w.source.l <Key-J> [list event generate %W <Shift-Key-Down>]
	bind $w.source.l <Key-k> [list event generate %W <Key-Up>]
	bind $w.source.l <Key-j> [list event generate %W <Key-Down>]
	bind $w.source.l <Key-h> [list event generate %W <Key-Left>]
	bind $w.source.l <Key-l> [list event generate %W <Key-Right>]
	bind $w.source.l <Key-v> $_visualize

	bind $w <$M1B-Key-Return> $_start
	bind $w <Visibility> "grab $w; focus $w.source.l"
	bind $w <Key-Escape> "unlock_index;destroy $w"
	wm protocol $w WM_DELETE_WINDOW "unlock_index;destroy $w"
	wm title $w "[appname] ([reponame]): Merge"
	tkwait window $w
}

proc reset_hard {} {
	global HEAD commit_type file_states

	if {[string match amend* $commit_type]} {
		info_popup {Cannot abort while amending.

You must finish amending this commit.
}
		return
	}

	if {![lock_index abort]} return

	if {[string match *merge* $commit_type]} {
		set op merge
	} else {
		set op commit
	}

	if {[ask_popup "Abort $op?

Aborting the current $op will cause *ALL* uncommitted changes to be lost.

Continue with aborting the current $op?"] eq {yes}} {
		set fd [open "| git read-tree --reset -u HEAD" r]
		fconfigure $fd -blocking 0 -translation binary
		fileevent $fd readable [namespace code [list _reset_wait $fd]]
		set ui_status_value {Aborting... please wait...}
	} else {
		unlock_index
	}
}

proc _reset_wait {fd} {
	global ui_comm

	read $fd
	if {[eof $fd]} {
		close $fd
		unlock_index

		$ui_comm delete 0.0 end
		$ui_comm edit modified false

		catch {file delete [gitdir MERGE_HEAD]}
		catch {file delete [gitdir rr-cache MERGE_RR]}
		catch {file delete [gitdir SQUASH_MSG]}
		catch {file delete [gitdir MERGE_MSG]}
		catch {file delete [gitdir GITGUI_MSG]}

		rescan {set ui_status_value {Abort completed.  Ready.}}
	}
}

}
