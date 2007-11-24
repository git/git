# git-gui diff viewer
# Copyright (C) 2006, 2007 Shawn Pearce

proc clear_diff {} {
	global ui_diff current_diff_path current_diff_header
	global ui_index ui_workdir

	$ui_diff conf -state normal
	$ui_diff delete 0.0 end
	$ui_diff conf -state disabled

	set current_diff_path {}
	set current_diff_header {}

	$ui_index tag remove in_diff 0.0 end
	$ui_workdir tag remove in_diff 0.0 end
}

proc reshow_diff {} {
	global file_states file_lists
	global current_diff_path current_diff_side

	set p $current_diff_path
	if {$p eq {}} {
		# No diff is being shown.
	} elseif {$current_diff_side eq {}
		|| [catch {set s $file_states($p)}]
		|| [lsearch -sorted -exact $file_lists($current_diff_side) $p] == -1} {
		clear_diff
	} else {
		show_diff $p $current_diff_side
	}
}

proc handle_empty_diff {} {
	global current_diff_path file_states file_lists

	set path $current_diff_path
	set s $file_states($path)
	if {[lindex $s 0] ne {_M}} return

	info_popup [mc "No differences detected.

%s has no changes.

The modification date of this file was updated by another application, but the content within the file was not changed.

A rescan will be automatically started to find other files which may have the same state." [short_path $path]]

	clear_diff
	display_file $path __
	rescan ui_ready 0
}

proc show_diff {path w {lno {}}} {
	global file_states file_lists
	global is_3way_diff diff_active repo_config
	global ui_diff ui_index ui_workdir
	global current_diff_path current_diff_side current_diff_header

	if {$diff_active || ![lock_index read]} return

	clear_diff
	if {$lno == {}} {
		set lno [lsearch -sorted -exact $file_lists($w) $path]
		if {$lno >= 0} {
			incr lno
		}
	}
	if {$lno >= 1} {
		$w tag add in_diff $lno.0 [expr {$lno + 1}].0
	}

	set s $file_states($path)
	set m [lindex $s 0]
	set is_3way_diff 0
	set diff_active 1
	set current_diff_path $path
	set current_diff_side $w
	set current_diff_header {}
	ui_status [mc "Loading diff of %s..." [escape_path $path]]

	# - Git won't give us the diff, there's nothing to compare to!
	#
	if {$m eq {_O}} {
		set max_sz [expr {128 * 1024}]
		set type unknown
		if {[catch {
				set type [file type $path]
				switch -- $type {
				directory {
					set type submodule
					set content {}
					set sz 0
				}
				link {
					set content [file readlink $path]
					set sz [string length $content]
				}
				file {
					set fd [open $path r]
					fconfigure $fd -eofchar {}
					set content [read $fd $max_sz]
					close $fd
					set sz [file size $path]
				}
				default {
					error "'$type' not supported"
				}
				}
			} err ]} {
			set diff_active 0
			unlock_index
			ui_status [mc "Unable to display %s" [escape_path $path]]
			error_popup [strcat [mc "Error loading file:"] "\n\n$err"]
			return
		}
		$ui_diff conf -state normal
		if {$type eq {submodule}} {
			$ui_diff insert end [append \
				"* " \
				[mc "Git Repository (subproject)"] \
				"\n"] d_@
		} elseif {![catch {set type [exec file $path]}]} {
			set n [string length $path]
			if {[string equal -length $n $path $type]} {
				set type [string range $type $n end]
				regsub {^:?\s*} $type {} type
			}
			$ui_diff insert end "* $type\n" d_@
		}
		if {[string first "\0" $content] != -1} {
			$ui_diff insert end \
				[mc "* Binary file (not showing content)."] \
				d_@
		} else {
			if {$sz > $max_sz} {
				$ui_diff insert end \
"* Untracked file is $sz bytes.
* Showing only first $max_sz bytes.
" d_@
			}
			$ui_diff insert end $content
			if {$sz > $max_sz} {
				$ui_diff insert end "
* Untracked file clipped here by [appname].
* To see the entire file, use an external editor.
" d_@
			}
		}
		$ui_diff conf -state disabled
		set diff_active 0
		unlock_index
		ui_ready
		return
	}

	set cmd [list]
	if {$w eq $ui_index} {
		lappend cmd diff-index
		lappend cmd --cached
	} elseif {$w eq $ui_workdir} {
		if {[string index $m 0] eq {U}} {
			lappend cmd diff
		} else {
			lappend cmd diff-files
		}
	}

	lappend cmd -p
	lappend cmd --no-color
	if {$repo_config(gui.diffcontext) >= 0} {
		lappend cmd "-U$repo_config(gui.diffcontext)"
	}
	if {$w eq $ui_index} {
		lappend cmd [PARENT]
	}
	lappend cmd --
	lappend cmd $path

	if {[catch {set fd [eval git_read --nice $cmd]} err]} {
		set diff_active 0
		unlock_index
		ui_status [mc "Unable to display %s" [escape_path $path]]
		error_popup [strcat [mc "Error loading diff:"] "\n\n$err"]
		return
	}

	fconfigure $fd \
		-blocking 0 \
		-encoding binary \
		-translation binary
	fileevent $fd readable [list read_diff $fd]
}

proc read_diff {fd} {
	global ui_diff diff_active
	global is_3way_diff current_diff_header

	$ui_diff conf -state normal
	while {[gets $fd line] >= 0} {
		# -- Cleanup uninteresting diff header lines.
		#
		if {   [string match {diff --git *}      $line]
			|| [string match {diff --cc *}       $line]
			|| [string match {diff --combined *} $line]
			|| [string match {--- *}             $line]
			|| [string match {+++ *}             $line]} {
			append current_diff_header $line "\n"
			continue
		}
		if {[string match {index *} $line]} continue
		if {$line eq {deleted file mode 120000}} {
			set line "deleted symlink"
		}

		# -- Automatically detect if this is a 3 way diff.
		#
		if {[string match {@@@ *} $line]} {set is_3way_diff 1}

		if {[string match {mode *} $line]
			|| [string match {new file *} $line]
			|| [string match {deleted file *} $line]
			|| [string match {deleted symlink} $line]
			|| [string match {Binary files * and * differ} $line]
			|| $line eq {\ No newline at end of file}
			|| [regexp {^\* Unmerged path } $line]} {
			set tags {}
		} elseif {$is_3way_diff} {
			set op [string range $line 0 1]
			switch -- $op {
			{  } {set tags {}}
			{@@} {set tags d_@}
			{ +} {set tags d_s+}
			{ -} {set tags d_s-}
			{+ } {set tags d_+s}
			{- } {set tags d_-s}
			{--} {set tags d_--}
			{++} {
				if {[regexp {^\+\+([<>]{7} |={7})} $line _g op]} {
					set line [string replace $line 0 1 {  }]
					set tags d$op
				} else {
					set tags d_++
				}
			}
			default {
				puts "error: Unhandled 3 way diff marker: {$op}"
				set tags {}
			}
			}
		} else {
			set op [string index $line 0]
			switch -- $op {
			{ } {set tags {}}
			{@} {set tags d_@}
			{-} {set tags d_-}
			{+} {
				if {[regexp {^\+([<>]{7} |={7})} $line _g op]} {
					set line [string replace $line 0 0 { }]
					set tags d$op
				} else {
					set tags d_+
				}
			}
			default {
				puts "error: Unhandled 2 way diff marker: {$op}"
				set tags {}
			}
			}
		}
		$ui_diff insert end $line $tags
		if {[string index $line end] eq "\r"} {
			$ui_diff tag add d_cr {end - 2c}
		}
		$ui_diff insert end "\n" $tags
	}
	$ui_diff conf -state disabled

	if {[eof $fd]} {
		close $fd
		set diff_active 0
		unlock_index
		ui_ready

		if {[$ui_diff index end] eq {2.0}} {
			handle_empty_diff
		}
	}
}

proc apply_hunk {x y} {
	global current_diff_path current_diff_header current_diff_side
	global ui_diff ui_index file_states

	if {$current_diff_path eq {} || $current_diff_header eq {}} return
	if {![lock_index apply_hunk]} return

	set apply_cmd {apply --cached --whitespace=nowarn}
	set mi [lindex $file_states($current_diff_path) 0]
	if {$current_diff_side eq $ui_index} {
		set failed_msg [mc "Failed to unstage selected hunk."]
		lappend apply_cmd --reverse
		if {[string index $mi 0] ne {M}} {
			unlock_index
			return
		}
	} else {
		set failed_msg [mc "Failed to stage selected hunk."]
		if {[string index $mi 1] ne {M}} {
			unlock_index
			return
		}
	}

	set s_lno [lindex [split [$ui_diff index @$x,$y] .] 0]
	set s_lno [$ui_diff search -backwards -regexp ^@@ $s_lno.0 0.0]
	if {$s_lno eq {}} {
		unlock_index
		return
	}

	set e_lno [$ui_diff search -forwards -regexp ^@@ "$s_lno + 1 lines" end]
	if {$e_lno eq {}} {
		set e_lno end
	}

	if {[catch {
		set p [eval git_write $apply_cmd]
		fconfigure $p -translation binary -encoding binary
		puts -nonewline $p $current_diff_header
		puts -nonewline $p [$ui_diff get $s_lno $e_lno]
		close $p} err]} {
		error_popup [append $failed_msg "\n\n$err"]
		unlock_index
		return
	}

	$ui_diff conf -state normal
	$ui_diff delete $s_lno $e_lno
	$ui_diff conf -state disabled

	if {[$ui_diff get 1.0 end] eq "\n"} {
		set o _
	} else {
		set o ?
	}

	if {$current_diff_side eq $ui_index} {
		set mi ${o}M
	} elseif {[string index $mi 0] eq {_}} {
		set mi M$o
	} else {
		set mi ?$o
	}
	unlock_index
	display_file $current_diff_path $mi
	if {$o eq {_}} {
		clear_diff
	}
}
