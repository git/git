# git-gui branch merge support
# Copyright (C) 2006, 2007 Shawn Pearce

class merge {

field w         ; # top level window
field w_list    ; # widget of available branches
field list      ; # list of available branches

method _can_merge {} {
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
		rescan ui_ready
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

method _refs {} {
	set r {}
	foreach i [$w_list curselection] {
		lappend r [lindex [lindex $list $i] 0]
	}
	return $r
}

method _visualize {} {
	set revs [_refs $this]
	if {$revs eq {}} return
	lappend revs --not HEAD
	do_gitk $revs
}

method _start {} {
	global HEAD current_branch

	set cmd [list git merge]
	set names [_refs $this]
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
	ui_status "$msg..."
	set cons [console::new "Merge" $msg]
	console::exec $cons $cmd [cb _finish $revcnt $cons]

	wm protocol $w WM_DELETE_WINDOW {}
	destroy $w
}

method _finish {revcnt cons ok} {
	console::done $cons $ok
	if {$ok} {
		set msg {Merge completed successfully.}
	} else {
		if {$revcnt != 1} {
			info_popup "Octopus merge failed.

Your merge of $revcnt branches has failed.

There are file-level conflicts between the branches which must be resolved manually.

The working directory will now be reset.

You can attempt this merge again by merging only one branch at a time." $w

			set fd [git_read read-tree --reset -u HEAD]
			fconfigure $fd -blocking 0 -translation binary
			fileevent $fd readable [cb _reset_wait $fd]
			ui_status {Aborting... please wait...}
			return
		}

		set msg {Merge failed.  Conflict resolution is required.}
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

	set fmt {list %(objectname) %(*objectname) %(refname) %(subject)}
	set fr_fd [git_read for-each-ref \
		--tcl \
		--format=$fmt \
		refs/heads \
		refs/remotes \
		refs/tags \
		]
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

	set list [list]
	set fr_fd [git_read rev-list --all --not HEAD]
	while {[gets $fr_fd line] > 0} {
		if {[catch {set ref $sha1($line)}]} continue
		foreach n $ref {
			lappend list [list $n $line]
		}
	}
	close $fr_fd
	set list [lsort -unique $list]

	make_toplevel top w
	wm title $top "[appname] ([reponame]): Merge"
	if {$top ne {.}} {
		wm geometry $top "+[winfo rootx .]+[winfo rooty .]"
	}

	set _visualize [cb _visualize]
	set _start [cb _start]

	label $w.header \
		-text "Merge Into $current_branch" \
		-font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.visualize -text Visualize -command $_visualize
	pack $w.buttons.visualize -side left
	button $w.buttons.create -text Merge -command $_start
	pack $w.buttons.create -side right
	button $w.buttons.cancel \
		-text {Cancel} \
		-command [cb _cancel]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.source -text {Source Branches}
	set w_list $w.source.l
	listbox $w_list \
		-height 10 \
		-width 70 \
		-font font_diff \
		-selectmode extended \
		-yscrollcommand [list $w.source.sby set]
	scrollbar $w.source.sby -command [list $w_list yview]
	pack $w.source.sby -side right -fill y
	pack $w_list -side left -fill both -expand 1
	pack $w.source -fill both -expand 1 -pady 5 -padx 5

	foreach ref $list {
		set n [lindex $ref 0]
		if {[string length $n] > 20} {
			set n "[string range $n 0 16]..."
		}
		$w_list insert end [format {%s %-20s %s} \
			[string range [lindex $ref 1] 0 5] \
			$n \
			$subj([lindex $ref 0])]
	}

	bind $w_list <Key-K> [list event generate %W <Shift-Key-Up>]
	bind $w_list <Key-J> [list event generate %W <Shift-Key-Down>]
	bind $w_list <Key-k> [list event generate %W <Key-Up>]
	bind $w_list <Key-j> [list event generate %W <Key-Down>]
	bind $w_list <Key-h> [list event generate %W <Key-Left>]
	bind $w_list <Key-l> [list event generate %W <Key-Right>]
	bind $w_list <Key-v> $_visualize

	bind $w <$M1B-Key-Return> $_start
	bind $w <Visibility> [cb _visible]
	bind $w <Key-Escape> [cb _cancel]
	wm protocol $w WM_DELETE_WINDOW [cb _cancel]
	tkwait window $w
}

method _visible {} {
	grab $w
	focus $w_list
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
		set fd [git_read read-tree --reset -u HEAD]
		fconfigure $fd -blocking 0 -translation binary
		fileevent $fd readable [namespace code [list _reset_wait $fd]]
		ui_status {Aborting... please wait...}
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

		rescan {ui_status {Abort completed.  Ready.}}
	}
}

}
