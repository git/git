# git-gui diff viewer
# Copyright (C) 2006, 2007 Shawn Pearce

proc apply_tab_size {{firsttab {}}} {
	global have_tk85 repo_config ui_diff

	set w [font measure font_diff "0"]
	if {$have_tk85 && $firsttab != 0} {
		$ui_diff configure -tabs [list [expr {$firsttab * $w}] [expr {($firsttab + $repo_config(gui.tabsize)) * $w}]]
	} elseif {$have_tk85 || $repo_config(gui.tabsize) != 8} {
		$ui_diff configure -tabs [expr {$repo_config(gui.tabsize) * $w}]
	} else {
		$ui_diff configure -tabs {}
	}
}

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

proc reshow_diff {{after {}}} {
	global file_states file_lists
	global current_diff_path current_diff_side
	global ui_diff

	set p $current_diff_path
	if {$p eq {}} {
		# No diff is being shown.
	} elseif {$current_diff_side eq {}} {
		clear_diff
	} elseif {[catch {set s $file_states($p)}]
		|| [lsearch -sorted -exact $file_lists($current_diff_side) $p] == -1} {

		if {[find_next_diff $current_diff_side $p {} {[^O]}]} {
			next_diff $after
		} else {
			clear_diff
		}
	} else {
		set save_pos [lindex [$ui_diff yview] 0]
		show_diff $p $current_diff_side {} $save_pos $after
	}
}

proc force_diff_encoding {enc} {
	global current_diff_path

	if {$current_diff_path ne {}} {
		force_path_encoding $current_diff_path $enc
		reshow_diff
	}
}

proc handle_empty_diff {} {
	global current_diff_path file_states file_lists
	global diff_empty_count

	set path $current_diff_path
	set s $file_states($path)
	if {[lindex $s 0] ne {_M} || [has_textconv $path]} return

	# Prevent infinite rescan loops
	incr diff_empty_count
	if {$diff_empty_count > 1} return

	info_popup [mc "No differences detected.

%s has no changes.

The modification date of this file was updated by another application, but the content within the file was not changed.

A rescan will be automatically started to find other files which may have the same state." [short_path $path]]

	clear_diff
	display_file $path __
	rescan ui_ready 0
}

proc show_diff {path w {lno {}} {scroll_pos {}} {callback {}}} {
	global file_states file_lists
	global is_3way_diff is_conflict_diff diff_active repo_config
	global ui_diff ui_index ui_workdir
	global current_diff_path current_diff_side current_diff_header
	global current_diff_queue

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
		$w see $lno.0
	}

	set s $file_states($path)
	set m [lindex $s 0]
	set is_conflict_diff 0
	set current_diff_path $path
	set current_diff_side $w
	set current_diff_queue {}
	ui_status [mc "Loading diff of %s..." [escape_path $path]]

	set cont_info [list $scroll_pos $callback]

	apply_tab_size 0

	if {[string first {U} $m] >= 0} {
		merge_load_stages $path [list show_unmerged_diff $cont_info]
	} elseif {$m eq {_O}} {
		show_other_diff $path $w $m $cont_info
	} else {
		start_show_diff $cont_info
	}

	global current_diff_path selected_paths
	set selected_paths($current_diff_path) 1
}

proc show_unmerged_diff {cont_info} {
	global current_diff_path current_diff_side
	global merge_stages ui_diff is_conflict_diff
	global current_diff_queue

	if {$merge_stages(2) eq {}} {
		set is_conflict_diff 1
		lappend current_diff_queue \
			[list [mc "LOCAL: deleted\nREMOTE:\n"] d= \
			    [list ":1:$current_diff_path" ":3:$current_diff_path"]]
	} elseif {$merge_stages(3) eq {}} {
		set is_conflict_diff 1
		lappend current_diff_queue \
			[list [mc "REMOTE: deleted\nLOCAL:\n"] d= \
			    [list ":1:$current_diff_path" ":2:$current_diff_path"]]
	} elseif {[lindex $merge_stages(1) 0] eq {120000}
		|| [lindex $merge_stages(2) 0] eq {120000}
		|| [lindex $merge_stages(3) 0] eq {120000}} {
		set is_conflict_diff 1
		lappend current_diff_queue \
			[list [mc "LOCAL:\n"] d= \
			    [list ":1:$current_diff_path" ":2:$current_diff_path"]]
		lappend current_diff_queue \
			[list [mc "REMOTE:\n"] d= \
			    [list ":1:$current_diff_path" ":3:$current_diff_path"]]
	} else {
		start_show_diff $cont_info
		return
	}

	advance_diff_queue $cont_info
}

proc advance_diff_queue {cont_info} {
	global current_diff_queue ui_diff

	set item [lindex $current_diff_queue 0]
	set current_diff_queue [lrange $current_diff_queue 1 end]

	$ui_diff conf -state normal
	$ui_diff insert end [lindex $item 0] [lindex $item 1]
	$ui_diff conf -state disabled

	start_show_diff $cont_info [lindex $item 2]
}

proc show_other_diff {path w m cont_info} {
	global file_states file_lists
	global is_3way_diff diff_active repo_config
	global ui_diff ui_index ui_workdir
	global current_diff_path current_diff_side current_diff_header

	# - Git won't give us the diff, there's nothing to compare to!
	#
	if {$m eq {_O}} {
		set max_sz 100000
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
					fconfigure $fd \
						-eofchar {} \
						-encoding [get_path_encoding $path]
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
			$ui_diff insert end \
				"* [mc "Git Repository (subproject)"]\n" \
				d_info
		} elseif {![catch {set type [exec file $path]}]} {
			set n [string length $path]
			if {[string equal -length $n $path $type]} {
				set type [string range $type $n end]
				regsub {^:?\s*} $type {} type
			}
			$ui_diff insert end "* $type\n" d_info
		}
		if {[string first "\0" $content] != -1} {
			$ui_diff insert end \
				[mc "* Binary file (not showing content)."] \
				d_info
		} else {
			if {$sz > $max_sz} {
				$ui_diff insert end [mc \
"* Untracked file is %d bytes.
* Showing only first %d bytes.
" $sz $max_sz] d_info
			}
			$ui_diff insert end $content
			if {$sz > $max_sz} {
				$ui_diff insert end [mc "
* Untracked file clipped here by %s.
* To see the entire file, use an external editor.
" [appname]] d_info
			}
		}
		$ui_diff conf -state disabled
		set diff_active 0
		unlock_index
		set scroll_pos [lindex $cont_info 0]
		if {$scroll_pos ne {}} {
			update
			$ui_diff yview moveto $scroll_pos
		}
		ui_ready
		set callback [lindex $cont_info 1]
		if {$callback ne {}} {
			eval $callback
		}
		return
	}
}

proc start_show_diff {cont_info {add_opts {}}} {
	global file_states file_lists
	global is_3way_diff is_submodule_diff diff_active repo_config
	global ui_diff ui_index ui_workdir
	global current_diff_path current_diff_side current_diff_header

	set path $current_diff_path
	set w $current_diff_side

	set s $file_states($path)
	set m [lindex $s 0]
	set is_3way_diff 0
	set is_submodule_diff 0
	set diff_active 1
	set current_diff_header {}
	set conflict_size [gitattr $path conflict-marker-size 7]

	set cmd [list]
	if {$w eq $ui_index} {
		lappend cmd diff-index
		lappend cmd --cached
		if {[git-version >= "1.7.2"]} {
			lappend cmd --ignore-submodules=dirty
		}
	} elseif {$w eq $ui_workdir} {
		if {[string first {U} $m] >= 0} {
			lappend cmd diff
		} else {
			lappend cmd diff-files
		}
	}
	if {![is_config_false gui.textconv] && [git-version >= 1.6.1]} {
		lappend cmd --textconv
	}

	if {[string match {160000 *} [lindex $s 2]]
	 || [string match {160000 *} [lindex $s 3]]} {
		set is_submodule_diff 1

		if {[git-version >= "1.6.6"]} {
			lappend cmd --submodule
		}
	}

	lappend cmd -p
	lappend cmd --color
	set cmd [concat $cmd $repo_config(gui.diffopts)]
	if {$repo_config(gui.diffcontext) >= 1} {
		lappend cmd "-U$repo_config(gui.diffcontext)"
	}
	if {$w eq $ui_index} {
		lappend cmd [PARENT]
	}
	if {$add_opts ne {}} {
		eval lappend cmd $add_opts
	} else {
		lappend cmd --
		lappend cmd $path
	}

	if {$is_submodule_diff && [git-version < "1.6.6"]} {
		if {$w eq $ui_index} {
			set cmd [list submodule summary --cached -- $path]
		} else {
			set cmd [list submodule summary --files -- $path]
		}
	}

	if {[catch {set fd [eval git_read --nice $cmd]} err]} {
		set diff_active 0
		unlock_index
		ui_status [mc "Unable to display %s" [escape_path $path]]
		error_popup [strcat [mc "Error loading diff:"] "\n\n$err"]
		return
	}

	set ::current_diff_inheader 1
	# Detect pre-image lines of the diff3 conflict-style. They are just
	# '++' lines which is not bijective. Thus, we need to maintain a state
	# across lines.
	set ::conflict_in_pre_image 0
	fconfigure $fd \
		-blocking 0 \
		-encoding [get_path_encoding $path] \
		-translation lf
	fileevent $fd readable [list read_diff $fd $conflict_size $cont_info]
}

proc parse_color_line {line} {
	set start 0
	set result ""
	set markup [list]
	set regexp {\033\[((?:\d+;)*\d+)?m}
	set need_reset 0
	while {[regexp -indices -start $start $regexp $line match code]} {
		foreach {begin end} $match break
		append result [string range $line $start [expr {$begin - 1}]]
		set pos [string length $result]
		set col [eval [linsert $code 0 string range $line]]
		set start [incr end]
		if {$col eq "0" || $col eq ""} {
			if {!$need_reset} continue
			set need_reset 0
		} else {
			set need_reset 1
		}
		lappend markup $pos $col
	}
	append result [string range $line $start end]
	if {[llength $markup] < 4} {set markup {}}
	return [list $result $markup]
}

proc read_diff {fd conflict_size cont_info} {
	global ui_diff diff_active is_submodule_diff
	global is_3way_diff is_conflict_diff current_diff_header
	global current_diff_queue
	global diff_empty_count

	$ui_diff conf -state normal
	while {[gets $fd line] >= 0} {
		foreach {line markup} [parse_color_line $line] break
		set line [string map {\033 ^} $line]

		set tags {}

		# -- Check for start of diff header.
		if {   [string match {diff --git *}      $line]
		    || [string match {diff --cc *}       $line]
		    || [string match {diff --combined *} $line]} {
			set ::current_diff_inheader 1
		}

		# -- Check for end of diff header (any hunk line will do this).
		#
		if {[regexp {^@@+ } $line]} {set ::current_diff_inheader 0}

		# -- Automatically detect if this is a 3 way diff.
		#
		if {[string match {@@@ *} $line]} {
			set is_3way_diff 1
			apply_tab_size 1
		}

		if {$::current_diff_inheader} {

			# -- These two lines stop a diff header and shouldn't be in there
			if {   [string match {Binary files * and * differ} $line]
			    || [regexp {^\* Unmerged path }                $line]} {
				set ::current_diff_inheader 0
			} else {
				append current_diff_header $line "\n"
			}

			# -- Cleanup uninteresting diff header lines.
			#
			if {   [string match {diff --git *}      $line]
			    || [string match {diff --cc *}       $line]
			    || [string match {diff --combined *} $line]
			    || [string match {--- *}             $line]
			    || [string match {+++ *}             $line]
			    || [string match {index *}           $line]} {
				continue
			}

			# -- Name it symlink, not 120000
			#    Note, that the original line is in $current_diff_header
			regsub {^(deleted|new) file mode 120000} $line {\1 symlink} line

		} elseif {   $line eq {\ No newline at end of file}} {
			# -- Handle some special lines
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
				set regexp [string map [list %conflict_size $conflict_size]\
								{^\+\+([<>=|]){%conflict_size}(?: |$)}]
				if {[regexp $regexp $line _g op]} {
					set is_conflict_diff 1
					set line [string replace $line 0 1 {  }]
					set tags d$op

					# The ||| conflict-marker marks the start of the pre-image.
					# All those lines are also prefixed with '++'. Thus we need
					# to maintain this state.
					set ::conflict_in_pre_image [expr {$op eq {|}}]
				} elseif {$::conflict_in_pre_image} {
					# This is a pre-image line. It is the one which both sides
					# are based on. As it has also the '++' line start, it is
					# normally shown as 'added'. Invert this to '--' to make
					# it a 'removed' line.
					set line [string replace $line 0 1 {--}]
					set tags d_--
				} else {
					set tags d_++
				}
			}
			default {
				puts "error: Unhandled 3 way diff marker: {$op}"
				set tags {}
			}
			}
		} elseif {$is_submodule_diff} {
			if {$line == ""} continue
			if {[regexp {^Submodule } $line]} {
				set tags d_info
			} elseif {[regexp {^\* } $line]} {
				set line [string replace $line 0 1 {Submodule }]
				set tags d_info
			} else {
				set op [string range $line 0 2]
				switch -- $op {
				{  <} {set tags d_-}
				{  >} {set tags d_+}
				{  W} {set tags {}}
				default {
					puts "error: Unhandled submodule diff marker: {$op}"
					set tags {}
				}
				}
			}
		} else {
			set op [string index $line 0]
			switch -- $op {
			{ } {set tags {}}
			{@} {set tags d_@}
			{-} {set tags d_-}
			{+} {
				set regexp [string map [list %conflict_size $conflict_size]\
								{^\+([<>=]){%conflict_size}(?: |$)}]
				if {[regexp $regexp $line _g op]} {
					set is_conflict_diff 1
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
		set mark [$ui_diff index "end - 1 line linestart"]
		$ui_diff insert end $line $tags
		if {[string index $line end] eq "\r"} {
			$ui_diff tag add d_cr {end - 2c}
		}
		$ui_diff insert end "\n" $tags

		foreach {posbegin colbegin posend colend} $markup {
			set prefix clr
			foreach style [lsort -integer [split $colbegin ";"]] {
				if {$style eq "7"} {append prefix i; continue}
				if {$style != 4 && ($style < 30 || $style > 47)} {continue}
				set a "$mark linestart + $posbegin chars"
				set b "$mark linestart + $posend chars"
				catch {$ui_diff tag add $prefix$style $a $b}
			}
		}
	}
	$ui_diff conf -state disabled

	if {[eof $fd]} {
		close $fd

		if {$current_diff_queue ne {}} {
			advance_diff_queue $cont_info
			return
		}

		set diff_active 0
		unlock_index
		set scroll_pos [lindex $cont_info 0]
		if {$scroll_pos ne {}} {
			update
			$ui_diff yview moveto $scroll_pos
		}
		ui_ready

		if {[$ui_diff index end] eq {2.0}} {
			handle_empty_diff
		} else {
			set diff_empty_count 0
		}

		set callback [lindex $cont_info 1]
		if {$callback ne {}} {
			eval $callback
		}
	}
}

proc apply_or_revert_hunk {x y revert} {
	global current_diff_path current_diff_header current_diff_side
	global ui_diff ui_index file_states last_revert last_revert_enc

	if {$current_diff_path eq {} || $current_diff_header eq {}} return
	if {![lock_index apply_hunk]} return

	set apply_cmd {apply --whitespace=nowarn}
	set mi [lindex $file_states($current_diff_path) 0]
	if {$current_diff_side eq $ui_index} {
		set failed_msg [mc "Failed to unstage selected hunk."]
		lappend apply_cmd --reverse --cached
		set file_state [string index $mi 0]
		if {$file_state ne {M} && $file_state ne {A}} {
			unlock_index
			return
		}
	} else {
		if {$revert} {
			set failed_msg [mc "Failed to revert selected hunk."]
			lappend apply_cmd --reverse
		} else {
			set failed_msg [mc "Failed to stage selected hunk."]
			lappend apply_cmd --cached
		}

		set file_state [string index $mi 1]
		if {$file_state ne {M} && $file_state ne {A}} {
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

	set wholepatch "$current_diff_header[$ui_diff get $s_lno $e_lno]"

	if {[catch {
		set enc [get_path_encoding $current_diff_path]
		set p [eval git_write $apply_cmd]
		fconfigure $p -translation binary -encoding $enc
		puts -nonewline $p $wholepatch
		close $p} err]} {
		error_popup "$failed_msg\n\n$err"
		unlock_index
		return
	}

	if {$revert} {
		# Save a copy of this patch for undoing reverts.
		set last_revert $wholepatch
		set last_revert_enc $enc
	}

	$ui_diff conf -state normal
	$ui_diff delete $s_lno $e_lno
	$ui_diff conf -state disabled

	# Check if the hunk was the last one in the file.
	if {[$ui_diff get 1.0 end] eq "\n"} {
		set o _
	} else {
		set o ?
	}

	# Update the status flags.
	if {$revert} {
		set mi [string index $mi 0]$o
	} elseif {$current_diff_side eq $ui_index} {
		set mi ${o}M
	} elseif {[string index $mi 0] eq {_}} {
		set mi M$o
	} else {
		set mi ?$o
	}
	unlock_index
	display_file $current_diff_path $mi
	# This should trigger shift to the next changed file
	if {$o eq {_}} {
		reshow_diff
	}
}

proc apply_or_revert_range_or_line {x y revert} {
	global current_diff_path current_diff_header current_diff_side
	global ui_diff ui_index file_states last_revert

	set selected [$ui_diff tag nextrange sel 0.0]

	if {$selected == {}} {
		set first [$ui_diff index "@$x,$y"]
		set last $first
	} else {
		set first [lindex $selected 0]
		set last [lindex $selected 1]
	}

	set first_l [$ui_diff index "$first linestart"]
	set last_l [$ui_diff index "$last lineend"]

	if {$current_diff_path eq {} || $current_diff_header eq {}} return
	if {![lock_index apply_hunk]} return

	set apply_cmd {apply --whitespace=nowarn}
	set mi [lindex $file_states($current_diff_path) 0]
	if {$current_diff_side eq $ui_index} {
		set failed_msg [mc "Failed to unstage selected line."]
		set to_context {+}
		lappend apply_cmd --reverse --cached
		set file_state [string index $mi 0]
		if {$file_state ne {M} && $file_state ne {A}} {
			unlock_index
			return
		}
	} else {
		if {$revert} {
			set failed_msg [mc "Failed to revert selected line."]
			set to_context {+}
			lappend apply_cmd --reverse
		} else {
			set failed_msg [mc "Failed to stage selected line."]
			set to_context {-}
			lappend apply_cmd --cached
		}

		set file_state [string index $mi 1]
		if {$file_state ne {M} && $file_state ne {A}} {
			unlock_index
			return
		}
	}

	set wholepatch {}

	while {$first_l < $last_l} {
		set i_l [$ui_diff search -backwards -regexp ^@@ $first_l 0.0]
		if {$i_l eq {}} {
			# If there's not a @@ above, then the selected range
			# must have come before the first_l @@
			set i_l [$ui_diff search -regexp ^@@ $first_l $last_l]
		}
		if {$i_l eq {}} {
			unlock_index
			return
		}
		# $i_l is now at the beginning of a line

		# pick start line number from hunk header
		set hh [$ui_diff get $i_l "$i_l + 1 lines"]
		set hh [lindex [split $hh ,] 0]
		set hln [lindex [split $hh -] 1]
		set hln [lindex [split $hln " "] 0]

		# There is a special situation to take care of. Consider this
		# hunk:
		#
		#    @@ -10,4 +10,4 @@
		#     context before
		#    -old 1
		#    -old 2
		#    +new 1
		#    +new 2
		#     context after
		#
		# We used to keep the context lines in the order they appear in
		# the hunk. But then it is not possible to correctly stage only
		# "-old 1" and "+new 1" - it would result in this staged text:
		#
		#    context before
		#    old 2
		#    new 1
		#    context after
		#
		# (By symmetry it is not possible to *un*stage "old 2" and "new
		# 2".)
		#
		# We resolve the problem by introducing an asymmetry, namely,
		# when a "+" line is *staged*, it is moved in front of the
		# context lines that are generated from the "-" lines that are
		# immediately before the "+" block. That is, we construct this
		# patch:
		#
		#    @@ -10,4 +10,5 @@
		#     context before
		#    +new 1
		#     old 1
		#     old 2
		#     context after
		#
		# But we do *not* treat "-" lines that are *un*staged in a
		# special way.
		#
		# With this asymmetry it is possible to stage the change "old
		# 1" -> "new 1" directly, and to stage the change "old 2" ->
		# "new 2" by first staging the entire hunk and then unstaging
		# the change "old 1" -> "new 1".
		#
		# Applying multiple lines adds complexity to the special
		# situation.  The pre_context must be moved after the entire
		# first block of consecutive staged "+" lines, so that
		# staging both additions gives the following patch:
		#
		#    @@ -10,4 +10,6 @@
		#     context before
		#    +new 1
		#    +new 2
		#     old 1
		#     old 2
		#     context after

		# This is non-empty if and only if we are _staging_ changes;
		# then it accumulates the consecutive "-" lines (after
		# converting them to context lines) in order to be moved after
		# "+" change lines.
		set pre_context {}

		set n 0
		set m 0
		set i_l [$ui_diff index "$i_l + 1 lines"]
		set patch {}
		while {[$ui_diff compare $i_l < "end - 1 chars"] &&
		       [$ui_diff get $i_l "$i_l + 2 chars"] ne {@@}} {
			set next_l [$ui_diff index "$i_l + 1 lines"]
			set c1 [$ui_diff get $i_l]
			if {[$ui_diff compare $first_l <= $i_l] &&
			    [$ui_diff compare $i_l < $last_l] &&
			    ($c1 eq {-} || $c1 eq {+})} {
				# a line to stage/unstage
				set ln [$ui_diff get $i_l $next_l]
				if {$c1 eq {-}} {
					set n [expr $n+1]
					set patch "$patch$pre_context$ln"
					set pre_context {}
				} else {
					set m [expr $m+1]
					set patch "$patch$ln"
				}
			} elseif {$c1 ne {-} && $c1 ne {+}} {
				# context line
				set ln [$ui_diff get $i_l $next_l]
				set patch "$patch$pre_context$ln"
				# Skip the "\ No newline at end of
				# file". Depending on the locale setting
				# we don't know what this line looks
				# like exactly. The only thing we do
				# know is that it starts with "\ "
				if {![string match {\\ *} $ln]} {
					set n [expr $n+1]
					set m [expr $m+1]
				}
				set pre_context {}
			} elseif {$c1 eq $to_context} {
				# turn change line into context line
				set ln [$ui_diff get "$i_l + 1 chars" $next_l]
				if {$c1 eq {-}} {
					set pre_context "$pre_context $ln"
				} else {
					set patch "$patch $ln"
				}
				set n [expr $n+1]
				set m [expr $m+1]
			} else {
				# a change in the opposite direction of
				# to_context which is outside the range of
				# lines to apply.
				set patch "$patch$pre_context"
				set pre_context {}
			}
			set i_l $next_l
		}
		set patch "$patch$pre_context"
		set wholepatch "$wholepatch@@ -$hln,$n +$hln,$m @@\n$patch"
		set first_l [$ui_diff index "$next_l + 1 lines"]
	}

	if {[catch {
		set enc [get_path_encoding $current_diff_path]
		set p [eval git_write $apply_cmd]
		fconfigure $p -translation binary -encoding $enc
		puts -nonewline $p $current_diff_header
		puts -nonewline $p $wholepatch
		close $p} err]} {
		error_popup "$failed_msg\n\n$err"
		unlock_index
		return
	}

	if {$revert} {
		# Save a copy of this patch for undoing reverts.
		set last_revert $current_diff_header$wholepatch
		set last_revert_enc $enc
	}

	unlock_index
}

# Undo the last line/hunk reverted. When hunks and lines are reverted, a copy
# of the diff applied is saved. Re-apply that diff to undo the revert.
#
# Right now, we only use a single variable to hold the copy, and not a
# stack/deque for simplicity, so multiple undos are not possible. Maybe this
# can be added if the need for something like this is felt in the future.
proc undo_last_revert {} {
	global last_revert current_diff_path current_diff_header
	global last_revert_enc

	if {$last_revert eq {}} return
	if {![lock_index apply_hunk]} return

	set apply_cmd {apply --whitespace=nowarn}
	set failed_msg [mc "Failed to undo last revert."]

	if {[catch {
		set enc $last_revert_enc
		set p [eval git_write $apply_cmd]
		fconfigure $p -translation binary -encoding $enc
		puts -nonewline $p $last_revert
		close $p} err]} {
		error_popup "$failed_msg\n\n$err"
		unlock_index
		return
	}

	set last_revert {}

	unlock_index
}
