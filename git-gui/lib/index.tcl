# git-gui index (add/remove) support
# Copyright (C) 2006, 2007 Shawn Pearce

proc update_indexinfo {msg pathList after} {
	global update_index_cp

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	ui_status [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		0.0]
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
		$msg \
		$after \
		]
}

proc write_update_indexinfo {fd pathList totalCnt batch msg after} {
	global update_index_cp
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		close $fd
		unlock_index
		uplevel #0 $after
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
		M? {set new _M}
		D_ {set new _D}
		D? {set new _?}
		?? {continue}
		}
		set info [lindex $s 2]
		if {$info eq {}} continue

		puts -nonewline $fd "$info\t[encoding convertto $path]\0"
		display_file $path $new
	}

	ui_status [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		[expr {100.0 * $update_index_cp / $totalCnt}]]
}

proc update_index {msg pathList after} {
	global update_index_cp

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	ui_status [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		0.0]
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
		$msg \
		$after \
		]
}

proc write_update_index {fd pathList totalCnt batch msg after} {
	global update_index_cp
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		close $fd
		unlock_index
		uplevel #0 $after
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
		AM {set new A_}
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

	ui_status [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		[expr {100.0 * $update_index_cp / $totalCnt}]]
}

proc checkout_index {msg pathList after} {
	global update_index_cp

	if {![lock_index update]} return

	set update_index_cp 0
	set pathList [lsort $pathList]
	set totalCnt [llength $pathList]
	set batch [expr {int($totalCnt * .01) + 1}]
	if {$batch > 25} {set batch 25}

	ui_status [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		0.0]
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
		$msg \
		$after \
		]
}

proc write_checkout_index {fd pathList totalCnt batch msg after} {
	global update_index_cp
	global file_states current_diff_path

	if {$update_index_cp >= $totalCnt} {
		close $fd
		unlock_index
		uplevel #0 $after
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
		?D {
			puts -nonewline $fd "[encoding convertto $path]\0"
			display_file $path ?_
		}
		}
	}

	ui_status [format \
		"$msg... %i/%i files (%.2f%%)" \
		$update_index_cp \
		$totalCnt \
		[expr {100.0 * $update_index_cp / $totalCnt}]]
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
			"Unstaging [short_path $current_diff_path] from commit" \
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
		_O -
		?M -
		?D -
		U? {
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
			[concat $after {ui_status {Ready to commit.}}]
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
			"Adding [short_path $current_diff_path]" \
			[list $current_diff_path]
	}
}

proc do_add_all {} {
	global file_states

	set paths [list]
	foreach path [array names file_states] {
		switch -glob -- [lindex $file_states($path) 0] {
		U? {continue}
		?M -
		?D {lappend paths $path}
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
		?D {
			lappend pathList $path
			if {$path eq $current_diff_path} {
				set after {reshow_diff;}
			}
		}
		}
	}

	set n [llength $pathList]
	if {$n == 0} {
		unlock_index
		return
	} elseif {$n == 1} {
		set s "[short_path [lindex $pathList]]"
	} else {
		set s "these $n files"
	}

	set reply [tk_dialog \
		.confirm_revert \
		"[appname] ([reponame])" \
		"Revert changes in $s?

Any unstaged changes will be permanently lost by the revert." \
		question \
		1 \
		{Do Nothing} \
		{Revert Changes} \
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
			{Reverting selected files} \
			[array names selected_paths]
	} elseif {$current_diff_path ne {}} {
		revert_helper \
			"Reverting [short_path $current_diff_path]" \
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
