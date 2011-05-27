# git-gui index (add/remove) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc _delete_indexlock {} {
	if {[catch {file delete -- [gitdir index.lock]} err]} {
		error_popup [strcat [mc "Unable to unlock the index."] "\n\n$err"]
	}
}

proc _close_updateindex {fd after} {
	global use_ttk NS
	fconfigure $fd -blocking 1
	if {[catch {close $fd} err]} {
		set w .indexfried
		Dialog $w
		wm withdraw $w
		wm title $w [strcat "[appname] ([reponame]): " [mc "Index Error"]]
		wm geometry $w "+[winfo rootx .]+[winfo rooty .]"
		set s [mc "Updating the Git index failed.  A rescan will be automatically started to resynchronize git-gui."]
		text $w.msg -yscrollcommand [list $w.vs set] \
			-width [string length $s] -relief flat \
			-borderwidth 0 -highlightthickness 0 \
			-background [get_bg_color $w]
		$w.msg tag configure bold -font font_uibold -justify center
		${NS}::scrollbar $w.vs -command [list $w.msg yview]
		$w.msg insert end $s bold \n\n$err {}
		$w.msg configure -state disabled

		${NS}::button $w.continue \
			-text [mc "Continue"] \
			-command [list destroy $w]
		${NS}::button $w.unlock \
			-text [mc "Unlock Index"] \
			-command "destroy $w; _delete_indexlock"
		grid $w.msg - $w.vs -sticky news
		grid $w.unlock $w.continue - -sticky se -padx 2 -pady 2
		grid columnconfigure $w 0 -weight 1
		grid rowconfigure $w 0 -weight 1

		wm protocol $w WM_DELETE_WINDOW update
		bind $w.continue <Visibility> "
			grab $w
			focus %W
		"
		wm deiconify $w
		tkwait window $w

		$::main_status stop
		unlock_index
		rescan $after 0
		return
	}

	$::main_status stop
	unlock_index
	uplevel #0 $after
}

proc update_indexinfo {msg pathList after} {
	global update_index_cp

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	$::main_status start $msg [mc "files"]
	set fd [git_write update-index -z --index-info]
	fconfigure $fd \
		-blocking 0 \
		-buffering full \
		-buffersize 512 \
		-encoding binary \
		-translation binary
	fileevent $fd writable [list \
		write_update_indexinfo \
		$fd \
		$pathList \
		$totalCnt \
		$batch \
		$after \
		]
}

proc write_update_indexinfo {fd pathList totalCnt batch after} {
	global update_index_cp
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		_close_updateindex $fd $after
		return
	}

	for {set i $batch} \
		{$update_index_cp < $totalCnt && $i > 0} \
		{incr i -1} {
		set path [lindex $pathList $update_index_cp]
		incr update_index_cp

		set s $file_states($path)
		switch -glob -- [lindex $s 0] {
		A? {set new _O}
		MT -
		TM -
		T_ {set new _T}
		M? {set new _M}
		TD -
		D_ {set new _D}
		D? {set new _?}
		?? {continue}
		}
		set info [lindex $s 2]
		if {$info eq {}} continue

		puts -nonewline $fd "$info\t[encoding convertto $path]\0"
		display_file $path $new
	}

	$::main_status update $update_index_cp $totalCnt
}

proc update_index {msg pathList after} {
	global update_index_cp

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	$::main_status start $msg [mc "files"]
	set fd [git_write update-index --add --remove -z --stdin]
	fconfigure $fd \
		-blocking 0 \
		-buffering full \
		-buffersize 512 \
		-encoding binary \
		-translation binary
	fileevent $fd writable [list \
		write_update_index \
		$fd \
		$pathList \
		$totalCnt \
		$batch \
		$after \
		]
}

proc write_update_index {fd pathList totalCnt batch after} {
	global update_index_cp
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		_close_updateindex $fd $after
		return
	}

	for {set i $batch} \
		{$update_index_cp < $totalCnt && $i > 0} \
		{incr i -1} {
		set path [lindex $pathList $update_index_cp]
		incr update_index_cp

		switch -glob -- [lindex $file_states($path) 0] {
		AD {set new __}
		?D {set new D_}
		_O -
		AT -
		AM {set new A_}
		TM -
		MT -
		_T {set new T_}
		_U -
		U? {
			if {[file exists $path]} {
				set new M_
			} else {
				set new D_
			}
		}
		?M {set new M_}
		?? {continue}
		}
		puts -nonewline $fd "[encoding convertto $path]\0"
		display_file $path $new
	}

	$::main_status update $update_index_cp $totalCnt
}

proc checkout_index {msg pathList after} {
	global update_index_cp

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	$::main_status start $msg [mc "files"]
	set fd [git_write checkout-index \
		--index \
		--quiet \
		--force \
		-z \
		--stdin \
		]
	fconfigure $fd \
		-blocking 0 \
		-buffering full \
		-buffersize 512 \
		-encoding binary \
		-translation binary
	fileevent $fd writable [list \
		write_checkout_index \
		$fd \
		$pathList \
		$totalCnt \
		$batch \
		$after \
		]
}

proc write_checkout_index {fd pathList totalCnt batch after} {
	global update_index_cp
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		_close_updateindex $fd $after
		return
	}

	for {set i $batch} \
		{$update_index_cp < $totalCnt && $i > 0} \
		{incr i -1} {
		set path [lindex $pathList $update_index_cp]
		incr update_index_cp
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?T -
		?D {
			puts -nonewline $fd "[encoding convertto $path]\0"
			display_file $path ?_
		}
		}
	}

	$::main_status update $update_index_cp $totalCnt
}

proc unstage_helper {txt paths} {
	global file_states current_diff_path

	if {![lock_index begin-update]} return

	set pathList [list]
	set after {}
	foreach path $paths {
		switch -glob -- [lindex $file_states($path) 0] {
		A? -
		M? -
		T? -
		D? {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}
	if {$pathList eq {}} {
		unlock_index
	} else {
		update_indexinfo \
			$txt \
			$pathList \
			[concat $after [list ui_ready]]
	}
}

proc do_unstage_selection {} {
	global current_diff_path selected_paths

	if {[array size selected_paths] > 0} {
		unstage_helper \
			{Unstaging selected files from commit} \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		unstage_helper \
			[mc "Unstaging %s from commit" [short_path $current_diff_path]] \
			[list $current_diff_path]
	}
}

proc add_helper {txt paths} {
	global file_states current_diff_path

	if {![lock_index begin-update]} return

	set pathList [list]
	set after {}
	foreach path $paths {
		switch -glob -- [lindex $file_states($path) 0] {
		_U -
		U? {
			if {$path eq $current_diff_path} {
				unlock_index
				merge_stage_workdir $path
				return
			}
		}
		_O -
		?M -
		?D -
		?T {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}
	if {$pathList eq {}} {
		unlock_index
	} else {
		update_index \
			$txt \
			$pathList \
			[concat $after {ui_status [mc "Ready to commit."]}]
	}
}

proc do_add_selection {} {
	global current_diff_path selected_paths

	if {[array size selected_paths] > 0} {
		add_helper \
			{Adding selected files} \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		add_helper \
			[mc "Adding %s" [short_path $current_diff_path]] \
			[list $current_diff_path]
	}
}

proc do_add_all {} {
	global file_states

	set paths [list]
	set unknown_paths [list]
	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?T -
		?D {lappend paths $path}
		?O {lappend unknown_paths $path}
		}
	}
	if {[llength $unknown_paths]} {
		set reply [ask_popup [mc "There are unknown files do you also want
to stage those?"]]
		if {$reply} {
			set paths [concat $paths $unknown_paths]
		}
	}
	add_helper {Adding all changed files} $paths
}

proc revert_helper {txt paths} {
	global file_states current_diff_path

	if {![lock_index begin-update]} return

	set pathList [list]
	set after {}
	foreach path $paths {
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?T -
		?D {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}


	# Split question between singular and plural cases, because
	# such distinction is needed in some languages. Previously, the
	# code used "Revert changes in" for both, but that can't work
	# in languages where 'in' must be combined with word from
	# rest of string (in diffrent way for both cases of course).
	#
	# FIXME: Unfortunately, even that isn't enough in some languages
	# as they have quite complex plural-form rules. Unfortunately,
	# msgcat doesn't seem to support that kind of string translation.
	#
	set n [llength $pathList]
	if {$n == 0} {
		unlock_index
		return
	} elseif {$n == 1} {
		set query [mc "Revert changes in file %s?" [short_path [lindex $pathList]]]
	} else {
		set query [mc "Revert changes in these %i files?" $n]
	}

	set reply [tk_dialog \
		.confirm_revert \
		"[appname] ([reponame])" \
		"$query

[mc "Any unstaged changes will be permanently lost by the revert."]" \
		question \
		1 \
		[mc "Do Nothing"] \
		[mc "Revert Changes"] \
		]
	if {$reply == 1} {
		checkout_index \
			$txt \
			$pathList \
			[concat $after [list ui_ready]]
	} else {
		unlock_index
	}
}

proc do_revert_selection {} {
	global current_diff_path selected_paths

	if {[array size selected_paths] > 0} {
		revert_helper \
			[mc "Reverting selected files"] \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		revert_helper \
			[mc "Reverting %s" [short_path $current_diff_path]] \
			[list $current_diff_path]
	}
}

proc do_select_commit_type {} {
	global commit_type selected_commit_type

	if {$selected_commit_type eq {new}
		&& [string match amend* $commit_type]} {
		create_new_commit
	} elseif {$selected_commit_type eq {amend}
		&& ![string match amend* $commit_type]} {
		load_last_commit

		# The amend request was rejected...
		#
		if {![string match amend* $commit_type]} {
			set selected_commit_type new
		}
	}
}
