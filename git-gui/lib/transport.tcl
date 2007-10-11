# git-gui transport (fetch/push) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc fetch_from {remote} {
	set w [console::new \
		[mc "fetch %s" $remote] \
		[mc "Fetching new changes from %s" $remote]]
	set cmds [list]
	lappend cmds [list exec git fetch $remote]
	if {[is_config_true gui.pruneduringfetch]} {
		lappend cmds [list exec git remote prune $remote]
	}
	console::chain $w $cmds
}

proc prune_from {remote} {
	set w [console::new \
		[mc "remote prune %s" $remote] \
		[mc "Pruning tracking branches deleted from %s" $remote]]
	console::exec $w [list git remote prune $remote]
}

proc push_to {remote} {
	set w [console::new \
		[mc "push %s" $remote] \
		[mc "Pushing changes to %s" $remote]]
	set cmd [list git push]
	lappend cmd -v
	lappend cmd $remote
	console::exec $w $cmd
}

proc start_push_anywhere_action {w} {
	global push_urltype push_remote push_url push_thin push_tags
	global push_force

	set r_url {}
	switch -- $push_urltype {
	remote {set r_url $push_remote}
	url {set r_url $push_url}
	}
	if {$r_url eq {}} return

	set cmd [list git push]
	lappend cmd -v
	if {$push_thin} {
		lappend cmd --thin
	}
	if {$push_force} {
		lappend cmd --force
	}
	if {$push_tags} {
		lappend cmd --tags
	}
	lappend cmd $r_url
	set cnt 0
	foreach i [$w.source.l curselection] {
		set b [$w.source.l get $i]
		lappend cmd "refs/heads/$b:refs/heads/$b"
		incr cnt
	}
	if {$cnt == 0} {
		return
	} elseif {$cnt == 1} {
		set unit branch
	} else {
		set unit branches
	}

	set cons [console::new \
		[mc "push %s" $r_url] \
		[mc "Pushing %s %s to %s" $cnt $unit $r_url]]
	console::exec $cons $cmd
	destroy $w
}

trace add variable push_remote write \
	[list radio_selector push_urltype remote]

proc do_push_anywhere {} {
	global all_remotes current_branch
	global push_urltype push_remote push_url push_thin push_tags
	global push_force

	set w .push_setup
	toplevel $w
	wm geometry $w "+[winfo rootx .]+[winfo rooty .]"

	label $w.header -text [mc "Push Branches"] -font font_uibold
	pack $w.header -side top -fill x

	frame $w.buttons
	button $w.buttons.create -text [mc Push] \
		-default active \
		-command [list start_push_anywhere_action $w]
	pack $w.buttons.create -side right
	button $w.buttons.cancel -text [mc "Cancel"] \
		-default normal \
		-command [list destroy $w]
	pack $w.buttons.cancel -side right -padx 5
	pack $w.buttons -side bottom -fill x -pady 10 -padx 10

	labelframe $w.source -text [mc "Source Branches"]
	listbox $w.source.l \
		-height 10 \
		-width 70 \
		-selectmode extended \
		-yscrollcommand [list $w.source.sby set]
	foreach h [load_all_heads] {
		$w.source.l insert end $h
		if {$h eq $current_branch} {
			$w.source.l select set end
		}
	}
	scrollbar $w.source.sby -command [list $w.source.l yview]
	pack $w.source.sby -side right -fill y
	pack $w.source.l -side left -fill both -expand 1
	pack $w.source -fill both -expand 1 -pady 5 -padx 5

	labelframe $w.dest -text [mc "Destination Repository"]
	if {$all_remotes ne {}} {
		radiobutton $w.dest.remote_r \
			-text [mc "Remote:"] \
			-value remote \
			-variable push_urltype
		eval tk_optionMenu $w.dest.remote_m push_remote $all_remotes
		grid $w.dest.remote_r $w.dest.remote_m -sticky w
		if {[lsearch -sorted -exact $all_remotes origin] != -1} {
			set push_remote origin
		} else {
			set push_remote [lindex $all_remotes 0]
		}
		set push_urltype remote
	} else {
		set push_urltype url
	}
	radiobutton $w.dest.url_r \
		-text [mc "Arbitrary URL:"] \
		-value url \
		-variable push_urltype
	entry $w.dest.url_t \
		-borderwidth 1 \
		-relief sunken \
		-width 50 \
		-textvariable push_url \
		-validate key \
		-validatecommand {
			if {%d == 1 && [regexp {\s} %S]} {return 0}
			if {%d == 1 && [string length %S] > 0} {
				set push_urltype url
			}
			return 1
		}
	grid $w.dest.url_r $w.dest.url_t -sticky we -padx {0 5}
	grid columnconfigure $w.dest 1 -weight 1
	pack $w.dest -anchor nw -fill x -pady 5 -padx 5

	labelframe $w.options -text [mc "Transfer Options"]
	checkbutton $w.options.force \
		-text [mc "Force overwrite existing branch (may discard changes)"] \
		-variable push_force
	grid $w.options.force -columnspan 2 -sticky w
	checkbutton $w.options.thin \
		-text [mc "Use thin pack (for slow network connections)"] \
		-variable push_thin
	grid $w.options.thin -columnspan 2 -sticky w
	checkbutton $w.options.tags \
		-text [mc "Include tags"] \
		-variable push_tags
	grid $w.options.tags -columnspan 2 -sticky w
	grid columnconfigure $w.options 1 -weight 1
	pack $w.options -anchor nw -fill x -pady 5 -padx 5

	set push_url {}
	set push_force 0
	set push_thin 0
	set push_tags 0

	bind $w <Visibility> "grab $w; focus $w.buttons.create"
	bind $w <Key-Escape> "destroy $w"
	bind $w <Key-Return> [list start_push_anywhere_action $w]
	wm title $w [append "[appname] ([reponame]): " [mc "Push"]]
	tkwait window $w
}
