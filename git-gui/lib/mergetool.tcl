# git-gui merge conflict resolution
# parts based on git-mergetool (c) 2006 Theodore Y. Ts'o

proc merge_resolve_one {stage} {
	global current_diff_path

	switch -- $stage {
		1 { set targetquestion [mc "Force resolution to the base version?"] }
		2 { set targetquestion [mc "Force resolution to this branch?"] }
		3 { set targetquestion [mc "Force resolution to the other branch?"] }
	}

	set op_question [strcat $targetquestion "\n" \
[mc "Note that the diff shows only conflicting changes.

%s will be overwritten.

This operation can be undone only by restarting the merge." \
		[short_path $current_diff_path]]]

	if {[ask_popup $op_question] eq {yes}} {
		merge_load_stages $current_diff_path [list merge_force_stage $stage]
	}
}

proc merge_stage_workdir {path {lno {}}} {
	global current_diff_path diff_active
	global current_diff_side ui_workdir

	if {$diff_active} return

	if {$path ne $current_diff_path || $ui_workdir ne $current_diff_side} {
		show_diff $path $ui_workdir $lno {} [list do_merge_stage_workdir $path]
	} else {
		do_merge_stage_workdir $path
	}
}

proc do_merge_stage_workdir {path} {
	global current_diff_path is_conflict_diff

	if {$path ne $current_diff_path} return;

	if {$is_conflict_diff} {
		if {[ask_popup [mc "File %s seems to have unresolved conflicts, still stage?" \
				[short_path $path]]] ne {yes}} {
			return
		}
	}

	merge_add_resolution $path
}

proc merge_add_resolution {path} {
	global current_diff_path ui_workdir

	set after [next_diff_after_action $ui_workdir $path {} {^_?U}]

	update_index \
		[mc "Adding resolution for %s" [short_path $path]] \
		[list $path] \
		[concat $after {ui_ready;}]
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

	set merge_stages_fd [git_read [list ls-files -u -z -- $path]]

	fconfigure $merge_stages_fd -blocking 0 -translation binary
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

proc merge_resolve_tool {} {
	global current_diff_path

	merge_load_stages $current_diff_path [list merge_resolve_tool2]
}

proc merge_resolve_tool2 {} {
	global current_diff_path merge_stages

	# Validate the stages
	if {$merge_stages(2) eq {} ||
	    [lindex $merge_stages(2) 0] eq {120000} ||
	    [lindex $merge_stages(2) 0] eq {160000} ||
	    $merge_stages(3) eq {} ||
	    [lindex $merge_stages(3) 0] eq {120000} ||
	    [lindex $merge_stages(3) 0] eq {160000}
	} {
		error_popup [mc "Cannot resolve deletion or link conflicts using a tool"]
		return
	}

	if {![file exists $current_diff_path]} {
		error_popup [mc "Conflict file does not exist"]
		return
	}

	# Determine the tool to use
	set tool [get_config merge.tool]
	if {$tool eq {}} { set tool meld }

	set merge_tool_path [get_config "mergetool.$tool.path"]
	if {$merge_tool_path eq {}} {
		switch -- $tool {
		emerge { set merge_tool_path "emacs" }
		araxis { set merge_tool_path "compare" }
		default { set merge_tool_path $tool }
		}
	}

	# Make file names
	set filebase [file rootname $current_diff_path]
	set fileext  [file extension $current_diff_path]
	set basename [lindex [file split $current_diff_path] end]

	set MERGED   $current_diff_path
	set BASE     "./$MERGED.BASE$fileext"
	set LOCAL    "./$MERGED.LOCAL$fileext"
	set REMOTE   "./$MERGED.REMOTE$fileext"
	set BACKUP   "./$MERGED.BACKUP$fileext"

	set base_stage $merge_stages(1)

	# Build the command line
	switch -- $tool {
	araxis {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" -wait -merge -3 -a1 \
				-title1:"'$MERGED (Base)'" -title2:"'$MERGED (Local)'" \
				-title3:"'$MERGED (Remote)'" \
				"$BASE" "$LOCAL" "$REMOTE" "$MERGED"]
		} else {
			set cmdline [list "$merge_tool_path" -wait -2 \
				 -title1:"'$MERGED (Local)'" -title2:"'$MERGED (Remote)'" \
				 "$LOCAL" "$REMOTE" "$MERGED"]
		}
	}
	bc3 {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" "$LOCAL" "$REMOTE" "$BASE" "-mergeoutput=$MERGED"]
		} else {
			set cmdline [list "$merge_tool_path" "$LOCAL" "$REMOTE" "-mergeoutput=$MERGED"]
		}
	}
	ecmerge {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" "$BASE" "$LOCAL" "$REMOTE" --default --mode=merge3 --to="$MERGED"]
		} else {
			set cmdline [list "$merge_tool_path" "$LOCAL" "$REMOTE" --default --mode=merge2 --to="$MERGED"]
		}
	}
	emerge {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" -f emerge-files-with-ancestor-command \
					"$LOCAL" "$REMOTE" "$BASE" "$basename"]
		} else {
			set cmdline [list "$merge_tool_path" -f emerge-files-command \
					"$LOCAL" "$REMOTE" "$basename"]
		}
	}
	gvimdiff {
		set cmdline [list "$merge_tool_path" -f "$LOCAL" "$MERGED" "$REMOTE"]
	}
	kdiff3 {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" --auto --L1 "$MERGED (Base)" \
				--L2 "$MERGED (Local)" --L3 "$MERGED (Remote)" -o "$MERGED" "$BASE" "$LOCAL" "$REMOTE"]
		} else {
			set cmdline [list "$merge_tool_path" --auto --L1 "$MERGED (Local)" \
				--L2 "$MERGED (Remote)" -o "$MERGED" "$LOCAL" "$REMOTE"]
		}
	}
	meld {
		set cmdline [list "$merge_tool_path" "$LOCAL" "$MERGED" "$REMOTE"]
	}
	opendiff {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" "$LOCAL" "$REMOTE" -ancestor "$BASE" -merge "$MERGED"]
		} else {
			set cmdline [list "$merge_tool_path" "$LOCAL" "$REMOTE" -merge "$MERGED"]
		}
	}
	p4merge {
		set cmdline [list "$merge_tool_path" "$BASE" "$REMOTE" "$LOCAL" "$MERGED"]
	}
	tkdiff {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" -a "$BASE" -o "$MERGED" "$LOCAL" "$REMOTE"]
		} else {
			set cmdline [list "$merge_tool_path" -o "$MERGED" "$LOCAL" "$REMOTE"]
		}
	}
	vimdiff {
		error_popup [mc "Not a GUI merge tool: '%s'" $tool]
		return
	}
	winmerge {
		if {$base_stage ne {}} {
			# This tool does not support 3-way merges.
			# Use the 'conflict file' resolution feature instead.
			set cmdline [list "$merge_tool_path" -e -ub "$MERGED"]
		} else {
			set cmdline [list "$merge_tool_path" -e -ub -wl \
				-dl "Theirs File" -dr "Mine File" "$REMOTE" "$LOCAL" "$MERGED"]
		}
	}
	xxdiff {
		if {$base_stage ne {}} {
			set cmdline [list "$merge_tool_path" -X --show-merged-pane \
					    -R {Accel.SaveAsMerged: "Ctrl-S"} \
					    -R {Accel.Search: "Ctrl+F"} \
					    -R {Accel.SearchForward: "Ctrl-G"} \
					    --merged-file "$MERGED" "$LOCAL" "$BASE" "$REMOTE"]
		} else {
			set cmdline [list "$merge_tool_path" -X --show-merged-pane \
					    -R {Accel.SaveAsMerged: "Ctrl-S"} \
					    -R {Accel.Search: "Ctrl+F"} \
					    -R {Accel.SearchForward: "Ctrl-G"} \
					    --merged-file "$MERGED" "$LOCAL" "$REMOTE"]
		}
	}
	default {
		set tool_cmd [get_config mergetool.$tool.cmd]
		if {$tool_cmd ne {}} {
			if {([string first {[} $tool_cmd] != -1) || ([string first {]} $tool_cmd] != -1)} {
				error_popup [mc "Unable to process square brackets in \"mergetool.%s.cmd\" configuration option.

Please remove the square brackets." $tool]
				return
			} else {
				set cmdline {}
				foreach command_part $tool_cmd {
					lappend cmdline [subst -nobackslashes -nocommands $command_part]
				}
			}
		} else {
			error_popup [mc "Unsupported merge tool '%s'.

To use this tool, configure \"mergetool.%s.cmd\" as shown in the git-config manual page." $tool $tool]
			return
		}
	}
	}

	merge_tool_start $cmdline $MERGED $BACKUP [list $BASE $LOCAL $REMOTE]
}

proc delete_temp_files {files} {
	foreach fname $files {
		file delete $fname
	}
}

proc merge_tool_get_stages {target stages} {
	global merge_stages

	set i 1
	foreach fname $stages {
		if {$merge_stages($i) eq {}} {
			file delete $fname
			catch { close [safe_open_file $fname w] }
		} else {
			# A hack to support autocrlf properly
			git checkout-index -f --stage=$i -- $target
			file rename -force -- $target $fname
		}
		incr i
	}
}

proc merge_tool_start {cmdline target backup stages} {
	global merge_stages mtool_target mtool_tmpfiles mtool_fd mtool_mtime

	if {[info exists mtool_fd]} {
		if {[ask_popup [mc "Merge tool is already running, terminate it?"]] eq {yes}} {
			catch { kill_file_process $mtool_fd }
			catch { close $mtool_fd }
			unset mtool_fd

			set old_backup [lindex $mtool_tmpfiles end]
			file rename -force -- $old_backup $mtool_target
			delete_temp_files $mtool_tmpfiles
		} else {
			return
		}
	}

	# Save the original file
	file rename -force -- $target $backup

	# Get the blobs; it destroys $target
	if {[catch {merge_tool_get_stages $target $stages} err]} {
		file rename -force -- $backup $target
		delete_temp_files $stages
		error_popup [mc "Error retrieving versions:\n%s" $err]
		return
	}

	# Restore the conflict file
	file copy -force -- $backup $target

	# Initialize global state
	set mtool_target $target
	set mtool_mtime [file mtime $target]
	set mtool_tmpfiles $stages

	lappend mtool_tmpfiles $backup

	# Force redirection to avoid interpreting output on stderr
	# as an error, and launch the tool
	set redir [list {2>@1}]

	if {[catch { set mtool_fd [safe_open_command $cmdline $redir] } err]} {
		delete_temp_files $mtool_tmpfiles
		error_popup [mc "Could not start the merge tool:\n\n%s" $err]
		return
	}

	ui_status [mc "Running merge tool..."]

	fconfigure $mtool_fd -blocking 0 -translation binary
	fileevent $mtool_fd readable [list read_mtool_output $mtool_fd]
}

proc read_mtool_output {fd} {
	global mtool_fd mtool_tmpfiles

	read $fd
	if {[eof $fd]} {
		unset mtool_fd

		fconfigure $fd -blocking 1
		merge_tool_finish $fd
	}
}

proc merge_tool_finish {fd} {
	global mtool_tmpfiles mtool_target mtool_mtime

	set backup [lindex $mtool_tmpfiles end]
	set failed 0

	# Check the return code
	if {[catch {close $fd} err]} {
		set failed 1
		if {$err ne {child process exited abnormally}} {
			error_popup [strcat [mc "Merge tool failed."] "\n\n$err"]
		}
	}

	# Finish
	if {$failed} {
		file rename -force -- $backup $mtool_target
		delete_temp_files $mtool_tmpfiles
		ui_status [mc "Merge tool failed."]
	} else {
		if {[is_config_true mergetool.keepbackup]} {
			file rename -force -- $backup "$mtool_target.orig"
		}

		delete_temp_files $mtool_tmpfiles

		reshow_diff
	}
}
