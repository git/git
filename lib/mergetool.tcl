# git-gui merge conflict resolution
# parts based on git-mergetool (c) 2006 Theodore Y. Ts'o

proc merge_resolve_one {stage} {
	global current_diff_path

	switch -- $stage {
		1 { set target [mc "the base version"] }
		2 { set target [mc "this branch"] }
		3 { set target [mc "the other branch"] }
	}

	set op_question [mc "Force resolution to %s?
Note that the diff shows only conflicting changes.

%s will be overwritten.

This operation can be undone only by restarting the merge." \
		$target [short_path $current_diff_path]]

	if {[ask_popup $op_question] eq {yes}} {
		merge_load_stages $current_diff_path [list merge_force_stage $stage]
	}
}

proc merge_add_resolution {path} {
	global current_diff_path

	if {$path eq $current_diff_path} {
		set after {reshow_diff;}
	} else {
		set after {}
	}

	update_index \
		[mc "Adding resolution for %s" [short_path $path]] \
		[list $path] \
		[concat $after [list ui_ready]]
}

proc merge_force_stage {stage} {
	global current_diff_path merge_stages

	if {$merge_stages($stage) ne {}} {
		git checkout-index -f --stage=$stage -- $current_diff_path
	} else {
		file delete -- $current_diff_path
	}

	merge_add_resolution $current_diff_path
}

proc merge_load_stages {path cont} {
	global merge_stages_fd merge_stages merge_stages_buf

	if {[info exists merge_stages_fd]} {
		catch { kill_file_process $merge_stages_fd }
		catch { close $merge_stages_fd }
	}

	set merge_stages(0) {}
	set merge_stages(1) {}
	set merge_stages(2) {}
	set merge_stages(3) {}
	set merge_stages_buf {}

	set merge_stages_fd [eval git_read ls-files -u -z -- $path]

	fconfigure $merge_stages_fd -blocking 0 -translation binary -encoding binary
	fileevent $merge_stages_fd readable [list read_merge_stages $merge_stages_fd $cont]
}

proc read_merge_stages {fd cont} {
	global merge_stages_buf merge_stages_fd merge_stages

	append merge_stages_buf [read $fd]
	set pck [split $merge_stages_buf "\0"]
	set merge_stages_buf [lindex $pck end]

	if {[eof $fd] && $merge_stages_buf ne {}} {
		lappend pck {}
		set merge_stages_buf {}
	}

	foreach p [lrange $pck 0 end-1] {
		set fcols [split $p "\t"]
		set cols  [split [lindex $fcols 0] " "]
		set stage [lindex $cols 2]
		
		set merge_stages($stage) [lrange $cols 0 1]
	}

	if {[eof $fd]} {
		close $fd
		unset merge_stages_fd
		eval $cont
	}
}
